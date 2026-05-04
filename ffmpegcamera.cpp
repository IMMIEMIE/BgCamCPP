#include "ffmpegcamera.h"

#include <cerrno>
#include <chrono>
#include <QByteArray>
#include <QStringList>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

FfmpegCamera::FfmpegCamera()
    : formatContext(nullptr)
    , codecContext(nullptr)
    , swsContext(nullptr)
    , packet(nullptr)
    , decodedFrame(nullptr)
    , videoStreamIndex(-1)
    , frameWidth(0)
    , frameHeight(0)
    , hasNewFrame(false)
    , stopFlag(false)
    , openedFlag(false)
{
    ensureRegistered();
}

FfmpegCamera::~FfmpegCamera()
{
    release();
}

QStringList FfmpegCamera::listVideoDevices(QString *errorMessage)
{
    ensureRegistered();

    QStringList devices;
    const AVInputFormat *inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        if (errorMessage) {
            *errorMessage = "当前 FFmpeg 不支持 DirectShow 输入。";
        }
        return devices;
    }

    AVDeviceInfoList *deviceList = nullptr;
    const int ret = avdevice_list_input_sources(inputFormat, nullptr, nullptr, &deviceList);
    if (ret < 0) {
        if (errorMessage) {
            *errorMessage = "枚举摄像头失败：" + avError(ret);
        }
        return devices;
    }

    for (int i = 0; i < deviceList->nb_devices; ++i) {
        AVDeviceInfo *device = deviceList->devices[i];
        if (!device) {
            continue;
        }

        bool isVideoDevice = device->nb_media_types == 0;
        for (int j = 0; j < device->nb_media_types; ++j) {
            if (device->media_types[j] == AVMEDIA_TYPE_VIDEO) {
                isVideoDevice = true;
                break;
            }
        }
        if (!isVideoDevice) {
            continue;
        }

        // dshow 的 device_description 是显示用的友好名（如"USB2.0 HD Camera"），
        // device_name 通常是带 "(alternative pin name)" 的长名或设备路径，且都是 UTF-8。
        // 优先用 description；缺失时再退回 name。
        const char *display = device->device_description;
        if (!display || !*display) {
            display = device->device_name;
        }
        if (!display || !*display) {
            continue;
        }

        const QString name = QString::fromUtf8(display).trimmed();
        if (!name.isEmpty() && !devices.contains(name)) {
            devices.append(name);
        }
    }

    avdevice_free_list_devices(&deviceList);
    return devices;
}

bool FfmpegCamera::open(const QString &deviceName, int width, int height, int fps, QString *errorMessage)
{
    release();
    ensureRegistered();

    const AVInputFormat *inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        if (errorMessage) {
            *errorMessage = "当前 FFmpeg 不支持 DirectShow 输入。";
        }
        return false;
    }

    AVFormatContext *inputContext = avformat_alloc_context();
    if (!inputContext) {
        if (errorMessage) {
            *errorMessage = "创建 FFmpeg 输入上下文失败。";
        }
        return false;
    }
    inputContext->video_codec_id = AV_CODEC_ID_MJPEG;
    // 让 stopFlag 能立即打断阻塞的 av_read_frame
    inputContext->interrupt_callback.callback = &FfmpegCamera::interruptCallback;
    inputContext->interrupt_callback.opaque = this;
    // 不要在 demuxer/decoder 里再做缓冲，画面要尽量贴近实时
    inputContext->flags |= AVFMT_FLAG_NOBUFFER;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "video_size", QString("%1x%2").arg(width).arg(height).toUtf8().constData(), 0);
    av_dict_set(&options, "framerate", QByteArray::number(fps).constData(), 0);
    av_dict_set(&options, "vcodec", "mjpeg", 0);
    // dshow 输入端缓冲：太大会积压旧帧造成滞后；保留一帧左右的余量
    av_dict_set(&options, "rtbufsize", "32M", 0);
    av_dict_set(&options, "fflags", "nobuffer", 0);
    av_dict_set(&options, "flags", "low_delay", 0);

    // dshow 期望的设备名是 UTF-8；toLocal8Bit 在中文 Windows 上会用 GBK，
    // 与 FFmpeg 内部比较的 UTF-8 不匹配，会导致打开失败或乱码。
    const QByteArray inputName = "video=" + deviceName.toUtf8();
    int ret = avformat_open_input(&inputContext, inputName.constData(), inputFormat, &options);
    av_dict_free(&options);
    if (ret < 0) {
        if (errorMessage) {
            *errorMessage = QString("打开摄像头失败：%1\n设备：%2")
                                .arg(avError(ret), deviceName);
        }
        avformat_free_context(inputContext);
        return false;
    }

    ret = avformat_find_stream_info(inputContext, nullptr);
    if (ret < 0) {
        if (errorMessage) {
            *errorMessage = "读取摄像头流信息失败：" + avError(ret);
        }
        avformat_close_input(&inputContext);
        return false;
    }

    ret = av_find_best_stream(inputContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (ret < 0) {
        if (errorMessage) {
            *errorMessage = "未找到摄像头视频流：" + avError(ret);
        }
        avformat_close_input(&inputContext);
        return false;
    }
    const int streamIndex = ret;
    AVStream *stream = inputContext->streams[streamIndex];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        if (errorMessage) {
            *errorMessage = "找不到摄像头视频解码器。";
        }
        avformat_close_input(&inputContext);
        return false;
    }

    AVCodecContext *decoderContext = avcodec_alloc_context3(decoder);
    if (!decoderContext) {
        if (errorMessage) {
            *errorMessage = "创建摄像头解码上下文失败。";
        }
        avformat_close_input(&inputContext);
        return false;
    }

    ret = avcodec_parameters_to_context(decoderContext, stream->codecpar);
    if (ret < 0) {
        if (errorMessage) {
            *errorMessage = "复制摄像头解码参数失败：" + avError(ret);
        }
        avcodec_free_context(&decoderContext);
        avformat_close_input(&inputContext);
        return false;
    }

    decoderContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    decoderContext->flags2 |= AV_CODEC_FLAG2_FAST;

    ret = avcodec_open2(decoderContext, decoder, nullptr);
    if (ret < 0) {
        if (errorMessage) {
            *errorMessage = "打开摄像头解码器失败：" + avError(ret);
        }
        avcodec_free_context(&decoderContext);
        avformat_close_input(&inputContext);
        return false;
    }

    AVPacket *newPacket = av_packet_alloc();
    AVFrame *newFrame = av_frame_alloc();
    if (!newPacket || !newFrame) {
        if (errorMessage) {
            *errorMessage = "分配摄像头帧缓存失败。";
        }
        if (newPacket) av_packet_free(&newPacket);
        if (newFrame) av_frame_free(&newFrame);
        avcodec_free_context(&decoderContext);
        avformat_close_input(&inputContext);
        return false;
    }

    formatContext = inputContext;
    codecContext = decoderContext;
    packet = newPacket;
    decodedFrame = newFrame;
    videoStreamIndex = streamIndex;
    frameWidth = codecContext->width;
    frameHeight = codecContext->height;
    hasNewFrame = false;
    stopFlag.store(false);
    openedFlag.store(true);

    captureThread = std::thread(&FfmpegCamera::captureLoop, this);
    return true;
}

void FfmpegCamera::captureLoop()
{
    while (!stopFlag.load()) {
        const int readRet = av_read_frame(formatContext, packet);
        if (readRet < 0) {
            if (stopFlag.load()) {
                break;
            }
            // 临时性错误（例如设备繁忙），稍等后再试
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        int sendRet = avcodec_send_packet(codecContext, packet);
        av_packet_unref(packet);
        if (sendRet < 0) {
            continue;
        }

        int recvRet = avcodec_receive_frame(codecContext, decodedFrame);
        if (recvRet == AVERROR(EAGAIN)) {
            continue;
        }
        if (recvRet < 0) {
            continue;
        }

        if (decodedFrame->width <= 0 || decodedFrame->height <= 0) {
            av_frame_unref(decodedFrame);
            continue;
        }

        if (!swsContext ||
            frameWidth != decodedFrame->width ||
            frameHeight != decodedFrame->height) {
            sws_freeContext(swsContext);
            swsContext = sws_getContext(
                decodedFrame->width,
                decodedFrame->height,
                static_cast<AVPixelFormat>(decodedFrame->format),
                decodedFrame->width,
                decodedFrame->height,
                AV_PIX_FMT_BGR24,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr);
            frameWidth = decodedFrame->width;
            frameHeight = decodedFrame->height;
        }

        if (!swsContext) {
            av_frame_unref(decodedFrame);
            continue;
        }

        // 每次解码完成都申请一块新的 cv::Mat，让 UI 线程拿到的 frame 与
        // 后台线程下一帧使用的缓冲互不影响（依赖 cv::Mat 的引用计数）。
        cv::Mat newCvFrame(frameHeight, frameWidth, CV_8UC3);
        uint8_t *dstData[4] = { newCvFrame.data, nullptr, nullptr, nullptr };
        int dstLinesize[4] = { static_cast<int>(newCvFrame.step), 0, 0, 0 };
        sws_scale(
            swsContext,
            decodedFrame->data,
            decodedFrame->linesize,
            0,
            decodedFrame->height,
            dstData,
            dstLinesize);
        av_frame_unref(decodedFrame);

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            // 老的 latestFrame 直接被覆盖：UI 线程如果还持有，引用计数让它继续可用；
            // 这就是"始终保留最新帧"的低延迟策略。
            latestFrame = newCvFrame;
            hasNewFrame = true;
        }
        frameCv.notify_one();
    }
}

bool FfmpegCamera::read(cv::Mat &frame)
{
    if (!openedFlag.load()) {
        return false;
    }

    std::unique_lock<std::mutex> lock(frameMutex);
    frameCv.wait_for(lock, std::chrono::milliseconds(100), [this] {
        return hasNewFrame || stopFlag.load() || !openedFlag.load();
    });
    if (!hasNewFrame) {
        return false;
    }
    // 浅拷贝即可，cv::Mat 引用计数会保证后台线程下一帧不破坏当前帧
    frame = latestFrame;
    hasNewFrame = false;
    return true;
}

void FfmpegCamera::closeFfmpegResources()
{
    if (packet) {
        av_packet_free(&packet);
    }
    if (decodedFrame) {
        av_frame_free(&decodedFrame);
    }
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    if (codecContext) {
        avcodec_free_context(&codecContext);
    }
    if (formatContext) {
        avformat_close_input(&formatContext);
    }

    videoStreamIndex = -1;
    frameWidth = 0;
    frameHeight = 0;
}

void FfmpegCamera::release()
{
    stopFlag.store(true);
    frameCv.notify_all();

    if (captureThread.joinable()) {
        captureThread.join();
    }

    closeFfmpegResources();

    {
        std::lock_guard<std::mutex> lock(frameMutex);
        latestFrame.release();
        hasNewFrame = false;
    }

    openedFlag.store(false);
    stopFlag.store(false);
}

bool FfmpegCamera::isOpened() const
{
    return openedFlag.load();
}

int FfmpegCamera::width() const
{
    return frameWidth;
}

int FfmpegCamera::height() const
{
    return frameHeight;
}

void FfmpegCamera::ensureRegistered()
{
    static bool registered = false;
    if (!registered) {
        avdevice_register_all();
        registered = true;
    }
}

QString FfmpegCamera::avError(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

int FfmpegCamera::interruptCallback(void *opaque)
{
    auto *self = static_cast<FfmpegCamera *>(opaque);
    return (self && self->stopFlag.load()) ? 1 : 0;
}
