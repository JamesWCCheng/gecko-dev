/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <limits>
#include <stdint.h>

#include "GeneratedJNINatives.h"
#include "GeneratedJNIWrappers.h"
#include "HLSDemuxer.h"
#include "HLSResource.h"
#include "HLSUtils.h"
#include "MediaCodec.h"
#include "nsPrintfCString.h"
#include "mozilla/Unused.h"

using namespace mozilla::java;

namespace mozilla {

static Atomic<uint32_t> sStreamSourceID(0u);

typedef TrackInfo::TrackType TrackType;
using media::TimeUnit;
using media::TimeIntervals;
using media::TimeInterval;

static
VideoInfo::Rotation getVideoInfoRotation(int aRotation) {
  switch (aRotation) {
    case 0:
      return VideoInfo::Rotation::kDegree_0;
    case 90:
      return VideoInfo::Rotation::kDegree_90;
    case 180:
      return VideoInfo::Rotation::kDegree_180;
    case 270:
      return VideoInfo::Rotation::kDegree_270;
    default:
      return VideoInfo::Rotation::kDegree_0;
  }
}

static
mozilla::StereoMode getStereoMode(int aMode) {
  switch (aMode) {
    case 0:
      return mozilla::StereoMode::MONO;
    case 1:
      return mozilla::StereoMode::TOP_BOTTOM;
    case 2:
      return mozilla::StereoMode::LEFT_RIGHT;
    default:
      return mozilla::StereoMode::MONO;
  }
}

 class HlsDemuxerCallbacksSupport
   : public GeckoHlsDemuxerWrapper::HlsDemuxerCallbacks::Natives<HlsDemuxerCallbacksSupport>
 {
 public:
   typedef GeckoHlsDemuxerWrapper::HlsDemuxerCallbacks::Natives<HlsDemuxerCallbacksSupport> NativeCallbacks;
   using NativeCallbacks::DisposeNative;
   using NativeCallbacks::AttachNative;

   HlsDemuxerCallbacksSupport(HLSDemuxer* aDemuxer)
     : mDemuxer(aDemuxer)
   {
     MOZ_ASSERT(mDemuxer);
   }

   void OnInitialized(bool aHasAudio, bool aHasVideo)
   {
       MOZ_ASSERT(mDemuxer);
       mDemuxer->GetTaskQueue()->Dispatch(NS_NewRunnableFunction(
         [=] () {
           mDemuxer->OnInitialized(aHasAudio, aHasVideo);
         }));
   }

   // TODO: Handle the unexpected error signal from the java implementation.
   void OnError(int aErrorCode) { }

 private:
   HLSDemuxer* mDemuxer;
 };

HLSDemuxer::HLSDemuxer(MediaResource* aResource)
  : mResource(aResource)
  , mTaskQueue(new AutoTaskQueue(GetMediaThreadPool(MediaThreadType::PLAYBACK),
                                 /* aSupportsTailDispatch = */ false))
  , mMutex("HLSDemuxer")
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aResource);
   HlsDemuxerCallbacksSupport::Init();
   mJavaCallbacks = GeckoHlsDemuxerWrapper::HlsDemuxerCallbacks::New();
   MOZ_ASSERT(mJavaCallbacks);

   HlsDemuxerCallbacksSupport::AttachNative(mJavaCallbacks,
                                            mozilla::MakeUnique<HlsDemuxerCallbacksSupport>(this));

   auto resourceWrapper = static_cast<HLSResource*>(aResource)->GetResourceWrapper();
   mHlsDemuxerWrapper = GeckoHlsDemuxerWrapper::Create(resourceWrapper->GetPlayer(), mJavaCallbacks);
   MOZ_ASSERT(mHlsDemuxerWrapper);
}

void
HLSDemuxer::OnInitialized(bool aHasAudio, bool aHasVideo)
{
  MOZ_ASSERT(OnTaskQueue());

  if (aHasAudio) {
    UpdateAudioInfo(0);
  }
  if (aHasVideo) {
    UpdateVideoInfo(0);
  }
  if (mInitPromise.IsEmpty()) {
    return;
  }
  mInitPromise.ResolveIfExists(NS_OK, __func__);
}

RefPtr<HLSDemuxer::InitPromise>
HLSDemuxer::Init()
{
  RefPtr<HLSDemuxer> self = this;
  return InvokeAsync(GetTaskQueue(), __func__,
    [self](){
      RefPtr<InitPromise> p = self->mInitPromise.Ensure(__func__);
      return p;
    });
}

void HLSDemuxer::NotifyDataArrived()
{
  HLS_DEBUG("HLSDemuxer", "NotifyDataArrived");
}

bool
HLSDemuxer::HasTrackType(TrackType aType) const
{
  MutexAutoLock lock(mMutex);
  HLS_DEBUG("HLSDemuxer", "HasTrackType(%d)", aType);
  switch (aType) {
    case TrackType::kAudioTrack:
      return mInfo.HasAudio();
    case TrackType::kVideoTrack:
      return mInfo.HasVideo();
    default:
      return false;
  }
}

uint32_t
HLSDemuxer::GetNumberTracks(TrackType aType) const
{
  switch (aType) {
    case TrackType::kAudioTrack:
      return mHlsDemuxerWrapper->GetNumberOfTracks(TrackType::kAudioTrack);
    case TrackType::kVideoTrack:
      return mHlsDemuxerWrapper->GetNumberOfTracks(TrackType::kVideoTrack);
    default:
      return 0;
  }
}

already_AddRefed<MediaTrackDemuxer>
HLSDemuxer::GetTrackDemuxer(TrackType aType, uint32_t aTrackNumber)
{
  RefPtr<HLSTrackDemuxer> e = new HLSTrackDemuxer(this, aType);
  mDemuxers.AppendElement(e);
  return e.forget();
}

bool
HLSDemuxer::IsSeekable() const
{
  return !mHlsDemuxerWrapper->IsLiveStream();
}

UniquePtr<EncryptionInfo>
HLSDemuxer::GetCrypto()
{
  // TODO: Currently, our HLS implementation hasn't supported encrypted content.
  // Return null at this stage.
  return nullptr;
}

TrackInfo*
HLSDemuxer::GetTrackInfo(TrackType aTrack)
{
  MutexAutoLock lock(mMutex);
  switch (aTrack) {
    case TrackType::kAudioTrack: {
      return &mInfo.mAudio;
    }
    case TrackType::kVideoTrack: {
      return &mInfo.mVideo;
    }
    default:
      return nullptr;
  }
}

int64_t
HLSDemuxer::GetNextKeyFrameTime()
{
  MOZ_ASSERT(mHlsDemuxerWrapper);
  return mHlsDemuxerWrapper->GetNextKeyFrameTime();
}

void
HLSDemuxer::UpdateAudioInfo(int index)
{
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(mHlsDemuxerWrapper);
  HLS_DEBUG("HLSDemuxer", "UpdateAudioInfo (%d)", index);
  jni::Object::LocalRef infoObj = mHlsDemuxerWrapper->GetAudioInfo(index);
  if (infoObj) {
    java::GeckoAudioInfo::LocalRef audioInfo(mozilla::Move(infoObj));
    mInfo.mAudio.mRate = audioInfo->Rate();
    mInfo.mAudio.mChannels = audioInfo->Channels();
    mInfo.mAudio.mProfile = audioInfo->Profile();
    mInfo.mAudio.mBitDepth = audioInfo->BitDepth();
    mInfo.mAudio.mMimeType = NS_ConvertUTF16toUTF8(audioInfo->MimeType()->ToString());
    mInfo.mAudio.mDuration = TimeUnit::FromMicroseconds(audioInfo->Duration());
    auto&& csd = audioInfo->CodecSpecificData()->GetElements();
    mInfo.mAudio.mCodecSpecificConfig->Clear();
    mInfo.mAudio.mCodecSpecificConfig->AppendElements(reinterpret_cast<uint8_t*>(&csd[0]),
                                                          csd.Length());
  }
}

void
HLSDemuxer::UpdateVideoInfo(int index)
{
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(mHlsDemuxerWrapper);
  jni::Object::LocalRef infoObj = mHlsDemuxerWrapper->GetVideoInfo(index);
  if (infoObj) {
    java::GeckoVideoInfo::LocalRef videoInfo(mozilla::Move(infoObj));
    mInfo.mVideo.mStereoMode = getStereoMode(videoInfo->StereoMode());
    mInfo.mVideo.mRotation = getVideoInfoRotation(videoInfo->Rotation());
    mInfo.mVideo.mImage.width = videoInfo->DisplayWidth();
    mInfo.mVideo.mImage.height = videoInfo->DisplayHeight();
    mInfo.mVideo.mDisplay.width = videoInfo->PictureWidth();
    mInfo.mVideo.mDisplay.height = videoInfo->PictureHeight();
    mInfo.mVideo.mMimeType = NS_ConvertUTF16toUTF8(videoInfo->MimeType()->ToString());
    mInfo.mVideo.mDuration = TimeUnit::FromMicroseconds(videoInfo->Duration());
    HLS_DEBUG("HLSDemuxer", "UpdateVideoInfo (%d) / I(%dx%d) / D(%dx%d)",
     index, mInfo.mVideo.mImage.width, mInfo.mVideo.mImage.height,
     mInfo.mVideo.mDisplay.width, mInfo.mVideo.mDisplay.height);
  }
}

bool
HLSDemuxer::OnTaskQueue() const
{
  return mTaskQueue->IsCurrentThreadIn();
}

HLSDemuxer::~HLSDemuxer()
{
  HLS_DEBUG("HLSDemuxer", "~HLSDemuxer()");
  if (mJavaCallbacks) {
   HlsDemuxerCallbacksSupport::DisposeNative(mJavaCallbacks);
   mJavaCallbacks = nullptr;
  }
  if (mHlsDemuxerWrapper) {
   mHlsDemuxerWrapper->Destroy();
   mHlsDemuxerWrapper = nullptr;
  }
  mInitPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
}

HLSTrackDemuxer::HLSTrackDemuxer(HLSDemuxer* aParent, TrackInfo::TrackType aType)
  : mParent(aParent)
  , mType(aType)
{
}

UniquePtr<TrackInfo>
HLSTrackDemuxer::GetInfo() const
{
  return mParent->GetTrackInfo(mType)->Clone();
}

RefPtr<HLSTrackDemuxer::SeekPromise>
HLSTrackDemuxer::Seek(const media::TimeUnit& aTime)
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  return InvokeAsync<media::TimeUnit&&>(mParent->GetTaskQueue(), this,
                                        __func__, &HLSTrackDemuxer::DoSeek,
                                        aTime);
}

RefPtr<HLSTrackDemuxer::SeekPromise>
HLSTrackDemuxer::DoSeek(const media::TimeUnit& aTime)
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  mQueuedSample = nullptr;
  int64_t seekTimeUs = aTime.ToMicroseconds();
  int64_t seekTimeMs = seekTimeUs / 1000;
  bool result = mParent->mHlsDemuxerWrapper->Seek(seekTimeMs);
  if (!result) {
    return SeekPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA,
                                        __func__);
  }
  TimeUnit seekTime = TimeUnit::FromMicroseconds(seekTimeUs);
  return SeekPromise::CreateAndResolve(seekTime, __func__);
}

RefPtr<HLSTrackDemuxer::SamplesPromise>
HLSTrackDemuxer::GetSamples(int32_t aNumSamples)
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  return InvokeAsync(mParent->GetTaskQueue(), this, __func__,
                     &HLSTrackDemuxer::DoGetSamples, aNumSamples);
}

RefPtr<HLSTrackDemuxer::SamplesPromise>
HLSTrackDemuxer::DoGetSamples(int32_t aNumSamples)
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  if (!aNumSamples) {
    return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                                           __func__);
  }
  RefPtr<SamplesHolder> samples = new SamplesHolder;
  if (mQueuedSample) {
    MOZ_ASSERT(mQueuedSample->mKeyframe,
               "mQueuedSample must be a keyframe");
    samples->mSamples.AppendElement(mQueuedSample);
    mQueuedSample = nullptr;
    aNumSamples--;
  }
  if (aNumSamples == 0) {
    // Return the queued sample.
    return SamplesPromise::CreateAndResolve(samples, __func__);
  }
  mozilla::jni::ObjectArray::LocalRef demuxedSamples =
    (mType == TrackInfo::kAudioTrack) ?
      mParent->mHlsDemuxerWrapper->GetSamples(TrackInfo::kAudioTrack, aNumSamples):
      mParent->mHlsDemuxerWrapper->GetSamples(TrackInfo::kVideoTrack, aNumSamples);
  nsTArray<jni::Object::LocalRef> sampleObjectArray(demuxedSamples->GetElements());

  if (sampleObjectArray.IsEmpty()) {
    return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
  }

  for (auto&& demuxedSample : sampleObjectArray) {
    java::GeckoHlsSample::LocalRef sample(mozilla::Move(demuxedSample));
    if (sample->IsEOS()) {
      HLS_DEBUG("HLSTrackDemuxer", "Met BUFFER_FLAG_END_OF_STREAM.");
      return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_END_OF_STREAM, __func__);
    }
    RefPtr<MediaRawData> mrd = ConvertToMediaRawData(sample);
    if (!mrd) {
      return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_DEMUXER_ERR, __func__);
    }
    samples->mSamples.AppendElement(mrd);
  }
  if (mType == TrackInfo::kVideoTrack &&
      (mNextKeyframeTime.isNothing()
       || samples->mSamples.LastElement()->mTime
       >= mNextKeyframeTime.value())) {
    // Only need to find NextKeyFrame for Video
    UpdateNextKeyFrameTime();
  }

  return SamplesPromise::CreateAndResolve(samples, __func__);
}

RefPtr<MediaRawData>
HLSTrackDemuxer::ConvertToMediaRawData(java::GeckoHlsSample::LocalRef aSample) {
  java::sdk::BufferInfo::LocalRef info = aSample->Info();
  // Currently extract PTS, Size and Data without Crypto information.
  // Transform java Sample into MediaRawData
  RefPtr<MediaRawData> mrd = new MediaRawData();
  int64_t presentationTimeUs = 0;
  bool ok = NS_SUCCEEDED(info->PresentationTimeUs(&presentationTimeUs));
  mrd->mTime = TimeUnit::FromMicroseconds(presentationTimeUs);
  mrd->mTimecode = TimeUnit::FromMicroseconds(presentationTimeUs);
  mrd->mKeyframe = aSample->IsKeyFrame();
  mrd->mDuration = (mType == TrackInfo::kVideoTrack) ?
    TimeUnit::FromMicroseconds(aSample->Duration()) : TimeUnit::Zero();

  int32_t size = 0;
  ok &= NS_SUCCEEDED(info->Size(&size));
  if (!ok) {
    HLS_DEBUG("HLSTrackDemuxer", "Error occurred during extraction from Sample java object.");
    return nullptr;
  }

  // Update streamSouceID & videoInfo for MFR.
  if (mType == TrackInfo::kVideoTrack) {
    auto sampleFormatIndex = aSample->FormatIndex();
    if (mLastFormatIndex != sampleFormatIndex) {
      mLastFormatIndex = sampleFormatIndex;
      mParent->UpdateVideoInfo(mLastFormatIndex);
      mrd->mTrackInfo = new TrackInfoSharedPtr(mParent->mInfo.mVideo, ++sStreamSourceID);
    }
  }

  // Write payload into MediaRawData
  UniquePtr<MediaRawDataWriter> writer(mrd->CreateWriter());
  if (!writer->SetSize(size)) {
    HLS_DEBUG("HLSTrackDemuxer", "Exit failed to allocate media buffer");
    return nullptr;
  }
  jni::ByteBuffer::LocalRef dest =
    jni::ByteBuffer::New(writer->Data(), writer->Size());
  aSample->WriteToByteBuffer(dest);

  // Write Crypto information
  java::sdk::CryptoInfo::LocalRef cryptoInfo = aSample->CryptoInfo();
  if (cryptoInfo) {
    HLS_DEBUG("HLSTrackDemuxer", "Has Crypto Info");
    writer->mCrypto.mValid = true;
    int32_t mode = 0;
    ok &= NS_SUCCEEDED(cryptoInfo->Mode(&mode));
    writer->mCrypto.mMode = mode;
    mozilla::jni::ByteArray::LocalRef ivData;
    ok &= NS_SUCCEEDED(cryptoInfo->Iv(&ivData));
    // Data in mIV is uint8_t and jbyte is signed char
    auto&& ivArr= ivData->GetElements();
    writer->mCrypto.mIV.AppendElements(reinterpret_cast<uint8_t*>(&ivArr[0]),
                                       ivArr.Length());
    writer->mCrypto.mIVSize = ivArr.Length();
    mozilla::jni::ByteArray::LocalRef keyData;
    ok &= NS_SUCCEEDED(cryptoInfo->Key(&keyData));
    auto&& keyArr = keyData->GetElements();
    // Data in mKeyId is uint8_t and jbyte is signed char
    writer->mCrypto.mKeyId.AppendElements(reinterpret_cast<uint8_t*>(&keyArr[0]),
                                          keyArr.Length());

    mozilla::jni::IntArray::LocalRef clearData;
    ok &= NS_SUCCEEDED(cryptoInfo->NumBytesOfClearData(&clearData));
    // Data in mPlainSizes is uint16_t, NumBytesOfClearData is int32_t
    // , so need a for loop to copy
    for (const auto& b : clearData->GetElements()) {
      writer->mCrypto.mPlainSizes.AppendElement(b);
    }

    mozilla::jni::IntArray::LocalRef encryptedData;
    ok &= NS_SUCCEEDED(cryptoInfo->NumBytesOfEncryptedData(&encryptedData));
    auto&& encryptedArr = encryptedData->GetElements();
    // Data in mEncryptedSizes is uint32_t, NumBytesOfEncryptedData is int32_t
    writer->mCrypto.mEncryptedSizes.AppendElements(reinterpret_cast<uint32_t*>(&encryptedArr[0]),
                                                   encryptedArr.Length());
    int subSamplesNum = 0;
    ok &= NS_SUCCEEDED(cryptoInfo->NumSubSamples(&subSamplesNum));
    writer->mCrypto.mPlainSizes[0] -= (writer->Size() - subSamplesNum);

    if (!ok) {
      writer->mCrypto.mValid = false;
      HLS_DEBUG("HLSTrackDemuxer", "Error occurred during extraction from CryptoInfo java object.");
    }
  }
  return mrd;
}

void
HLSTrackDemuxer::Reset()
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
}

void
HLSTrackDemuxer::UpdateNextKeyFrameTime()
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  int64_t nextKeyFrameTime = mParent->GetNextKeyFrameTime();
  if (nextKeyFrameTime != mNextKeyframeTime.valueOr(media::TimeUnit::FromInfinity()).ToMicroseconds()) {
    HLS_DEBUG("HLSTrackDemuxer", "Update nextKeyFrameTime to %" PRId64 , nextKeyFrameTime);
    mNextKeyframeTime = Some(media::TimeUnit::FromMicroseconds(nextKeyFrameTime));
  }
}

nsresult
HLSTrackDemuxer::GetNextRandomAccessPoint(media::TimeUnit* aTime)
{
  if (mNextKeyframeTime.isNothing()) {
    // There's no next key frame.
    *aTime =
      media::TimeUnit::FromMicroseconds(std::numeric_limits<int64_t>::max());
  } else {
    *aTime = mNextKeyframeTime.value();
  }
  return NS_OK;
}

RefPtr<HLSTrackDemuxer::SkipAccessPointPromise>
HLSTrackDemuxer::SkipToNextRandomAccessPoint(
  const TimeUnit& aTimeThreshold)
{
  return InvokeAsync(
           mParent->GetTaskQueue(), this, __func__,
           &HLSTrackDemuxer::DoSkipToNextRandomAccessPoint,
           aTimeThreshold);
}

RefPtr<HLSTrackDemuxer::SkipAccessPointPromise>
HLSTrackDemuxer::DoSkipToNextRandomAccessPoint(
  const TimeUnit& aTimeThreshold)
{
  mQueuedSample = nullptr;
  uint32_t parsed = 0;
  bool found = false;
  while (!found) {
    mozilla::jni::ObjectArray::LocalRef demuxedSamples =
      (mType == TrackInfo::kAudioTrack) ?
        mParent->mHlsDemuxerWrapper->GetSamples(TrackInfo::kAudioTrack, 1):
        mParent->mHlsDemuxerWrapper->GetSamples(TrackInfo::kVideoTrack, 1);
    nsTArray<jni::Object::LocalRef> sampleObjectArray(demuxedSamples->GetElements());
    if (sampleObjectArray.IsEmpty()) {
      SkipFailureHolder holder(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, parsed);
      return SkipAccessPointPromise::CreateAndReject(holder, __func__);
    }
    parsed++;
    java::GeckoHlsSample::LocalRef sample(mozilla::Move(sampleObjectArray[0]));
    if (sample->IsEOS()) {
      SkipFailureHolder holder(NS_ERROR_DOM_MEDIA_END_OF_STREAM, parsed);
      return SkipAccessPointPromise::CreateAndReject(holder, __func__);
    }
    if (sample->IsKeyFrame()) {
      java::sdk::BufferInfo::LocalRef info = sample->Info();
      int64_t presentationTimeUs = 0;
      bool ok = NS_SUCCEEDED(info->PresentationTimeUs(&presentationTimeUs));
      if (ok && TimeUnit::FromMicroseconds(presentationTimeUs) >= aTimeThreshold) {
        found = true;
        mQueuedSample = ConvertToMediaRawData(sample);
        break;
      }
    }
  }
  return SkipAccessPointPromise::CreateAndResolve(parsed, __func__);
}

media::TimeIntervals
HLSTrackDemuxer::GetBuffered()
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  //int64_t bufferedTime = mParent->mHlsDemuxerWrapper->GetBuffered(); //us
  int64_t bufferedTime = 0;
  return TimeIntervals(TimeInterval(TimeUnit(), TimeUnit::FromMicroseconds(bufferedTime)));
}

void
HLSTrackDemuxer::BreakCycles()
{
  RefPtr<HLSTrackDemuxer> self = this;
  nsCOMPtr<nsIRunnable> task =
    NS_NewRunnableFunction([self]() {
      self->mParent = nullptr;
    } );
  mParent->GetTaskQueue()->Dispatch(task.forget());
}

HLSTrackDemuxer::~HLSTrackDemuxer()
{
}

} // namespace mozilla
