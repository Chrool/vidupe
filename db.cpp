#include <QApplication>
#include <QCryptographicHash>
#include <QSqlQuery>
#include "db.h"
#include "video.h"
#include <QSqlError>

Db::Db(const QString &connectionParam, QWidget *mainwPtr)
{
    _connection = connectionParam;       //connection name is unique (generated from full path+filename)

    const QString dbfilename = QStringLiteral("%1/cache.db").arg(QApplication::applicationDirPath());
    connect(this, SIGNAL(sendStatusMessage(const QString &)), mainwPtr, SLOT(addStatusMessage(const QString &)));

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
                              "codec TEXT, audio TEXT, width INTEGER, height INTEGER, access_date INTEGER);"));

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
/*
//make hashmap
void Db::populateMetadatas(const QHash<QString, Video *> _everyVideo) const
{
    QSqlQuery query(_db);
    QString inArgs = "";
    int count = 0;
    const int limit = 5000;
    int now = QDateTime::currentSecsSinceEpoch();
    //delete from capture where id in (select  capture.id from capture left join metadata on metadata.id = capture.id where metadata.id is null    )
    //run compact databsae in menu
    QString updateQuery = QStringLiteral("UPDATE metadata set access_date = '%1' WHERE id in ").arg(now);

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
            query.exec(updateQuery + QStringLiteral("(%1);").arg(inArgs));
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
             inArgs = "";
        }
    }
}
*/

void Db::populateMetadatas(const QHash<QString, Video *> _everyVideo) const
{
    const int limit = 5000;
    int now = QDateTime::currentSecsSinceEpoch();

    QSqlQuery updateQuery(_db);
    QSqlQuery selectQuery(_db);

    QStringList batchIds;
    QHashIterator<QString, Video *> i(_everyVideo);

    while (i.hasNext()) {
        i.next();
        batchIds << i.key();

        if (batchIds.size() == limit || !i.hasNext()) {
            // Prepare placeholders
            QStringList quotedItems;
            for (const QString &item : batchIds)
                quotedItems << QString("'%1'").arg(item);

            QString inClause = quotedItems.join(", ");

            // Update access_date
            updateQuery.prepare("UPDATE metadata SET access_date = " + QString(now) + " WHERE id IN (" + inClause + ")");
            //updateQuery.addBindValue(now);

            if (!updateQuery.exec()) {
                qWarning() << "Update failed:" << updateQuery.lastError().text();
                emit sendStatusMessage(QString("Update failed: %1").arg(updateQuery.lastError().text()));
            }

            // Select metadata
            QString select = "SELECT * FROM metadata WHERE id IN (" + inClause + ")";
            selectQuery.prepare(select);

            if (!selectQuery.exec()) {
                qWarning() << "Select failed:" << selectQuery.lastError().text();
                emit sendStatusMessage(QString("Select failed: %1").arg(selectQuery.lastError().text()));
            } else {
                while (selectQuery.next()) {
                    QString id = selectQuery.value("id").toString();
                    if (!_everyVideo.contains(id)){
                        qWarning() << "Id not found:" << id;
                        continue;
                    }

                    Video *video = _everyVideo[id];
                    video->size = selectQuery.value("size").toLongLong();
                    video->duration = selectQuery.value("duration").toLongLong();
                    video->bitrate = selectQuery.value("bitrate").toInt();
                    video->framerate = selectQuery.value("framerate").toDouble();
                    video->codec = selectQuery.value("codec").toString();
                    video->audio = selectQuery.value("audio").toString();
                    video->width = static_cast<short>(selectQuery.value("width").toInt());
                    video->height = static_cast<short>(selectQuery.value("height").toInt());
                    video->cachedMetadata = true;
                }
            }

            batchIds.clear();
        }
    }
}

//make hashmap
void Db::populateCaptures(const QHash<QString, Video *> _everyVideo, const QVector<int> &percentages) const
{
    QSqlQuery query(_db);
    QString inArgs = "";
    QString args = "";
    int count = 0;
    const int limit = 2000;
    int overallCount = 0;

    QHashIterator<QString, Video *> i(_everyVideo);
    for(auto percentage : percentages)
    {
        if(args.length() == 0){
           args = "SELECT id, at" + QString::number(percentage);
        } else {
            args += ", at" + QString::number(percentage);
        }
    }

    while (i.hasNext()) {
        i.next();
        if(inArgs.length() == 0){
           inArgs = QStringLiteral("'%1'").arg(i.key());
        } else {
            inArgs += QStringLiteral(", '%1'").arg(i.key());
        }

        count++;
        overallCount++;
        if(count == limit || !i.hasNext()){
            const QString query_string = args + QStringLiteral(" FROM capture WHERE id in (%1);").arg(inArgs);

             query.exec(query_string);

             while(query.next()){
                 const QString id = query.value("id").toString();
                 Video *video = _everyVideo[id];
                 for(auto percentage : percentages)
                 {
                      video->captures[percentage] = nullptr;
                 }
                 for(auto percentage : percentages)
                 {
                     video->captures[percentage] = query.value(QStringLiteral("at%1").arg(percentage)).toByteArray();
                 }
             }
             count = 0;
             inArgs = "";
             query.clear();
        }
    }
}
void Db::writeMetadata(const Video &video) const
{
    int now = QDateTime::currentSecsSinceEpoch();

    QSqlQuery query(_db);
    query.prepare("INSERT OR REPLACE INTO metadata "
                  "(id, size, duration, bitrate, framerate, codec, audio, width, height, access_date) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    query.addBindValue(video.id);
    query.addBindValue(video.size);
    query.addBindValue(video.duration);
    query.addBindValue(video.bitrate);
    query.addBindValue(video.framerate);
    query.addBindValue(video.codec);
    query.addBindValue(video.audio);
    query.addBindValue(video.width);
    query.addBindValue(video.height);
    query.addBindValue(now);

    if (!query.exec()) {
        qWarning() << "Failed to insert metadata:" << query.lastError().text();
        emit sendStatusMessage(QString("Select failed: %1").arg(query.lastError().text()));
    }
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
