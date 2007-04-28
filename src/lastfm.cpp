/***************************************************************************
 * copyright            : (C) 2006 Chris Muehlhaeuser <chris@chris.de>     *
 *                      : (C) 2006 Seb Ruiz <me@sebruiz.net>               *
 *                      : (C) 2006 Ian Monroe <ian@monroe.nu>              *
 *                      : (C) 2006 Mark Kretschmann <markey@web.de>        *
 **************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#define DEBUG_PREFIX "LastFm"

#include "amarok.h"         //APP_VERSION, actioncollection
#include "amarokconfig.h"   //last.fm username and passwd
#include "collectiondb.h"
#include "debug.h"
#include "enginecontroller.h"
#include "lastfm.h"
#include "statusbar.h"      //showError()

#include <qdom.h>
#include <q3http.h>
#include <QLabel>
#include <QRegExp>
#include <kapplication.h>
//Added by qt3to4:
#include <Q3ValueList>
#include <QByteArray>

#include <kaction.h>
#include <klineedit.h>
#include <kcodecs.h>       //md5sum
#include <kmessagebox.h>
#include <kio/job.h>
#include <kio/jobclasses.h>
#include <kprotocolmanager.h>
#include <kshortcut.h>
#include <kurl.h>
#include <khbox.h>
#include <kvbox.h>

#include <time.h>
#include <unistd.h>

using namespace LastFm;

///////////////////////////////////////////////////////////////////////////////
// CLASS AmarokHttp
// AmarokHttp is a hack written so that lastfm code could easily use something proxy aware.
// DO NOT use this class for anything else, use KIO directly instead.
////////////////////////////////////////////////////////////////////////////////
AmarokHttp::AmarokHttp ( const QString& hostname, quint16 port,
                         QObject* parent )
    : QObject( parent ),
      m_hostname( hostname ),
      m_port( port )
{}

int
AmarokHttp::get ( const QString & path )
{
    QString uri = QString( "http://%1:%2/%3" )
                  .arg( m_hostname )
                  .arg( m_port )
                  .arg( path );

    m_done = false;
    m_error = Q3Http::NoError;
    m_state = Q3Http::Connecting;
    KIO::TransferJob *job = KIO::get(uri, true, false);
    connect(job,  SIGNAL(data(KIO::Job*, const QByteArray&)),
            this, SLOT(slotData(KIO::Job*, const QByteArray&)));
    connect(job,  SIGNAL(result(KIO::Job*)),
            this, SLOT(slotResult(KIO::Job*)));

    return 0;
}

Q3Http::State
AmarokHttp::state() const
{
    return m_state;
}

QByteArray
AmarokHttp::readAll ()
{
    return m_result;
}

Q3Http::Error
AmarokHttp::error()
{
    return m_error;
}

void
AmarokHttp::slotData(KIO::Job*, const QByteArray& data)
{
    if( data.size() == 0 ) {
        return;
    }
    else if ( m_result.size() == 0 ) {
        m_result = data;
    }
    else {
        m_result.resize( m_result.size() + data.size() );
        memcpy( m_result.end(), data.data(),  data.size() );
    }
}

void
AmarokHttp::slotResult(KIO::Job* job)
{
    bool err = job->error();
    if( err || m_error != Q3Http::NoError ) {
        m_error = Q3Http::UnknownError;
    }
    else {
        m_error = Q3Http::NoError;
    }
    m_done = true;
    m_state = Q3Http::Unconnected;
    emit( requestFinished( 0, err ) );
}



///////////////////////////////////////////////////////////////////////////////
// CLASS Controller
////////////////////////////////////////////////////////////////////////////////

Controller *Controller::s_instance = 0;

Controller::Controller()
    : QObject( EngineController::instance(), "lastfmController" )
    , m_service( 0 )
{
    KActionCollection* ac = Amarok::actionCollection();
    KAction *action = new KAction( KIcon( Amarok::icon( "remove" ) ), i18n( "Ban" ), ac );
    connect( action, SIGNAL( triggered( bool ) ), this, SLOT( ban() ) );
    action->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_B ) );
    m_actionList.append( action );

    action = new KAction( KIcon( Amarok::icon( "love" ) ), i18n( "Love" ), ac );
    connect( action, SIGNAL( triggered( bool ) ), this, SLOT( love() ) );
    action->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_L ) );
    m_actionList.append( action );

    action = new KAction( KIcon( Amarok::icon( "next" ) ), i18n( "Skip" ), ac );
    connect( action, SIGNAL( triggered( bool ) ), this, SLOT(  skip() ) );
    action->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_K ) );
    m_actionList.append( action );
    setActionsEnabled( false );
}


Controller*
Controller::instance()
{
    if( !s_instance ) s_instance = new Controller();
    return s_instance;
}


KUrl
Controller::getNewProxy( QString genreUrl )
{
    DEBUG_BLOCK

    m_genreUrl = genreUrl;

    if ( m_service ) playbackStopped();

    m_service = new WebService( this );

    if( checkCredentials() )
    {
        QString user = AmarokConfig::scrobblerUsername();
        QString pass = AmarokConfig::scrobblerPassword();

        if( !user.isEmpty() && !pass.isEmpty() &&
            m_service->handshake( user, pass ) )
        {
            bool ok = m_service->changeStation( m_genreUrl );
            if( ok ) // else playbackStopped()
            {
                if( !AmarokConfig::submitPlayedSongs() )
                    m_service->enableScrobbling( false );
                setActionsEnabled( true );
                return KUrl( m_service->proxyUrl() );
            }
        }
    }

    // Some kind of failure happened, so crap out
    playbackStopped();
    return KUrl();
}


void
Controller::playbackStopped() //SLOT
{
    setActionsEnabled( false );

    delete m_service;
    m_service = 0;
}


bool
Controller::checkCredentials() //static
{
    if( AmarokConfig::scrobblerUsername().isEmpty() || AmarokConfig::scrobblerPassword().isEmpty() )
    {
        LoginDialog dialog( 0 );
        dialog.setCaption( "last.fm" );
        return dialog.exec() == QDialog::Accepted;
    }
    return true;
}


QString
Controller::createCustomStation() //static
{
    QString token;
    CustomStationDialog dialog( 0 );

    if( dialog.exec() == QDialog::Accepted ) {
        const QStringList artists = QStringList::split( ",", dialog.text() );
        for( uint i = 0; i < artists.count(); i++ )
            token += ( i > 0 ? "," : "" ) + artists[i].simplified();
    }

    return token;
}


void
Controller::ban()
{
    if( m_service )
        m_service->ban();
}


void
Controller::love()
{
    if( m_service )
        m_service->love();
}


void
Controller::skip()
{
    if( m_service )
        m_service->skip();
}


void
Controller::setActionsEnabled( bool enable )
{   //pausing last.fm streams doesn't do anything good
    Amarok::actionCollection()->action( "play_pause" )->setEnabled( !enable );
    Amarok::actionCollection()->action( "pause" )->setEnabled( !enable );

    KAction* action;
    for( action = m_actionList.first(); action; action = m_actionList.next() )
        action->setEnabled( enable );
}

/// return a translatable description of the station we are connected to
QString
Controller::stationDescription( QString url )
{
    if( url.isEmpty() && instance() && instance()->isPlaying() )
        url = instance()->getService()->currentStation();

    if( url.isEmpty() ) return QString();

    QStringList elements = QStringList::split( "/", url );

    /// TAG RADIOS
    // eg: lastfm://globaltag/rock
    if ( elements[1] == "globaltags" )
        return i18n( "Global Tag Radio: %1", elements[2] );

    /// ARTIST RADIOS
    if ( elements[1] == "artist" )
    {
        // eg: lastfm://artist/Queen/similarartists
        if ( elements[3] == "similarartists" )
            return i18n( "Similar Artists to %1", elements[2] );

        if ( elements[3] == "fans" )
            return i18n( "Artist Fan Radio: %1", elements[2] );
    }

    /// CUSTOM STATION
    if ( elements[1] == "artistnames" )
    {
        // eg: lastfm://artistnames/genesis,pink floyd,queen

        // turn "genesis,pink floyd,queen" into "Genesis, Pink Floyd, Queen"
        QString artists = elements[2];
        artists.replace( ",", ", " );
        const QStringList words = QStringList::split( " ", QString( artists ).remove( "," ) );
        oldForeach( words ) {
            QString capitalized = *it;
            capitalized.replace( 0, 1, (*it)[0].toUpper() );
            artists.replace( *it, capitalized );
        }

        return i18n( "Custom Station: %1", artists );
    }

    /// USER RADIOS
    else if ( elements[1] == "user" )
    {
        // eg: lastfm://user/sebr/neighbours
        if ( elements[3] == "neighbours" )
            return i18n( "%1's Neighbor Radio", elements[2] );

        // eg: lastfm://user/sebr/personal
        if ( elements[3] == "personal" )
            return i18n( "%1's Personal Radio", elements[2] );

        // eg: lastfm://user/sebr/loved
        if ( elements[3] == "loved" )
            return i18n( "%1's Loved Radio", elements[2] );

        // eg: lastfm://user/sebr/recommended/100 : 100 is number for how obscure the music should be
        if ( elements[3] == "recommended" )
            return i18n( "%1's Recommended Radio", elements[2] );
    }

    /// GROUP RADIOS
    //eg: lastfm://group/Amarok%20users
    else if ( elements[1] == "group" )
        return i18n( "Group Radio: %1", elements[2] );

    /// TRACK RADIOS
    else if ( elements[1] == "play" )
    {
        if ( elements[2] == "tracks" )
            return i18n( "Track Radio" );
        else if ( elements[2] == "artists" )
            return i18n( "Artist Radio" );
    }
    //kaput!
    return url;
}



////////////////////////////////////////////////////////////////////////////////
// CLASS WebService
////////////////////////////////////////////////////////////////////////////////

WebService::WebService( QObject* parent )
    : QObject( parent, "lastfmParent" )
    , m_server( 0 )
{
    debug() << "Initialising Web Service" << endl;
}


WebService::~WebService()
{
    DEBUG_BLOCK

    delete m_server;
}


void
WebService::readProxy() //SLOT
{
    QString line;

    while( m_server->readln( line ) != -1 ) {
        debug() << line << endl;

        if( line == "AMAROK_PROXY: SYNC" )
            requestMetaData();
    }
}


bool
WebService::handshake( const QString& username, const QString& password )
{
    DEBUG_BLOCK

    m_username = username;
    m_password = password;

    AmarokHttp http( "ws.audioscrobbler.com", 80 );

    const QString path =
            QString( "/radio/handshake.php?version=%1&platform=%2&username=%3&passwordmd5=%4&debug=%5" )
            .arg( APP_VERSION )             //Muesli-approved: Amarok version, and Amarok-as-platform
            .arg( QString("Amarok") )
            .arg( QString( Q3Url( username ).encodedPathAndQuery() ) )
            .arg( KMD5( m_password.toUtf8() ).hexDigest().data() )
            .arg( "0" );

    http.get( path );

    do
        kapp->processEvents();
    while( http.state() != Q3Http::Unconnected );

    if ( http.error() != Q3Http::NoError )
        return false;

    const QString result( http.readAll() );

    debug() << "result: " << result << endl;

    m_session = parameter( "session", result );
    m_baseHost = parameter( "base_url", result );
    m_basePath = parameter( "base_path", result );
    m_subscriber = parameter( "subscriber", result ) == "1";
    m_streamUrl = Q3Url( parameter( "stream_url", result ) );
//     bool banned = parameter( "banned", result ) == "1";

    if ( m_session.toLower() == "failed" ) {
        Amarok::StatusBar::instance()->longMessage( i18n(
        "Amarok failed to establish a session with last.fm. <br>"
        "Check if your last.fm user and password are correctly set."
        ) );
        return false;
    }

    Amarok::config( "Scrobbler" ).writeEntry( "Subscriber", m_subscriber );

    // Find free port
    MyServerSocket* socket = new MyServerSocket();
    const int port = socket->port();
    debug() << "Proxy server using port: " << port << endl;
    delete socket;

    m_proxyUrl = QString( "http://localhost:%1/lastfm.mp3" ).arg( port );

    m_server = new Amarok::ProcIO();
    m_server->setComm( K3Process::Communication( K3Process::AllOutput ) );
    *m_server << "amarok_proxy.rb";
    *m_server << "--lastfm";
    *m_server << QString::number( port );
    *m_server << m_streamUrl.toString();
    *m_server << AmarokConfig::soundSystem();
    *m_server << Amarok::proxyForUrl( m_streamUrl.toString() );

    if( !m_server->start( K3ProcIO::NotifyOnExit, true ) ) {
        error() << "Failed to start amarok_proxy.rb" << endl;
        return false;
    }

    QString line;
    while( true ) {
        kapp->processEvents();
        m_server->readln( line );
        if( line == "AMAROK_PROXY: startup" ) break;
    }

    connect( m_server, SIGNAL( readReady( K3ProcIO* ) ), this, SLOT( readProxy() ) );
    connect( m_server, SIGNAL( processExited( K3Process* ) ), Controller::instance(), SLOT( playbackStopped() ) );

    return true;
}


bool
WebService::changeStation( QString url )
{
    debug() << "Changing station:" << url << endl;

    AmarokHttp http( m_baseHost, 80 );

    http.get( QString( m_basePath + "/adjust.php?session=%1&url=%2&debug=0" )
             .arg( m_session )
             .arg( url ) );

    do
        kapp->processEvents();
    while( http.state() != Q3Http::Unconnected );

    if ( http.error() != Q3Http::NoError )
    {
        showError( E_OTHER ); // default error
        return false;
    }

    const QString result( http.readAll() );
    const int errCode = parameter( "error", result ).toInt();

    if ( errCode )
    {
        showError( errCode );
        return false;
    }

    const QString _url = parameter( "url", result );
    if ( _url.startsWith( "lastfm://" ) )
    {
        m_station = _url; // parse it in stationDescription
        emit stationChanged( _url, m_station );
    }
    else
        emit stationChanged( _url, QString() );

    return true;
}

void
WebService::requestMetaData() //SLOT
{
    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( int, bool ) ), this, SLOT( metaDataFinished( int, bool ) ) );

    http->get( QString( m_basePath + "/np.php?session=%1&debug=%2" )
                  .arg( m_session )
                  .arg( "0" ) );
}


void
WebService::metaDataFinished( int /*id*/, bool error ) //SLOT
{
    DEBUG_BLOCK

    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if( error ) return;

    const QString result( http->readAll() );
    debug() << result << endl;

    int errCode = parameter( "error", result ).toInt();
    if ( errCode > 0 ) {
        debug() << "Metadata failed with error code: " << errCode << endl;
        showError( errCode );
        return;
    }

    m_metaBundle.setArtist( parameter( "artist", result ) );
    m_metaBundle.setAlbum ( parameter( "album", result )  );
    m_metaBundle.setTitle ( parameter( "track", result )  );
    m_metaBundle.setUrl   ( KUrl( Controller::instance()->getGenreUrl() ) );
    m_metaBundle.setLength( parameter( "trackduration", result ).toInt()  );

    Bundle lastFmStuff;
    QString imageUrl = parameter( "albumcover_medium", result );

    if( imageUrl == "http://static.last.fm/coverart/" ||
        imageUrl == "http://static.last.fm/depth/catalogue/no_album_large.gif" )
        imageUrl.clear();

    lastFmStuff.setImageUrl ( CollectionDB::instance()->notAvailCover( true ) );
    lastFmStuff.setArtistUrl( parameter( "artist_url", result ) );
    lastFmStuff.setAlbumUrl ( parameter( "album_url", result ) );
    lastFmStuff.setTitleUrl ( parameter( "track_url", result ) );
//     bool discovery = parameter( "discovery", result ) != "-1";

    m_metaBundle.setLastFmBundle( lastFmStuff );

    const KUrl u( imageUrl );
    if( !u.isValid() ) {
        debug() << "imageUrl empty or invalid." << endl;
        emit metaDataResult( m_metaBundle );
        return;
    }

    KIO::Job* job = KIO::storedGet( u, true, false );
    connect( job, SIGNAL( result( KIO::Job* ) ), this, SLOT( fetchImageFinished( KIO::Job* ) ) );
}


void
WebService::fetchImageFinished( KIO::Job* job ) //SLOT
{
    DEBUG_BLOCK

    if( job->error() == 0 ) {
        const QString path = Amarok::saveLocation() + "lastfm_image.png";
        const int size = AmarokConfig::coverPreviewSize();

        QImage img( static_cast<KIO::StoredTransferJob*>( job )->data() );
        img.smoothScale( size, size ).save( path, "PNG" );

        m_metaBundle.lastFmBundle()->setImageUrl( CollectionDB::makeShadowedImage( path, false ) );
    }
    emit metaDataResult( m_metaBundle );
}


void
WebService::enableScrobbling( bool enabled ) //SLOT
{
    if ( enabled )
        debug() << "Enabling Scrobbling!" << endl;
    else
        debug() << "Disabling Scrobbling!" << endl;

    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( int, bool ) ), this, SLOT( enableScrobblingFinished( int, bool ) ) );

    http->get( QString( m_basePath + "/control.php?session=%1&command=%2&debug=%3" )
                  .arg( m_session )
                  .arg( enabled ? QString( "rtp" ) : QString( "nortp" ) )
                  .arg( "0" ) );
}


void
WebService::enableScrobblingFinished( int /*id*/, bool error ) //SLOT
{
    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if ( error ) return;

    emit enableScrobblingDone();
}


void
WebService::love() //SLOT
{
    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( int, bool ) ), this, SLOT( loveFinished( int, bool ) ) );

    http->get( QString( m_basePath + "/control.php?session=%1&command=love&debug=%2" )
                  .arg( m_session )
                  .arg( "0" ) );
    Amarok::StatusBar::instance()->shortMessage( i18nc("love, as in affection", "Loving song...") );
}


void
WebService::skip() //SLOT
{
    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( int, bool ) ), this, SLOT( skipFinished( int, bool ) ) );

    http->get( QString( m_basePath + "/control.php?session=%1&command=skip&debug=%2" )
                  .arg( m_session )
                  .arg( "0" ) );
    Amarok::StatusBar::instance()->shortMessage( i18n("Skipping song...") );
}


void
WebService::ban() //SLOT
{
    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( int, bool ) ), this, SLOT( banFinished( int, bool ) ) );

    http->get( QString( m_basePath + "/control.php?session=%1&command=ban&debug=%2" )
                  .arg( m_session )
                  .arg( "0" ) );
    Amarok::StatusBar::instance()->shortMessage( i18nc("Ban, as in dislike", "Banning song...") );
}


void
WebService::loveFinished( int /*id*/, bool error ) //SLOT
{
    DEBUG_BLOCK

    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if( error ) return;

    emit loveDone();
}


void
WebService::skipFinished( int /*id*/, bool error ) //SLOT
{
    DEBUG_BLOCK

    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if( error ) return;

    EngineController::engine()->flushBuffer();
    emit skipDone();
}


void
WebService::banFinished( int /*id*/, bool error ) //SLOT
{
    DEBUG_BLOCK

    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if( error ) return;

    EngineController::engine()->flushBuffer();
    emit banDone();
    emit skipDone();
}


void
WebService::friends( QString username )
{
    if ( username.isEmpty() )
        username = m_username;

    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( bool ) ), this, SLOT( friendsFinished( bool ) ) );

    http->get( QString( "/1.0/user/%1/friends.xml" )
                  .arg( QString( Q3Url( username ).encodedPathAndQuery() ) ) );
}


void
WebService::friendsFinished( int /*id*/, bool error ) //SLOT
{
    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if( error ) return;

    QDomDocument document;
    document.setContent( http->readAll() );

    if ( document.elementsByTagName( "friends" ).length() == 0 )
    {
        emit friendsResult( QString( "" ), QStringList() );
        return;
    }

    QStringList friends;
    QString user = document.elementsByTagName( "friends" ).item( 0 ).attributes().namedItem( "user" ).nodeValue();
    QDomNodeList values = document.elementsByTagName( "user" );
    for ( uint i = 0; i < values.count(); i++ )
    {
        friends << values.item( i ).attributes().namedItem( "username" ).nodeValue();
    }

    emit friendsResult( user, friends );
}


void
WebService::neighbours( QString username )
{
    if ( username.isEmpty() )
        username = m_username;

    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( bool ) ), this, SLOT( neighboursFinished( bool ) ) );

    http->get( QString( "/1.0/user/%1/neighbours.xml" )
                  .arg( QString( Q3Url( username ).encodedPathAndQuery() ) ) );
}


void
WebService::neighboursFinished( int /*id*/, bool error ) //SLOT
{
    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if( error )  return;

    QDomDocument document;
    document.setContent( http->readAll() );

    if ( document.elementsByTagName( "neighbours" ).length() == 0 )
    {
        emit friendsResult( QString( "" ), QStringList() );
        return;
    }

    QStringList neighbours;
    QString user = document.elementsByTagName( "neighbours" ).item( 0 ).attributes().namedItem( "user" ).nodeValue();
    QDomNodeList values = document.elementsByTagName( "user" );
    for ( uint i = 0; i < values.count(); i++ )
    {
        neighbours << values.item( i ).attributes().namedItem( "username" ).nodeValue();
    }

    emit neighboursResult( user, neighbours );
}


void
WebService::userTags( QString username )
{
    if ( username.isEmpty() )
        username = m_username;

    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( bool ) ), this, SLOT( userTagsFinished( bool ) ) );

    http->get( QString( "/1.0/user/%1/tags.xml?debug=%2" )
                  .arg( QString( Q3Url( username ).encodedPathAndQuery() ) ) );
}


void
WebService::userTagsFinished( int /*id*/, bool error ) //SLOT
{
    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if( error ) return;

    QDomDocument document;
    document.setContent( http->readAll() );

    if ( document.elementsByTagName( "toptags" ).length() == 0 )
    {
        emit userTagsResult( QString(), QStringList() );
        return;
    }

    QStringList tags;
    QDomNodeList values = document.elementsByTagName( "tag" );
    QString user = document.elementsByTagName( "toptags" ).item( 0 ).attributes().namedItem( "user" ).nodeValue();
    for ( uint i = 0; i < values.count(); i++ )
    {
        QDomNode item = values.item( i ).namedItem( "name" );
        tags << item.toElement().text();
    }
    emit userTagsResult( user, tags );
}


void
WebService::recentTracks( QString username )
{
    if ( username.isEmpty() )
        username = m_username;

    AmarokHttp *http = new AmarokHttp( m_baseHost, 80, this );
    connect( http, SIGNAL( requestFinished( bool ) ), this, SLOT( recentTracksFinished( bool ) ) );

    http->get( QString( "/1.0/user/%1/recenttracks.xml" )
                  .arg( QString( Q3Url( username ).encodedPathAndQuery() ) ) );
}


void
WebService::recentTracksFinished( int /*id*/, bool error ) //SLOT
{
    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();
    if( error ) return;

    Q3ValueList< QPair<QString, QString> > songs;
    QDomDocument document;
    document.setContent( http->readAll() );

    if ( document.elementsByTagName( "recenttracks" ).length() == 0 )
    {
        emit recentTracksResult( QString(), songs );
        return;
    }

    QDomNodeList values = document.elementsByTagName( "track" );
    QString user = document.elementsByTagName( "recenttracks" ).item( 0 ).attributes().namedItem( "user" ).nodeValue();
    for ( uint i = 0; i < values.count(); i++ )
    {
        QPair<QString, QString> song;
        song.first = values.item( i ).namedItem( "artist" ).toElement().text();
        song.second = values.item( i ).namedItem( "name" ).toElement().text();

        songs << song;
    }
    emit recentTracksResult( user, songs );
}


void
WebService::recommend( int type, QString username, QString artist, QString token )
{
    QString modeToken = "";
    switch ( type )
    {
        case 0:
            modeToken = QString( "artist_name=%1" ).arg( QString( Q3Url( artist ).encodedPathAndQuery() ) );
            break;

        case 1:
            modeToken = QString( "album_artist=%1&album_name=%2" )
                           .arg( QString( Q3Url( artist ).encodedPathAndQuery() ) )
                           .arg( QString( Q3Url( token ).encodedPathAndQuery() ) );
            break;

        case 2:
            modeToken = QString( "track_artist=%1&track_name=%2" )
                           .arg( QString( Q3Url( artist ).encodedPathAndQuery() ) )
                           .arg( QString( Q3Url( token ).encodedPathAndQuery() ) );
            break;
    }

    Q3Http *http = new Q3Http( "wsdev.audioscrobbler.com", 80, this );
    connect( http, SIGNAL( requestFinished( bool ) ), this, SLOT( recommendFinished( bool ) ) );

    uint currentTime = QDateTime::currentDateTime( Qt::UTC ).toTime_t();
    QString challenge = QString::number( currentTime );

    QByteArray md5pass = KMD5( KMD5( m_password.toUtf8() ).hexDigest().append( QString::number( currentTime ).toLocal8Bit() ) ).hexDigest();

    QString atoken = QString( "user=%1&auth=%2&nonce=%3recipient=%4" )
                       .arg( QString( Q3Url( currentUsername() ).encodedPathAndQuery() ) )
                       .arg( QString( Q3Url( md5pass ).encodedPathAndQuery() ) )
                       .arg( QString( Q3Url( challenge ).encodedPathAndQuery() ) )
                       .arg( QString( Q3Url( username ).encodedPathAndQuery() ) );

    Q3HttpRequestHeader header( "POST", "/1.0/rw/recommend.php?" + atoken.toUtf8() );
    header.setValue( "Host", "wsdev.audioscrobbler.com" );
    header.setContentType( "application/x-www-form-urlencoded" );
    http->request( header, modeToken.toUtf8() );
}


void
WebService::recommendFinished( int /*id*/, bool /*error*/ ) //SLOT
{
    AmarokHttp* http = (AmarokHttp*) sender();
    http->deleteLater();

    debug() << "Recommendation:" << http->readAll() << endl;
}


QString
WebService::parameter( const QString keyName, const QString data ) const
{
    QStringList list = QStringList::split( '\n', data );

    for ( uint i = 0; i < list.size(); i++ )
    {
        QStringList values = QStringList::split( '=', list[i] );
        if ( values[0] == keyName )
        {
            values.remove( values.at(0) );
            return QString::fromUtf8( values.join( "=" ).toAscii() );
        }
    }

    return QString( "" );
}


QStringList
WebService::parameterArray( const QString keyName, const QString data ) const
{
    QStringList result;
    QStringList list = QStringList::split( '\n', data );

    for ( uint i = 0; i < list.size(); i++ )
    {
        QStringList values = QStringList::split( '=', list[i] );
        if ( values[0].startsWith( keyName ) )
        {
            values.remove( values.at(0) );
            result.append( QString::fromUtf8( values.join( "=" ).toAscii() ) );
        }
    }

    return result;
}


QStringList
WebService::parameterKeys( const QString keyName, const QString data ) const
{
    QStringList result;
    QStringList list = QStringList::split( '\n', data );

    for ( uint i = 0; i < list.size(); i++ )
    {
        QStringList values = QStringList::split( '=', list[i] );
        if ( values[0].startsWith( keyName ) )
        {
            values = QStringList::split( '[', values[0] );
            values = QStringList::split( ']', values[1] );
            result.append( values[0] );
        }
    }


    return result;
}

void
WebService::showError( int code, QString message )
{
    switch ( code )
    {
        case E_NOCONTENT:
            message = i18n( "There is not enough content to play this station." );
            break;
        case E_NOMEMBERS:
            message = i18n( "This group does not have enough members for radio." );
            break;
        case E_NOFANS:
            message = i18n( "This artist does not have enough fans for radio." );
            break;
        case E_NOAVAIL:
            message = i18n( "This item is not available for streaming." );
            break;
        case E_NOSUBSCRIBER:
            message = i18n( "This feature is only available to last.fm subscribers." );
            break;
        case E_NONEIGHBOURS:
            message = i18n( "There are not enough neighbors for this radio." );
            break;
        case E_NOSTOPPED:
            message = i18n( "This stream has stopped. Please try another station." );
            break;
        default:
            if( message.isEmpty() )
                message = i18n( "Failed to play this last.fm stream." );
    }

    Amarok::StatusBar::instance()->longMessage( message, KDE::StatusBar::Sorry );
}

////////////////////////////////////////////////////////////////////////////////
// CLASS LastFm::Bundle
////////////////////////////////////////////////////////////////////////////////

Bundle::Bundle( const Bundle& lhs )
    : m_imageUrl( lhs.m_imageUrl )
    , m_albumUrl( lhs.m_albumUrl )
    , m_artistUrl( lhs.m_artistUrl )
    , m_titleUrl( lhs.m_titleUrl )
{}

////////////////////////////////////////////////////////////////////////////////
// CLASS LastFm::LoginDialog
////////////////////////////////////////////////////////////////////////////////
LoginDialog::LoginDialog( QWidget *parent )
    : KDialog( parent )
{
    setModal( true );
    setButtons( Ok | Cancel );

    //makeGridMainWidget( 1, Qt::Horizontal );
    KVBox* vbox = new KVBox( this );
    setMainWidget( vbox );
    new QLabel( i18n( "To use last.fm with Amarok, you need a last.fm profile." ), vbox );

    //makeGridMainWidget( 2, Qt::Horizontal );
    KHBox* hbox = new KHBox( vbox );
    QLabel *nameLabel = new QLabel( i18n("&Username:"), hbox );
    m_userLineEdit = new KLineEdit( hbox );
    nameLabel->setBuddy( m_userLineEdit );

    QLabel *passLabel = new QLabel( i18n("&Password:"), hbox );
    m_passLineEdit = new KLineEdit( hbox );
    m_passLineEdit->setEchoMode( QLineEdit::Password );
    passLabel->setBuddy( m_passLineEdit );

    m_userLineEdit->setFocus();
}


void LoginDialog::slotButtonClicked( ButtonCode button )
{
    if ( button == Ok ) {
        AmarokConfig::setScrobblerUsername( m_userLineEdit->text() );
        AmarokConfig::setScrobblerPassword( m_passLineEdit->text() );
    }

    KDialog::slotButtonClicked( button );
}


////////////////////////////////////////////////////////////////////////////////
// CLASS LastFm::CustomStationDialog
////////////////////////////////////////////////////////////////////////////////
CustomStationDialog::CustomStationDialog( QWidget *parent )
    : KDialog( parent )
{
    setCaption( i18n( "Create Custom Station" ) );
    setModal( true );
    setButtons( Ok | Cancel );


    KVBox *vbox = new KVBox( this );
    setMainWidget( vbox );


    new QLabel( i18n( "Enter the name of a band or artist you like:\n(You can enter multiple artists separated by commas)" ), mainWidget() );

    m_edit = new KLineEdit( mainWidget() );
    m_edit->setFocus();
}


QString
CustomStationDialog::text() const
{
    return m_edit->text();
}


#include "lastfm.moc"
