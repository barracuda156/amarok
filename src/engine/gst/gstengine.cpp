/***************************************************************************
                       gstengine.cpp - GStreamer audio interface

begin                : Jan 02 2003
copyright            : (C) 2003 by Mark Kretschmann
email                : markey@web.de
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "config/gstconfig.h"
#include "enginebase.h"
#include "gstengine.h"
#include "streamsrc.h"

#include <math.h>           //interpolate()
#include <vector>

#include <qfile.h>
#include <qtimer.h>

#include <kapplication.h>
#include <kdebug.h>
#include <kio/job.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kurl.h>

#include <gst/gst.h>

using std::vector;


AMAROK_EXPORT_PLUGIN( GstEngine )

/////////////////////////////////////////////////////////////////////////////////////
// static
/////////////////////////////////////////////////////////////////////////////////////

static const int
SCOPEBUF_SIZE = 40000;

static const int
STREAMBUF_SIZE = 1000000; // == 1MB

static const int
STREAMBUF_MAX = STREAMBUF_SIZE - 50000;

GError*
GstEngine::error_msg;

GstEngine*
GstEngine::s_instance;


void
GstEngine::eos_cb( GstElement*, GstElement* )
{
    kdDebug() << k_funcinfo << endl;

    //this is the Qt equivalent to an idle function: delay the call until all events are finished,
    //otherwise gst will crash horribly
    QTimer::singleShot( 0, instance(), SLOT( stopAtEnd() ) );
}


void
GstEngine::handoff_cb( GstElement*, GstBuffer* buf, gpointer )
{
    instance()->m_scopeBufIndex = 0;
    
    int channels = 2;  //2 == default, if we cannot determine the value from gst
    GstCaps* caps = gst_pad_get_caps( gst_element_get_pad( instance()->m_gst_spider, "src_0" ) );

    for ( int i = 0; i < gst_caps_get_size( caps ); i++ ) {
        GstStructure* structure = gst_caps_get_structure( caps, i );

        if ( gst_structure_has_field( structure, "channels" ) )
            gst_structure_get_int( structure, "channels", &channels );
    }
    gst_caps_free( caps );

    if ( GST_IS_BUFFER( buf ) ) {
        gint16 * data = ( gint16* ) GST_BUFFER_DATA( buf );

        // Divide length by 2 for casting from 8bit to 16bit, and divide by number of channels
        for ( ulong i = 0; i < GST_BUFFER_SIZE( buf ) / 2 / channels; i += channels ) {
            if ( instance()->m_scopeBufIndex == instance()->m_scopeBuf.size() )
                instance()->m_scopeBufIndex = 0;

            Engine::Scope::value_type temp = 0;
            for ( int j = 0; j < channels; j++ ) {
                // Add all channels together so we effectively get a mono scope
                temp += data[ i + j ];
            }
            instance()->m_scopeBuf[ instance()->m_scopeBufIndex++ ] = temp;
        }
    }
}


void
GstEngine::error_cb( GstElement* /*element*/, GstElement* /*source*/, GError* /*error*/, gchar* /*debug*/, gpointer /*data*/ )
{
    kdDebug() << k_funcinfo << endl;

    QTimer::singleShot( 0, instance(), SLOT( handleError() ) );
}


void
GstEngine::kio_resume_cb()
{
    if ( instance()->m_transferJob && instance()->m_transferJob->isSuspended() ) {
        instance()->m_transferJob->resume();
        kdDebug() << "Gst-Engine: RESUMING kio transfer.\n";
    }
}


/////////////////////////////////////////////////////////////////////////////////////
// CLASS GSTENGINE
/////////////////////////////////////////////////////////////////////////////////////

GstEngine::GstEngine()
        : Engine::Base( Engine::Signal, true )
        , m_gst_thread( 0 )
        , m_scopeBuf( SCOPEBUF_SIZE )
        , m_scopeBufIndex( 0 )
        , m_streamBuf( new char[STREAMBUF_SIZE] )
        , m_transferJob( 0 )
        , m_fadeValue( 0.0 )
        , m_pipelineFilled( false )
{
    kdDebug() << k_funcinfo << endl;
}


GstEngine::~GstEngine()
{
    kdDebug() << "BEGIN " << k_funcinfo << endl;

    stop();
    cleanPipeline();
    delete[] m_streamBuf;

    // Save configuration
    GstConfig::writeConfig();
    
    kdDebug() << "END " << k_funcinfo << endl;
}


/////////////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
/////////////////////////////////////////////////////////////////////////////////////

bool
GstEngine::init()
{
    kdDebug() << "BEGIN " << k_funcinfo << endl;
    
    s_instance = this;
    
    // GStreamer initilization
    if ( !gst_init_check( NULL, NULL ) ) {
        KMessageBox::error( 0,
            i18n( "<h3>GStreamer could not be initialized.</h3> "
                  "<p>Please make sure that you have installed all necessary GStreamer plugins, and run <i>'gst-register'</i> afterwards.</p>"
                  "<p>For further assistance consult the GStreamer manual, and join #gstreamer on irc.freenode.net.</p>" ) );
        return false;
    }
            
    // Check if registry exists
    GstElement* dummy = gst_element_factory_make ( "fakesink", "fakesink" );
    if ( !dummy || !gst_scheduler_factory_make( NULL, GST_ELEMENT ( dummy ) ) ) {
        KMessageBox::error( 0,
            i18n( "<h3>GStreamer is missing a registry.</h3> "
                  "<p>Please make sure that you have installed all necessary GStreamer plugins, and run <i>'gst-register'</i> afterwards.</p>"
                  "<p>For further assistance consult the GStreamer manual, and join #gstreamer on irc.freenode.net.</p>" ) );
        return false;
    }
                      
    startTimer( TIMER_INTERVAL );

    kdDebug() << "END " << k_funcinfo << endl;
    return true;
}


bool
GstEngine::canDecode( const KURL &url )
{
    if ( GstConfig::soundOutput().isEmpty() ) {
        errorNoOutput();
        return false;
    }    
    
    bool success = false;
    GstElement *pipeline, *filesrc, *spider, *audioconvert, *audioscale, *audiosink;
    
    if ( !( pipeline = createElement( "pipeline" ) ) ) return false;
    if ( !( filesrc = createElement( "filesrc", pipeline ) ) ) return false;
    if ( !( spider = createElement( "spider", pipeline ) ) ) return false;
    if ( !( audioconvert = createElement( "audioconvert", pipeline ) ) ) return false;
    if ( !( audioscale = createElement( "audioscale", pipeline ) ) ) return false;
    if ( !( audiosink = createElement( GstConfig::soundOutput().latin1(), pipeline ) ) ) return false;

    /* setting device property for AudioSink*/
    if ( GstConfig::useCustomSoundDevice() && !GstConfig::soundDevice().isEmpty() )
        g_object_set( G_OBJECT ( audiosink ), "device", GstConfig::soundDevice().latin1(), NULL );

    g_object_set( G_OBJECT( filesrc ), "location", (const char*) QFile::encodeName( url.path() ), NULL );

    gst_element_link_many( filesrc, spider, audioconvert, audioscale, audiosink, NULL );
    gst_element_set_state( pipeline, GST_STATE_PLAYING );

    // Try to iterate over the bin, if it works gst can decode our file
    if ( gst_bin_iterate ( GST_BIN ( pipeline ) ) )
        success = true;

    gst_element_set_state( pipeline, GST_STATE_NULL );
    gst_object_unref( GST_OBJECT( pipeline ) );

    return success;
}


uint
GstEngine::position() const
{
    if ( !m_pipelineFilled ) return 0;

    GstFormat fmt = GST_FORMAT_TIME;
    // Value will hold the current time position in nanoseconds. Must be initialized!
    gint64 value = 0;
    gst_element_query( m_gst_spider, GST_QUERY_POSITION, &fmt, &value );

    return static_cast<long>( ( value / GST_MSECOND ) ); // nanosec -> msec
}


Engine::State
GstEngine::state() const
{
    if ( !m_pipelineFilled ) return Engine::Empty;

    switch ( gst_element_get_state( m_gst_thread ) )
    {
        case GST_STATE_NULL:
            return Engine::Empty;
        case GST_STATE_READY:
            return Engine::Idle;
        case GST_STATE_PLAYING:
            return Engine::Playing;
        case GST_STATE_PAUSED:
            return Engine::Paused;

        default:
            return Engine::Empty;
    }
}


const Engine::Scope&
GstEngine::scope()
{
    for ( int i = 0; i < 512; i++ )
        m_scope[i] = m_scopeBuf[i];
    
    m_scopeBufIndex = 0;

    return m_scope;
}


amaroK::PluginConfig*
GstEngine::configure() const
{
    kdDebug() << k_funcinfo << endl;
    
    return new GstConfigDialog( instance() );
}

/////////////////////////////////////////////////////////////////////////////////////
// PUBLIC SLOTS
/////////////////////////////////////////////////////////////////////////////////////

bool
GstEngine::load( const KURL& url, bool stream )  //SLOT
{
    stopNow();
    Engine::Base::load( url, stream );
    kdDebug() << "Gst-Engine: url.url() == " << url.url() << endl;
    
    if ( GstConfig::soundOutput().isEmpty() ) {
        errorNoOutput();
        return false;
    }    
    kdDebug() << "Thread scheduling priority: " << GstConfig::threadPriority() << endl;
    kdDebug() << "Sound output method: " << GstConfig::soundOutput() << endl;
    kdDebug() << "CustomSoundDevice: " << ( GstConfig::useCustomSoundDevice() ? "true" : "false" ) << endl;
    kdDebug() << "Sound Device: " << GstConfig::soundDevice() << endl;
    kdDebug() << "CustomOutputParams: " << ( GstConfig::useCustomOutputParams() ? "true" : "false" ) << endl;
    kdDebug() << "Output Params: " << GstConfig::outputParams() << endl;
    
    QCString output;
    
    /* create a new pipeline (thread) to hold the elements */
    if ( !( m_gst_thread = createElement( "thread" ) ) ) { goto error; }
    g_object_set( G_OBJECT( m_gst_thread ), "priority", GstConfig::threadPriority(), NULL );
    
    // Let gst construct the output element from a string
    output  = GstConfig::soundOutput().latin1();
    if ( GstConfig::useCustomOutputParams() ) {
        output += " ";
        output += GstConfig::outputParams().latin1();
    }
    GError* err;
    if ( !( m_gst_audiosink = gst_parse_launch( output, &err ) ) ) { goto error; }
    gst_bin_add( GST_BIN( m_gst_thread ), m_gst_audiosink );
    
    /* setting device property for AudioSink*/
    if ( GstConfig::useCustomSoundDevice() && !GstConfig::soundDevice().isEmpty() )
        g_object_set( G_OBJECT ( m_gst_audiosink ), "device", GstConfig::soundDevice().latin1(), NULL );

    if ( !( m_gst_identity = createElement( "identity", m_gst_thread ) ) ) { goto error; }
    if ( !( m_gst_volume = createElement( "volume", m_gst_thread ) ) ) { goto error; }
    if ( !( m_gst_volumeFade = createElement( "volume", m_gst_thread ) ) ) { goto error; }
    if ( !( m_gst_audioconvert = createElement( "audioconvert", m_gst_thread ) ) ) { goto error; }
    if ( !( m_gst_audioscale = createElement( "audioscale", m_gst_thread ) ) ) { goto error; }

    g_object_set( G_OBJECT( m_gst_volumeFade ), "volume", 1.0, NULL );
    g_signal_connect( G_OBJECT( m_gst_identity ), "handoff", G_CALLBACK( handoff_cb ), m_gst_thread );
    g_signal_connect( G_OBJECT( m_gst_audiosink ), "eos", G_CALLBACK( eos_cb ), m_gst_thread );
//     g_signal_connect ( G_OBJECT( m_thread ), "error", G_CALLBACK ( error_cb ), m_thread );

    if ( url.isLocalFile() ) {
        // Use gst's filesrc element for local files, cause it's less overhead than KIO
        if ( !( m_gst_src = createElement( "filesrc", m_gst_thread ) ) ) { goto error; }
        // Set file path
        g_object_set( G_OBJECT( m_gst_src ), "location", static_cast<const char*>( QFile::encodeName( url.path() ) ), NULL );
    }
    else {
        // Create our custom streamsrc element, which transports data into the pipeline
        m_gst_src = GST_ELEMENT( gst_streamsrc_new( m_streamBuf, &m_streamBufIndex, &m_streamBufStop ) );
        gst_bin_add ( GST_BIN ( m_gst_thread ), m_gst_src );
        g_signal_connect( G_OBJECT( m_gst_src ), "kio_resume", G_CALLBACK( kio_resume_cb ), m_gst_thread );
    }
    
    if ( !( m_gst_spider = createElement( "spider", m_gst_thread ) ) ) { goto error; }
    /* link all elements */
    gst_element_link_many( m_gst_src, m_gst_spider, m_gst_volumeFade, m_gst_identity, m_gst_volume, m_gst_audioconvert, m_gst_audioscale, m_gst_audiosink, 0 );
    
    gst_element_set_state( m_gst_thread, GST_STATE_READY );
    m_pipelineFilled = true;
    setVolume( m_volume );
    
    if ( !url.isLocalFile()  ) {
        m_streamBufIndex = 0;
        m_streamBufStop = false;  
        
        if ( !stream ) {
            // Use KIO for non-local files, except http, which is handled by TitleProxy
            m_transferJob = KIO::get( url, false, false );
            connect( m_transferJob, SIGNAL( data( KIO::Job*, const QByteArray& ) ),
                              this,   SLOT( newKioData( KIO::Job*, const QByteArray& ) ) );
            connect( m_transferJob, SIGNAL( result( KIO::Job* ) ),
                              this,   SLOT( kioFinished() ) );
        }
    }
    emit stateChanged( Engine::Idle );
    return true;

error:
    return false;
}


bool
GstEngine::play( uint )  //SLOT
{
    kdDebug() << k_funcinfo << endl;
    if ( !m_pipelineFilled ) return false;

    /* start playing */
    gst_element_set_state( m_gst_thread, GST_STATE_PLAYING );
    
    emit stateChanged( Engine::Playing );
    return true;
}


void
GstEngine::stop()  //SLOT
{
    kdDebug() << k_funcinfo << endl;
    if ( !m_pipelineFilled ) return ;
    
    // Is a fade running?
    if ( m_fadeValue == 0.0 ) {   
        // Not fading --> start fade now
        m_fadeValue = 1.0;
    }
    else
        // Fading --> stop playback
        stopNow();        
    
    emit stateChanged( Engine::Empty );
}
                
    
void
GstEngine::pause()  //SLOT
{
    kdDebug() << k_funcinfo << endl;
    if ( !m_pipelineFilled ) return ;

    if ( state() == Engine::Paused )
        gst_element_set_state( m_gst_thread, GST_STATE_PLAYING );
    else
        gst_element_set_state( m_gst_thread, GST_STATE_PAUSED );
    
    emit stateChanged( state() );
}


void
GstEngine::seek( uint ms )  //SLOT
{
    if ( !m_pipelineFilled ) return ;

    if ( ms > 0 )
    {
        GstEvent* event = gst_event_new_seek( ( GstSeekType ) ( GST_FORMAT_TIME | GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH ),
                                              ms * GST_MSECOND );

        gst_element_send_event( m_gst_audiosink, event );
    }
}


void
GstEngine::setVolumeSW( uint percent )  //SLOT
{
    if ( m_pipelineFilled )
        // We're using a logarithmic function to make the volume ramp more natural
        g_object_set( G_OBJECT( m_gst_volume ), "volume",
                     (double) 1.0 - log10( ( 100 - percent ) * 0.09 + 1.0 ), NULL );
}


void
GstEngine::newStreamData( char* buf, int size )  //SLOT
{
    if ( m_streamBufIndex + size >= STREAMBUF_SIZE ) {
        m_streamBufIndex = 0;
        kdDebug() << "Gst-Engine: Stream buffer overflow!" << endl;
    }

    // Copy data into stream buffer
    memcpy( m_streamBuf + m_streamBufIndex, buf, size );
    // Adjust index
    m_streamBufIndex += size;
}


/////////////////////////////////////////////////////////////////////////////////////
// PROTECTED
/////////////////////////////////////////////////////////////////////////////////////

void GstEngine::timerEvent( QTimerEvent* )
{
   // In this timer-event we handle the volume fading transition
   
   // Are we currently fading?
   if ( m_fadeValue > 0.0 )
   {
        m_fadeValue -= ( m_xfadeLength ) ?  1.0 / m_xfadeLength * TIMER_INTERVAL : 1.0;
        
        // Fade finished?
        if ( m_fadeValue <= 0.0 ) {
            // Fade transition has finished, stop playback
            kdDebug() << "FADEOUT finished." << endl;
            m_fadeValue = 0.0;
            cleanPipeline();
            
            if ( m_transferJob ) {
                m_transferJob->kill();
                m_transferJob = 0;
            }
        }
        
        if ( m_pipelineFilled ) {
            // Set new value for fadeout volume element
            double value = 1.0 - log10( ( 1.0 - m_fadeValue ) * 9.0 + 1.0 );
            g_object_set( G_OBJECT( m_gst_volumeFade ), "volume", value, NULL );
        }
    }
}


/////////////////////////////////////////////////////////////////////////////////////
// PRIVATE SLOTS
/////////////////////////////////////////////////////////////////////////////////////

void
GstEngine::handleError()  //SLOT
{
    kdDebug() << "Error message: " << static_cast<const char*>( error_msg->message ) << endl;
}


void
GstEngine::stopAtEnd()  //SLOT
{
    kdDebug() << k_funcinfo << endl;
    if ( !m_pipelineFilled ) return ;

    // Stop fading
    m_fadeValue = 0.0;
    
    cleanPipeline();
    m_transferJob = 0;
    
    emit trackEnded();
}


void
GstEngine::newKioData( KIO::Job*, const QByteArray& array )  //SLOT
{
    int size = array.size();
    
    if ( m_streamBufIndex >= STREAMBUF_MAX ) {
        kdDebug() << "Gst-Engine: SUSPENDING kio transfer.\n";
        if ( m_transferJob ) m_transferJob->suspend();
    }
            
    if ( m_streamBufIndex + size >= STREAMBUF_SIZE ) {
        m_streamBufIndex = 0;
        kdDebug() << "Gst-Engine: Stream buffer overflow!" << endl;
    }

    // Copy data into stream buffer
    memcpy( m_streamBuf + m_streamBufIndex, array.data(), size );
    // Adjust index
    m_streamBufIndex += size;
}


void
GstEngine::kioFinished()  //SLOT
{
    kdDebug() << k_funcinfo << endl;

    // KIO::Job deletes itself when finished, so we need to zero the pointer
    m_transferJob = 0;
    
    // Tell streamsrc: This is the end, my friend
    m_streamBufStop = true;  
}


/////////////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
/////////////////////////////////////////////////////////////////////////////////////

QStringList
GstEngine::getPluginList( const QCString& classname )
{
    GList * pool_registries = NULL;
    GList* registries = NULL;
    GList* plugins = NULL;
    GList* features = NULL;
    QStringList results;

    pool_registries = gst_registry_pool_list ();
    registries = pool_registries;

    while ( registries ) {
        GstRegistry * registry = GST_REGISTRY ( registries->data );
        plugins = registry->plugins;

        while ( plugins ) {
            GstPlugin * plugin = GST_PLUGIN ( plugins->data );
            features = gst_plugin_get_feature_list ( plugin );

            while ( features ) {
                GstPluginFeature * feature = GST_PLUGIN_FEATURE ( features->data );

                if ( GST_IS_ELEMENT_FACTORY ( feature ) ) {
                    GstElementFactory * factory = GST_ELEMENT_FACTORY ( feature );

                    if ( g_strrstr ( factory->details.klass, classname ) )
                        results << g_strdup ( GST_OBJECT_NAME ( factory ) );
                }
                features = g_list_next ( features );
            }
            plugins = g_list_next ( plugins );
        }
        registries = g_list_next ( registries );
    }
    g_list_free ( pool_registries );
    pool_registries = NULL;

    return results;
}


GstElement*
GstEngine::createElement( const QCString& factoryName, GstElement* bin, const QCString& name )
{
    GstElement* element = gst_element_factory_make( factoryName, name );

    if ( element ) {
        if ( bin ) gst_bin_add( GST_BIN( bin ), element );
    }
    else {
        KMessageBox::error( 0,
            i18n( "<h3>GStreamer could not create the element: <i>%1</i></h3> "
                  "<p>Please make sure that you have installed all necessary GStreamer plugins, and run <i>'gst-register'</i> afterwards.</p>"
                  "<p>For further assistance consult the GStreamer manual, and join #gstreamer on irc.freenode.net.</p>" ).arg( factoryName ) );
        gst_object_unref( GST_OBJECT( bin ) );
    }

    return element;
}


void
GstEngine::stopNow()
{    
    m_fadeValue = 0.0;
    cleanPipeline();
    
    if ( m_transferJob ) {
        m_transferJob->kill();
        m_transferJob = 0;
    }
}


void
GstEngine::cleanPipeline()
{
    if ( m_pipelineFilled ) {
        gst_element_set_state ( m_gst_thread, GST_STATE_NULL );
        gst_object_unref( GST_OBJECT( m_gst_thread ) );
        m_pipelineFilled = false;
    }
}


void
GstEngine::interpolate( const Engine::Scope& inVec, Engine::Scope& outVec )
{
    double pos = 0.0;
    const double step = (double) m_scopeBufIndex / outVec.size();

    for ( uint i = 0; i < outVec.size(); ++i, pos += step ) {
        unsigned long index = (unsigned long) pos;

        if ( index >= m_scopeBufIndex )
            index = m_scopeBufIndex - 1;

        outVec[i] = inVec[index];
    }
}


void
GstEngine::errorNoOutput() const
{
    KMessageBox::error( 0,
        i18n( "<h3>No sound output selected!</h3>"
              "<p>Please select an <i>output plugin</i> in the engine settings dialog.</p>" ) );
}
                  
                  
#include "gstengine.moc"


