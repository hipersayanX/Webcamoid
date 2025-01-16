/* Webcamoid, webcam capture application.
 * Copyright (C) 2024  Gonzalo Exequiel Pedone
 *
 * Webcamoid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Webcamoid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
 *
 * Web-Site: http://webcamoid.github.io/
 */

#include <QLibrary>
#include <QMutex>
#include <QThread>
#include <QVariant>
#include <akfrac.h>
#include <akpacket.h>
#include <akvideocaps.h>
#include <akcompressedvideocaps.h>
#include <akpluginmanager.h>
#include <akvideoconverter.h>
#include <akvideopacket.h>
#include <akcompressedvideopacket.h>
#include <iak/akelement.h>
#include <media/NdkMediaCodec.h>

#include "videoencoderndkmediaelement.h"

#define BITRATE_MODE_CQ  0
#define BITRATE_MODE_VBR 1
#define BITRATE_MODE_CBR 2

#if __ANDROID_API__ < 28
#define FORMAT_KEY_SLICE_HEIGHT "slice-height"
#else
#define FORMAT_KEY_SLICE_HEIGHT AMEDIAFORMAT_KEY_SLICE_HEIGHT
#endif

#define PROCESSING_TIMEOUT 3000

#define VideoCodecID_amvp8  AkCompressedVideoCaps::VideoCodecID(AK_MAKE_FOURCC(0xA, 'V', 'P', '8'))
#define VideoCodecID_amvp9  AkCompressedVideoCaps::VideoCodecID(AK_MAKE_FOURCC(0xA, 'V', 'P', '9'))
#define VideoCodecID_amav1  AkCompressedVideoCaps::VideoCodecID(AK_MAKE_FOURCC(0xA, 'A', 'V', '1'))
#define VideoCodecID_amh264 AkCompressedVideoCaps::VideoCodecID(AK_MAKE_FOURCC(0xA, 'A', 'V', 'C'))
#define VideoCodecID_amhevc AkCompressedVideoCaps::VideoCodecID(AK_MAKE_FOURCC(0xA, 'H', 'E', 'V'))

struct NDKMediaCodecs
{
    AkVideoEncoderCodecID codecID;
    const char *mimeType;
    const char *name;
    const char *description;

    static inline const NDKMediaCodecs *table()
    {
        static const NDKMediaCodecs ndkmediaVideoEncCodecsTable[] = {
            {VideoCodecID_amvp8                         , "video/x-vnd.on2.vp8", "vp8" , "VP8" },
            {VideoCodecID_amvp9                         , "video/x-vnd.on2.vp9", "vp9" , "VP9" },
            {VideoCodecID_amav1                         , "video/av01"         , "av1" , "AV1" },
            {VideoCodecID_amh264                        , "video/avc"          , "h264", "H264"},
            {VideoCodecID_amhevc                        , "video/hevc"         , "hevc", "HEVC"},
            {AkCompressedVideoCaps::VideoCodecID_unknown, ""                   , ""    , ""    },
        };

        return ndkmediaVideoEncCodecsTable;
    }

    static inline QStringList codecs()
    {
        QStringList codecs;

        for (auto codec = table();
             codec->codecID != AkCompressedVideoCaps::VideoCodecID_unknown;
             codec++) {
            codecs << codec->name;
        }

        return codecs;
    }

    static inline const NDKMediaCodecs *byCodecID(AkVideoEncoderCodecID codecID)
    {
        auto codec = table();

        for (;
             codec->codecID != AkCompressedVideoCaps::VideoCodecID_unknown;
             codec++) {
            if (codec->codecID == codecID)
                return codec;
        }

        return codec;
    }

    static inline const NDKMediaCodecs *byName(const QString &name)
    {
        auto codec = table();

        for (;
             codec->codecID != AkCompressedVideoCaps::VideoCodecID_unknown;
             codec++) {
            if (codec->name == name)
                return codec;
        }

        return codec;
    }
};

struct CodecInfo
{
    QString name;
    QString description;
    AkVideoEncoderCodecID codecID;
    QString mimeType;
    AkVideoCaps::PixelFormatList formats;
};

#define COLOR_FormatMonochrome                1
#define COLOR_Format8bitRGB332                2
#define COLOR_Format12bitRGB444               3
#define COLOR_Format16bitARGB4444             4
#define COLOR_Format16bitARGB1555             5
#define COLOR_Format16bitRGB565               6
#define COLOR_Format16bitBGR565               7
#define COLOR_Format18bitRGB666               8
#define COLOR_Format18bitARGB1665             9
#define COLOR_Format19bitARGB1666             10
#define COLOR_Format24bitRGB888               11
#define COLOR_Format24bitBGR888               12
#define COLOR_Format24bitARGB1887             13
#define COLOR_Format25bitARGB1888             14
#define COLOR_Format32bitBGRA8888             15
#define COLOR_Format32bitARGB8888             16
#define COLOR_FormatYUV411Planar              17
#define COLOR_FormatYUV411PackedPlanar        18
#define COLOR_FormatYUV420Planar              19
#define COLOR_FormatYUV420PackedPlanar        20
#define COLOR_FormatYUV420SemiPlanar          21
#define COLOR_FormatYUV422Planar              22
#define COLOR_FormatYUV422PackedPlanar        23
#define COLOR_FormatYUV422SemiPlanar          24
#define COLOR_FormatYCbYCr                    25
#define COLOR_FormatYCrYCb                    26
#define COLOR_FormatCbYCrY                    27
#define COLOR_FormatCrYCbY                    28
#define COLOR_FormatYUV444Interleaved         29
#define COLOR_FormatRawBayer8bit              30
#define COLOR_FormatRawBayer10bit             31
#define COLOR_FormatRawBayer8bitcompressed    32
#define COLOR_FormatL2                        33
#define COLOR_FormatL4                        34
#define COLOR_FormatL8                        35
#define COLOR_FormatL16                       36
#define COLOR_FormatL24                       37
#define COLOR_FormatL32                       38
#define COLOR_FormatYUV420PackedSemiPlanar    39
#define COLOR_FormatYUV422PackedSemiPlanar    40
#define COLOR_Format18BitBGR666               41
#define COLOR_Format24BitARGB6666             42
#define COLOR_Format24BitABGR6666             43
#define COLOR_TI_FormatYUV420PackedSemiPlanar 0x7f000100
#define COLOR_FormatSurface                   0x7f000789
#define COLOR_Format32bitABGR8888             0x7f00a000
#define COLOR_FormatYUV420Flexible            0x7f420888
#define COLOR_FormatYUV422Flexible            0x7f422888
#define COLOR_FormatYUV444Flexible            0x7f444888
#define COLOR_FormatRGBFlexible               0x7f36b888
#define COLOR_FormatRGBAFlexible              0x7f36a888
#define COLOR_QCOM_FormatYUV420SemiPlanar     0x7fa30c00

struct PixelFormatsTable
{
    int32_t ndkFormat;
    AkVideoCaps::PixelFormat format;

    inline static const PixelFormatsTable *table()
    {
        static const PixelFormatsTable ndkmediaEncoderPixelFormatsTable[] {
            //{COLOR_Format16bitRGB565   , AkVideoCaps::Format_rgb565le},
            //{COLOR_Format24bitBGR888   , AkVideoCaps::Format_bgr24   },
            //{COLOR_FormatL8            , AkVideoCaps::Format_y8      },
            //{COLOR_FormatL16           , AkVideoCaps::Format_y16le   },
            //{COLOR_Format32bitABGR8888 , AkVideoCaps::Format_abgr    },
            {COLOR_FormatYUV420Flexible, AkVideoCaps::Format_yuv420p },
            {COLOR_FormatYUV422Flexible, AkVideoCaps::Format_yuv422p },
            {COLOR_FormatYUV444Flexible, AkVideoCaps::Format_yuv444p },
            //{COLOR_FormatRGBFlexible   , AkVideoCaps::Format_rgb24p  },
            //{COLOR_FormatRGBAFlexible  , AkVideoCaps::Format_rgbap   },

            // Deprecated pixel formats

            //{COLOR_Format8bitRGB332            , AkVideoCaps::Format_rgb332    },
            //{COLOR_Format12bitRGB444           , AkVideoCaps::Format_rgb444le  },
            //{COLOR_Format16bitARGB4444         , AkVideoCaps::Format_argb4444le},
            //{COLOR_Format16bitARGB1555         , AkVideoCaps::Format_argb1555le},
            //{COLOR_Format16bitBGR565           , AkVideoCaps::Format_bgr565le  },
            //{COLOR_Format24bitRGB888           , AkVideoCaps::Format_rgb24     },
            //{COLOR_Format32bitBGRA8888         , AkVideoCaps::Format_bgra      },
            //{COLOR_Format32bitARGB8888         , AkVideoCaps::Format_argb      },
            //{COLOR_FormatYUV411Planar          , AkVideoCaps::Format_yuv411p   },
            //{COLOR_FormatYUV411PackedPlanar    , AkVideoCaps::Format_yuv411p   },
            //{COLOR_FormatYUV420Planar          , AkVideoCaps::Format_yuv420p   },
            //{COLOR_FormatYUV420PackedPlanar    , AkVideoCaps::Format_yuv420p   },
            //{COLOR_FormatYUV420SemiPlanar      , AkVideoCaps::Format_nv12      },
            //{COLOR_FormatYUV422Planar          , AkVideoCaps::Format_yuv422p   },
            //{COLOR_FormatYUV422PackedPlanar    , AkVideoCaps::Format_yuv422p   },
            //{COLOR_FormatYUV422SemiPlanar      , AkVideoCaps::Format_yuv422p   },
            //{COLOR_FormatYCbYCr                , AkVideoCaps::Format_yuyv422   },
            //{COLOR_FormatYCrYCb                , AkVideoCaps::Format_yvyu422   },
            //{COLOR_FormatCbYCrY                , AkVideoCaps::Format_uyvy422   },
            //{COLOR_FormatCrYCbY                , AkVideoCaps::Format_vyuy422   },
            //{COLOR_FormatYUV444Interleaved     , AkVideoCaps::Format_yuv444    },
            //{COLOR_FormatL32                   , AkVideoCaps::Format_y32le     },
            //{COLOR_FormatYUV420PackedSemiPlanar, AkVideoCaps::Format_yuv420p   },
            //{COLOR_FormatYUV422PackedSemiPlanar, AkVideoCaps::Format_yuv422p   },
            {0                                 , AkVideoCaps::Format_none      },
        };

        return ndkmediaEncoderPixelFormatsTable;
    }

    inline static const PixelFormatsTable *byFormat(AkVideoCaps::PixelFormat format)
    {
        auto item = table();

        for (; item->format != AkVideoCaps::Format_none; ++item)
            if (item->format == format)
                return item;

        return item;
    }

    inline static const PixelFormatsTable *byNdkFormat(int32_t format)
    {
        auto item = table();

        for (; item->format != AkVideoCaps::Format_none; ++item)
            if (item->ndkFormat == format)
                return item;

        return item;
    }

    inline static AkVideoCaps::PixelFormatList formats()
    {
        AkVideoCaps::PixelFormatList formats;

        for (auto item = table();
             item->format != AkVideoCaps::Format_none;
             ++item) {
            formats << item->format;
        }

        return formats;
    }
};

using AMediaFormatPtr = QSharedPointer<AMediaFormat>;

class VideoEncoderNDKMediaElementPrivate
{
    public:
        VideoEncoderNDKMediaElement *self;
        AkVideoConverter m_videoConverter;
        AkCompressedVideoCaps m_outputCaps;
        QByteArray m_headers;
        QVector<CodecInfo> m_codecs;
        AMediaCodec *m_codec {nullptr};
        AMediaFormatPtr m_mediaFormat;
        QMutex m_mutex;
        qint64 m_id {0};
        int m_index {0};
        bool m_initialized {false};
        bool m_paused {false};
        qint64 m_encodedTimePts {0};
        AkElementPtr m_fpsControl {akPluginManager->create<AkElement>("VideoFilter/FpsControl")};

        explicit VideoEncoderNDKMediaElementPrivate(VideoEncoderNDKMediaElement *self);
        ~VideoEncoderNDKMediaElementPrivate();
        static const char *errorToStr(media_status_t status);
        bool isAvailable(const QString &mimeType) const;
        void listCodecs();
        bool init();
        void uninit();
        void updateHeaders();
        void updateOutputCaps();
        void writeFrame(const AkVideoPacket &packet,
                        uint8_t *buffer,
                        size_t bufferSize) const;
        void encodeFrame(const AkVideoPacket &src);
        void sendFrame(const uint8_t *data,
                       const AMediaCodecBufferInfo &info) const;
};

VideoEncoderNDKMediaElement::VideoEncoderNDKMediaElement():
    AkVideoEncoder()
{
    this->d = new VideoEncoderNDKMediaElementPrivate(this);
    this->d->listCodecs();
    this->setCodec(NDKMediaCodecs::codecs().value(0));
}

VideoEncoderNDKMediaElement::~VideoEncoderNDKMediaElement()
{
    this->d->uninit();
    delete this->d;
}

QStringList VideoEncoderNDKMediaElement::codecs() const
{
    QStringList codecs;

    for (auto &codec: this->d->m_codecs)
        codecs << codec.name;

    return codecs;
}

AkVideoEncoderCodecID VideoEncoderNDKMediaElement::codecID(const QString &codec) const
{
    auto it = std::find_if(this->d->m_codecs.constBegin(),
                           this->d->m_codecs.constEnd(),
                           [&codec] (const CodecInfo &codecInfo) -> bool {
        return codecInfo.name == codec;
    });

    if (it == this->d->m_codecs.constEnd())
        return AkCompressedVideoCaps::VideoCodecID_unknown;

    return it->codecID;
}

QString VideoEncoderNDKMediaElement::codecDescription(const QString &codec) const
{
    auto it = std::find_if(this->d->m_codecs.constBegin(),
                           this->d->m_codecs.constEnd(),
                           [&codec] (const CodecInfo &codecInfo) -> bool {
        return codecInfo.name == codec;
    });

    if (it == this->d->m_codecs.constEnd())
        return {};

    return it->description;
}

AkCompressedVideoCaps VideoEncoderNDKMediaElement::outputCaps() const
{
    return this->d->m_outputCaps;
}

QByteArray VideoEncoderNDKMediaElement::headers() const
{
    return this->d->m_headers;
}

qint64 VideoEncoderNDKMediaElement::encodedTimePts() const
{
    return this->d->m_encodedTimePts;
}

AkPacket VideoEncoderNDKMediaElement::iVideoStream(const AkVideoPacket &packet)
{
    QMutexLocker mutexLocker(&this->d->m_mutex);

    if (this->d->m_paused || !this->d->m_initialized || !this->d->m_fpsControl)
        return {};

    bool discard = false;
    QMetaObject::invokeMethod(this->d->m_fpsControl.data(),
                              "discard",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, discard),
                              Q_ARG(AkVideoPacket, packet));

    if (discard)
        return {};

    this->d->m_videoConverter.begin();
    auto src = this->d->m_videoConverter.convert(packet);
    this->d->m_videoConverter.end();

    if (!src)
        return {};

    this->d->m_fpsControl->iStream(src);

    return {};
}

bool VideoEncoderNDKMediaElement::setState(ElementState state)
{
    auto curState = this->state();

    switch (curState) {
    case AkElement::ElementStateNull: {
        switch (state) {
        case AkElement::ElementStatePaused:
            this->d->m_paused = state == AkElement::ElementStatePaused;
        case AkElement::ElementStatePlaying:
            if (!this->d->init()) {
                this->d->m_paused = false;

                return false;
            }

            return AkElement::setState(state);
        default:
            break;
        }

        break;
    }
    case AkElement::ElementStatePaused: {
        switch (state) {
        case AkElement::ElementStateNull:
            this->d->uninit();

            return AkElement::setState(state);
        case AkElement::ElementStatePlaying:
            this->d->m_paused = false;

            return AkElement::setState(state);
        default:
            break;
        }

        break;
    }
    case AkElement::ElementStatePlaying: {
        switch (state) {
        case AkElement::ElementStateNull:
            this->d->uninit();

            return AkElement::setState(state);
        case AkElement::ElementStatePaused:
            this->d->m_paused = true;

            return AkElement::setState(state);
        default:
            break;
        }

        break;
    }
    }

    return false;
}

VideoEncoderNDKMediaElementPrivate::VideoEncoderNDKMediaElementPrivate(VideoEncoderNDKMediaElement *self):
    self(self)
{
    this->m_videoConverter.setAspectRatioMode(AkVideoConverter::AspectRatioMode_Fit);

    QObject::connect(self,
                     &AkVideoEncoder::inputCapsChanged,
                     [this] (const AkVideoCaps &inputCaps) {
                        Q_UNUSED(inputCaps)

                        this->updateOutputCaps();
                     });

    if (this->m_fpsControl)
        QObject::connect(this->m_fpsControl.data(),
                         &AkElement::oStream,
                         [this] (const AkPacket &packet) {
                             this->encodeFrame(packet);
                         });
}

VideoEncoderNDKMediaElementPrivate::~VideoEncoderNDKMediaElementPrivate()
{

}

const char *VideoEncoderNDKMediaElementPrivate::errorToStr(media_status_t status)
{
    static const struct
    {
        media_status_t status;
        const char *str;
    } audioNkmEncErrorsStr[] = {
        {AMEDIACODEC_ERROR_INSUFFICIENT_RESOURCE, "INSUFFICIENT_RESOURCE"        },
        {AMEDIACODEC_ERROR_RECLAIMED            , "ERROR_RECLAIMED"              },
        {AMEDIA_ERROR_BASE                      , "ERROR_BASE"                   },
        {AMEDIA_ERROR_UNKNOWN                   , "ERROR_UNKNOWN"                },
        {AMEDIA_ERROR_MALFORMED                 , "ERROR_MALFORMED"              },
        {AMEDIA_ERROR_UNSUPPORTED               , "ERROR_UNSUPPORTED"            },
        {AMEDIA_ERROR_INVALID_OBJECT            , "ERROR_INVALID_OBJECT"         },
        {AMEDIA_ERROR_INVALID_PARAMETER         , "ERROR_INVALID_PARAMETER"      },
        {AMEDIA_ERROR_INVALID_OPERATION         , "ERROR_INVALID_OPERATION"      },
        {AMEDIA_ERROR_END_OF_STREAM             , "ERROR_END_OF_STREAM"          },
        {AMEDIA_ERROR_IO                        , "ERROR_IO"                     },
        {AMEDIA_ERROR_WOULD_BLOCK               , "ERROR_WOULD_BLOCK"            },
        {AMEDIA_DRM_ERROR_BASE                  , "DRM_ERROR_BASE"               },
        {AMEDIA_DRM_NOT_PROVISIONED             , "DRM_NOT_PROVISIONED"          },
        {AMEDIA_DRM_RESOURCE_BUSY               , "DRM_RESOURCE_BUSY"            },
        {AMEDIA_DRM_DEVICE_REVOKED              , "DRM_DEVICE_REVOKED"           },
        {AMEDIA_DRM_SHORT_BUFFER                , "DRM_SHORT_BUFFER"             },
        {AMEDIA_DRM_SESSION_NOT_OPENED          , "DRM_SESSION_NOT_OPENED"       },
        {AMEDIA_DRM_TAMPER_DETECTED             , "DRM_TAMPER_DETECTED"          },
        {AMEDIA_DRM_VERIFY_FAILED               , "DRM_VERIFY_FAILED"            },
        {AMEDIA_DRM_NEED_KEY                    , "DRM_NEED_KEY"                 },
        {AMEDIA_DRM_LICENSE_EXPIRED             , "DRM_LICENSE_EXPIRED"          },
        {AMEDIA_IMGREADER_ERROR_BASE            , "IMGREADER_ERROR_BASE"         },
        {AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE   , "IMGREADER_NO_BUFFER_AVAILABLE"},
        {AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED   , "IMGREADER_MAX_IMAGES_ACQUIRED"},
        {AMEDIA_IMGREADER_CANNOT_LOCK_IMAGE     , "IMGREADER_CANNOT_LOCK_IMAGE"  },
        {AMEDIA_IMGREADER_CANNOT_UNLOCK_IMAGE   , "IMGREADER_CANNOT_UNLOCK_IMAGE"},
        {AMEDIA_IMGREADER_IMAGE_NOT_LOCKED      , "IMGREADER_IMAGE_NOT_LOCKED"   },
        {AMEDIA_OK                              , "OK"                           },
    };

    auto errorStatus = audioNkmEncErrorsStr;

    for (; errorStatus->status != AMEDIA_OK; ++errorStatus)
        if (errorStatus->status == status)
            return errorStatus->str;

    return errorStatus->str;
}

bool VideoEncoderNDKMediaElementPrivate::isAvailable(const QString &mimeType) const
{
    static struct
    {
        char mimeType[1024];
        bool isAvailable;
    } ndkmediaVideoEncAvailableCodecs[32];
    static size_t ndkmediaVideoEncAvailableCodecsSize = 0;

    for (size_t i = 0; i < ndkmediaVideoEncAvailableCodecsSize; ++i)
        if (ndkmediaVideoEncAvailableCodecs[i].mimeType == mimeType)
            return ndkmediaVideoEncAvailableCodecs[i].isAvailable;

    auto codec = AMediaCodec_createEncoderByType(mimeType.toStdString().c_str());
    bool isAvailable = false;

    if (codec) {
        isAvailable = true;
        AMediaCodec_delete(codec);
    }

    auto i = ndkmediaVideoEncAvailableCodecsSize++;
    qstrncpy(ndkmediaVideoEncAvailableCodecs[i].mimeType,
            mimeType.toStdString().c_str(),
            1024);
    ndkmediaVideoEncAvailableCodecs[i].isAvailable = isAvailable;

    return isAvailable;
}

void VideoEncoderNDKMediaElementPrivate::listCodecs()
{
    auto formats = PixelFormatsTable::formats();

    for (auto codec = NDKMediaCodecs::table();
         codec->codecID != AkCompressedVideoCaps::VideoCodecID_unknown;
         ++codec) {

        if (!this->isAvailable(codec->mimeType))
            continue;

        this->m_codecs << CodecInfo {QString(codec->name),
                                     QString(codec->description),
                                     codec->codecID,
                                     codec->mimeType,
                                     formats};
    }
}

bool VideoEncoderNDKMediaElementPrivate::init()
{
    this->uninit();

    auto inputCaps = self->inputCaps();

    if (!inputCaps) {
        qCritical() << "Invalid input format.";

        return false;
    }

    auto mimeType = NDKMediaCodecs::byName(self->codec())->mimeType;

    if (QString(mimeType).isEmpty()) {
        qCritical() << "Mimetype not found";

        return false;
    }

    this->m_codec =
            AMediaCodec_createEncoderByType(mimeType);

    if (!this->m_codec) {
        qCritical() << "Encoder not found";

        return false;
    }

    this->m_mediaFormat =
            AMediaFormatPtr(AMediaFormat_new(),
                            [] (AMediaFormat *mediaFormat) {
        AMediaFormat_delete(mediaFormat);
    });
    AMediaFormat_setString(this->m_mediaFormat.data(),
                           AMEDIAFORMAT_KEY_MIME,
                           mimeType);
    AMediaFormat_setInt32(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_BIT_RATE,
                          self->bitrate());
    AMediaFormat_setString(this->m_mediaFormat.data(),
                           AMEDIAFORMAT_KEY_LANGUAGE,
                           "und");
    AMediaFormat_setInt32(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_COLOR_FORMAT,
                          PixelFormatsTable::byFormat(this->m_videoConverter.outputCaps().format())->ndkFormat);
    AMediaFormat_setInt32(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_WIDTH,
                          this->m_videoConverter.outputCaps().width());
    AMediaFormat_setInt32(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_HEIGHT,
                          this->m_videoConverter.outputCaps().height());
    AMediaFormat_setFloat(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_FRAME_RATE,
                          float(this->m_videoConverter.outputCaps().fps().value()));
    auto specs = AkVideoCaps::formatSpecs(this->m_videoConverter.outputCaps().format());
    auto &plane = specs.plane(0);
    size_t stride = this->m_videoConverter.outputCaps().width() & 0x1?
                        plane.bitsSize() * (this->m_videoConverter.outputCaps().width() + 1) / 8:
                        plane.bitsSize() * this->m_videoConverter.outputCaps().width() / 8;
    AMediaFormat_setInt32(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_STRIDE,
                          stride);
    AMediaFormat_setInt32(this->m_mediaFormat.data(),
                          FORMAT_KEY_SLICE_HEIGHT,
                          this->m_videoConverter.outputCaps().height());
    int gop =
            qMax(self->gop() * this->m_videoConverter.outputCaps().fps().num()
                 / (1000 * this->m_videoConverter.outputCaps().fps().den()), 1);
    AMediaFormat_setInt32(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_I_FRAME_INTERVAL,
                          gop);

#if __ANDROID_API__ >= 28
    AMediaFormat_setInt32(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_BITRATE_MODE,
                          BITRATE_MODE_CBR);
#endif

    auto result =
            AMediaCodec_configure(this->m_codec,
                                  this->m_mediaFormat.data(),
                                  nullptr,
                                  nullptr,
                                  AMEDIACODEC_CONFIGURE_FLAG_ENCODE);

    if (result != AMEDIA_OK) {
        qCritical() << "Encoder configuration failed:" << errorToStr(result);

        return false;
    }

    result = AMediaCodec_start(this->m_codec);

    if (result != AMEDIA_OK) {
        qCritical() << "Failed to start the encoding:" << errorToStr(result);

        return false;
    }

    this->updateHeaders();

    if (this->m_fpsControl) {
        this->m_fpsControl->setProperty("fps", QVariant::fromValue(this->m_videoConverter.outputCaps().fps()));
        this->m_fpsControl->setProperty("fillGaps", self->fillGaps());
        QMetaObject::invokeMethod(this->m_fpsControl.data(),
                                  "restart",
                                  Qt::DirectConnection);
    }

    this->m_encodedTimePts = 0;
    this->m_initialized = true;

    return true;
}

void VideoEncoderNDKMediaElementPrivate::uninit()
{
    QMutexLocker mutexLocker(&this->m_mutex);

    if (!this->m_initialized)
        return;

    this->m_initialized = false;

    if (this->m_codec) {
        auto bufferIndex =
                AMediaCodec_dequeueInputBuffer(this->m_codec, PROCESSING_TIMEOUT);

        if (bufferIndex >= 0) {
            AMediaCodec_queueInputBuffer(this->m_codec,
                                         size_t(bufferIndex),
                                         0,
                                         0,
                                         0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        }

        bool eos = false;

        while (!eos) {
            AMediaCodecBufferInfo info;
            memset(&info, 0, sizeof(AMediaCodecBufferInfo));
            auto bufferIndex = AMediaCodec_dequeueOutputBuffer(this->m_codec,
                                                               &info,
                                                               PROCESSING_TIMEOUT);

            if (bufferIndex < 0)
                break;

            if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)
                continue;

            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
                eos = true;

            size_t bufferSize = 0;
            auto data = AMediaCodec_getOutputBuffer(this->m_codec,
                                                    size_t(bufferIndex),
                                                    &bufferSize);
            this->sendFrame(data, info);
            AMediaCodec_releaseOutputBuffer(this->m_codec,
                                            size_t(bufferIndex),
                                            info.size > 0);
        }

        AMediaCodec_stop(this->m_codec);
    }

    this->m_mediaFormat = {};

    if (this->m_codec) {
        AMediaCodec_delete(this->m_codec);
        this->m_codec = nullptr;
    }

    if (this->m_fpsControl)
        QMetaObject::invokeMethod(this->m_fpsControl.data(),
                                  "restart",
                                  Qt::DirectConnection);

    this->m_paused = false;
}

void VideoEncoderNDKMediaElementPrivate::updateHeaders()
{
    auto mfptr = qsizetype(this->m_mediaFormat.data());
    QByteArray headers(reinterpret_cast<char *>(&mfptr), sizeof(qsizetype));

    if (this->m_headers == headers)
        return;

    this->m_headers = headers;
    emit self->headersChanged(headers);
}

void VideoEncoderNDKMediaElementPrivate::updateOutputCaps()
{
    auto inputCaps = self->inputCaps();

    if (!inputCaps) {
        if (!this->m_outputCaps)
            return;

        this->m_outputCaps = {};
        emit self->outputCapsChanged({});

        return;
    }

    auto codecID = self->codecID(self->codec());

    if (codecID == AkCompressedVideoCaps::VideoCodecID_unknown) {
        if (!this->m_outputCaps)
            return;

        this->m_outputCaps = {};
        emit self->outputCapsChanged({});

        return;
    }

    auto it = std::find_if(this->m_codecs.constBegin(),
                           this->m_codecs.constEnd(),
                           [this] (const CodecInfo &codec) -> bool {
        return codec.name == self->codec();
    });

    if (it == this->m_codecs.constEnd()) {
        if (!this->m_outputCaps)
            return;

        this->m_outputCaps = {};
        emit self->outputCapsChanged({});

        return;
    }

    AkVideoCaps::PixelFormat format =
            it->formats.contains(inputCaps.format())?
                inputCaps.format():
                it->formats.first();

    auto fps = inputCaps.fps();

    if (!fps)
        fps = {30, 1};

    this->m_videoConverter.setOutputCaps({format,
                                          inputCaps.width(),
                                          inputCaps.height(),
                                          fps});
    AkCompressedVideoCaps outputCaps(codecID,
                                     this->m_videoConverter.outputCaps(),
                                     self->bitrate());

    if (this->m_outputCaps == outputCaps)
        return;

    this->m_outputCaps = outputCaps;
    emit self->outputCapsChanged(outputCaps);
}

void VideoEncoderNDKMediaElementPrivate::writeFrame(const AkVideoPacket &packet,
                                                    uint8_t *buffer,
                                                    size_t bufferSize) const
{
    Q_UNUSED(bufferSize)

    int32_t stride = 0;
    AMediaFormat_getInt32(this->m_mediaFormat.data(),
                          AMEDIAFORMAT_KEY_STRIDE,
                          &stride);
    int32_t sliceHeight = 0;
    AMediaFormat_getInt32(this->m_mediaFormat.data(),
                          FORMAT_KEY_SLICE_HEIGHT,
                          &sliceHeight);

    if (sliceHeight < packet.caps().height())
        sliceHeight = packet.caps().height();

    auto oData = buffer;

    for (int plane = 0; plane < packet.planes(); ++plane) {
        auto iLineSize = packet.lineSize(plane);
        auto oLineSize = packet.planes() > 1?
                             stride >> packet.widthDiv(plane):
                                       stride;
        auto lineSize = qMin<size_t>(iLineSize, oLineSize);
        auto heightDiv = packet.heightDiv(plane);

        for (int y = 0; y < packet.caps().height(); ++y) {
            int ys = y >> heightDiv;
            memcpy(oData + ys * oLineSize,
                   packet.constLine(plane, y),
                   lineSize);
        }

        oData += oLineSize * (sliceHeight >> heightDiv);
    }
}

void VideoEncoderNDKMediaElementPrivate::encodeFrame(const AkVideoPacket &src)
{
    this->m_id = src.id();
    this->m_index = src.index();

    // Write the current frame.
    auto bufferIndex =
            AMediaCodec_dequeueInputBuffer(this->m_codec, PROCESSING_TIMEOUT);

    if (bufferIndex >= 0) {
        size_t bufferSize = 0;
        auto buffer = AMediaCodec_getInputBuffer(this->m_codec,
                                                 size_t(bufferIndex),
                                                 &bufferSize);
        this->writeFrame(src, buffer, bufferSize);
        uint64_t presentationTimeUs =
                qRound64(1e6 * src.pts() * src.timeBase().value());
        AMediaCodec_queueInputBuffer(this->m_codec,
                                     size_t(bufferIndex),
                                     0,
                                     bufferSize,
                                     presentationTimeUs,
                                     0);
    }

    forever {
        AMediaCodecBufferInfo info;
        memset(&info, 0, sizeof(AMediaCodecBufferInfo));
        auto bufferIndex = AMediaCodec_dequeueOutputBuffer(this->m_codec,
                                                           &info,
                                                           PROCESSING_TIMEOUT);

        if (bufferIndex < 0)
            break;

        if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)
            continue;

        size_t bufferSize = 0;
        auto data = AMediaCodec_getOutputBuffer(this->m_codec,
                                                size_t(bufferIndex),
                                                &bufferSize);
        this->sendFrame(data, info);
        AMediaCodec_releaseOutputBuffer(this->m_codec,
                                        size_t(bufferIndex),
                                        info.size > 0);
    }

    this->m_encodedTimePts = src.pts() + src.duration();
    emit self->encodedTimePtsChanged(this->m_encodedTimePts);
}

void VideoEncoderNDKMediaElementPrivate::sendFrame(const uint8_t *data,
                                                   const AMediaCodecBufferInfo &info) const
{
    AkCompressedVideoPacket packet(this->m_outputCaps, info.size);
    memcpy(packet.data(), data, packet.size());
    packet.setFlags(info.flags & 1?
                        AkCompressedVideoPacket::VideoPacketTypeFlag_KeyFrame:
                        AkCompressedVideoPacket::VideoPacketTypeFlag_None);
    qint64 pts = qRound64(info.presentationTimeUs
                          * this->m_outputCaps.rawCaps().fps().value()
                          / 1e6);
    packet.setPts(pts);
    packet.setDts(pts);
    packet.setDuration(1);
    packet.setTimeBase(this->m_outputCaps.rawCaps().fps().invert());
    packet.setId(this->m_id);
    packet.setIndex(this->m_index);
    packet.setExtraData({reinterpret_cast<const char *>(&info),
                         sizeof(AMediaCodecBufferInfo)});

    emit self->oStream(packet);
}

#include "moc_videoencoderndkmediaelement.cpp"