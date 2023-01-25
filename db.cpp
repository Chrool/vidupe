#include <QApplication>
#include <QCryptographicHash>
#include <QSqlQuery>
#include "db.h"
#include "video.h"

Db::Db(const QString &connectionParam)
{
    _connection = connectionParam;       //connection name is unique (generated from full path+filename)

    const QString dbfilename = QStringLiteral("%1/cache.db").arg(QApplication::applicationDirPath());
    _db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), _connection);
    _db.setDatabaseName(dbfilename);
    _db.open();

    //createTables();
}

QString Db::uniqueId(const QString &filename, const QDateTime &dateMod, const QString &id)
{
    if(filename.isEmpty())
        return id;

    const QString name_modified = QStringLiteral("%1_%2").arg(filename)
                                  .arg(dateMod.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")));
    return QCryptographicHash::hash(name_modified.toLatin1(), QCryptographicHash::Md5).toHex();
}

void Db::createTables() const
{
    QSqlQuery query(_db);
    query.exec(QStringLiteral("PRAGMA synchronous = OFF;"));
    query.exec(QStringLiteral("PRAGMA journal_mode = WAL;"));

    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS metadata (id TEXT PRIMARY KEY, "
                              "size INTEGER, duration INTEGER, bitrate INTEGER, framerate REAL, "
                              "codec TEXT, audio TEXT, width INTEGER, height INTEGER);"));

    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS capture (id TEXT PRIMARY KEY, "
                              " at8 BLOB, at16 BLOB, at24 BLOB, at32 BLOB, at36 BLOB, at40 BLOB, at48 BLOB, at52 BLOB, "
                              "at56 BLOB, at60 BLOB, at64 BLOB, at68 BLOB, at72 BLOB, at80 BLOB, at88 BLOB, at96 BLOB);"));

    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS version (version TEXT PRIMARY KEY);"));
    query.exec(QStringLiteral("INSERT OR REPLACE INTO version VALUES('%1');").arg(APP_VERSION));
}

bool Db::readMetadata(Video &video) const
{
    QSqlQuery query(_db);
    query.exec(QStringLiteral("SELECT * FROM metadata WHERE id = '%1';").arg(video.id));

    while(query.next())
    {
        video.modified = video.modified;
        video.size = query.value(1).toLongLong();
        video.duration = query.value(2).toLongLong();
        video.bitrate = query.value(3).toInt();
        video.framerate = query.value(4).toDouble();
        video.codec = query.value(5).toString();
        video.audio = query.value(6).toString();
        video.width = static_cast<short>(query.value(7).toInt());
        video.height = static_cast<short>(query.value(8).toInt());
        return true;
    }
    return false;
}

//make hashmap
void Db::populateMetadatas(const QHash<QString, Video *> _everyVideo) const
{
    QSqlQuery query(_db);
    QString inArgs = "";
    int count = 0;
    const int limit = 1000;

    QHashIterator<QString, Video *> i(_everyVideo);

    while (i.hasNext()) {
        i.next();
        if(inArgs.length() == 0){
           inArgs = QStringLiteral("'%1'").arg(i.key());
        } else {
            inArgs += QStringLiteral(", '%1'").arg(i.key());
        }
        count++;
        if(count == limit || !i.hasNext()){
             query.exec(QStringLiteral("SELECT * FROM metadata WHERE id in (%1);").arg(inArgs));

             while(query.next()){
                 const QString id = query.value("id").toString();
                 Video *video = _everyVideo[id];
                 video->size = query.value(1).toLongLong();
                 video->duration = query.value(2).toLongLong();
                 video->bitrate = query.value(3).toInt();
                 video->framerate = query.value(4).toDouble();
                 video->codec = query.value(5).toString();
                 video->audio = query.value(6).toString();
                 video->width = static_cast<short>(query.value(7).toInt());
                 video->height = static_cast<short>(query.value(8).toInt());
                 video->cachedMetadata = true;
             }
             count = 0;
        }
    }
}

void Db::writeMetadata(const Video &video) const
{
    QSqlQuery query(_db);
    query.exec(QStringLiteral("INSERT OR REPLACE INTO metadata VALUES('%1',%2,%3,%4,%5,'%6','%7',%8,%9);")
               .arg(video.id).arg(video.size).arg(video.duration).arg(video.bitrate).arg(video.framerate)
               .arg(video.codec).arg(video.audio).arg(video.width).arg(video.height));
}

QByteArray Db::readCapture(const QString &id, const int &percent) const
{
    QSqlQuery query(_db);
    query.exec(QStringLiteral("SELECT at%1 FROM capture WHERE id = '%2';").arg(percent).arg(id));

    while(query.next())
        return query.value(0).toByteArray();
    return nullptr;
}

QHash<int, QByteArray>  Db::readCaptures(const QString &id, const QVector<int> &percentages) const
{
    QSqlQuery query(_db);
    QString args = "";
    QHash<int, QByteArray> result;

    for(auto percentage : percentages)
    {
        if(args.length() == 0){
           args = "SELECT at" + QString::number(percentage);
        } else {
            args += ", at" + QString::number(percentage);
        }
    }
    query.exec(args + QStringLiteral(" FROM capture WHERE id = '%1';").arg(id));

    for(auto percentage : percentages)
    {
        result[percentage] = nullptr;
    }
    while(query.next()){
        for(auto percentage : percentages)
        {
            result[percentage] = query.value(QStringLiteral("at%1").arg(percentage)).toByteArray();
        }
    }
    return result;
}


QHash<int, QByteArray>  Db::readCapturesOfVideos(const QVector<QString> &ids, const QVector<int> &percentages) const
{
    QSqlQuery query(_db);
    QString args = "";
    QString inArgs = "";
    QHash<int, QByteArray> result;

    for(auto percentage : percentages)
    {
        if(args.length() == 0){
           args = "SELECT id, at" + QString::number(percentage);
        } else {
            args += ", at" + QString::number(percentage);
        }
    }

    for(auto id : ids)
    {
        if(inArgs.length() == 0){
           inArgs = QStringLiteral("'%1'").arg(id);
        } else {
            inArgs += QStringLiteral(", '%1'").arg(id);
        }
    }
    query.exec(args + QStringLiteral(" FROM capture WHERE id in (%1);").arg(inArgs));

    for(auto percentage : percentages)
    {
        result[percentage] = nullptr;
    }
    while(query.next()){
        const QString id = query.value("id").toString();
        for(auto percentage : percentages)
        {
            result[percentage] = query.value(QStringLiteral("at%1").arg(percentage)).toByteArray();
        }
    }
    return result;
}

void Db::writeCapture(const QString &id, const int &percent, const QByteArray &image) const
{
    QSqlQuery query(_db);
    query.exec(QStringLiteral("INSERT OR IGNORE INTO capture (id) VALUES('%1');").arg(id));

    query.prepare(QStringLiteral("UPDATE capture SET at%1 = :image WHERE id = '%2';").arg(percent).arg(id));
    query.bindValue(QStringLiteral(":image"), image);
    query.exec();
}

bool Db::removeVideo(const QString &id) const
{
    QSqlQuery query(_db);

    bool idCached = false;
    query.exec(QStringLiteral("SELECT id FROM metadata WHERE id = '%1';").arg(id));
    while(query.next())
        idCached = true;
    if(!idCached)
        return false;

    query.exec(QStringLiteral("DELETE FROM metadata WHERE id = '%1';").arg(id));
    query.exec(QStringLiteral("DELETE FROM capture WHERE id = '%1';").arg(id));

    query.exec(QStringLiteral("SELECT id FROM metadata WHERE id = '%1';").arg(id));
    while(query.next())
        return false;
    return true;
}
