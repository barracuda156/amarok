/*
 *  Copyright (c) 2007 Jeff Mitchell <kde-dev@emailgoeshere.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "pmpkioslave_mtpbackend.h"
#include "pmpkioslave.h"

#include <sys/stat.h>

#include <QCoreApplication>
#include <QString>
#include <QVariant>

#include <kapplication.h>
#include <kcomponentdata.h>
#include <kdebug.h>
#include <klocale.h>
#include <kurl.h>
#include <kio/slavebase.h>
#include <solid/device.h>
#include <solid/genericinterface.h>

MTPBackend::MTPBackend( PMPProtocol* slave, const Solid::Device &device )
            : PMPBackend( slave, device )
            , m_device( 0 )
            , m_gotTracklisting( false )
            , m_defaultMusicLocation( "Music" )
{
    kDebug() << "Creating MTPBackend" << endl;

    if( !m_slave->mtpInitialized() )
        LIBMTP_Init();

    if( LIBMTP_Get_Connected_Devices( &m_deviceList ) != LIBMTP_ERROR_NONE )
    {
        m_slave->error( KIO::ERR_INTERNAL, i18n( "Could not get a connected device list from libmtp." ) );
        return;
    }
    quint32 deviceCount = LIBMTP_Number_Devices_In_List( m_deviceList );
    if( deviceCount == 0 )
    {
        m_slave->error( KIO::ERR_INTERNAL, i18n( "libmtp found no devices." ) );
        return;
    }
}

MTPBackend::~MTPBackend()
{
    kDebug() << endl << "In MTPBackend destructor, releasing device" << endl << endl;
    if( m_device )
        LIBMTP_Release_Device( m_device );
}

void
MTPBackend::initialize()
{
    kDebug() << "Initializing MTPBackend for device " << m_solidDevice.udi() << endl;
    Solid::GenericInterface *gi = m_solidDevice.as<Solid::GenericInterface>();
    if( !gi )
    {
        m_slave->error( KIO::ERR_INTERNAL, i18n( "Error getting a GenericInterface to the device from Solid." ) );
        return;
    }
    if( !gi->propertyExists( "usb.serial" ) )
    {
        m_slave->error( KIO::ERR_INTERNAL, i18n( "Could not find serial number of MTP device in HAL. \
                                                  When filing a bug please include the full output of \"lshal -lt\"." ) );
        return;
    }
    QVariant possibleSerial = gi->property( "usb.serial" );
    if( !possibleSerial.isValid() || possibleSerial.isNull() || !possibleSerial.canConvert( QVariant::String ) )
    {
        m_slave->error( KIO::ERR_INTERNAL, i18n( "Could not get the serial number from HAL." ) );
        return;
    }
    QString serial = possibleSerial.toString();
    kDebug() << endl << endl << "Case-insensitively looking for serial number starting with: " << serial << endl << endl;
    LIBMTP_mtpdevice_t *currdevice;
    for( currdevice = m_deviceList; currdevice != NULL; currdevice = currdevice->next )
    {
        kDebug() << "currdevice serial number = " << LIBMTP_Get_Serialnumber( currdevice ) << endl;
        //WARNING: a startsWith is done below, as the value reported by HAL for the serial number seems to be about half
        //the length of the value reported by libmtp...is this always true?  Could this cause two devices to
        //be recognized as the same one?
        if( QString( LIBMTP_Get_Serialnumber( currdevice ) ).startsWith( serial, Qt::CaseInsensitive ) )
        {
            kDebug() << endl << endl << "Found a matching serial!" << endl << endl;
            m_device = currdevice;
        }
        else
            LIBMTP_Release_Device( currdevice );
    }

    if( m_device )
        kDebug() << "FOUND THE MTP DEVICE WE WERE LOOKING FOR!" << endl;

    return;
}

QString
MTPBackend::getFriendlyName()
{
    kDebug() << "Getting MTPBackend friendly name" << endl;
    char* name = LIBMTP_Get_Friendlyname( m_device );
    QString friendlyName = QString::fromUtf8( name );
    free( name );
    return friendlyName;
}

void
MTPBackend::setFriendlyName( const QString &name )
{
    kDebug() << "Setting MTPBackend friendly name" << endl;
    if( LIBMTP_Set_Friendlyname( m_device, name.toUtf8() ) != 0 )
        m_slave->warning( i18n( "Failed to set friendly name on the device!" ) );
}

QString
MTPBackend::getModelName()
{
    kDebug() << "Getting MTPBackend model name" << endl;
    char* model = LIBMTP_Get_Modelname( m_device );
    QString modelName = QString::fromUtf8( model );
    free( model );
    return modelName;
}

void
MTPBackend::get( const KUrl &url )
{
    QString path = getFilePath( url );
    kDebug() << "in MTPBackend::get, path is: " << path << endl;
}

void
MTPBackend::listDir( const KUrl &url )
{
    QString path = getFilePath( url );
    kDebug() << "in MTPBackend::listDir, path is: " << path << endl;
    //first case: no specific folder chosen, display a list of available actions as folders
    if( path.isEmpty() )
    {
        LIBMTP_folder_t *folderList = LIBMTP_Get_Folder_List( m_device );
        if( folderList == 0 )
        {
            m_slave->warning( i18n( "Could not find any folders on device!" ) );
            return;
        }
        while( folderList )
        {
            KIO::UDSEntry entry;
            entry.insert( KIO::UDSEntry::UDS_NAME, folderList->name);
            entry.insert( KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
            entry.insert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IRGRP | S_IROTH);
            m_slave->listEntry( entry, false );
            folderList = folderList->sibling;
        }
        m_slave->listEntry( KIO::UDSEntry(), true );
        return;
    }
    if( !m_gotTracklisting )
        buildMusicListing();
    //next case: User requests something specific...first, find out what they requested
    //and error if not appropriate
    bool endOfPath = path.indexOf( '/' ) == -1;
    QString nextLevel, nextPath;
    if( endOfPath )
        nextLevel = path;
    else
    {
        nextLevel = path.left( path.indexOf( '/' ) );
        nextPath = path.right( path.indexOf( '/' ) );
    }
    if( nextLevel == m_defaultMusicLocation )
    {
        listMusic( nextPath );
        m_slave->listEntry( KIO::UDSEntry(), true );
    }
    else
        m_slave->error( KIO::ERR_INTERNAL, i18n( "Invalid path requested!" ) );
}

void
MTPBackend::listMusic( const QString &pathOffset )
{
    kDebug() << "(listMusic) pathOffset = " << pathOffset << endl;
    if( pathOffset.isEmpty() )
    {
        kDebug() << "LIST ENTRIES IN " << m_defaultMusicLocation << " NOW" << endl;
    }
    else
    {
        kDebug() << "LIST ENTRIES IN " << m_defaultMusicLocation << '/' << pathOffset << " NOW" << endl;
    }
}

void
MTPBackend::rename( const KUrl &src, const KUrl &dest, bool overwrite )
{
    Q_UNUSED( src );
    Q_UNUSED( dest );
    Q_UNUSED( overwrite );
    //make sure they're renaming playlists to playlists, tracks to tracks, etc...
}

void
MTPBackend::stat( const KUrl &url )
{
   QString path = getFilePath( url );
   kDebug() << "in MTPBackend::stat, path is: " << path << endl;
   KIO::UDSEntry entry;
   entry.insert( KIO::UDSEntry::UDS_NAME,m_solidDevice.product());
   entry.insert(KIO::UDSEntry::UDS_FILE_TYPE,S_IFDIR);
   entry.insert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IRGRP | S_IROTH);
   m_slave->statEntry( entry );
}

void
MTPBackend::buildMusicListing()
{
    LIBMTP_track_t* trackList;
    trackList = LIBMTP_Get_Tracklisting_With_Callback( m_device, progressCallback, (void *)this );
    LIBMTP_folder_t* folderList = LIBMTP_Get_Folder_List( m_device );
    QString defaultMusicLocation = LIBMTP_Find_Folder( folderList, m_device->default_music_folder )->name;
    m_defaultMusicLocation = defaultMusicLocation;
    kDebug() << "defaultMusicLocation set to: " << defaultMusicLocation << endl;
    buildFolderList( folderList, defaultMusicLocation );
    QString folderPath;
    while( trackList != 0 )
    {
        if( trackList->parent_id == 0 )
        {
            kDebug() << "Found track " << defaultMusicLocation << '/' << trackList->filename << endl;
            m_trackParentToPtrHash.insert( defaultMusicLocation, trackList );
        }
        else
        {
            folderPath = m_folderIdToPathHash.value( trackList->parent_id );
            kDebug() << "Found track " << folderPath << '/' << trackList->filename << endl;
            m_trackParentToPtrHash.insert( folderPath, trackList );
        }
    }
}

void
MTPBackend::buildFolderList( LIBMTP_folder_t *folderList, const QString &parentPath )
{
    if( folderList == 0 )
        return;

    kDebug() << "Found folder " << parentPath << '/' << folderList->name << endl;

    m_folderParentToPtrHash.insert( parentPath, folderList );
    m_folderIdToPathHash.insert( folderList->folder_id, parentPath + '/' + folderList->name );

    buildFolderList( folderList->child, parentPath + '/' + folderList->name );
    buildFolderList( folderList->sibling, parentPath );
}

int
MTPBackend::progressCallback( quint64 const sent, quint64 const total, void const * const data )
{
    Q_UNUSED( data );
    kapp->processEvents();
    MTPBackend *backend = (MTPBackend *)(data);
    if( sent == total )
        backend->getSlave()->infoMessage( i18n( "Receiving tracklisting...done" ) );
    else
        backend->getSlave()->infoMessage( i18n( "Receiving tracklisting...%1\%", sent/total ) );
    kDebug() << "libmtp progress callback called, with " << sent/total << " done." << endl;
    return 0;
}

#include "pmpkioslave_mtpbackend.moc"

