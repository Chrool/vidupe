#include <QPainter>
#include "video.h"

Prefs Video::_prefs;
int Video::_jpegQuality = _okJpegQuality;

Video::Video(const Prefs &prefsParam, const QString &filenameParam) : filename(filenameParam)
{
    _prefs = prefsParam;
    if(_prefs._numberOfVideos > _hugeAmountVideos)       //save memory to avoid crash due to 32 bit limit
        _jpegQuality = _lowJpegQuality;

    QObject::connect(this, SIGNAL(rejectVideo(Video *)), _prefs._mainwPtr, SLOT(removeVideo(Video *)));
    QObject::connect(this, SIGNAL(acceptVideo(Video *)), _prefs._mainwPtr, SLOT(addVideo(Video *)));
}

void Video::run()
{
    if(!QFileInfo::exists(filename))
    {
        emit rejectVideo(this);
        return;
    }

    Db cache(filename);
    if(!cache.readMetadata(*this))      //check first if video properties are cached
    {
        getMetadata(filename);          //if not, read them with ffmpeg
        cache.writeMetadata(*this);
    }
    if(width == 0 || height == 0 || duration == 0)
    {
        emit rejectVideo(this);
        return;
    }

    const int ret = takeScreenCaptures(cache);
    if(ret == _failure)
        emit rejectVideo(this);
    else if((_prefs._thumbnails != cutEnds && hash[0] == 0 ) ||
            (_prefs._thumbnails == cutEnds && hash[0] == 0 && hash[4] == 0))   //all screen captures black
        emit rejectVideo(this);
    else
        emit acceptVideo(this);
}

void Video::getMetadata(const QString &filename)
{
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(QStringLiteral("ffmpeg -hide_banner -i \"%1\"").arg(QDir::toNativeSeparators(filename)));
    probe.waitForFinished();

    bool rotatedOnce = false;
    const QString analysis(probe.readAllStandardOutput());
    const QStringList analysisLines = analysis.split(QStringLiteral("\r\n"));
    for(auto line : analysisLines)
    {
        if(line.contains(QStringLiteral(" Duration:")))
        {
            const QString time = line.split(QStringLiteral(" ")).value(3);
            if(time == QLatin1String("N/A,"))
                duration = 0;
            else
            {
                const int h  = time.midRef(0,2).toInt();
                const int m  = time.midRef(3,2).toInt();
                const int s  = time.midRef(6,2).toInt();
                const int ms = time.midRef(9,2).toInt();
                duration = h*60*60*1000 + m*60*1000 + s*1000 + ms*10;
            }
            bitrate = line.split(QStringLiteral("bitrate: ")).value(1).split(QStringLiteral(" ")).value(0).toInt();
        }
        if(line.contains(QStringLiteral(" Video:")) &&
          (line.contains(QStringLiteral("kb/s")) || line.contains(QStringLiteral(" fps")) || analysis.count(" Video:") == 1))
        {
            line.replace(QRegExp(QStringLiteral("\\([^\\)]+\\)")), QStringLiteral(""));
            codec = line.split(QStringLiteral(" ")).value(7).replace(QStringLiteral(","), QStringLiteral(""));
            const QString resolution = line.split(QStringLiteral(",")).value(2);
            width = static_cast<short>(resolution.split(QStringLiteral("x")).value(0).toInt());
            height = static_cast<short>(resolution.split(QStringLiteral("x")).value(1).split(QStringLiteral(" ")).value(0).toInt());
            const QStringList fields = line.split(QStringLiteral(","));
            for(const auto &field : fields)
                if(field.contains(QStringLiteral("fps")))
                {
                    framerate = QStringLiteral("%1").arg(field).remove(QStringLiteral("fps")).toDouble();
                    framerate = round(framerate * 10) / 10;     //round to one decimal point
                }
        }
        if(line.contains(QStringLiteral(" Audio:")))
        {
            const QString audioCodec = line.split(QStringLiteral(" ")).value(7);
            const QString rate = line.split(QStringLiteral(",")).value(1);
            QString channels = line.split(QStringLiteral(",")).value(2);
            if(channels == QLatin1String(" 1 channels"))
                channels = QStringLiteral(" mono");
            else if(channels == QLatin1String(" 2 channels"))
                channels = QStringLiteral(" stereo");
            audio = QStringLiteral("%1%2%3").arg(audioCodec, rate, channels);
            const QString kbps = line.split(QStringLiteral(",")).value(4).split(QStringLiteral("kb/s")).value(0);
            if(!kbps.isEmpty() && kbps != QStringLiteral(" 0 "))
                audio = QStringLiteral("%1%2kb/s").arg(audio, kbps);
        }
        if(line.contains(QStringLiteral("rotate")) && !rotatedOnce)
        {
            const int rotate = line.split(QStringLiteral(":")).value(1).toInt();
            if(rotate == 90 || rotate == 270)
            {
                const short temp = width;
                width = height;
                height = temp;
            }
            rotatedOnce = true;     //rotate only once (AUDIO metadata can contain rotate keyword)
        }
    }

    const QFileInfo videoFile(filename);
    size = videoFile.size();
    modified = videoFile.lastModified();
}

int Video::takeScreenCaptures(const Db &cache)
{
    Thumbnail thumb(_prefs._thumbnails);
    QImage thumbnail(thumb.cols() * width, thumb.rows() * height, QImage::Format_RGB888);
    const QVector<int> percentages = thumb.percentages();
    int capture = percentages.count();
    int ofDuration = 100;

    while(--capture >= 0)           //screen captures are taken in reverse order so errors are found early
    {
        QImage frame;
        QByteArray cachedImage = cache.readCapture(percentages[capture]);
        QBuffer captureBuffer(&cachedImage);
        bool writeToCache = false;

        if(!cachedImage.isNull())   //image was already in cache
        {
            frame.load(&captureBuffer, QByteArrayLiteral("JPG"));   //was saved in cache as small size, resize to original
            frame = frame.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        else
        {
            frame = captureAt(percentages[capture], ofDuration);
            if(frame.isNull())                                  //taking screen capture may fail if video is broken
            {
                ofDuration = ofDuration - _goBackwardsPercent;
                if(ofDuration >= _videoStillUsable)             //retry a few times, always closer to beginning
                {
                    capture = percentages.count();
                    continue;
                }
                return _failure;
            }
            writeToCache = true;
        }
        if(frame.width() > width || frame.height() > height)    //metadata parsing error or variable resolution
            return _failure;

        QPainter painter(&thumbnail);                           //copy captured frame into right place in thumbnail
        painter.drawImage(capture % thumb.cols() * width, capture / thumb.cols() * height, frame);

        if(writeToCache)
        {
            frame = minimizeImage(frame);
            frame.save(&captureBuffer, QByteArrayLiteral("JPG"), _okJpegQuality);
            cache.writeCapture(percentages[capture], cachedImage);
        }
    }

    const int hashes = _prefs._thumbnails == cutEnds? 16 : 1;    //if cutEnds mode: separate hash for beginning and end
    processThumbnail(thumbnail, hashes);
    return _success;
}

void Video::processThumbnail(QImage &thumbnail, const int &hashes)
{
    for(int hash=0; hash<hashes; hash++)
    {
        QImage image = thumbnail;
        int y = (int)hash / 4;
        int x = hash % 4;
        if(_prefs._thumbnails == cutEnds)           //if cutEnds mode: separate thumbnail into first and last frames
            image = thumbnail.copy(x, y, thumbnail.width()/4, thumbnail.height()/4);

        cv::Mat mat = cv::Mat(image.height(), image.width(), CV_8UC3, image.bits(), static_cast<uint>(image.bytesPerLine()));
        this->hash[hash] = computePhash(mat);                           //pHash

        cv::resize(mat, mat, cv::Size(_ssimSize, _ssimSize), 0, 0, cv::INTER_AREA);
        cv::cvtColor(mat, grayThumb[hash], cv::COLOR_BGR2GRAY);
        grayThumb[hash].cv::Mat::convertTo(grayThumb[hash], CV_32F);    //ssim
    }

    thumbnail = minimizeImage(thumbnail);
    QBuffer buffer(&this->thumbnail);
    thumbnail.save(&buffer, QByteArrayLiteral("JPG"), _jpegQuality);    //save GUI thumbnail as tiny JPEG
}

uint64_t Video::computePhash(const cv::Mat &input) const
{
    cv::Mat resizeImg, grayImg, grayFImg, dctImg, topLeftDCT;
    cv::resize(input, resizeImg, cv::Size(_pHashSize, _pHashSize), 0, 0, cv::INTER_AREA);
    cv::cvtColor(resizeImg, grayImg, cv::COLOR_BGR2GRAY);           //resize image to 32x32 grayscale

    int shadesOfGray = 0;
    uchar* pixel = reinterpret_cast<uchar*>(grayImg.data);          //pointer to pixel values, starts at first one
    const uchar* lastPixel = pixel + _pHashSize * _pHashSize;
    const uchar firstPixel = *pixel;

    for(pixel++; pixel<lastPixel; pixel++)              //skip first element since that one is already firstPixel
        shadesOfGray += qAbs(firstPixel - *pixel);      //compare all pixels with first one, tabulate differences
    if(shadesOfGray < _almostBlackBitmap)
        return 0;                                       //reject video if capture was (almost) monochrome

    grayImg.convertTo(grayFImg, CV_32F);
    cv::dct(grayFImg, dctImg);                          //compute DCT (discrete cosine transform)
    dctImg(cv::Rect(0, 0, 8, 8)).copyTo(topLeftDCT);    //use only upper left 8*8 transforms (most significant ones)

    const float firstElement = *reinterpret_cast<float*>(topLeftDCT.data);      //compute avg but skip first element
    const float average = (static_cast<float>(cv::sum(topLeftDCT)[0]) - firstElement) / 63;         //(it's very big)

    uint64_t hash = 0;
    float* transform = reinterpret_cast<float*>(topLeftDCT.data);
    const float* endOfData = transform + 64;
    for(int i=0; transform<endOfData; i++, transform++)             //construct hash from all 8x8 bits
        if(*transform > average)
            hash |= 1ULL << i;                                      //larger than avg = 1, smaller than avg = 0

    return hash;
}

QImage Video::minimizeImage(const QImage &image) const
{
    if(image.width() > image.height())
    {
        if(image.width() > _thumbnailMaxWidth)
            return image.scaledToWidth(_thumbnailMaxWidth, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    }
    else if(image.height() > _thumbnailMaxHeight)
        return image.scaledToHeight(_thumbnailMaxHeight, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);

    return image;
}

QString Video::msToHHMMSS(const int64_t &time) const
{
    const int hours   = time / (1000*60*60) % 24;
    const int minutes = time / (1000*60) % 60;
    const int seconds = time / 1000 % 60;
    const int msecs   = time % 1000;

    QString paddedHours = QStringLiteral("%1").arg(hours);
    if(hours < 10)
        paddedHours = QStringLiteral("0%1").arg(paddedHours);

    QString paddedMinutes = QStringLiteral("%1").arg(minutes);
    if(minutes < 10)
        paddedMinutes = QStringLiteral("0%1").arg(paddedMinutes);

    QString paddedSeconds = QStringLiteral("%1").arg(seconds);
    if(seconds < 10)
        paddedSeconds = QStringLiteral("0%1").arg(paddedSeconds);

    return QStringLiteral("%1:%2:%3.%4").arg(paddedHours, paddedMinutes, paddedSeconds).arg(msecs);
}



int Video::phashSimilarity(const Video *right, const int &hashes)
{
    if(self->distances.contains(right->filename){
        return self->distances[right->filename];
    }     
    int nearestDistance = 64;
    for(int leftHash=0; leftHash<hashes; leftHash++){
        for(int rightHash=0; rightHash<hashes; rightHash++){
            if(this->hash[leftHash] == 0 && right->hash[rightHash] == 0)
                continue;

            uint64_t differentBits = this->hash[leftHash] ^ right->hash[rightHash];    //XOR to value (only ones for differing bits)
            int distance = 64;
            while(differentBits)
            {
                differentBits &= differentBits - 1;                 //count number of bits of value
                distance--;
            }

            if( qAbs(this->duration - right->duration) <= 1000 )
                _durationModifier = 0 + _prefs._sameDurationModifier;               //lower distance if both durations within 1s
            else
                _durationModifier = 0 - _prefs._differentDurationModifier;          //raise distance if both durations differ 1s

            distance = distance + _durationModifier;
            nearestDistance = nearestDistance > distance ? distance : nearestDistance;
        }
    }

    self->distances[right->filename] = nearestDistance > 64? 64 : distance;
    return nearestDistance > 64? 64 : nearestDistance;
}

QImage Video::captureAt(const int &percent, const int &ofDuration) const
{
    const QTemporaryDir tempDir;
    if(!tempDir.isValid())
        return QImage();

    const QString screenshot = QStringLiteral("%1/vidupe%2.bmp").arg(tempDir.path()).arg(percent);
    QProcess ffmpeg;
    const QString ffmpegCommand = QStringLiteral("ffmpeg -ss %1 -i \"%2\" -an -frames:v 1 -pix_fmt rgb24 %3")
                                  .arg(msToHHMMSS(duration * (percent * ofDuration) / (100 * 100)),
                                  QDir::toNativeSeparators(filename), QDir::toNativeSeparators(screenshot));
    ffmpeg.start(ffmpegCommand);
    ffmpeg.waitForFinished(10000);

    const QImage img(screenshot, "BMP");
    QFile::remove(screenshot);
    return img;
}
