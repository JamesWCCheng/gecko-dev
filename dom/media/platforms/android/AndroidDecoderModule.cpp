/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/jni/Types.h"
#include "AndroidDecoderModule.h"
#include "AndroidBridge.h"
#include "AndroidSurfaceTexture.h"
#include "GLImages.h"

#include "MediaData.h"
#include "MediaInfo.h"
#include "VPXDecoder.h"

#include "nsThreadUtils.h"
#include "nsAutoPtr.h"
#include "nsPromiseFlatString.h"
#include "nsIGfxInfo.h"

#include "prlog.h"

#include <jni.h>
#include "nsTArray.h"

#include "ezlogger.h"
#include "GeneratedJNIWrappers.h"
#include "mozilla/GMPCDMProxy.h"
using namespace mozilla::widget::sdk;
using namespace mozilla;
/* Will initialed in AndroidDecoderModule cstr*/
UUID::GlobalRef widevineUUID;
UUID::GlobalRef clearkeyUUID;
/* Modify this to switch the current testing drm module.*/
UUID::GlobalRef currentTestingUUID;
static UUID::LocalRef GenDrmUUID(int64_t m, int64_t l)
{
  UUID::LocalRef uuid;
  auto rv = UUID::New(m, l, &uuid);
  return uuid;
}
template<class T>
static jbyteArray FillJByteArray(const T& data, jsize length)
{
  JNIEnv* const jenv = mozilla::jni::GetEnvForThread();
  jbyteArray result = jenv->NewByteArray(length);
  jenv->SetByteArrayRegion(result, 0, length, reinterpret_cast<const jbyte*>(const_cast<T>(data)));
  return result;
}
template<class T>
static jintArray FillJIntArray(const T& data, jsize length)
{
  JNIEnv* const jenv = mozilla::jni::GetEnvForThread();
  jintArray result = jenv->NewIntArray(length);
  jenv->SetIntArrayRegion(result, 0, length, reinterpret_cast<const jint*>(const_cast<T>(data)));
  return result;
}

// same as https://dxr.mozilla.org/mozilla-central/source/mobile/android/base/java/org/mozilla/gecko/util/HardwareCodecCapabilityUtils.java#40
void PrintSecureDecoderNameForMime() {
  using namespace mozilla::widget::sdk;
  int codec_num = 0;
  auto rvGetCodecCount = MediaCodecList::GetCodecCount(&codec_num);
  for (int i = 0; i < codec_num; ++i) {
    MediaCodecInfo::LocalRef mediainfotest;
    auto rvGetCodecInfoAt2 = MediaCodecList::GetCodecInfoAt(i, ReturnTo(&mediainfotest));
    mozilla::jni::Object::LocalRef info;
    auto rvGetCodecInfoAt = MediaCodecList::GetCodecInfoAt(i, &info);
    MediaCodecInfo::LocalRef mediainfo = MediaCodecInfo::LocalRef::Adopt(info.Forget());
    bool isEncoder = false;
    mediainfo->IsEncoder(&isEncoder);
    if (isEncoder) {
        continue;
    }
    mozilla::jni::ObjectArray::LocalRef supportedTypes;
    auto rvGetSupportedTypes = mediainfo->GetSupportedTypes(&supportedTypes);
    mozilla::jni::String::LocalRef codeecNameStr;
    mediainfo->GetName(&codeecNameStr);
    PR(codeecNameStr->ToCString());
    JNIEnv* const env = jni::GetEnvForThread();
    jsize length = env->GetArrayLength(supportedTypes.Get());
    for (int j = 0; j < length; ++j) {
      auto supportType =
        nsJNIString(
            static_cast<jstring>(env->GetObjectArrayElement(supportedTypes.Get(), j)), env);
        PG(supportType);
    }
  }
}

nsCString GetSecureDecoderNameForMime(const nsACString& aMime)
{
  using namespace mozilla::widget::sdk;
  int codec_num = 0;
  auto rvGetCodecCount = MediaCodecList::GetCodecCount(&codec_num);
  for (int i = 0; i < codec_num; ++i) {
    mozilla::jni::Object::LocalRef info;
    auto rvGetCodecInfoAt = MediaCodecList::GetCodecInfoAt(i, &info);
    MediaCodecInfo::LocalRef mediainfo = MediaCodecInfo::LocalRef::Adopt(info.Forget());
    bool isEncoder = false;
    mediainfo->IsEncoder(&isEncoder);
    if (isEncoder) {
        continue;
    }
    mozilla::jni::ObjectArray::LocalRef supportedTypes;
    auto rvGetSupportedTypes = mediainfo->GetSupportedTypes(&supportedTypes);
    mozilla::jni::String::LocalRef nameStr;
    mediainfo->GetName(&nameStr);
    auto codecName = nameStr->ToCString();
    JNIEnv* const env = jni::GetEnvForThread();
    jsize length = env->GetArrayLength(supportedTypes.Get());
    for (int j = 0; j < length; ++j) {
      auto supportType =
        nsJNIString(
            static_cast<jstring>(env->GetObjectArrayElement(supportedTypes.Get(), j)), env);
        PG(supportType);
        NS_ConvertUTF16toUTF8 mime(supportType);
        if (mime.EqualsIgnoreCase(PromiseFlatCString(aMime).get())) {
           codecName.AppendLiteral(".secure");
           PR(codecName);
           return codecName;
        }
    }
  }
  return NS_LITERAL_CSTRING("");
}
static PRLogModuleInfo* AndroidDecoderModuleLog()
{
  static PRLogModuleInfo* sLogModule = nullptr;
  if (!sLogModule) {
    sLogModule = PR_NewLogModule("AndroidDecoderModule");
  }
  return sLogModule;
}

#undef LOG
#define LOG(arg, ...) MOZ_LOG(AndroidDecoderModuleLog(), \
    mozilla::LogLevel::Debug, ("AndroidDecoderModule(%p)::%s: " arg, \
      this, __func__, ##__VA_ARGS__))

using namespace mozilla;
using namespace mozilla::gl;
using namespace mozilla::widget::sdk;
using media::TimeUnit;

namespace mozilla {

template<class T>
auto ToStringHelper(T&& arg) -> nsString
{
  mozilla::jni::String::LocalRef str;
  arg->ToString(&str);
  return str->ToString();
}
template<class T>
auto ToCStringHelper(T&& arg) -> nsCString
{
  mozilla::jni::String::LocalRef str;
  arg->ToString(&str);
  return str->ToCString();
}
#define INVOKE_CALLBACK(Func, ...) \
  if (mCallback) { \
    mCallback->Func(__VA_ARGS__); \
  } else { \
    NS_WARNING("Callback not set"); \
  }

nsCString
TranslateMimeType(const nsACString& aMimeType)
{
  if (VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP8)) {
    return NS_LITERAL_CSTRING("video/x-vnd.on2.vp8");
  } else if (VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP9)) {
    return NS_LITERAL_CSTRING("video/x-vnd.on2.vp9");
  }
  nsCString mimetype(aMimeType);
  return mimetype;
  //return PromiseFlatCString(aMimeType).get();
}

static MediaCodec::LocalRef
CreateDecoder(const nsACString& aMimeType, bool isEncrypted, bool isRequiresSecureDecoderComponent)
{
  auto mimetype = TranslateMimeType(aMimeType);
  PR(mimetype);
  MediaCodec::LocalRef codec;

  // http://androidxref.com/5.1.1_r6/xref/cts/tests/tests/media/src/android/media/cts/MediaCodecCencPlayer.java#289
  if (isRequiresSecureDecoderComponent && isEncrypted) {
      // Need secure decoder, will append .secure postfix.
      auto codecName = GetSecureDecoderNameForMime(mimetype);
      auto rvCreateByCodecName = MediaCodec::CreateByCodecName(codecName, &codec);
      PR(codecName);
  } else {
      NS_ENSURE_SUCCESS(MediaCodec::CreateDecoderByType(TranslateMimeType(aMimeType),
                                                        &codec), nullptr);
  }
  // http://androidxref.com/5.1.1_r6/xref/external/chromium_org/media/base/android/java/src/org/chromium/media/MediaCodecBridge.java#260
  // PR(widget::GeckoAppShell::CheckIsAdaptivePlayback(codec, mimetype));
  return codec;
}

static bool
GetFeatureStatus(int32_t aFeature)
{
  nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();
  int32_t status = nsIGfxInfo::FEATURE_STATUS_UNKNOWN;
  nsCString discardFailureId;
  if (!gfxInfo || NS_FAILED(gfxInfo->GetFeatureStatus(aFeature, discardFailureId, &status))) {
    return false;
  }
  return status == nsIGfxInfo::FEATURE_STATUS_OK;
};

class VideoDataDecoder : public MediaCodecDataDecoder
{
public:
  VideoDataDecoder(const VideoInfo& aConfig,
                   MediaFormat::Param aFormat,
                   MediaDataDecoderCallback* aCallback,
                   layers::ImageContainer* aImageContainer,
                   TaskQueue* aTaskQueue,
                   CDMProxy* aProxy)
    : MediaCodecDataDecoder(MediaData::Type::VIDEO_DATA, aConfig.mMimeType,
                            aFormat, aCallback, aTaskQueue, aProxy)
    , mImageContainer(aImageContainer)
    , mConfig(aConfig)
  {
    //PG(mSamplesWaitingForKey.get());
  }

  const char* GetDescriptionName() const override
  {
    return "android video decoder";
  }

  RefPtr<InitPromise> Init() override
  {
    //auto cdm = static_cast<GMPCDMProxy*>(mProxy.get());
    auto cdmtemp = new GMPCDMProxy();
    mSurfaceView = cdmtemp->GetSurfaceView();
    mSurfaceTexture = AndroidSurfaceTexture::Create();
    if (!mSurfaceTexture) {
      NS_WARNING("Failed to create SurfaceTexture for video decode\n");
      return InitPromise::CreateAndReject(DecoderFailureReason::INIT_ERROR, __func__);
    }

    auto rvholder = mSurfaceView->GetHolder(ReturnTo(&mSurfaceHolder));
    PG(rvholder);
    auto rvSurface = mSurfaceHolder->GetSurface(ReturnTo(&mSurfaceV));
    // if (NS_FAILED(InitDecoder(mSurfaceTexture->JavaSurface()))) {
    // if (NS_FAILED(InitDecoder(nullptr))) {
    if (NS_FAILED(InitDecoder(mSurfaceV))) {
      return InitPromise::CreateAndReject(DecoderFailureReason::INIT_ERROR, __func__);
    }

    return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
  }

  void Cleanup() override
  {
  }

  nsresult Input(MediaRawData* aSample) override
  {
    return MediaCodecDataDecoder::Input(aSample);
  }

  nsresult PostOutput(BufferInfo::Param aInfo, MediaFormat::Param aFormat,
                      const TimeUnit& aDuration) override
  {
    RefPtr<layers::Image> img =
      new SurfaceTextureImage(mSurfaceTexture.get(), mConfig.mDisplay,
                              gl::OriginPos::BottomLeft);

    nsresult rv;
    int32_t flags;
    NS_ENSURE_SUCCESS(rv = aInfo->Flags(&flags), rv);

    bool isSync = !!(flags & MediaCodec::BUFFER_FLAG_SYNC_FRAME);

    int32_t offset;
    NS_ENSURE_SUCCESS(rv = aInfo->Offset(&offset), rv);

    int64_t presentationTimeUs;
    NS_ENSURE_SUCCESS(rv = aInfo->PresentationTimeUs(&presentationTimeUs), rv);

    RefPtr<VideoData> v =
      VideoData::CreateFromImage(mConfig,
                                 mImageContainer,
                                 offset,
                                 presentationTimeUs,
                                 aDuration.ToMicroseconds(),
                                 img,
                                 isSync,
                                 presentationTimeUs,
                                 gfx::IntRect(0, 0,
                                              mConfig.mDisplay.width,
                                              mConfig.mDisplay.height));
    PG(presentationTimeUs);
    INVOKE_CALLBACK(Output, v);
    return NS_OK;
  }

protected:
  layers::ImageContainer* mImageContainer;
  const VideoInfo& mConfig;
  RefPtr<AndroidSurfaceTexture> mSurfaceTexture;
  RefPtr<TaskQueue> mTaskQueue;
};

class AudioDataDecoder : public MediaCodecDataDecoder
{
public:
  AudioDataDecoder(const AudioInfo& aConfig, MediaFormat::Param aFormat,
                   MediaDataDecoderCallback* aCallback,
                   TaskQueue* aTaskQueue,
                   CDMProxy* aProxy)
    : MediaCodecDataDecoder(MediaData::Type::AUDIO_DATA, aConfig.mMimeType,
                            aFormat, aCallback, aTaskQueue, aProxy)
  {
    JNIEnv* const env = jni::GetEnvForThread();

    jni::Object::LocalRef buffer(env);
    NS_ENSURE_SUCCESS_VOID(aFormat->GetByteBuffer(NS_LITERAL_STRING("csd-0"),
                                                  &buffer));

    if (!buffer && aConfig.mCodecSpecificConfig->Length() >= 2) {
      buffer = jni::Object::LocalRef::Adopt(
          env, env->NewDirectByteBuffer(aConfig.mCodecSpecificConfig->Elements(),
          aConfig.mCodecSpecificConfig->Length()));
      NS_ENSURE_SUCCESS_VOID(aFormat->SetByteBuffer(NS_LITERAL_STRING("csd-0"),
                                                    buffer));
    }
    PG(mSamplesWaitingForKey.get());
  }

  const char* GetDescriptionName() const override
  {
    return "android audio decoder";
  }

  nsresult Output(BufferInfo::Param aInfo, void* aBuffer,
                  MediaFormat::Param aFormat, const TimeUnit& aDuration)
  {
    PG0();
    // The output on Android is always 16-bit signed
    nsresult rv;
    int32_t numChannels;
    NS_ENSURE_SUCCESS(rv =
        aFormat->GetInteger(NS_LITERAL_STRING("channel-count"), &numChannels), rv);
    AudioConfig::ChannelLayout layout(numChannels);
    if (!layout.IsValid()) {
      return NS_ERROR_FAILURE;
    }

    int32_t sampleRate;
    NS_ENSURE_SUCCESS(rv =
        aFormat->GetInteger(NS_LITERAL_STRING("sample-rate"), &sampleRate), rv);

    int32_t size;
    NS_ENSURE_SUCCESS(rv = aInfo->Size(&size), rv);

    int32_t offset;
    NS_ENSURE_SUCCESS(rv = aInfo->Offset(&offset), rv);
    PR(offset, size);

#ifdef MOZ_SAMPLE_TYPE_S16
    const int32_t numSamples = size / 2;
#else
#error We only support 16-bit integer PCM
#endif

    const int32_t numFrames = numSamples / numChannels;
    AlignedAudioBuffer audio(numSamples);
    if (!audio) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    const uint8_t* bufferStart = static_cast<uint8_t*>(aBuffer) + offset;
    PodCopy(audio.get(), reinterpret_cast<const AudioDataValue*>(bufferStart),
            numSamples);

    int64_t presentationTimeUs;
    NS_ENSURE_SUCCESS(rv = aInfo->PresentationTimeUs(&presentationTimeUs), rv);

    RefPtr<AudioData> data = new AudioData(0, presentationTimeUs,
                                           aDuration.ToMicroseconds(),
                                           numFrames,
                                           Move(audio),
                                           numChannels,
                                           sampleRate);
    INVOKE_CALLBACK(Output, data);
    return NS_OK;
  }
private:
  RefPtr<TaskQueue> mTaskQueue;
};

AndroidDecoderModule::AndroidDecoderModule(CDMProxy *aProxy/*will be null in non-drm case*/)
  :mProxy(aProxy)
{
  widevineUUID = GenDrmUUID(0xedef8ba979d64acell, 0xa3c827dcd51d21edll);
  clearkeyUUID = GenDrmUUID(0x1077efecc0b24d02ll, 0xace33c1e52e2fb4bll);
  /* Modify this to switch the current testing drm module.*/
  currentTestingUUID = clearkeyUUID;
}

bool
AndroidDecoderModule::SupportsMimeType(const nsACString& aMimeType,
                                       DecoderDoctorDiagnostics* aDiagnostics) const
{
  PG(ToPrintable(aMimeType));
  if (!AndroidBridge::Bridge() ||
      AndroidBridge::Bridge()->GetAPIVersion() < 16) {
    return false;
  }

  if (aMimeType.EqualsLiteral("video/mp4") ||
      aMimeType.EqualsLiteral("video/avc")) {
    return true;
  }

  // When checking "audio/x-wav", CreateDecoder can cause a JNI ERROR by
  // Accessing a stale local reference leading to a SIGSEGV crash.
  // To avoid this we check for wav types here.
  if (aMimeType.EqualsLiteral("audio/x-wav") ||
      aMimeType.EqualsLiteral("audio/wave; codecs=1") ||
      aMimeType.EqualsLiteral("audio/wave; codecs=6") ||
      aMimeType.EqualsLiteral("audio/wave; codecs=7") ||
      aMimeType.EqualsLiteral("audio/wave; codecs=65534")) {
    return false;
  }  

  if ((VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP8) &&
       !GetFeatureStatus(nsIGfxInfo::FEATURE_VP8_HW_DECODE)) ||
      (VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP9) &&
       !GetFeatureStatus(nsIGfxInfo::FEATURE_VP9_HW_DECODE))) {
    return false;
  }
  auto result = widget::HardwareCodecCapabilityUtils::FindDecoderCodecInfoForMimeType(
      nsCString(TranslateMimeType(aMimeType)));
  PG(result);
  return result;
}

already_AddRefed<MediaDataDecoder>
AndroidDecoderModule::CreateVideoDecoder(
    const VideoInfo& aConfig, layers::LayersBackend aLayersBackend,
    layers::ImageContainer* aImageContainer, FlushableTaskQueue* aVideoTaskQueue,
    MediaDataDecoderCallback* aCallback,
    DecoderDoctorDiagnostics* aDiagnostics)
{
  MediaFormat::LocalRef format;

  NS_ENSURE_SUCCESS(MediaFormat::CreateVideoFormat(
      TranslateMimeType(aConfig.mMimeType),
      aConfig.mDisplay.width,
      aConfig.mDisplay.height,
      &format), nullptr);
  MediaCodecList::LocalRef mcl1;
  auto rvmcl1 = MediaCodecList::New(MediaCodecList::ALL_CODECS, &mcl1);
  PG(rvmcl1);
  mozilla::jni::String::LocalRef decoderName1;
  mcl1->FindDecoderForFormat(format, &decoderName1);
  PR(decoderName1->ToCString());

  //auto rvsetfeaturevideo = format->SetFeatureEnabled("secure-playback", true);
  //PR(rvsetfeaturevideo);
  MediaCodecList::LocalRef mcl;
  auto rvmcl = MediaCodecList::New(MediaCodecList::ALL_CODECS, &mcl);
  PG(rvmcl);
  mozilla::jni::String::LocalRef decoderName;
  mcl->FindDecoderForFormat(format, &decoderName);
  PR(decoderName->ToCString());

  RefPtr<MediaDataDecoder> decoder =
    new VideoDataDecoder(aConfig, format, aCallback, aImageContainer, aVideoTaskQueue, mProxy);

  return decoder.forget();
}

already_AddRefed<MediaDataDecoder>
AndroidDecoderModule::CreateAudioDecoder(
    const AudioInfo& aConfig, FlushableTaskQueue* aAudioTaskQueue,
    MediaDataDecoderCallback* aCallback,
    DecoderDoctorDiagnostics* aDiagnostics)
{
  MOZ_ASSERT(aConfig.mBitDepth == 16, "We only handle 16-bit audio!");

  MediaFormat::LocalRef format;

  LOG("CreateAudioFormat with mimeType=%s, mRate=%d, channels=%d",
      aConfig.mMimeType.Data(), aConfig.mRate, aConfig.mChannels);

  NS_ENSURE_SUCCESS(MediaFormat::CreateAudioFormat(
      aConfig.mMimeType,
      aConfig.mRate,
      aConfig.mChannels,
      &format), nullptr);

  MediaCodecList::LocalRef mcl1;
  auto rvmcl1 = MediaCodecList::New(MediaCodecList::ALL_CODECS, &mcl1);
  PG(rvmcl1);
  mozilla::jni::String::LocalRef decoderName1;
  mcl1->FindDecoderForFormat(format, &decoderName1);
  PR(decoderName1->ToCString());

  // auto rvsetfeatureaudio = format->SetFeatureEnabled("secure-playback", true);
  // PR(rvsetfeatureaudio);
  MediaCodecList::LocalRef mcl;
  auto rvmcl = MediaCodecList::New(MediaCodecList::ALL_CODECS, &mcl);
  PG(rvmcl);
  mozilla::jni::String::LocalRef decoderName;
  mcl->FindDecoderForFormat(format, &decoderName);
  PR(decoderName->ToCString());
  RefPtr<MediaDataDecoder> decoder =
    new AudioDataDecoder(aConfig, format, aCallback, aAudioTaskQueue, mProxy);

  return decoder.forget();
}

PlatformDecoderModule::ConversionRequired
AndroidDecoderModule::DecoderNeedsConversion(const TrackInfo& aConfig) const
{
  if (aConfig.IsVideo()) {
    return kNeedAnnexB;
  }
  return kNeedNone;
}

MediaCodecDataDecoder::MediaCodecDataDecoder(MediaData::Type aType,
                                             const nsACString& aMimeType,
                                             MediaFormat::Param aFormat,
                                             MediaDataDecoderCallback* aCallback,
                                             TaskQueue* aTaskQueue,
                                             CDMProxy* aProxy)
  : mType(aType)
  , mMimeType(aMimeType)
  , mFormat(aFormat)
  , mCallback(aCallback)
  , mInputBuffers(nullptr)
  , mOutputBuffers(nullptr)
  , mMonitor("MediaCodecDataDecoder::mMonitor")
  , mState(kDecoding)
  , mProxy(aProxy)
  , mSamplesWaitingForKey(new SamplesWaitingForKey(this, aTaskQueue, aProxy))
{
  //PG(mSamplesWaitingForKey.get());
}

MediaCodecDataDecoder::~MediaCodecDataDecoder()
{
  Shutdown();
}

RefPtr<MediaDataDecoder::InitPromise>
MediaCodecDataDecoder::Init()
{
  nsresult rv = InitDecoder(nullptr);

  TrackInfo::TrackType type =
    (mType == MediaData::AUDIO_DATA ? TrackInfo::TrackType::kAudioTrack
                                    : TrackInfo::TrackType::kVideoTrack);

  return NS_SUCCEEDED(rv) ?
           InitPromise::CreateAndResolve(type, __func__) :
           InitPromise::CreateAndReject(
               MediaDataDecoder::DecoderFailureReason::INIT_ERROR, __func__);
}

nsresult
MediaCodecDataDecoder::InitDecoder(Surface::Param aSurface)
{
  nsresult rv;

  auto uuidStr = ToStringHelper(currentTestingUUID);
  PB(uuidStr);
  MediaDrm::LocalRef mediadrm;
  auto rvmediadrm = MediaDrm::New(currentTestingUUID, &mediadrm);
  PG(rvmediadrm);
  mozilla::jni::ByteArray::LocalRef sessionid;
  auto rvopensession = mediadrm->OpenSession(&sessionid);
  PG(rvopensession);
  PG(sessionid->GetElements());

  auto sidarr = sessionid->GetElements();
  JNIEnv* const jenv = mozilla::jni::GetEnvForThread();
  jbyteArray sessionidArray = FillJByteArray(&sidarr[0], sidarr.Length());

  mozilla::jni::ByteArray::LocalRef keysetid;
  unsigned char response[] = {0x7b,0x22,0x6b,0x65,0x79,0x73,0x22,0x3a,0x5b,0x7b,0x22,0x6b,0x74,0x79,0x22,0x3a,0x22,0x6f,0x63,0x74,0x22,0x2c,0x22,0x6b,0x69,0x64,0x22,0x3a,0x22,0x59,0x41,0x59,0x65,0x41,0x58,0x35,0x48,0x66,0x6f,0x64,0x2b,0x56,0x39,0x41,0x4e,0x48,0x74,0x41,0x4e,0x48,0x67,0x22,0x2c,0x22,0x6b,0x22,0x3a,0x22,0x47,0x6f,0x6f,0x67,0x6c,0x65,0x54,0x65,0x73,0x74,0x4b,0x65,0x79,0x42,0x61,0x73,0x65,0x36,0x34,0x67,0x67,0x67,0x22,0x7d,0x5d,0x7d};
  unsigned char *responsedata = response;
  auto responsearray = FillJByteArray(responsedata, sizeof(response));

  auto rvprovide = mediadrm->ProvideKeyResponse(sessionid, mozilla::jni::ByteArray::Ref::From(responsearray), &keysetid);
  PG(rvprovide);

  MediaCrypto::LocalRef mediacrypto;
  auto rvmediacrypto = MediaCrypto::New(currentTestingUUID, mozilla::jni::ByteArray::Ref::From(sessionidArray), &mediacrypto);
  PG(rvmediacrypto);
  bool isCryptoSchemeSupported = false;
  auto rvIscryptosupported =
    mediacrypto->IsCryptoSchemeSupported(currentTestingUUID, &isCryptoSchemeSupported);
  PG(isCryptoSchemeSupported);
  mMediacrypto = mediacrypto;

  bool isRequiresSecureDecoderComponent = false;
  auto rvRequiresSecureDecoderComponent = mMediacrypto->RequiresSecureDecoderComponent(mMimeType, &isRequiresSecureDecoderComponent);
  mDecoder = CreateDecoder(mMimeType, (mProxy != nullptr)/*means EME, so assume it is encrypted data case*/
                           , isRequiresSecureDecoderComponent);
  if (!mDecoder) {
    INVOKE_CALLBACK(Error);
    return NS_ERROR_FAILURE;
  }

  auto rvconfigure = mDecoder->Configure(mFormat, aSurface, mediacrypto, 0);

  NS_ENSURE_SUCCESS(rv = mDecoder->Start(), rv);

  NS_ENSURE_SUCCESS(rv = ResetInputBuffers(), rv);
  NS_ENSURE_SUCCESS(rv = ResetOutputBuffers(), rv);

  nsCOMPtr<nsIRunnable> r = NewRunnableMethod(this, &MediaCodecDataDecoder::DecoderLoop);
  rv = NS_NewNamedThread("MC Decoder", getter_AddRefs(mThread), r);

  return rv;
}
// Video go here
nsresult
MediaCodecDataDecoder::Output(widget::sdk::BufferInfo::Param aInfo, void* aBuffer,
      widget::sdk::MediaFormat::Param aFormat, const media::TimeUnit& aDuration)
{
  //PR(ToCStringHelper(aFormat));
  return NS_OK;
}

  // Audio go here
nsresult
MediaCodecDataDecoder::PostOutput(widget::sdk::BufferInfo::Param aInfo,
      widget::sdk::MediaFormat::Param aFormat, const media::TimeUnit& aDuration)
{
  //PG0();
  return NS_OK;
}
// This is in usec, so that's 10ms.
static const int64_t kDecoderTimeout = 10000;

#define BREAK_ON_DECODER_ERROR() \
  if (NS_FAILED(res)) { \
    NS_WARNING("Exiting decoder loop due to exception"); \
    if (State() == kDrainDecoder) { \
      INVOKE_CALLBACK(DrainComplete); \
      State(kDecoding); \
    } \
    INVOKE_CALLBACK(Error); \
    break; \
  }

nsresult
MediaCodecDataDecoder::GetInputBuffer(
    JNIEnv* aEnv, int aIndex, jni::Object::LocalRef* aBuffer)
{
  MOZ_ASSERT(aEnv);
  MOZ_ASSERT(!*aBuffer);

  int numTries = 2;

  while (numTries--) {
    *aBuffer = jni::Object::LocalRef::Adopt(
        aEnv->GetObjectArrayElement(mInputBuffers.Get(), aIndex));
    if (*aBuffer) {
      return NS_OK;
    }
    nsresult res = ResetInputBuffers();
    if (NS_FAILED(res)) {
      return res;
    }
  }
  return NS_ERROR_FAILURE;
}

bool
MediaCodecDataDecoder::WaitForInput()
{
  MonitorAutoLock lock(mMonitor);

  while (State() == kDecoding && mQueue.empty()) {
    // Signal that we require more input.
    INVOKE_CALLBACK(InputExhausted);
    lock.Wait();
  }

  return State() != kStopping;
}


already_AddRefed<MediaRawData>
MediaCodecDataDecoder::PeekNextSample()
{
  MonitorAutoLock lock(mMonitor);

  if (State() == kFlushing) {
    mDecoder->Flush();
    ClearQueue();
    State(kDecoding);
    lock.Notify();
    return nullptr;
  }

  if (mQueue.empty()) {
    if (State() == kDrainQueue) {
      State(kDrainDecoder);
    }
    return nullptr;
  }

  // We're not stopping or flushing, so try to get a sample.
  return RefPtr<MediaRawData>(mQueue.front()).forget();
}

nsresult
MediaCodecDataDecoder::QueueSample(const MediaRawData* aSample)
{
  MOZ_ASSERT(aSample);
  AutoLocalJNIFrame frame(jni::GetEnvForThread(), 1);

  // We have a sample, try to feed it to the decoder.
  int32_t inputIndex = -1;
  nsresult res = mDecoder->DequeueInputBuffer(kDecoderTimeout, &inputIndex);
  if (NS_FAILED(res)) {
    return res;
  }

  if (inputIndex < 0) {
    // There is no valid input buffer available.
    return NS_ERROR_FAILURE;
  }

  jni::Object::LocalRef buffer(frame.GetEnv());
  res = GetInputBuffer(frame.GetEnv(), inputIndex, &buffer);
  if (NS_FAILED(res)) {
    return res;
  }

  void* directBuffer = frame.GetEnv()->GetDirectBufferAddress(buffer.Get());

  MOZ_ASSERT(frame.GetEnv()->GetDirectBufferCapacity(buffer.Get()) >=
             aSample->Size(),
             "Decoder buffer is not large enough for sample");

  PodCopy(static_cast<uint8_t*>(directBuffer), aSample->Data(), aSample->Size());

  PG(bin2hex(aSample->Data(), aSample->Size()));
  // Test DRM
  if (aSample->mCrypto.mValid) {
    if (mSamplesWaitingForKey->WaitIfKeyNotUsable(const_cast<MediaRawData*>(aSample))) {
      return NS_OK;
    }

    nsAutoPtr<MediaRawDataWriter> writer(const_cast<MediaRawData*>(aSample)->CreateWriter());
    mProxy->GetSessionIdsForKeyId(aSample->mCrypto.mKeyId,
                                  writer->mCrypto.mSessionIds);
    CryptoInfo::LocalRef cryptoInfo;
    auto rvcryptoInfo = CryptoInfo::New(&cryptoInfo);
    PG(rvcryptoInfo);
    auto& cryptoObj = aSample->mCrypto;
    //https://dxr.mozilla.org/mozilla-central/rev/3461f3cae78495f100a0f7d3d2e0b89292d3ec02/dom/media/gmp/GMPEncryptedBufferDataImpl.cpp#89
    int newNumSubSamples = std::min<uint32_t>(cryptoObj.mPlainSizes.Length(), cryptoObj.mEncryptedSizes.Length());

    // mPlainSizes is uint16_t, need to transform to uint32_t first
    nsTArray<uint32_t> clearData;
    for (auto& clear : cryptoObj.mPlainSizes) {
      clearData.AppendElement(clear);
    }

    auto snapshotIV(cryptoObj.mIV);
    PR(snapshotIV);
    auto length = snapshotIV.Length();
    if (snapshotIV.Length() < 16) {
      for (size_t i = 0; i < (16 - length); i++) {

        snapshotIV.AppendElement(0); //pad with 0
        //PG(snapshotIV);
      }
    }
    PR(newNumSubSamples);
    PR(clearData);
    PR(cryptoObj.mEncryptedSizes);
    PR(cryptoObj.mIV);
    PR(snapshotIV, snapshotIV.Length());
    PR(cryptoObj.mKeyId);
    auto newNumBytesOfClearData = FillJIntArray(&clearData[0], clearData.Length());
    auto newNumBytesOfEncryptedData = FillJIntArray(&cryptoObj.mEncryptedSizes[0], cryptoObj.mEncryptedSizes.Length());
    auto newIV = FillJByteArray(&snapshotIV[0], snapshotIV.Length());
    auto newKey = FillJByteArray(&cryptoObj.mKeyId[0], cryptoObj.mKeyId.Length());
    /*
    // Take EMEMediaDataDecoderProxy::Input(MediaRawData* aSample) for reference.
    //auto Set(int32_t, mozilla::jni::IntArray::Param, mozilla::jni::IntArray::Param, mozilla::jni::ByteArray::Param, mozilla::jni::ByteArray::Param, int32_t) const -> nsresult;
    //set (int newNumSubSamples, int[] newNumBytesOfClearData, int[] newNumBytesOfEncryptedData, byte[] newKey, byte[] newIV, int newMode)
    // cryptoInfo->Set();
    // res = mDecoder->QueueSecureInputBuffer(inputIndex, 0,cryptoInfo,
    //                                  aSample->mTime, 0);
    nsTArray<uint8_t> mKeyId;
    nsTArray<uint16_t> mPlainSizes;
    nsTArray<uint32_t> mEncryptedSizes;
    nsTArray<uint8_t> mIV;
    nsTArray<nsCString> mSessionIds;
    */

    // cryptoObj.mKeyId
    // cryptoObj.mIV
    // cryptoObj.mPlainSizes
    // cryptoObj.mEncryptedSizes
    auto rvcryptoset = cryptoInfo->Set(newNumSubSamples,
                    mozilla::jni::IntArray::Ref::From(newNumBytesOfClearData),
                    mozilla::jni::IntArray::Ref::From(newNumBytesOfEncryptedData),
                    mozilla::jni::ByteArray::Ref::From(newKey),
                    mozilla::jni::ByteArray::Ref::From(newIV),
                    1/*CRYPTO_MODE_AES_CTR*/);
    PG(rvcryptoset);
    // res = mDecoder->QueueSecureInputBuffer(inputIndex, 0,cryptoInfo,
    //                                        aSample->mTime, 0);
    // Call Java version to know the exception msg.
    widget::GeckoAppShell::CallQueueSecureInputBuffer(mDecoder, cryptoInfo, inputIndex, 0, aSample->mTime, 0);
    // PG(aSample->mTime);
    // PG(res);
  } else {
    res = mDecoder->QueueInputBuffer(inputIndex, 0, aSample->Size(),
                                     aSample->mTime, 0);
  }


  if (NS_FAILED(res)) {
    return res;
  }

  mDurations.push_back(TimeUnit::FromMicroseconds(aSample->mDuration));
  return NS_OK;
}

nsresult
MediaCodecDataDecoder::QueueEOS()
{
  mMonitor.AssertCurrentThreadOwns();

  nsresult res = NS_OK;
  int32_t inputIndex = -1;
  res = mDecoder->DequeueInputBuffer(kDecoderTimeout, &inputIndex);
  if (NS_FAILED(res) || inputIndex < 0) {
    return res;
  }

  res = mDecoder->QueueInputBuffer(inputIndex, 0, 0, 0,
                                   MediaCodec::BUFFER_FLAG_END_OF_STREAM);
  if (NS_SUCCEEDED(res)) {
    State(kDrainWaitEOS);
    mMonitor.Notify();
  }
  return res;
}

void
MediaCodecDataDecoder::HandleEOS(int32_t aOutputStatus)
{
  MonitorAutoLock lock(mMonitor);

  if (State() == kDrainWaitEOS) {
    State(kDecoding);
    mMonitor.Notify();

    INVOKE_CALLBACK(DrainComplete);
  }

  mDecoder->ReleaseOutputBuffer(aOutputStatus, false);
}

TimeUnit
MediaCodecDataDecoder::GetOutputDuration()
{
  MOZ_ASSERT(!mDurations.empty(), "Should have had a duration queued");
  const TimeUnit duration = mDurations.front();
  mDurations.pop_front();
  return duration;
}

nsresult
MediaCodecDataDecoder::ProcessOutput(
    BufferInfo::Param aInfo, MediaFormat::Param aFormat, int32_t aStatus)
{
  AutoLocalJNIFrame frame(jni::GetEnvForThread(), 1);

  const TimeUnit duration = GetOutputDuration();
  const auto buffer = jni::Object::LocalRef::Adopt(
      frame.GetEnv()->GetObjectArrayElement(mOutputBuffers.Get(), aStatus));
  PG(aStatus);
  if (buffer) {
    PG(ToCStringHelper(aFormat));
    // The buffer will be null on Android L if we are decoding to a Surface.
    void* directBuffer = frame.GetEnv()->GetDirectBufferAddress(buffer.Get());
    Output(aInfo, directBuffer, aFormat, duration);
  } else
  {
    PG(buffer, this, mDecoder.Get());
  }

  // The Surface will be updated at this point (for video).
  mDecoder->ReleaseOutputBuffer(aStatus, true);
  PostOutput(aInfo, aFormat, duration);

  return NS_OK;
}

void
MediaCodecDataDecoder::DecoderLoop()
{
  bool isOutputDone = false;
  AutoLocalJNIFrame frame(jni::GetEnvForThread(), 1);
  MediaFormat::LocalRef outputFormat(frame.GetEnv());
  nsresult res = NS_OK;

  while (WaitForInput()) {
    RefPtr<MediaRawData> sample = PeekNextSample();

    {
      MonitorAutoLock lock(mMonitor);
      if (State() == kDrainDecoder) {
        MOZ_ASSERT(!sample, "Shouldn't have a sample when pushing EOF frame");
        res = QueueEOS();
        BREAK_ON_DECODER_ERROR();
      }
    }

    if (sample) {
      PG0();
      res = QueueSample(sample);
      if (NS_SUCCEEDED(res)) {
        // We've fed this into the decoder, so remove it from the queue.
        MonitorAutoLock lock(mMonitor);
        MOZ_RELEASE_ASSERT(mQueue.size(), "Queue may not be empty");
        mQueue.pop_front();
        isOutputDone = false;
      }
    }

    if (isOutputDone) {
      continue;
    }
    PG0();
    BufferInfo::LocalRef bufferInfo;
    nsresult res = BufferInfo::New(&bufferInfo);
    BREAK_ON_DECODER_ERROR();

    int32_t outputStatus = -1;
    res = mDecoder->DequeueOutputBuffer(bufferInfo, kDecoderTimeout * 100,
                                        &outputStatus);
    PR(outputStatus);
    BREAK_ON_DECODER_ERROR();

    if (outputStatus == MediaCodec::INFO_TRY_AGAIN_LATER) {
      // We might want to call mCallback->InputExhausted() here, but there seems
      // to be some possible bad interactions here with the threading.
    } else if (outputStatus == MediaCodec::INFO_OUTPUT_BUFFERS_CHANGED) {
      res = ResetOutputBuffers();
      BREAK_ON_DECODER_ERROR();
    } else if (outputStatus == MediaCodec::INFO_OUTPUT_FORMAT_CHANGED) {
      res = mDecoder->GetOutputFormat(ReturnTo(&outputFormat));
      BREAK_ON_DECODER_ERROR();
    } else if (outputStatus < 0) {
      NS_WARNING("Unknown error from decoder!");
      INVOKE_CALLBACK(Error);
      // Don't break here just in case it's recoverable. If it's not, other
      // stuff will fail later and we'll bail out.
    } else {
      // We have a valid buffer index >= 0 here.
      int32_t flags;
      nsresult res = bufferInfo->Flags(&flags);
      BREAK_ON_DECODER_ERROR();

      if (flags & MediaCodec::BUFFER_FLAG_END_OF_STREAM) {
        HandleEOS(outputStatus);
        isOutputDone = true;
        // We only queue empty EOF frames, so we're done for now.
        continue;
      }

      res = ProcessOutput(bufferInfo, outputFormat, outputStatus);
      BREAK_ON_DECODER_ERROR();
    }
  }

  Cleanup();

  // We're done.
  MonitorAutoLock lock(mMonitor);
  State(kShutdown);
  mMonitor.Notify();
}

const char*
MediaCodecDataDecoder::ModuleStateStr(ModuleState aState) {
  static const char* kStr[] = {
    "Decoding", "Flushing", "DrainQueue", "DrainDecoder", "DrainWaitEOS",
    "Stopping", "Shutdown"
  };

  MOZ_ASSERT(aState < sizeof(kStr) / sizeof(kStr[0]));
  return kStr[aState];
}

MediaCodecDataDecoder::ModuleState
MediaCodecDataDecoder::State() const
{
  return mState;
}

bool
MediaCodecDataDecoder::State(ModuleState aState)
{
  //PR(aState);
  bool ok = true;

  if (mState == kShutdown) {
    ok = false;
  } else if (mState == kStopping) {
    ok = aState == kShutdown;
  } else if (aState == kDrainDecoder) {
    ok = mState == kDrainQueue;
  } else if (aState == kDrainWaitEOS) {
    ok = mState == kDrainDecoder;
  }

  if (ok) {
    LOG("%s -> %s", ModuleStateStr(mState), ModuleStateStr(aState));
    mState = aState;
  }

  return ok;
}

void
MediaCodecDataDecoder::ClearQueue()
{
  mMonitor.AssertCurrentThreadOwns();

  mQueue.clear();
  mDurations.clear();
}

nsresult
MediaCodecDataDecoder::Input(MediaRawData* aSample)
{
  MonitorAutoLock lock(mMonitor);
  mQueue.push_back(aSample);
  lock.NotifyAll();

  return NS_OK;
}

nsresult
MediaCodecDataDecoder::ResetInputBuffers()
{
  return mDecoder->GetInputBuffers(ReturnTo(&mInputBuffers));
}

nsresult
MediaCodecDataDecoder::ResetOutputBuffers()
{
  return mDecoder->GetOutputBuffers(ReturnTo(&mOutputBuffers));
}

nsresult
MediaCodecDataDecoder::Flush()
{
  MonitorAutoLock lock(mMonitor);
  if (!State(kFlushing)) {
    return NS_OK;
  }
  lock.Notify();

  while (State() == kFlushing) {
    lock.Wait();
  }

  return NS_OK;
}

nsresult
MediaCodecDataDecoder::Drain()
{
  MonitorAutoLock lock(mMonitor);
  if (State() == kDrainDecoder || State() == kDrainQueue) {
    return NS_OK;
  }

  State(kDrainQueue);
  lock.Notify();

  return NS_OK;
}


nsresult
MediaCodecDataDecoder::Shutdown()
{
  MonitorAutoLock lock(mMonitor);

  State(kStopping);
  lock.Notify();

  while (mThread && State() != kShutdown) {
    lock.Wait();
  }

  if (mThread) {
    mThread->Shutdown();
    mThread = nullptr;
  }

  if (mDecoder) {
    mDecoder->Stop();
    mDecoder->Release();
    mDecoder = nullptr;
  }

  if (mSamplesWaitingForKey) {
   mSamplesWaitingForKey->BreakCycles();
   mSamplesWaitingForKey = nullptr;
  }

  if (mProxy) {
    mProxy = nullptr;
  }

  return NS_OK;
}

} // mozilla
