#include <QMessageBox>
#include <QWheelEvent>
#include "comparison.h"
#include "ui_comparison.h"
#include <omp.h>

Comparison::Comparison(const QVector<Video *> &videosParam, const Prefs &prefsParam) :
    QDialog(prefsParam._mainwPtr, Qt::Window), _videos(videosParam), _prefs(prefsParam)
{
    ui = new Ui::Comparison;
    ui->setupUi(this);

    connect(this, SIGNAL(sendStatusMessage(const QString &)), _prefs._mainwPtr, SLOT(addStatusMessage(const QString &)));
    connect(this, SIGNAL(switchComparisonMode(const int &)),  _prefs._mainwPtr, SLOT(setComparisonMode(const int &)));
    connect(this, SIGNAL(adjustThresholdSlider(const int &)), _prefs._mainwPtr, SLOT(on_thresholdSlider_valueChanged(const int &)));
    //connect(this, SIGNAL(adjustThresholdSliderMax(const int &)), _prefs._mainwPtr, SLOT(on_thresholdSliderMax_valueChanged(const int &)));

    if(_prefs._comparisonMode == _prefs._SSIM)
        ui->selectSSIM->setChecked(true);
    ui->thresholdSlider->setValue(QVariant(_prefs._thresholdSSIM * 100).toInt());
    ui->thresholdSliderMax->setValue(QVariant(_prefs._thresholdSSIMMax * 100).toInt());
    ui->progressBar->setMaximum(100);

    on_preprocessVideo_clicked();
    on_nextVideo_clicked();
}

Comparison::~Comparison()
{
    delete ui;
}

void Comparison::reportMatchingVideos()
{
    int64_t combinedFilesize = 0;
    int foundMatches = 0;

    QVector<Video*>::const_iterator left, right, end = _videos.cend();
    for(left=_videos.cbegin(); left<end; left++)
        for(right=left+1; right<end; right++)
            if(bothVideosMatch(*left, *right))
            {   //smaller of two matching videos is likely the one to be deleted
                combinedFilesize += std::min((*left)->size , (*right)->size);
                foundMatches++;
                break;
            }

    if(foundMatches)
        emit sendStatusMessage(QStringLiteral("\n[%1] Found %2 video(s) (%3) with one or more matches")
             .arg(QTime::currentTime().toString()).arg(foundMatches).arg(readableFileSize(combinedFilesize)));
}

void Comparison::confirmToExit()
{
    int confirm = QMessageBox::Yes;
    if(!ui->leftFileName->text().isEmpty())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(QStringLiteral("Out of videos to compare"));
        msgBox.setText(QStringLiteral("Close window?                  "));
        msgBox.setIcon(QMessageBox::QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::Yes|QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);
        confirm = msgBox.exec();
    }
    if(confirm == QMessageBox::Yes)
    {
        if(_videosDeleted)
            emit sendStatusMessage(QStringLiteral("\n%1 file(s) deleted, %2 freed")
                                   .arg(_videosDeleted).arg(readableFileSize(_spaceSaved)));
        if(!ui->leftFileName->text().isEmpty())
            emit sendStatusMessage(QStringLiteral("\nPressing Find duplicates button opens comparison window "
                                                 "again if thumbnail mode and directories remain the same"));
        else
            emit sendStatusMessage(QStringLiteral("\nComparison window closed because no matching videos found "
                                                 "(a lower threshold may help to find more matches)"));

        QKeyEvent *closeEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::postEvent(this, closeEvent);  //"pressing" ESC closes dialog
    }
}

Video* Comparison::get_left_video() { return _preprocessedVideos.length() > 0 ? _preprocessedVideos[_vectorIndex].first : _videos[_leftVideo];}

Video* Comparison::get_left_video() const { return _preprocessedVideos.length() > 0 ? _preprocessedVideos[_vectorIndex].first : _videos[_leftVideo];}

Video* Comparison::get_right_video() { return _preprocessedVideos.length() > 0 ? _preprocessedVideos[_vectorIndex].second : _videos[_rightVideo];}

Video* Comparison::get_right_video() const { return _preprocessedVideos.length() > 0 ? _preprocessedVideos[_vectorIndex].second : _videos[_rightVideo];}



void Comparison::on_prevVideo_clicked()
{
    _seekForwards = false;

    if(_preprocessedVideos.length() > 0){
        while(1){
            _vectorIndex-=1;
            if(_vectorIndex < 0){
                    _vectorIndex = 0;
                    confirmToExit();
                    return;
            }

            const QPair<Video *, Video *> pair = _preprocessedVideos[_vectorIndex];
            if(QFileInfo::exists(pair.first->filename) && QFileInfo::exists(pair.second->filename)){
                showVideo(QStringLiteral("left"));
                showVideo(QStringLiteral("right"));
                highlightBetterProperties();
                if(_similarityMap.contains(pair)){
                    if(_prefs._comparisonMode == _prefs._PHASH)
                        _phashSimilarity = _similarityMap[pair];
                    if(_prefs._comparisonMode == _prefs._SSIM)
                        _ssimSimilarity = _similarityMap[pair];
                }
                updateUI();
                ui->progressBar->setValue(comparisonsSoFar());
                return;
            }
        }
    }

    emit sendStatusMessage(QString("no preproccessed, skipping"));

    QVector<Video*>::const_iterator left, right, begin = _videos.cbegin();
    for(_rightVideo--, left=begin+_leftVideo; left>=begin; left--, _leftVideo--)
    {
        for(right=begin+_rightVideo; right>left; right--, _rightVideo--)
            if(bothVideosMatch(*left, *right) && QFileInfo::exists((*left)->filename) && QFileInfo::exists((*right)->filename))
            {
                showVideo(QStringLiteral("left"));
                showVideo(QStringLiteral("right"));
                highlightBetterProperties();
                updateUI();
                return;
            }
        ui->progressBar->setValue(comparisonsSoFar());
        _rightVideo = _prefs._numberOfVideos - 1;
    }

    on_nextVideo_clicked();     //went over limit, go forwards until first match
}

void Comparison::on_nextVideo_clicked()
{
    _seekForwards = true;
    const int oldLeft = _leftVideo;
    const int oldRight = _rightVideo;

    if(_preprocessedVideos.length() > 0){
        emit sendStatusMessage(QString("using %1 preproccessed").arg(_preprocessedVideos.length()));
        while(1){
            _vectorIndex+=1;
            if(_vectorIndex >= _preprocessedVideos.length()){
                    _vectorIndex = _preprocessedVideos.length() - 1;
                    confirmToExit();
                    return;
            }
            emit sendStatusMessage(QString("%1 preproccessed index").arg(_vectorIndex));

            const QPair<Video *, Video *> pair = _preprocessedVideos[_vectorIndex];
            if(QFileInfo::exists(pair.first->filename) && QFileInfo::exists(pair.second->filename)){
                showVideo(QStringLiteral("left"));
                showVideo(QStringLiteral("right"));
                highlightBetterProperties();
                if(_similarityMap.contains(pair)){
                    if(_prefs._comparisonMode == _prefs._PHASH)
                        _phashSimilarity = _similarityMap[pair];
                    if(_prefs._comparisonMode == _prefs._SSIM)
                        _ssimSimilarity = _similarityMap[pair];
                }
                updateUI();
                ui->progressBar->setValue(comparisonsSoFar());

                return;
            } else {
                emit sendStatusMessage(QString("file %1 not exist").arg(pair.first->filename));
                emit sendStatusMessage(QString("file %1 not exist").arg(pair.second->filename));
            }
        }
    }

    emit sendStatusMessage(QString("no preproccessed, skipping"));

    QVector<Video*>::const_iterator left, right, begin = _videos.cbegin(), end = _videos.cend();
    for(left=begin+_leftVideo; left<end; left++, _leftVideo++)
    {
        for(_rightVideo++, right=begin+_rightVideo; right<end; right++, _rightVideo++)
            if(bothVideosMatch(*left, *right) && QFileInfo::exists((*left)->filename) && QFileInfo::exists((*right)->filename))
            {
                showVideo(QStringLiteral("left"));
                showVideo(QStringLiteral("right"));
                highlightBetterProperties();
                updateUI();
                return;
            }
        ui->progressBar->setValue(comparisonsSoFar());
        _rightVideo = _leftVideo + 1;
    }

    _leftVideo = oldLeft;       //went over limit, go to last matching pair
    _rightVideo = oldRight;
    confirmToExit();
}

void Comparison::on_preprocessVideo_clicked()
{
    _seekForwards = true;
    _preprocessedVideos.clear();
    _similarityMap.clear();

    //QVector<Video*>::const_iterator left, right, begin = _videos.cbegin(), end = _videos.cend() - 1;

    //QVector<QPair<Video *, Video *>> vec_private;
    #pragma omp parallel for
    for(int i = 0; i < _videos.size(); i++)
    {
        for(int j=i+1; j < _videos.size(); j++)
        {
            const double similarity = bothVideosMatch(_videos.at(i), _videos.at(j));
            if(similarity)
            {
                #pragma omp critical
                {
                    QPair<Video *, Video *> _pair = QPair<Video *, Video *>();
                    _pair.first = _videos.at(i);
                    _pair.second = _videos.at(j);
                    //_preprocessedVideos.append(_pair);
                    _preprocessedVideos.push_back(_pair);
                    if(_prefs._comparisonMode == _prefs._PHASH)
                        _similarityMap[_pair] = similarity;
                    if(_prefs._comparisonMode == _prefs._SSIM)
                        _similarityMap[_pair] = similarity;
                }
            }
        }
    }

    emit sendStatusMessage(QString("Preprocessed %1 matches").arg(_preprocessedVideos.length()));


    //_preprocessedVideos.append(vec_private);

    _leftVideo=0;
    _rightVideo=0;
    _vectorIndex=-1;

    emit sendStatusMessage(QString("Preprocessed %1 matches").arg(_preprocessedVideos.length()));
    on_nextVideo_clicked();
}

double Comparison::bothVideosMatch(const Video *left, const Video *right)
{
    _phashSimilarity = 0;
    double phashSimilarityResult = 0;

    //size and time filters
    const QString filter_out = "error";

    if(left->filename.contains(filter_out))
        return false;

    if(right->filename.contains(filter_out))
        return false;

    if(left->size < _prefs._minSizeBytes)
        return false;

    if(left->duration < _prefs._minTimeMs)
        return false;

    if(right->size < _prefs._minSizeBytes)
        return false;

    if(right->duration < _prefs._minTimeMs)
        return false;

    const int hashes = _prefs._thumbnails == cutEnds? 16 : 1;
    for(int left_hash=0; left_hash<hashes; left_hash++)
    {                               //if cutEnds mode: similarity is always the best one of both comparisons
        for(int right_hash=0; right_hash<hashes; right_hash++)
        {
            phashSimilarityResult = qMax((int)phashSimilarityResult, phashSimilarity(left, right, left_hash, right_hash));
            _phashSimilarity = phashSimilarityResult;
            if(_prefs._comparisonMode == _prefs._PHASH)
            {
                if(phashSimilarityResult >= _prefs._thresholdPhash && phashSimilarityResult <= _prefs._thresholdPhashMax )
                    return phashSimilarityResult;
            }                           //ssim comparison is slow, only do it if pHash differs at most 20 bits of 64
            else if(phashSimilarityResult >= qMax(_prefs._thresholdPhash, 44))
            {
                phashSimilarityResult = ssim(left->grayThumb[left_hash], right->grayThumb[right_hash], _prefs._ssimBlockSize);
                phashSimilarityResult = phashSimilarityResult + _durationModifier / 64.0;   // b/64 bits (phash) <=> p/100 % (ssim)
                _phashSimilarity = phashSimilarityResult;
                if(phashSimilarityResult > _prefs._thresholdSSIM && phashSimilarityResult <= _prefs._thresholdSSIMMax){
                    return phashSimilarityResult;
                }
            }
        }
    }
    return 0;
}

int Comparison::phashSimilarity(const Video *left, const Video *right, const int &leftHash, const int &rightHash)
{
    if(left->hash[leftHash] == 0 && right->hash[rightHash] == 0)
        return 0;

    int distance = 64;
    uint64_t differentBits = left->hash[leftHash] ^ right->hash[rightHash];    //XOR to value (only ones for differing bits)
    while(differentBits)
    {
        differentBits &= differentBits - 1;                 //count number of bits of value
        distance--;
    }

    if( qAbs(left->duration - right->duration) <= 1000 )
        _durationModifier = 0 + _prefs._sameDurationModifier;               //lower distance if both durations within 1s
    else
        _durationModifier = 0 - _prefs._differentDurationModifier;          //raise distance if both durations differ 1s

    distance = distance + _durationModifier;
    return distance > 64? 64 : distance;
}

void Comparison::showVideo(const QString &side) const
{
    Video* thisVideo = get_left_video();
    if(side == QLatin1String("right"))
        thisVideo = get_right_video();

    auto *Image = this->findChild<ClickableLabel *>(side + QStringLiteral("Image"));
    QBuffer pixels(&thisVideo->thumbnail);
    QImage image;
    image.load(&pixels, QByteArrayLiteral("JPG"));
    Image->setPixmap(QPixmap::fromImage(image).scaled(Image->width(), Image->height(), Qt::KeepAspectRatio));

    auto *FileName = this->findChild<ClickableLabel *>(side + QStringLiteral("FileName"));
    FileName->setText(QFileInfo(thisVideo->filename).fileName());
    FileName->setToolTip(QStringLiteral("%1\nOpen in file manager")
                                        .arg(QDir::toNativeSeparators(thisVideo->filename)));

    QFileInfo videoFile(thisVideo->filename);
    auto *PathName = this->findChild<QLabel *>(side + QStringLiteral("PathName"));
    PathName->setText(QDir::toNativeSeparators(videoFile.absolutePath()));

    auto *FileSize = this->findChild<QLabel *>(side + QStringLiteral("FileSize"));
    FileSize->setText(readableFileSize(thisVideo->size));

    auto *Duration = this->findChild<QLabel *>(side + QStringLiteral("Duration"));
    Duration->setText(readableDuration(thisVideo->duration));

    auto *Modified = this->findChild<QLabel *>(side + QStringLiteral("Modified"));
    Modified->setText(thisVideo->modified.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));

    const QString resolutionString = QStringLiteral("%1x%2").
                  arg(thisVideo->width).arg(thisVideo->height);
    auto *Resolution = this->findChild<QLabel *>(side + QStringLiteral("Resolution"));
    Resolution->setText(resolutionString);

    auto *FrameRate = this->findChild<QLabel *>(side + QStringLiteral("FrameRate"));
    const double fps = thisVideo->framerate;
    if(fps == 0.0)
        FrameRate->clear();
    else
        FrameRate->setText(QStringLiteral("%1 FPS").arg(fps));

    auto *BitRate = this->findChild<QLabel *>(side + QStringLiteral("BitRate"));
    BitRate->setText(readableBitRate(thisVideo->bitrate));

    auto *Codec = this->findChild<QLabel *>(side + QStringLiteral("Codec"));
    Codec->setText(thisVideo->codec);

    auto *Audio = this->findChild<QLabel *>(side + QStringLiteral("Audio"));
    Audio->setText(thisVideo->audio);
}

QString Comparison::readableDuration(const int64_t &milliseconds) const
{
    if(milliseconds == 0)
        return QStringLiteral("");

    const int hours   = milliseconds / (1000*60*60) % 24;
    const int minutes = milliseconds / (1000*60) % 60;
    const int seconds = milliseconds / 1000 % 60;

    QString readableDuration;
    if(hours > 0)
        readableDuration = QStringLiteral("%1h").arg(hours);
    if(minutes > 0)
        readableDuration = QStringLiteral("%1%2m").arg(readableDuration).arg(minutes);
    if(seconds > 0)
        readableDuration = QStringLiteral("%1%2s").arg(readableDuration).arg(seconds);

    return readableDuration;
}

QString Comparison::readableFileSize(const int64_t &filesize) const
{
    if(filesize < 1024 * 1024)
        return(QStringLiteral("%1 kB").arg(QString::number(filesize / 1024.0, 'i', 0))); //even kBs
    else if(filesize < 1024 * 1024 * 1024)                          //larger files have one decimal point
        return QStringLiteral("%1 MB").arg(QString::number(filesize / (1024.0 * 1024.0), 'f', 1));
    else
        return QStringLiteral("%1 GB").arg(QString::number(filesize / (1024.0 * 1024.0 * 1024.0), 'f', 1));
}

QString Comparison::readableBitRate(const double &kbps) const
{
    if(kbps == 0.0)
        return QStringLiteral("");
    return QStringLiteral("%1 kb/s").arg(kbps);
}

void Comparison::highlightBetterProperties() const
{
    Video* thisLeftVideo = get_left_video();
    Video* thisRightVideo = get_right_video();

    ui->leftFileSize->setStyleSheet(QStringLiteral(""));
    ui->rightFileSize->setStyleSheet(QStringLiteral(""));       //both filesizes within 100 kb
    if(qAbs(thisLeftVideo->size - thisRightVideo->size) <= 1024*100)
    {
        ui->leftFileSize->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
        ui->rightFileSize->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
    }
    else if(thisLeftVideo->size > thisRightVideo->size)
        ui->leftFileSize->setStyleSheet(QStringLiteral("QLabel { color : green; }"));
    else if(thisLeftVideo->size < thisRightVideo->size)
        ui->rightFileSize->setStyleSheet(QStringLiteral("QLabel { color : green; }"));

    ui->leftDuration->setStyleSheet(QStringLiteral(""));
    ui->rightDuration->setStyleSheet(QStringLiteral(""));       //both runtimes within 1 second
    if(qAbs(thisLeftVideo->duration - thisRightVideo->duration) <= 1000)
    {
        ui->leftDuration->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
        ui->rightDuration->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
    }
    else if(thisLeftVideo->duration > thisRightVideo->duration)
        ui->leftDuration->setStyleSheet(QStringLiteral("QLabel { color : green; }"));
    else if(thisLeftVideo->duration < thisRightVideo->duration)
        ui->rightDuration->setStyleSheet(QStringLiteral("QLabel { color : green; }"));

    ui->leftBitRate->setStyleSheet(QStringLiteral(""));
    ui->rightBitRate->setStyleSheet(QStringLiteral(""));
    if(thisLeftVideo->bitrate == thisRightVideo->bitrate)
    {
        ui->leftBitRate->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
        ui->rightBitRate->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
    }
    else if(thisLeftVideo->bitrate > thisRightVideo->bitrate)
        ui->leftBitRate->setStyleSheet(QStringLiteral("QLabel { color : green; }"));
    else if(thisLeftVideo->bitrate < thisRightVideo->bitrate)
        ui->rightBitRate->setStyleSheet(QStringLiteral("QLabel { color : green; }"));

    ui->leftFrameRate->setStyleSheet(QStringLiteral(""));
    ui->rightFrameRate->setStyleSheet(QStringLiteral(""));      //both framerates within 0.1 fps
    if(qAbs(thisLeftVideo->framerate - thisRightVideo->framerate) <= 0.1)
    {
        ui->leftFrameRate->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
        ui->rightFrameRate->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
    }
    else if(thisLeftVideo->framerate > thisRightVideo->framerate)
        ui->leftFrameRate->setStyleSheet(QStringLiteral("QLabel { color : green; }"));
    else if(thisLeftVideo->framerate < thisRightVideo->framerate)
        ui->rightFrameRate->setStyleSheet(QStringLiteral("QLabel { color : green; }"));

    ui->leftModified->setStyleSheet(QStringLiteral(""));
    ui->rightModified->setStyleSheet(QStringLiteral(""));
    if(thisLeftVideo->modified == thisRightVideo->modified)
    {
        ui->leftModified->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
        ui->rightModified->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
    }
    else if(thisLeftVideo->modified < thisRightVideo->modified)
        ui->leftModified->setStyleSheet(QStringLiteral("QLabel { color : green; }"));
    else if(thisLeftVideo->modified > thisRightVideo->modified)
        ui->rightModified->setStyleSheet(QStringLiteral("QLabel { color : green; }"));

    ui->leftResolution->setStyleSheet(QStringLiteral(""));
    ui->rightResolution->setStyleSheet(QStringLiteral(""));

    if(thisLeftVideo->width * thisLeftVideo->height ==
       thisRightVideo->width * thisRightVideo->height)
    {
        ui->leftResolution->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
        ui->rightResolution->setStyleSheet(QStringLiteral("QLabel { color : peru; }"));
    }
    else if(thisLeftVideo->width * thisLeftVideo->height >
       thisRightVideo->width *thisRightVideo->height)
        ui->leftResolution->setStyleSheet(QStringLiteral("QLabel { color : green; }"));
    else if(thisLeftVideo->width * thisLeftVideo->height <
            thisRightVideo->width * thisRightVideo->height)
        ui->rightResolution->setStyleSheet(QStringLiteral("QLabel { color : green; }"));
}

void Comparison::updateUI()
{
    if(ui->leftPathName->text() == ui->rightPathName->text())    //gray out move button if both videos in same folder
    {
        ui->leftMove->setDisabled(true);
        ui->rightMove->setDisabled(true);
    }
    else
    {
        ui->leftMove->setDisabled(false);
        ui->rightMove->setDisabled(false);
    }

    if(_prefs._comparisonMode == _prefs._PHASH)
        ui->identicalBits->setText(QString("%1/64 same bits").arg(_phashSimilarity));
    if(_prefs._comparisonMode == _prefs._SSIM)
        ui->identicalBits->setText(QString("%1 SSIM index").arg(QString::number(qMin(_ssimSimilarity, 1.0), 'f', 3)));
    _zoomLevel = 0;
    ui->progressBar->setValue(comparisonsSoFar());
}

int Comparison::comparisonsSoFar() const
{
    //returns percent 0-100
    if(_preprocessedVideos.length() > 0){
        return int(_leftVideo / _preprocessedVideos.length() * 100);
    } else {
        const int cmpFirst = _prefs._numberOfVideos;                    //comparisons done for first video
        const int cmpThis = cmpFirst - _leftVideo;                      //comparisons done for current video
        const int remaining = cmpThis * (cmpThis - 1) / 2;              //comparisons for remaining videos
        const int maxComparisons = cmpFirst * (cmpFirst - 1) / 2;       //comparing all videos with each other
        return int(((maxComparisons - remaining + _rightVideo - _leftVideo)/maxComparisons)*100);
    }
}

void Comparison::openFileManager(const QString &filename) const
{
    #if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("explorer /select, \"%1\"").arg(QDir::toNativeSeparators(filename)));
    #endif
    #if defined(Q_OS_MACX)
        QProcess::startDetached(QStringLiteral("open -R \"%1\"").arg(filename));
    #endif
    #if defined(Q_OS_X11)
        QProcess::startDetached(QStringLiteral("xdg-open \"%1\"").arg(filename.left(filename.lastIndexOf("/"))));
    #endif
}

void Comparison::deleteVideo(const QString &side)
{
    Video* thisVideo = get_left_video();
    if(side == QLatin1String("right"))
        thisVideo = get_right_video();

    const QString filename = thisVideo->filename;
    const QString onlyFilename = filename.right(filename.length() - filename.lastIndexOf("/") - 1);
    const Db cache(filename,  _prefs._mainwPtr);                       //generate unique id before file has been deleted
    const QString id = cache.uniqueId(filename, thisVideo->modified, "");

    if(!QFileInfo::exists(filename))                //video was already manually deleted, skip to next
    {
        _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
        return;
    }
    if(QMessageBox::question(this, "Delete file", QString("Are you sure you want to delete this file?\n\n%1")
                             .arg(onlyFilename), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
    {
        if(!QFile::remove(filename))
            QMessageBox::information(this, "", "Could not delete file. Check file permissions.");
        else
        {
            _videosDeleted++;
            _spaceSaved = _spaceSaved + thisVideo->size;
            cache.removeVideo(id);
            emit sendStatusMessage(QString("Deleted %1").arg(QDir::toNativeSeparators(filename)));
            _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
        }
    }
}

void Comparison::moveVideo(const QString &from, const QString &to)
{
    if(!QFileInfo::exists(from))
    {
        _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
        return;
    }

    const QString fromPath = from.left(from.lastIndexOf("/"));
    const QString toPath   = to.left(to.lastIndexOf("/"));
    const QString question = QString("Are you sure you want to move this file?\n\nFrom: %1\nTo:     %2")
                             .arg(QDir::toNativeSeparators(fromPath), QDir::toNativeSeparators(toPath));
    if(QMessageBox::question(this, "Move", question, QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes)
    {
        QFile moveThisFile(from);
        if(!moveThisFile.rename(QString("%1/%2").arg(toPath, from.right(from.length() - from.lastIndexOf("/") - 1))))
            QMessageBox::information(this, "", "Could not move file. Check file permissions and available disk space.");
        else
        {
            emit sendStatusMessage(QString("Moved %1 to %2").arg(QDir::toNativeSeparators(from), toPath));
            _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
        }
    }
}

void Comparison::on_swapFilenames_clicked() const
{
    Video* leftVideo = get_left_video();
    Video* rightVideo = get_right_video();

    const QFileInfo leftVideoFile(leftVideo->filename);
    const QString leftPathname = leftVideoFile.absolutePath();
    const QString oldLeftFilename = leftVideoFile.fileName();
    const QDateTime oldLeftDatetime = leftVideo->modified;
    const QString oldLeftNoExtension = oldLeftFilename.left(oldLeftFilename.lastIndexOf("."));
    const QString leftExtension = oldLeftFilename.right(oldLeftFilename.length() - oldLeftFilename.lastIndexOf("."));

    const QFileInfo rightVideoFile(rightVideo->filename);
    const QString rightPathname = rightVideoFile.absolutePath();
    const QString oldRightFilename = rightVideoFile.fileName();
    const QDateTime oldRightDatetime = rightVideo->modified;
    const QString oldRightNoExtension = oldRightFilename.left(oldRightFilename.lastIndexOf("."));
    const QString rightExtension = oldRightFilename.right(oldRightFilename.length() - oldRightFilename.lastIndexOf("."));

    const QString newLeftFilename = QStringLiteral("%1%2").arg(oldRightNoExtension, leftExtension);
    const QString newLeftPathAndFilename = QStringLiteral("%1/%2").arg(leftPathname, newLeftFilename);

    const QString newRightFilename = QStringLiteral("%1%2").arg(oldLeftNoExtension, rightExtension);
    const QString newRightPathAndFilename = QStringLiteral("%1/%2").arg(rightPathname, newRightFilename);

    QFile leftFile(leftVideo->filename);                  //rename files
    QFile rightFile(rightVideo->filename);
    leftFile.rename(QStringLiteral("%1/VidupeRenamedVideo.avi").arg(leftPathname));
    rightFile.rename(newRightPathAndFilename);
    leftFile.rename(newLeftPathAndFilename);

    leftVideo->filename = newLeftPathAndFilename;         //update filename in object
    rightVideo->filename = newRightPathAndFilename;

    ui->leftFileName->setText(newLeftFilename);                     //update UI
    ui->rightFileName->setText(newRightFilename);

    Db cache(leftVideo->filename,  _prefs._mainwPtr);
    cache.removeVideo(cache.uniqueId(oldLeftFilename, oldLeftDatetime, ""));             //remove both videos from cache
    cache.removeVideo(cache.uniqueId(oldRightFilename, oldRightDatetime, ""));
}

//should swap folder names
void Comparison::on_swapFolders_clicked() const
{
    Video* leftVideo = get_left_video();
    Video* rightVideo = get_right_video();


    const QFileInfo leftVideoFile(leftVideo->filename);
    const QString leftPathname = leftVideoFile.absolutePath();
    const QString leftFilename = leftVideoFile.fileName();
    QDir leftDir = leftVideoFile.dir();
    const QString leftDirName = leftDir.dirName();
    const QString leftDirPath = leftDir.absolutePath();
    const QDateTime oldLeftDatetime = leftVideo->modified;


    leftDir.cdUp();

    const QFileInfo rightVideoFile(rightVideo->filename);
    const QString rightPathname = rightVideoFile.absolutePath();
    const QString rightFilename = rightVideoFile.fileName();
    QDir rightDir = rightVideoFile.dir();
    const QString rightDirName = rightDir.dirName();
    const QString rightDirPath = rightDir.absolutePath();
    const QDateTime oldRightDatetime = rightVideo->modified;


    rightDir.cdUp();

    const QString tempLeftPath = QStringLiteral("%1/VidupeRenamedVideo").arg(leftDir.absolutePath());
    const QString newLeftPath = QStringLiteral("%1/%2").arg(leftDir.absolutePath(), rightDirName);
    const QString newRightPath = QStringLiteral("%1/%2").arg(rightDir.absolutePath(), leftDirName);

    const QString newLeftPathAndFilename = QStringLiteral("%1/%2").arg(newLeftPath, leftFilename);
    const QString newRightPathAndFilename = QStringLiteral("%1/%2").arg(newRightPath, rightFilename);

    QFile::rename(leftDirPath, tempLeftPath);
    QFile::rename(rightDirPath, newRightPath);
    QFile::rename(tempLeftPath, newLeftPath);

    leftVideo->filename = newLeftPathAndFilename;         //update filename in object
    rightVideo->filename = newRightPathAndFilename;

    ui->leftPathName->setText(newLeftPath);                     //update UI
    ui->rightPathName->setText(newRightPath);

    Db cache(leftVideo->filename,  _prefs._mainwPtr);
    cache.removeVideo(cache.uniqueId(leftFilename, oldLeftDatetime, ""));             //remove both videos from cache
    cache.removeVideo(cache.uniqueId(rightFilename, oldRightDatetime, ""));
}

//should swap folder names
void Comparison::on_swapFilesToFolders_clicked() const
{
    Video* leftVideo = get_left_video();
    Video* rightVideo = get_right_video();


    const QFileInfo leftVideoFile(leftVideo->filename);
    const QString leftPathname = leftVideoFile.absolutePath();
    const QString leftFilename = leftVideoFile.fileName();
    const QString leftNoExtension = leftFilename.left(leftFilename.lastIndexOf("."));
    const QString leftExtension = leftFilename.right(leftFilename.length() - leftFilename.lastIndexOf("."));
    const QDateTime oldLeftDatetime = leftVideo->modified;

    QDir leftDir = leftVideoFile.dir();
    const QString leftDirName = leftDir.dirName();
    const QString leftDirPath = leftDir.absolutePath();

    leftDir.cdUp();

    const QFileInfo rightVideoFile(rightVideo->filename);
    const QString rightPathname = rightVideoFile.absolutePath();
    const QString rightFilename = rightVideoFile.fileName();
    const QString rightNoExtension = rightFilename.left(rightFilename.lastIndexOf("."));
    const QString rightExtension = rightFilename.right(rightFilename.length() - rightFilename.lastIndexOf("."));
    const QDateTime oldRightDatetime = rightVideo->modified;

    QDir rightDir = rightVideoFile.dir();
    const QString rightDirName = rightDir.dirName();
    const QString rightDirPath = rightDir.absolutePath();

    rightDir.cdUp();

    const QString tempLeftPath = QStringLiteral("%1/VidupeRenamedVideo").arg(leftDir.absolutePath());
    const QString newLeftPath = QStringLiteral("%1/%2").arg(leftDir.absolutePath(), rightNoExtension);
    const QString newRightPath = QStringLiteral("%1/%2").arg(rightDir.absolutePath(), leftNoExtension);

    const QString newLeftPathAndFilename = QStringLiteral("%1/%2").arg(newLeftPath, leftFilename);
    const QString newRightPathAndFilename = QStringLiteral("%1/%2").arg(newRightPath, rightFilename);

    const QString newTempLeftPathAndFilename = QStringLiteral("%1/VidupeRenamedVideo.avi").arg(newLeftPath);
    const QString newLeftFilenameAfterRename = QStringLiteral("%2.%3").arg(rightDirName, leftExtension);
    const QString newRightFilenameAfterRename = QStringLiteral("%2.%3").arg(leftDirName, rightExtension);

    const QString newLeftPathAndFilenameAfterRename = QStringLiteral("%1/%2").arg(newLeftPath, newLeftFilenameAfterRename);
    const QString newRightPathAndFilenameAfterRename = QStringLiteral("%1/%2").arg(newRightPath, newRightFilenameAfterRename);

    QFile::rename(leftDirPath, tempLeftPath);
    QFile::rename(rightDirPath, newRightPath);
    QFile::rename(tempLeftPath, newLeftPath);

    QFile::rename(newLeftPathAndFilename, newTempLeftPathAndFilename);
    QFile::rename(newRightPathAndFilename, newRightPathAndFilenameAfterRename);
    QFile::rename(newTempLeftPathAndFilename, newLeftPathAndFilenameAfterRename);

    leftVideo->filename = newLeftPathAndFilenameAfterRename;         //update filename in object
    rightVideo->filename = newRightPathAndFilenameAfterRename;

    ui->leftPathName->setText(newLeftPath);                     //update UI
    ui->rightPathName->setText(newRightPath);
    ui->leftFileName->setText(newLeftFilenameAfterRename);
    ui->rightFileName->setText(newRightFilenameAfterRename);

    Db cache(leftVideo->filename,  _prefs._mainwPtr);
    cache.removeVideo(cache.uniqueId(leftFilename, oldLeftDatetime, ""));             //remove both videos from cache
    cache.removeVideo(cache.uniqueId(rightFilename, oldRightDatetime, ""));
}

void Comparison::on_thresholdSlider_valueChanged(const int &value)
{
    _preprocessedVideos.clear();
    _prefs._thresholdSSIM = value / 100.0;
    const int matchingBitsOf64 = static_cast<int>(round(64 * _prefs._thresholdSSIM));
    _prefs._thresholdPhash = matchingBitsOf64;

    const QString thresholdMessage = QStringLiteral(
                "Threshold: %1% (%2/64 bits = match)   Default: 89%\n"
                "Smaller: less strict, can match different videos (false positive)\n"
                "Larger: more strict, can miss identical videos (false negative)").arg(value).arg(matchingBitsOf64);
    ui->thresholdSlider->setToolTip(thresholdMessage);

    emit adjustThresholdSlider(ui->thresholdSlider->value());
    if(_prefs._thresholdSSIMMax <= _prefs._thresholdSSIM || _prefs._thresholdPhashMax <= _prefs._thresholdPhash)
    {
        if(_prefs._thresholdSSIMMax <= _prefs._thresholdSSIM)
            _prefs._thresholdSSIMMax = (_prefs._thresholdSSIM + .01);
        if(_prefs._thresholdPhashMax <= _prefs._thresholdPhash)
            _prefs._thresholdPhashMax = (_prefs._thresholdPhash + 1);
        emit adjustThresholdSliderMax(ui->thresholdSlider->value() + 2);
    }
}

void Comparison::on_thresholdSliderMax_valueChanged(const int &value)
{
    _preprocessedVideos.clear();
    _prefs._thresholdSSIMMax = value / 100.0;
    const int matchingBitsOf64 = static_cast<int>(round(64 * _prefs._thresholdSSIMMax));
    _prefs._thresholdPhashMax = matchingBitsOf64;

    const QString thresholdMessage = QStringLiteral(
                "Threshold: %1% (%2/64 bits = match)   Default: 89%\n"
                "Smaller: less strict, can match different videos (false positive)\n"
                "Larger: more strict, can miss identical videos (false negative)").arg(value).arg(matchingBitsOf64);
    ui->thresholdSliderMax->setToolTip(thresholdMessage);

    emit adjustThresholdSliderMax(ui->thresholdSliderMax->value());
    if(_prefs._thresholdSSIMMax <= _prefs._thresholdSSIM || _prefs._thresholdPhashMax <= _prefs._thresholdPhash)
    {
        if(_prefs._thresholdSSIMMax <= _prefs._thresholdSSIM)
            _prefs._thresholdSSIM = (_prefs._thresholdSSIMMax - .01);
        if(_prefs._thresholdPhashMax <= _prefs._thresholdPhash)
            _prefs._thresholdPhash = (_prefs._thresholdPhashMax - 1);
        emit adjustThresholdSlider(ui->thresholdSliderMax->value() - 2);
    }
}
void Comparison::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)

    if(ui->leftFileName->text().isEmpty() || _leftVideo >= _prefs._numberOfVideos || _rightVideo >= _prefs._numberOfVideos)
        return;     //automatic initial resize event can happen before closing when values went over limit

    QImage image;
    QBuffer leftPixels(&get_left_video()->thumbnail);
    image.load(&leftPixels, QByteArrayLiteral("JPG"));
    ui->leftImage->setPixmap(QPixmap::fromImage(image).scaled(
                             ui->leftImage->width(), ui->leftImage->height(), Qt::KeepAspectRatio));
    QBuffer rightPixels(&get_right_video()->thumbnail);
    image.load(&rightPixels, QByteArrayLiteral("JPG"));
    ui->rightImage->setPixmap(QPixmap::fromImage(image).scaled(
                              ui->rightImage->width(), ui->rightImage->height(), Qt::KeepAspectRatio));
}

void Comparison::wheelEvent(QWheelEvent *event)
{
    const QPoint pos = QCursor::pos();
    if(!QApplication::widgetAt(pos))
        return;
    ClickableLabel *imagePtr;
    if(QApplication::widgetAt(pos)->objectName() == QLatin1String("leftImage"))
        imagePtr = ui->leftImage;
    else if(QApplication::widgetAt(pos)->objectName() == QLatin1String("rightImage"))
        imagePtr = ui->rightImage;
    else
        return;

    const int wmax = imagePtr->mapToGlobal(QPoint(imagePtr->pixmap()->width(), 0)).x();         //image right edge
    const int hmax = imagePtr->mapToGlobal(QPoint(0, imagePtr->pixmap()->height())).y();        //image bottom edge
    const double ratiox = 1-static_cast<double>(wmax-pos.x()) / imagePtr->pixmap()->width();    //mouse pos inside image
    const double ratioy = 1-static_cast<double>(hmax-pos.y()) / imagePtr->pixmap()->height();

    const int widescreenBlack = (imagePtr->height() - imagePtr->pixmap()->height()) / 2;
    const int imgTop = imagePtr->mapToGlobal(QPoint(0,0)).y() + widescreenBlack;
    const int imgBtm = imgTop + imagePtr->pixmap()->height();
    if(pos.x() > wmax || pos.y() < imgTop || pos.y() > imgBtm)      //image is smaller than label underneath
        return;

    if(_zoomLevel == 0)     //first mouse wheel movement: retrieve actual screen captures in full resolution
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);

        QImage image;
        image = get_left_video()->captureAt(10);
        ui->leftImage->setPixmap(QPixmap::fromImage(image).scaled(
                                 ui->leftImage->width(), ui->leftImage->height(), Qt::KeepAspectRatio));
        _leftZoomed = QPixmap::fromImage(image);      //keep it in memory
        _leftW = image.width();
        _leftH = image.height();

        image = get_right_video()->captureAt(10);
        ui->rightImage->setPixmap(QPixmap::fromImage(image).scaled(
                                  ui->rightImage->width(), ui->rightImage->height(), Qt::KeepAspectRatio));
        _rightZoomed = QPixmap::fromImage(image);
        _rightW = image.width();
        _rightH = image.height();

        _zoomLevel = 1;
        QApplication::restoreOverrideCursor();
        return;
    }

    if(event->delta() > 0 && _zoomLevel < 10)   //mouse wheel up
        _zoomLevel = _zoomLevel * 2;
    if(event->delta() < 0 && _zoomLevel > 1)    //mouse wheel down
        _zoomLevel = _zoomLevel / 2;

    QPixmap pix;
    pix = _leftZoomed.copy(static_cast<int>(_leftW*ratiox-_leftW*ratiox/_zoomLevel),
                           static_cast<int>(_leftH*ratioy-_leftH*ratioy/_zoomLevel),
                           _leftW/_zoomLevel, _leftH/_zoomLevel);
    ui->leftImage->setPixmap(pix.scaled(ui->leftImage->width(), ui->leftImage->height(),
                                        Qt::KeepAspectRatio, Qt::FastTransformation));

    pix = _rightZoomed.copy(static_cast<int>(_rightW*ratiox-_rightW*ratiox/_zoomLevel),
                           static_cast<int>(_rightH*ratioy-_rightH*ratioy/_zoomLevel),
                           _rightW/_zoomLevel, _rightH/_zoomLevel);
    ui->rightImage->setPixmap(pix.scaled(ui->rightImage->width(), ui->rightImage->height(),
                                         Qt::KeepAspectRatio, Qt::FastTransformation));
}
