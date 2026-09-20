#include "video.h"

#include "stdio_compat.h"
#include "string_compat.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

//fixes sdmc:/ (switch and vita)
static char* ffmpegFileUrl(const char* path) {
    if (path == nullptr) return nullptr;
    if (strncmp(path, "file:", 5) == 0) return safeStrdup(path);
    const char* colon = strchr(path, ':');
    if (colon != nullptr && colon[1] == '/' && colon[2] == '/') {
        return safeStrdup(path);
    }
    size_t len = strlen(path);
    char* out = (char*)safeMalloc(len + 6);
    memcpy(out, "file:", 5);
    memcpy(out + 5, path, len + 1);
    return out;
}

#define VIDEO_AUDIO_CHANNELS 2
#define VIDEO_AUDIO_SAMPLE_RATE 48000

typedef struct {
    AVFormatContext* formatCtx;
    int32_t videoStreamIndex;
    AVStream* videoStream;
    AVCodecContext* videoCodecCtx;
    AVFrame* videoFrame;
    struct SwsContext* swsCtx;
    uint8_t* drawPixels;
    int32_t drawLineSize;
    int32_t width;
    int32_t height;
    int64_t lastPts;
    int64_t startPts;

    double durationSeconds;
    bool hasVideo;
    bool eof;
    bool finished;
    bool paused;
    bool loop;
} FfmpegVideoDecoder;

static int32_t ffmpegVideoDecoderConvertFrame(FfmpegVideoDecoder* d, AVFrame* frame) {
    if (d->swsCtx == nullptr || d->drawPixels == nullptr) return -1;
    uint8_t* dst[1] = {d->drawPixels};
    int32_t dstStride[1] = {d->drawLineSize};
    sws_scale(d->swsCtx, (const uint8_t**)frame->data, frame->linesize, 0, d->height, dst, dstStride);
    if (frame->pts != AV_NOPTS_VALUE) d->lastPts = frame->pts;
    return 0;
}

static int32_t ffmpegVideoDecoderUpdate(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    if (d == nullptr || d->formatCtx == nullptr || !d->hasVideo) return 1;
    if (d->paused) return 0;
    if (d->finished) return 1;
    bool gotFrame = false;
    bool wrapped = false;
    while (!gotFrame) {
        if (d->eof) {
            while (avcodec_receive_frame(d->videoCodecCtx, d->videoFrame) == 0) {
                if (ffmpegVideoDecoderConvertFrame(d, d->videoFrame) == 0) gotFrame = true;
                av_frame_unref(d->videoFrame);
            }
            if (!gotFrame) {
                if (!d->loop) {
                    d->finished = true;
                    break;
                }
                avcodec_flush_buffers(d->videoCodecCtx);
                if (avformat_seek_file(d->formatCtx, d->videoStreamIndex, INT64_MIN, d->startPts, INT64_MAX, 0) < 0) {
                    d->finished = true;
                    break;
                }
                d->eof = false;
                d->lastPts = AV_NOPTS_VALUE;
                wrapped = true;
                continue;
            }
            break;
        }
        AVPacket packet = {0};
        if (av_read_frame(d->formatCtx, &packet) < 0) {
            av_packet_unref(&packet);
            d->eof = true;
            avcodec_send_packet(d->videoCodecCtx, nullptr);
            continue;
        }
        if (packet.stream_index == d->videoStreamIndex) {
            if (avcodec_send_packet(d->videoCodecCtx, &packet) == 0) {
                while (avcodec_receive_frame(d->videoCodecCtx, d->videoFrame) == 0) {
                    if (ffmpegVideoDecoderConvertFrame(d, d->videoFrame) == 0) gotFrame = true;
                    av_frame_unref(d->videoFrame);
                }
            }
        }
        av_packet_unref(&packet);
    }
    return gotFrame ? (wrapped ? 2 : 0) : 1;
}

static void ffmpegVideoDecoderDraw(VideoDecoder* decoder, Runner* runner, int32_t surfaceId) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    if (d == nullptr || d->drawPixels == nullptr || runner->renderer == nullptr) return;
    if (runner->renderer->vtable->surfaceUploadPixels == nullptr) return;
    runner->renderer->vtable->surfaceUploadPixels(runner->renderer, surfaceId, d->width, d->height, d->drawPixels);
}

static bool ffmpegVideoDecoderIsRunning(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    return d != nullptr && d->hasVideo && !d->finished;
}

static bool ffmpegVideoDecoderIsPaused(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    return d != nullptr && d->paused;
}

static void ffmpegVideoDecoderPause(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    if (d != nullptr) d->paused = true;
}

static void ffmpegVideoDecoderResume(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    if (d != nullptr) d->paused = false;
}

static void ffmpegVideoDecoderSetLoop(VideoDecoder* decoder, bool loop) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    if (d != nullptr) d->loop = loop;
}

static double ffmpegVideoDecoderDuration(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    if (d == nullptr) return 0;
    return d->durationSeconds;
}

static double ffmpegVideoDecoderPosition(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    if (d == nullptr || d->videoStream == nullptr || d->lastPts == AV_NOPTS_VALUE) return 0;
    return (double)(d->lastPts - d->startPts) * av_q2d(d->videoStream->time_base);
}

static int32_t ffmpegVideoDecoderWidth(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    return d == nullptr ? 0 : d->width;
}

static int32_t ffmpegVideoDecoderHeight(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    return d == nullptr ? 0 : d->height;
}

static bool ffmpegVideoDecoderInit(void) {
    return true;
}

static void ffmpegVideoDecoderQuit(VideoDecoder* decoder) {
}

static void ffmpegVideoDecoderClose(VideoDecoder* decoder) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    if (d == nullptr) return;
    if (d->swsCtx != nullptr) { sws_freeContext(d->swsCtx); d->swsCtx = nullptr; }
    if (d->drawPixels != nullptr) { av_freep(&d->drawPixels); d->drawPixels = nullptr; }
    if (d->videoFrame != nullptr) { av_frame_free(&d->videoFrame); d->videoFrame = nullptr; }
    if (d->videoCodecCtx != nullptr) { avcodec_free_context(&d->videoCodecCtx); d->videoCodecCtx = nullptr; }
    if (d->formatCtx != nullptr) { avformat_close_input(&d->formatCtx); d->formatCtx = nullptr; }
    d->videoStream = nullptr;
    d->drawLineSize = 0;
    d->videoStreamIndex = -1;
    d->width = 0;
    d->height = 0;
    d->lastPts = AV_NOPTS_VALUE;
    d->startPts = 0;
    d->durationSeconds = 0;
    d->hasVideo = false;
    d->eof = false;
    d->finished = false;
    d->paused = false;
    d->loop = false;
}

static bool ffmpegVideoDecoderOpen(VideoDecoder* decoder, const char* url) {
    FfmpegVideoDecoder* d = (FfmpegVideoDecoder*)decoder->impl;
    char* ffmpegUrl = ffmpegFileUrl(url);
    int openResult = avformat_open_input(&d->formatCtx, ffmpegUrl, nullptr, nullptr);
    free(ffmpegUrl);
    if (openResult != 0) return false;
    if (avformat_find_stream_info(d->formatCtx, nullptr) < 0) {
        avformat_close_input(&d->formatCtx);
        d->formatCtx = nullptr;
        return false;
    }
    d->durationSeconds = d->formatCtx->duration > 0 ? (double)d->formatCtx->duration / (double)AV_TIME_BASE : 0;

    const AVCodec* videoCodec = nullptr;
    d->videoStreamIndex = av_find_best_stream(d->formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
    if (d->videoStreamIndex >= 0 && videoCodec != nullptr) {
        d->videoStream = d->formatCtx->streams[d->videoStreamIndex];
        d->videoCodecCtx = avcodec_alloc_context3(videoCodec);
        if (d->videoCodecCtx != nullptr) {
            if (avcodec_parameters_to_context(d->videoCodecCtx, d->videoStream->codecpar) == 0) {
                d->videoCodecCtx->thread_count = 1;
                if (avcodec_open2(d->videoCodecCtx, videoCodec, nullptr) == 0) d->hasVideo = true;
            }
            if (!d->hasVideo) {
                avcodec_free_context(&d->videoCodecCtx);
                d->videoCodecCtx = nullptr;
                d->videoStream = nullptr;
                d->videoStreamIndex = -1;
            }
        }
    }

    if (!d->hasVideo) {
        ffmpegVideoDecoderClose(decoder);
        return false;
    }

    d->width = d->videoCodecCtx->width;
    d->height = d->videoCodecCtx->height;
    d->startPts = d->videoStream->start_time == AV_NOPTS_VALUE ? 0 : d->videoStream->start_time;
    if (d->durationSeconds <= 0 && d->videoStream->duration > 0) {
        d->durationSeconds = (double)d->videoStream->duration * av_q2d(d->videoStream->time_base);
    }

    d->videoFrame = av_frame_alloc();
    if (d->videoFrame == nullptr) {
        ffmpegVideoDecoderClose(decoder);
        return false;
    }
    d->swsCtx = sws_getContext(d->width, d->height, d->videoCodecCtx->pix_fmt,
                               d->width, d->height, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    int drawLinesizes[4] = {0};
    uint8_t* drawPixels[4] = {nullptr};
    if (d->swsCtx == nullptr || av_image_alloc(drawPixels, drawLinesizes, d->width, d->height, AV_PIX_FMT_RGBA, 1) < 0) {
        ffmpegVideoDecoderClose(decoder);
        return false;
    }
    d->drawPixels = drawPixels[0];
    d->drawLineSize = drawLinesizes[0];

    d->eof = false;
    d->finished = false;
    d->paused = false;
    d->loop = false;
    d->lastPts = AV_NOPTS_VALUE;
    return true;
}

typedef struct VideoWavWriter {
    FILE* file;
    uint8_t* pcm;
    uint32_t pcmSize;
    uint32_t pcmCapacity;
    int32_t channels;
    int32_t sampleRate;
} VideoWavWriter;

static void videoAudioWriteWavHeader(uint8_t* wav, uint32_t dataSize, int32_t channels, int32_t sampleRate) {
    memcpy(wav, "RIFF", 4);
    uint32_t riffSize = 36 + dataSize;
    memcpy(wav + 4, &riffSize, 4);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(wav + 16, &fmtSize, 4);
    uint16_t audioFormat = 1;
    memcpy(wav + 20, &audioFormat, 2);
    uint16_t numChannels = (uint16_t)channels;
    memcpy(wav + 22, &numChannels, 2);
    memcpy(wav + 24, &sampleRate, 4);
    uint32_t byteRate = (uint32_t)(sampleRate * channels * 2);
    memcpy(wav + 28, &byteRate, 4);
    uint16_t blockAlign = (uint16_t)(channels * 2);
    memcpy(wav + 32, &blockAlign, 2);
    uint16_t bitsPerSample = 16;
    memcpy(wav + 34, &bitsPerSample, 2);
    memcpy(wav + 36, "data", 4);
    memcpy(wav + 40, &dataSize, 4);
}

static void videoWavWriterOpen(VideoWavWriter* w, const char* absPath, int32_t channels, int32_t sampleRate) {
    w->file = (absPath != nullptr) ? fopen(absPath, "wb") : nullptr;
    w->pcm = nullptr;
    w->pcmSize = 0;
    w->pcmCapacity = 0;
    w->channels = channels;
    w->sampleRate = sampleRate;
    if (w->file != nullptr) {
        uint8_t header[44];
        videoAudioWriteWavHeader(header, 0, channels, sampleRate);
        fwrite(header, 1, 44, w->file);
    }
}

static void videoWavWriterPush(VideoWavWriter* w, const uint8_t* data, uint32_t size) {
    if (w->file != nullptr) {
        fwrite(data, 1, size, w->file);
        return;
    }
    if (w->pcmSize + size > w->pcmCapacity) {
        w->pcmCapacity = w->pcmSize + size + 65536;
        w->pcm = (uint8_t*)safeRealloc(w->pcm, w->pcmCapacity);
    }
    memcpy(w->pcm + w->pcmSize, data, size);
    w->pcmSize += size;
}

static bool videoWavWriterFinish(VideoWavWriter* w, const char* wavPath, FileSystem* fs) {
    if (w->file != nullptr) {
        long pos = ftell(w->file);
        if (pos > 44) {
            uint32_t dataSize = (uint32_t)(pos - 44);
            uint32_t riffSize = 36 + dataSize;
            if (fseek(w->file, 4, SEEK_SET) == 0) fwrite(&riffSize, 1, 4, w->file);
            if (fseek(w->file, 40, SEEK_SET) == 0) fwrite(&dataSize, 1, 4, w->file);
        }
        fclose(w->file);
        w->file = nullptr;
        return pos > 44;
    }
    if (w->pcm != nullptr && w->pcmSize > 0 && fs != nullptr) {
        uint32_t wavSize = 44 + w->pcmSize;
        uint8_t* wav = (uint8_t*)safeMalloc(wavSize);
        videoAudioWriteWavHeader(wav, w->pcmSize, w->channels, w->sampleRate);
        memcpy(wav + 44, w->pcm, w->pcmSize);
        bool wrote = fs->vtable->writeFileBinary(fs, wavPath, wav, (int32_t)wavSize);
        free(wav);
        return wrote;
    }
    return false;
}

static void videoAudioAppendConverted(VideoWavWriter* w, struct SwrContext* swrCtx, AVFrame* frame) {
    if (swrCtx == nullptr || frame->nb_samples <= 0) return;
    int32_t outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
    if (outSamples <= 0) return;
    int32_t bytes = outSamples * VIDEO_AUDIO_CHANNELS * (int32_t)sizeof(int16_t);
    uint8_t* tmp = (uint8_t*)safeMalloc((size_t)bytes);
    int32_t converted = swr_convert(swrCtx, &tmp, outSamples, (const uint8_t**)frame->data, frame->nb_samples);
    if (converted > 0) {
        uint32_t convBytes = (uint32_t)converted * VIDEO_AUDIO_CHANNELS * (uint32_t)sizeof(int16_t);
        videoWavWriterPush(w, tmp, convBytes);
    }
    free(tmp);
}

static bool videoAudioExtract(VideoWavWriter* w, const char* url) {
    AVFormatContext* fmtCtx = nullptr;
    char* ffmpegUrl = ffmpegFileUrl(url);
    int openResult = avformat_open_input(&fmtCtx, ffmpegUrl, nullptr, nullptr);
    free(ffmpegUrl);
    if (openResult != 0) return false;
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }
    const AVCodec* audioCodec = nullptr;
    int32_t audioIndex = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &audioCodec, 0);
    if (audioIndex < 0 || audioCodec == nullptr) {
        avformat_close_input(&fmtCtx);
        return false;
    }
    AVCodecContext* audioCodecCtx = avcodec_alloc_context3(audioCodec);
    if (audioCodecCtx == nullptr) {
        avformat_close_input(&fmtCtx);
        return false;
    }
    bool ok = false;
    if (avcodec_parameters_to_context(audioCodecCtx, fmtCtx->streams[audioIndex]->codecpar) == 0 &&
        avcodec_open2(audioCodecCtx, audioCodec, nullptr) == 0) {
        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
        struct SwrContext* swrCtx = nullptr;
        swr_alloc_set_opts2(&swrCtx, &outLayout, AV_SAMPLE_FMT_S16, VIDEO_AUDIO_SAMPLE_RATE,
                            &audioCodecCtx->ch_layout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate, 0, nullptr);
        if (swrCtx != nullptr) swr_init(swrCtx);
        AVFrame* frame = av_frame_alloc();
        if (frame != nullptr) {
            AVPacket packet = {0};
            while (av_read_frame(fmtCtx, &packet) >= 0) {
                if (packet.stream_index == audioIndex) {
                    if (avcodec_send_packet(audioCodecCtx, &packet) == 0) {
                        while (avcodec_receive_frame(audioCodecCtx, frame) == 0) {
                            videoAudioAppendConverted(w, swrCtx, frame);
                            av_frame_unref(frame);
                        }
                    }
                }
                av_packet_unref(&packet);
            }
            avcodec_send_packet(audioCodecCtx, nullptr);
            while (avcodec_receive_frame(audioCodecCtx, frame) == 0) {
                videoAudioAppendConverted(w, swrCtx, frame);
                av_frame_unref(frame);
            }
            ok = true;
            av_frame_free(&frame);
        }
        if (swrCtx != nullptr) swr_free(&swrCtx);
    }
    avcodec_free_context(&audioCodecCtx);
    avformat_close_input(&fmtCtx);
    return ok;
}

static bool ffmpegVideoDecoderExtractAudio(VideoDecoder* decoder, Runner* runner, const char* url) {
    if (runner == nullptr || runner->fileSystem == nullptr) return false;
    const char* wavPath = "butterscotch_video_extract.wav";
    char* absPath = runner->fileSystem->vtable->resolvePath(runner->fileSystem, wavPath);
    VideoWavWriter writer;
    videoWavWriterOpen(&writer, absPath, VIDEO_AUDIO_CHANNELS, VIDEO_AUDIO_SAMPLE_RATE);
    bool extracted = videoAudioExtract(&writer, url);
    bool finished = videoWavWriterFinish(&writer, wavPath, runner->fileSystem);
    free(absPath);
    return extracted && finished;
}

static VideoDecoderVtable ffmpegVideoDecoderVtable;

VideoDecoder* VideoDecoder_createBackend(void) {
    VideoDecoder* decoder = (VideoDecoder*)safeCalloc(1, sizeof(VideoDecoder));
    FfmpegVideoDecoder* impl = (FfmpegVideoDecoder*)safeCalloc(1, sizeof(FfmpegVideoDecoder));
    impl->videoStreamIndex = -1;
    impl->lastPts = AV_NOPTS_VALUE;
    ffmpegVideoDecoderVtable.init = ffmpegVideoDecoderInit;
    ffmpegVideoDecoderVtable.quit = ffmpegVideoDecoderQuit;
    ffmpegVideoDecoderVtable.open = ffmpegVideoDecoderOpen;
    ffmpegVideoDecoderVtable.close = ffmpegVideoDecoderClose;
    ffmpegVideoDecoderVtable.extractAudio = ffmpegVideoDecoderExtractAudio;
    ffmpegVideoDecoderVtable.isRunning = ffmpegVideoDecoderIsRunning;
    ffmpegVideoDecoderVtable.isPaused = ffmpegVideoDecoderIsPaused;
    ffmpegVideoDecoderVtable.pause = ffmpegVideoDecoderPause;
    ffmpegVideoDecoderVtable.resume = ffmpegVideoDecoderResume;
    ffmpegVideoDecoderVtable.setLoop = ffmpegVideoDecoderSetLoop;
    ffmpegVideoDecoderVtable.update = ffmpegVideoDecoderUpdate;
    ffmpegVideoDecoderVtable.draw = ffmpegVideoDecoderDraw;
    ffmpegVideoDecoderVtable.duration = ffmpegVideoDecoderDuration;
    ffmpegVideoDecoderVtable.position = ffmpegVideoDecoderPosition;
    ffmpegVideoDecoderVtable.width = ffmpegVideoDecoderWidth;
    ffmpegVideoDecoderVtable.height = ffmpegVideoDecoderHeight;
    decoder->vtable = &ffmpegVideoDecoderVtable;
    decoder->impl = impl;
    return decoder;
}