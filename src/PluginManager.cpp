/****************************************************************************************
 * Copyright (c) 2004-2013 Mark Kretschmann <kretschmann@kde.org>                       *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#define DEBUG_PREFIX "PluginManager"

#include "PluginManager.h"

#include <core/support/Amarok.h>
#include <core/support/Components.h>
#include <core/support/Debug.h>
#include <core-impl/collections/support/CollectionManager.h>
#include <core-impl/storage/StorageManager.h>
#include <services/ServicePluginManager.h>
#include <statsyncing/Controller.h>

#include <KLocalizedString>
#include <KMessageBox>
#include <KProcess>
#include <KServiceTypeTrader>
#include <KStandardDirs>

#include <QApplication>
#include <QThread>
#include <QMetaEnum>


/** Defines the used plugin version number.
 *
 *  This must match the desktop files.
 */
const int Plugins::PluginManager::s_pluginFrameworkVersion = 73;
Plugins::PluginManager* Plugins::PluginManager::s_instance = 0;

Plugins::PluginManager*
Plugins::PluginManager::instance()
{
    return s_instance ? s_instance : new PluginManager();
}

void
Plugins::PluginManager::destroy()
{
    if( s_instance )
    {
        delete s_instance;
        s_instance = 0;
    }
}

Plugins::PluginManager::PluginManager( QObject *parent )
    : QObject( parent )
{
    DEBUG_BLOCK
    setObjectName( "PluginManager" );
    s_instance = this;

    PERF_LOG( "Initialising Plugin Manager" )
    init();
    PERF_LOG( "Initialised Plugin Manager" )
}

Plugins::PluginManager::~PluginManager()
{
    // tell the managers to get rid of their current factories
    QList<Plugins::PluginFactory*> emptyFactories;

    StatSyncing::Controller *controller = Amarok::Components::statSyncingController();
    if( controller )
        controller->setFactories( emptyFactories );
    ServicePluginManager::instance()->setFactories( emptyFactories );
    CollectionManager::instance()->setFactories( emptyFactories );
    StorageManager::instance()->setFactories( emptyFactories );
}

void
Plugins::PluginManager::init()
{
    checkPluginEnabledStates();
}

KPluginInfo::List
Plugins::PluginManager::plugins( Type type ) const
{
    return m_pluginInfosByType.value( type );
}

QList<Plugins::PluginFactory*>
Plugins::PluginManager::factories( Type type ) const
{
    return m_factoriesByType.value( type );
}

void
Plugins::PluginManager::checkPluginEnabledStates()
{
    // re-create all the member infos.
    m_pluginInfos.clear();
    m_pluginInfosByType.clear();
    m_factoriesByType.clear();

    m_pluginInfos = findPlugins(); // reload all the plugins plus their enabled state
    if( m_pluginInfos.isEmpty() ) // try it a second time with syscoca
        handleNoPluginsFound();

    QList<PluginFactory*> allFactories;

    // sort the plugin infos by type
    foreach( const KPluginInfo &pluginInfo, m_pluginInfos )
    {
        Type type;
        if( pluginInfo.category() == QLatin1String("Storage") )
            type = Storage;
        else if( pluginInfo.category() == QLatin1String("Collection") )
            type = Collection;
        else if( pluginInfo.category() == QLatin1String("Service") )
            type = Service;
        else if( pluginInfo.category() == QLatin1String("Importer") )
            type = Importer;
        else {
            warning() << pluginInfo.pluginName() << " has unknown category";
            continue;
        }
        m_pluginInfosByType[ type ] << pluginInfo;

        // create the factories and sort them by type
        PluginFactory *factory = createFactory( pluginInfo );
        if( factory )
        {
            m_factoriesByType[ type ] << factory;
            allFactories << factory;
        }
    }

    // the setFactories functions should:
    // - filter out factories not useful (e.g. services when setting collections)
    // - handle the new list of factories, disabling old ones and enabling new ones.

    PERF_LOG( "Loading storage plugins" )
    StorageManager::instance()->setFactories( m_factoriesByType.value( Storage ) );
    PERF_LOG( "Loaded storage plugins" )

    PERF_LOG( "Loading collection plugins" )
    CollectionManager::instance()->setFactories( m_factoriesByType.value( Collection ) );
    PERF_LOG( "Loaded collection plugins" )

    PERF_LOG( "Loading service plugins" )
    ServicePluginManager::instance()->setFactories( m_factoriesByType.value( Service ) );
    PERF_LOG( "Loaded service plugins" )

    PERF_LOG( "Loading importer plugins" )
    StatSyncing::Controller *controller = Amarok::Components::statSyncingController();
    if( controller )
        controller->setFactories( m_factoriesByType.value( Importer ) );
    PERF_LOG( "Loaded importer plugins" )

    // init all new factories
    // do this after they were added to the sub-manager so that they
    // have a chance to connect to signals
    //
    // we need to init by type and the storages need to go first
    foreach( PluginFactory* factory, m_factoriesByType[ Storage ] )
        factory->init();
    foreach( PluginFactory* factory, m_factoriesByType[ Collection ] )
        factory->init();
    foreach( PluginFactory* factory, m_factoriesByType[ Service ] )
        factory->init();
    foreach( PluginFactory* factory, m_factoriesByType[ Importer ] )
        factory->init();
}


bool
Plugins::PluginManager::isPluginEnabled( const KPluginInfo &pluginInfo ) const
{
    // mysql storage and collection are vital. They need to be loaded always
    if( pluginInfo.property("X-KDE-Amarok-vital").toBool() )
    {
        return true;
    }
    else
    {
        const QString pluginName = pluginInfo.pluginName();
        const bool enabledByDefault = pluginInfo.isPluginEnabledByDefault();
        return Amarok::config( "Plugins" ).readEntry( pluginName + "Enabled", enabledByDefault );
    }
}


Plugins::PluginFactory*
Plugins::PluginManager::createFactory( const KPluginInfo &pluginInfo )
{
    if( !isPluginEnabled( pluginInfo ) )
        return 0;

    // check if we already created this factory
    // note: old factories are not deleted.
    //   We can't very well just destroy a factory being
    //   currently used.
    const QString name = pluginInfo.pluginName();
    if( m_factoryCreated.contains(name) )
        return m_factoryCreated.value(name);


    KService::Ptr service = pluginInfo.service();
    KPluginLoader loader( *( service.constData() ) );
    KPluginFactory *pluginFactory( loader.factory() );

    if( !pluginFactory )
    {
        warning() << QString( "Failed to get factory '%1' from KPluginLoader: %2" )
            .arg( name, loader.errorString() );
        return 0;
    }

    // create the factory with this object as owner
    PluginFactory *factory = 0;
    if( !(factory = pluginFactory->create<PluginFactory>( this )) )
    {
        warning() << "Failed to create plugin" << name << loader.errorString();
        return 0;
    }

    m_factoryCreated[ pluginInfo.pluginName() ] = factory;
    return factory;
}


KPluginInfo::List
Plugins::PluginManager::findPlugins()
{
    QString query = QString::fromLatin1( "[X-KDE-Amarok-framework-version] == %1"
                                         " and [X-KDE-Amarok-rank] > 0" )
                    .arg( s_pluginFrameworkVersion );

    KConfigGroup pluginsConfig = Amarok::config(QLatin1String("Plugins"));
    KService::List services = KServiceTypeTrader::self()->query( "Amarok/Plugin", query );
    KPluginInfo::List plugins = KPluginInfo::fromServices( services, pluginsConfig );

    // load the plugins
    foreach( KPluginInfo info, plugins )
    {
        info.load( pluginsConfig ); // load the enabled state of plugin
        debug() << "found plugin:" << info.pluginName()
                << "enabled:" << info.isPluginEnabled();
    }
    debug() << plugins.count() << "plugins in total";

    return plugins;
}

void
Plugins::PluginManager::handleNoPluginsFound()
{
    DEBUG_BLOCK

    debug() << "No Amarok plugins found, running kbuildsycoca4.";

    // Run kbuildsycoca4 in a blocking fashion
    KProcess::execute( KStandardDirs::findExe( "kbuildsycoca4" ), QStringList( "--noincremental" ) );

    // Wait a bit (3 sec) until ksycoca has fully updated
    for( int i = 0; i < 30; i++ ) {
        QThread::currentThread()->wait( 100 );
        QApplication::processEvents();
    }

    debug() << "Second attempt at finding plugins";

    m_pluginInfos = findPlugins();
    if( m_pluginInfos.isEmpty() )
    {
        if( QApplication::type() != QApplication::Tty )
        {
            KMessageBox::error( 0, i18n( "Amarok could not find any plugins. This indicates an installation problem." ) );
        }
        else
        {
            warning() << "Amarok could not find any plugins. Bailing out.";
        }
        // don't use QApplication::exit, as the eventloop may not have started yet
        std::exit( EXIT_SUCCESS );
    }
}

int
Plugins::PluginManager::pluginFrameworkVersion()
{
    return s_pluginFrameworkVersion;
}

#include "PluginManager.moc"
