#ifndef FFMPEGCAMERA_H
#define FFMPEGCAMERA_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <QString>
#include <QStringList>
#include <opencv2/core.hpp>

extern "C" {
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
}

class FfmpegCamera
{
public:
    FfmpegCamera();
    ~FfmpegCamera();

    static QStringList listVideoDevices(QString *errorMessage = nullptr);

    bool open(const QString &deviceName, int width, int height, int fps, QString *errorMessage = nullptr);
    bool read(cv::Mat &frame);
    void release();

    bool isOpened() const;
    int width() const;
    int height() const;

private:
    static void ensureRegistered();
    static QString avError(int errorCode);
    static int interruptCallback(void *opaque);

    void captureLoop();
    void closeFfmpegResources();

    AVFormatContext *formatContext;
    AVCodecContext *codecContext;
    SwsContext *swsContext;
    AVPacket *packet;
    AVFrame *decodedFrame;
    int videoStreamIndex;
    int frameWidth;
    int frameHeight;

    std::thread captureThread;
    std::mutex frameMutex;
    std::condition_variable frameCv;
    cv::Mat latestFrame;
    bool hasNewFrame;
    std::atomic<bool> stopFlag;
    std::atomic<bool> openedFlag;
};

#endif // FFMPEGCAMERA_H
