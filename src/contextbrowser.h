// (c) 2004 Christian Muehlhaeuser <chris@chris.de>
// See COPYING file for licensing information

#ifndef AMAROK_CONTEXTBROWSER_H
#define AMAROK_CONTEXTBROWSER_H

#include "engineobserver.h"
#include <qvbox.h>

class CollectionDB;
class Color;
class MetaBundle;

class QPalette;

class KHTMLPart;
class KTabBar;
class KTempFile;

namespace KIO { class Job; }

class ContextBrowser : public QVBox, public EngineObserver
{
    Q_OBJECT

    public:
        ContextBrowser( const char *name );
        ~ContextBrowser();

        virtual void setFont( const QFont& );

    public slots:
        void openURLRequest(const KURL &url );
        void collectionScanStarted();
        void collectionScanDone();

    protected:
        void engineNewMetaData( const MetaBundle&, bool );
        void engineStateChanged( Engine::State );
        void paletteChange( const QPalette& );

    private slots:
        void tabChanged( int );
        void slotContextMenu( const QString& urlString, const QPoint& point );
        void showHome();
        void showCurrentTrack();
        void showLyrics( const QString& hash = QString::null );
        void showLyricSuggestions();

        void lyricsData( KIO::Job* job, const QByteArray& data );
        void lyricsResult( KIO::Job* job );
        void coverFetched( const QString &artist, const QString &album );
        void similarArtistsFetched( const QString &artist );

    private:
        void setStyleSheet();
        void setStyleSheet_Default( QString& styleSheet );
        void setStyleSheet_ExternalStyle( QString& styleSheet, QString& themeName );
        void showIntroduction();
        void saveHtmlData();
        void showScanning();
        
        KHTMLPart    *browser;
        KTabBar      *m_tabBar;

        int           m_tabHome;
        int           m_tabCurrent;
        int           m_tabLyrics;

        QString       m_styleSheet;
        bool          m_emptyDB;
        QString       m_lyrics;
        QStringList   m_lyricSuggestions;
        QStringList   m_lyricHashes;
        QString       m_lyricUrl;
        QString       m_HTMLSource;
        KTempFile    *m_bgGradientImage;
        KTempFile    *m_headerGradientImage;
        KTempFile    *m_shadowGradientImage;
        QStringList   m_metadataHistory;
        KURL          m_currentURL;
};

#endif /* AMAROK_CONTEXTBROWSER_H */
