// (c) 2004 Mark Kretschmann <markey@web.de>
// (c) 2004 Christian Muehlhaeuser <chris@chris.de>
// See COPYING file for licensing information.

#ifndef AMAROK_COLLECTIONDB_H
#define AMAROK_COLLECTIONDB_H

#include "engineobserver.h"
#include "sqlite/sqlite3.h"

#include <qdir.h>            //stack allocated
#include <qobject.h>         //baseclass
#include <qstringlist.h>     //stack allocated

class CollectionEmitter;
class MetaBundle;
class ThreadWeaver;

class CollectionDB : public QObject
{
    Q_OBJECT

    public:
        CollectionDB();
        ~CollectionDB();

        static CollectionEmitter* emitter() { return s_emitter; }

        //sql helper methods
        QStringList query( const QString& statement, QStringList* const names = 0, bool debug = false );
        int sqlInsertID();
        QString escapeString( QString string );

        //table management methods
        bool isValid();
        bool isEmpty();
        void createTables( const bool temporary = false );
        void dropTables( const bool temporary = false );
        void moveTempTables();
        void createStatsTable();
        void dropStatsTable();

        void updateTags( const QString &url, const MetaBundle &bundle, bool updateCB=true );
        void updateTag( const QString &url, const QString &field, const QString &newTag );

        //general management methods
        void scan( const QStringList& folders, bool recursively, bool importPlaylists );
        void scanModifiedDirs( bool recursively, bool importPlaylists );
        bool isDirInCollection( QString path );
        bool isFileInCollection( const QString url );
        void removeDirFromCollection( QString path );
        void updateDirStats( QString path, const long datetime );
        void removeSongsInDir( QString path );
        void purgeDirCache();

        //song methods
        bool getMetaBundleForUrl( const QString url, MetaBundle *bundle );
        void addAudioproperties( const MetaBundle& bundle );
        int addSongPercentage( const QString url, const int percentage );
        int getSongPercentage( const QString url );
        void setSongPercentage( const QString url, int percentage );

        //album methods
        bool isSamplerAlbum( const QString album );
        QString albumSongCount( const QString artist_id, const QString album_id );
        QString getPathForAlbum( const uint artist_id, const uint album_id );
        QString getPathForAlbum( const QString artist, const QString album );

        //list methods
        QStringList artistList( bool withUnknown = true, bool withCompilations = true );
        QStringList albumList( bool withUnknown = true, bool withCompilations = true );
        QStringList albumListOfArtist( const QString artist, bool withUnknown = true, bool withCompilations = true );
        QStringList artistAlbumList( bool withUnknown = true, bool withCompilations = true );

        //cover management methods
        /** Saves images located on the user's filesystem */
        bool setAlbumImage( const QString& artist, const QString& album, const KURL& url );
        /** Saves images obtained from CoverFetcher */
        bool setAlbumImage( const QString& artist, const QString& album, QImage img, const QString& amazonUrl = QString::null );

        QString albumImage( const uint artist_id, const uint album_id, const uint width = 1 );
        QString albumImage( const QString artist, const QString album, const uint width = 1 );

        bool removeAlbumImage( const uint artist_id, const uint album_id );
        bool removeAlbumImage( const QString artist, const QString album );

        void addImageToPath( const QString path, const QString image, bool temporary );
        QString getImageForPath( const QString path, uint width = 0 );

        uint artistID( QString value, bool autocreate = true, bool useTempTables = false );
        QString artistValue( uint id );
        uint albumID( QString value, bool autocreate = true, bool useTempTables = false );
        QString albumValue( uint id );
        uint genreID( QString value, bool autocreate = true, bool useTempTables = false );
        QString genreValue( uint id );
        uint yearID( QString value, bool autocreate = true, bool useTempTables = false );
        QString yearValue( uint id );

        //tree methods
        void retrieveFirstLevel( QString category1, QString category2, QString category3,
                                            QString filter, QStringList* const values, QStringList* const names );
        void retrieveSecondLevel( QString itemText, QString category1, QString category2, QString category3,
                                                 QString filter, QStringList* const values, QStringList* const names );
        void retrieveThirdLevel( QString itemText1, QString itemText2, QString category1, QString category2,
                                             QString category3, QString filter, QStringList* const values, QStringList* const names );
        void retrieveFourthLevel( QString itemText1, QString itemText2, QString itemText3, QString category1,
                                               QString category2, QString category3, QString filter,
                                               QStringList* const values, QStringList* const names );

        void retrieveFirstLevelURLs( QString itemText, QString category1, QString category2, QString category3,
                                                    QString filter, QStringList* const values, QStringList* const names );
        void retrieveSecondLevelURLs( QString itemText1, QString itemText2, QString category1, QString category2,
                                                         QString category3, QString filter,
                                                         QStringList* const values, QStringList* const names );
        void retrieveThirdLevelURLs( QString itemText1, QString itemText2, QString itemText3, QString category1,
                                                     QString category2, QString category3, QString filter,
                                                     QStringList* const values, QStringList* const names );

        //member variables
        QString m_amazonLicense;

        QStringList m_values;
        QStringList m_names;

    public slots:
        void fetchCover( QObject* parent, const QString& artist, const QString& album, bool noedit );
        void stopScan();

    private slots:
        void dirDirty( const QString& path );
        void saveCover( const QString& keyword, const QString& url, const QImage& image );
        void fetcherError();

    private:
        void customEvent( QCustomEvent* );
        uint IDFromValue( QString name, QString value, bool autocreate = true, bool useTempTables = false );
        QString valueFromID( QString table, uint id );

        static CollectionEmitter* s_emitter;
        sqlite3* m_db;
        ThreadWeaver* m_weaver;
        bool m_monitor;
        QDir m_cacheDir;
        QDir m_coverDir;

        QString m_cacheArtist;
        uint m_cacheArtistID;
        QString m_cacheAlbum;
        uint m_cacheAlbumID;
};


class CollectionEmitter : public QObject, public EngineObserver
{
    Q_OBJECT

    friend class CollectionDB;

    public:
        CollectionEmitter();

    protected:
        void engineTrackEnded( int finalPosition, int trackLength );

    signals:
        void scanStarted();
        void scanDone( bool changed );

        void scoreChanged( const QString &url, int score );
        void metaDataEdited( const MetaBundle &bundle );

        void coverFetched( const QString &keyword );
        void coverFetched();
        void coverFetcherError();
};


#endif /* AMAROK_COLLECTIONDB_H */
