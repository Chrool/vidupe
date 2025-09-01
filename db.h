#ifndef DB_H
#define DB_H

#include <QSqlDatabase>
#include <QDateTime>
#include <QDialog>

class Video;

class Db : public QObject {
    Q_OBJECT

public:
    Db(const QString &filename, QWidget *mainwPtr);
    ~Db() { _db.close(); _db = QSqlDatabase(); _db.removeDatabase(_connection); }

private:
    QSqlDatabase _db;
    QString _connection;
    //QString _id;
    //QDateTime _modified;

signals:
    void sendStatusMessage(const QString &message) const;

public:
    //return md5 hash of parameter's file, or (as convinience) md5 hash of the file given to constructor
    static QString uniqueId(const QString &filename, const QDateTime &dateMod, const QString &id);

    //constructor creates a database file if there is none already
    void createTables() const;

    //return true and updates member variables if the video metadata was cached
    bool readMetadata(Video &video) const;

    //save video properties in cache
    void writeMetadata(const Video &video) const;

    //returns screen capture if it was cached, else return null ptr
    QByteArray readCapture(const QString &id, const int &percent) const;

    //returns screen capture if it was cached, else return null ptr
    QHash<int, QByteArray> readCaptures(const QString &id, const QVector<int> &percentages) const;

    QHash<int, QByteArray> readCapturesOfVideos(const QVector<QString> &ids, const QVector<int> &percentages) const;

    //save image in cache
    void writeCapture(const QString &id, const int &percent, const QByteArray &image) const;

    //returns false if id not cached or could not be removed
    bool removeVideo(const QString &id) const;

    void populateMetadatas(const QHash<QString, Video *> _everyVideo) const;

    void populateCaptures(const QHash<QString, Video *> _everyVideo, const QVector<int> &percentages) const;
};

#endif // DB_H
