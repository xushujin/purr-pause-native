#include "media.h"
#include <QElapsedTimer>
#include <QFile>
#include <QMutexLocker>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace purr {
VideoPlayer::VideoPlayer(QObject *parent) : QThread(parent) {}
VideoPlayer::~VideoPlayer() { stop(); }
void VideoPlayer::play(const QString &path, bool loop) {
    stop();
    path_ = path; loop_ = loop; durationSeconds_ = 3.0;
    { QMutexLocker lock(&mutex_); latest_ = {}; }
    start();
}
void VideoPlayer::stop() {
    ++generation_;
    requestInterruption();
    wait();
    // run() may begin only after the first invalidation; suppress that run too.
    ++generation_;
    framePending_ = false;
}
QImage VideoPlayer::frame() const { QMutexLocker lock(&mutex_); return latest_; }
void VideoPlayer::run() {
    const quint64 generation = generation_.load();
    const auto fail = [this, generation](const QString &reason) {
        QMetaObject::invokeMethod(this, [this, generation, reason] {
            if (generation == generation_) emit failed(reason);
        }, Qt::QueuedConnection);
    };
    // Only local files are accepted; no playlists, nested URLs or network I/O.
    AVFormatContext *format = avformat_alloc_context();
    if (!format) { fail(QStringLiteral("无法分配视频读取缓冲区")); return; }
    format->interrupt_callback.callback = [](void *opaque) { return static_cast<VideoPlayer *>(opaque)->isInterruptionRequested() ? 1 : 0; };
    format->interrupt_callback.opaque = this;
    AVDictionary *options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    const QByteArray path = QFile::encodeName(path_);
    int opened = avformat_open_input(&format, path.constData(), nullptr, &options);
    av_dict_free(&options);
    if (opened < 0) { avformat_close_input(&format); fail(QStringLiteral("无法打开视频素材")); return; }
    const auto closeFormat = [](AVFormatContext *f) { avformat_close_input(&f); };
    std::unique_ptr<AVFormatContext, decltype(closeFormat)> formatGuard(format, closeFormat);
    if (avformat_find_stream_info(format, nullptr) < 0) { fail(QStringLiteral("无法读取视频信息")); return; }
    const int streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) { fail(QStringLiteral("素材中没有视频轨道")); return; }
    AVStream *stream = format->streams[streamIndex];
    // FFmpeg's native VP9 decoder drops WebM alpha; libvpx consumes BlockAdditional.
    const AVCodec *codec = avcodec_find_decoder_by_name(stream->codecpar->codec_id == AV_CODEC_ID_VP8 ? "libvpx" : "libvpx-vp9");
    if (!codec || (stream->codecpar->codec_id != AV_CODEC_ID_VP9 && stream->codecpar->codec_id != AV_CODEC_ID_VP8)) {
        fail(QStringLiteral("素材需要 VP8/VP9 WebM，且解码器必须支持 libvpx 透明通道")); return;
    }
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    const auto freeCodec = [](AVCodecContext *c) { avcodec_free_context(&c); };
    std::unique_ptr<AVCodecContext, decltype(freeCodec)> codecGuard(ctx, freeCodec);
    if (!ctx) { fail(QStringLiteral("无法分配视频解码器")); return; }
    if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0) { fail(QStringLiteral("无效视频参数")); return; }
    if (ctx->width <= 0 || ctx->height <= 0 || ctx->width > 8192 || ctx->height > 8192) { fail(QStringLiteral("视频尺寸无效或过大")); return; }
    ctx->thread_count = 2;
    if (avcodec_open2(ctx, codec, nullptr) < 0) { fail(QStringLiteral("视频解码器初始化失败")); return; }
    if (format->duration > 0) durationSeconds_ = double(format->duration) / AV_TIME_BASE;
    QMetaObject::invokeMethod(this, [this, generation, size=QSize(ctx->width,ctx->height), duration=durationSeconds_.load()] {
        if (generation == generation_) emit playbackReady(size, duration);
    }, Qt::QueuedConnection);
    AVPacket *packet = av_packet_alloc();
    AVFrame *decoded = av_frame_alloc();
    if (!packet || !decoded) {
        av_frame_free(&decoded); av_packet_free(&packet);
        fail(QStringLiteral("无法分配视频帧缓冲区")); return;
    }
    SwsContext *scaler = nullptr;
    bool decodedAny = false;
    bool eof = false;
    bool failedDecode = false;
    QElapsedTimer clock;
    clock.start();
    double firstPts = -1;
    qint64 frameNumber = 0;
    const double reportedFps = stream->avg_frame_rate.den ? av_q2d(stream->avg_frame_rate) : 0;
    const double fps = std::isfinite(reportedFps) && reportedFps>0 ? reportedFps : 30;
    while (!isInterruptionRequested()) {
        int readResult = eof ? AVERROR_EOF : av_read_frame(format, packet);
        if (readResult >= 0 && packet->stream_index != streamIndex) { av_packet_unref(packet); continue; }
        if (readResult < 0) eof = true;
        const int sent = avcodec_send_packet(ctx, eof ? nullptr : packet);
        av_packet_unref(packet);
        if (sent < 0 && sent != AVERROR_EOF && sent != AVERROR(EAGAIN)) { failedDecode = true; break; }
        int received = 0;
        while (!isInterruptionRequested() && (received = avcodec_receive_frame(ctx, decoded)) >= 0) {
            const double pts = decoded->best_effort_timestamp == AV_NOPTS_VALUE ? double(frameNumber) / std::max(1.0, fps) : decoded->best_effort_timestamp * av_q2d(stream->time_base);
            if (firstPts < 0) firstPts = pts;
            const qint64 due = qint64((pts - firstPts) * 1000);
            // Bounded waits keep shutdown responsive and honor variable frame timing.
            while (!isInterruptionRequested() && due > clock.elapsed()) msleep(static_cast<unsigned long>(std::min<qint64>(10, due - clock.elapsed())));
            if (isInterruptionRequested()) break;
            // Preserve custom media resolution; the widget scales at paint time.
            // A fixed 1080p decode cap silently loses detail on high-DPI screens.
            const QSize size(decoded->width, decoded->height);
            scaler = sws_getCachedContext(scaler, decoded->width, decoded->height, static_cast<AVPixelFormat>(decoded->format), size.width(), size.height(), AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!scaler) { failedDecode = true; break; }
            // libswscale defaults to BT.601; the original videos carry BT.709.
            int colorspace = SWS_CS_DEFAULT;
            if (decoded->colorspace == AVCOL_SPC_BT709) colorspace = SWS_CS_ITU709;
            else if (decoded->colorspace == AVCOL_SPC_BT2020_NCL || decoded->colorspace == AVCOL_SPC_BT2020_CL) colorspace = SWS_CS_BT2020;
            else if (decoded->colorspace == AVCOL_SPC_FCC) colorspace = SWS_CS_FCC;
            const int *coefficients = sws_getCoefficients(colorspace);
            sws_setColorspaceDetails(scaler,coefficients,decoded->color_range==AVCOL_RANGE_JPEG,coefficients,1,0,1<<16,1<<16);
            QImage image(size, QImage::Format_RGBA8888);
            if (image.isNull()) { failedDecode=true; break; }
            uint8_t *dest[4] = {image.bits(), nullptr, nullptr, nullptr};
            int strides[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
            sws_scale(scaler, decoded->data, decoded->linesize, 0, decoded->height, dest, strides);
            { QMutexLocker lock(&mutex_); latest_ = std::move(image); }
            if (!framePending_.exchange(true)) QMetaObject::invokeMethod(this, [this, generation] {
                if (generation != generation_) return;
                framePending_ = false;
                emit frameAvailable();
            }, Qt::QueuedConnection);
            decodedAny = true; ++frameNumber;
            av_frame_unref(decoded);
        }
        if (received<0 && received!=AVERROR(EAGAIN) && received!=AVERROR_EOF) failedDecode=true;
        if (failedDecode) break;
        if (eof) {
            // Keep the final frame on screen for its duration before ending or
            // looping; otherwise 30fps clips shorten by one frame every loop.
            const qint64 end = format->duration > 0 ? format->duration/1000 : qint64(frameNumber*1000/std::max(1.0,fps));
            while (!isInterruptionRequested() && end > clock.elapsed()) msleep(static_cast<unsigned long>(std::min<qint64>(10,end-clock.elapsed())));
            if (!loop_ || !decodedAny) break;
            if (av_seek_frame(format, streamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0) break;
            avcodec_flush_buffers(ctx);
            eof = false; firstPts = -1; frameNumber = 0; clock.restart();
        }
    }
    sws_freeContext(scaler); av_frame_free(&decoded); av_packet_free(&packet);
    if (!isInterruptionRequested()) {
        if (failedDecode || !decodedAny) fail(QStringLiteral("视频素材解码失败"));
        else QMetaObject::invokeMethod(this, [this, generation] {
            if (generation == generation_) emit playbackFinished();
        }, Qt::QueuedConnection);
    }
}
}
