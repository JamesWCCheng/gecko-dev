/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <limits>
#include <stdint.h>

#include "FennecJNINatives.h"
#include "HLSDemuxer.h"
#include "HLSUtils.h"
#include "MediaCodec.h"
#include "nsPrintfCString.h"
#include "mozilla/Unused.h"

using namespace mozilla::java;
using namespace mozilla::java::sdk;

namespace mozilla {

typedef TrackInfo::TrackType TrackType;
using media::TimeUnit;
using media::TimeIntervals;

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

  HlsDemuxerCallbacksSupport(HLSDemuxer* aDemuxer) {
    MOZ_ASSERT(aDemuxer);
    mDemuxer = aDemuxer;
  }

  void OnAudioFormatChanged() {
    MOZ_ASSERT(mDemuxer);
    mDemuxer->onAudioFormatChanged();
    mDemuxer->onCheckInitDone();
  }

  void OnVideoFormatChanged() {
    MOZ_ASSERT(mDemuxer);
    mDemuxer->onVideoFormatChanged();
    mDemuxer->onCheckInitDone();
  }

  void OnDataArrived() {
    MOZ_ASSERT(mDemuxer);
    mDemuxer->GetDecoder()->NotifyDataArrived();
  }

private:
  HLSDemuxer* mDemuxer;
};

HLSDemuxer::HLSDemuxer(MediaDecoder* aDecoder,
                       MediaResource* aResource,
                       AbstractThread* aAbstractMainThread)
  : mDecoder(aDecoder)
  , mResource(aResource)
  , mTaskQueue(new AutoTaskQueue(GetMediaThreadPool(MediaThreadType::PLAYBACK),
                                 aAbstractMainThread,
                                 /* aSupportsTailDispatch = */ false))
  , mMonitor("HLSDemuxer")
  , mAudioInfoUpdated(false)
  , mVideoInfoUpdated(false)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCString hlsURI;
  Unused << mResource->URI()->GetSpec(hlsURI);
  HLS_DEBUG("HLSDemuxer", "Ctor(%s)", hlsURI.get());
  HlsDemuxerCallbacksSupport::Init();
  mJavaCallbacks = GeckoHlsDemuxerWrapper::HlsDemuxerCallbacks::New();
  MOZ_ASSERT(mJavaCallbacks);

  HlsDemuxerCallbacksSupport::AttachNative(mJavaCallbacks,
                                           mozilla::MakeUnique<HlsDemuxerCallbacksSupport>(this));

  mHlsDemuxerWrapper = GeckoHlsDemuxerWrapper::Create(NS_ConvertUTF8toUTF16(hlsURI), mJavaCallbacks);
}

void
HLSDemuxer::onAudioFormatChanged()
{
  MOZ_ASSERT(NS_IsMainThread());
  HLS_DEBUG("HLSDemuxer", "onAudioFormatChanged");
  mAudioInfoUpdated = true;
}

void
HLSDemuxer::onVideoFormatChanged()
{
  MOZ_ASSERT(NS_IsMainThread());
  HLS_DEBUG("HLSDemuxer", "onVideoFormatChanged");
  mVideoInfoUpdated = true;
}

void
HLSDemuxer::onCheckInitDone()
{
  MOZ_ASSERT(NS_IsMainThread());
  HLS_DEBUG("HLSDemuxer", "onCheckInitDone");
  if (mInitPromise.IsEmpty()) {
    return;
  }
  if (mAudioInfoUpdated && mVideoInfoUpdated) {
    mInitPromise.ResolveIfExists(NS_OK, __func__);
  }
}

// Due to inaccuracies in determining buffer end
// frames (Bug 1065207). This value is based on videos seen in the wild.
const TimeUnit HLSDemuxer::EOS_FUZZ = media::TimeUnit::FromMicroseconds(500000);

RefPtr<HLSDemuxer::InitPromise>
HLSDemuxer::Init()
{
  HLS_DEBUG("HLSDemuxer", "Init()");
  RefPtr<HLSDemuxer> self = this;
  return InvokeAsync(GetTaskQueue(), __func__,
    [self](){
      RefPtr<InitPromise> p = self->mInitPromise.Ensure(__func__);
      return p;
    });
}

void HLSDemuxer::NotifyDataArrived()
{
  HLS_DEBUG("HLSDemuxer", "NotifyDataArrived()");
}

bool
HLSDemuxer::HasTrackType(TrackType aType) const
{
  MonitorAutoLock mon(mMonitor);
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
  HLS_DEBUG("HLSDemuxer", "GetNumberTracks(%d)", aType);
  switch (aType) {
    case TrackType::kAudioTrack:
      return mHlsDemuxerWrapper->GetNumberOfTracks(1);
    case TrackType::kVideoTrack:
      return mHlsDemuxerWrapper->GetNumberOfTracks(2);
    default:
      return 0;
  }
}

already_AddRefed<MediaTrackDemuxer>
HLSDemuxer::GetTrackDemuxer(TrackType aType, uint32_t aTrackNumber)
{
  HLS_DEBUG("HLSDemuxer", "GetTrackDemuxer(%d)", aType);
  RefPtr<HLSTrackDemuxer> e = new HLSTrackDemuxer(this, aType);
  mDemuxers.AppendElement(e);
  return e.forget();
}

bool
HLSDemuxer::IsSeekable() const
{
  return true;
}

UniquePtr<EncryptionInfo>
HLSDemuxer::GetCrypto()
{
  MonitorAutoLock mon(mMonitor);
  auto crypto = MakeUnique<EncryptionInfo>();
  *crypto = mInfo.mCrypto;
  return crypto;
}

TrackInfo*
HLSDemuxer::GetTrackInfo(TrackType aTrack)
{
  MonitorAutoLock mon(mMonitor);
  switch (aTrack) {
    case TrackType::kAudioTrack: {
      jni::Object::LocalRef infoObj = mHlsDemuxerWrapper->GetAudioInfo(0);
      java::HlsAudioInfo::LocalRef audioInfo(mozilla::Move(infoObj));
      mInfo.mAudio.mRate = audioInfo->Rate();
      mInfo.mAudio.mChannels = audioInfo->Channels();
      mInfo.mAudio.mProfile = audioInfo->Profile();
      mInfo.mAudio.mBitDepth = audioInfo->BitDepth();
      mInfo.mAudio.mMimeType = NS_ConvertUTF16toUTF8(audioInfo->MimeType()->ToString());
      return &mInfo.mAudio;
    }
    case TrackType::kVideoTrack: {
      jni::Object::LocalRef infoObj = mHlsDemuxerWrapper->GetVideoInfo(0);
      java::HlsVideoInfo::LocalRef videoInfo(mozilla::Move(infoObj));
      mInfo.mVideo.mStereoMode = getStereoMode(videoInfo->StereoMode());
      mInfo.mVideo.mRotation = getVideoInfoRotation(videoInfo->Rotation());
      mInfo.mVideo.mDisplay.width = videoInfo->DisplayX();
      mInfo.mVideo.mDisplay.height = videoInfo->DisplayY();
      mInfo.mVideo.mMimeType = NS_ConvertUTF16toUTF8(videoInfo->MimeType()->ToString());
      return &mInfo.mVideo;
    }
    default:
      return nullptr;
  }
}

HLSDemuxer::~HLSDemuxer()
{
  HLS_DEBUG("HLSDemuxer", "~HLSDemuxer()");
  HlsDemuxerCallbacksSupport::DisposeNative(mJavaCallbacks);
  mInitPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
}

HLSTrackDemuxer::HLSTrackDemuxer(HLSDemuxer* aParent, TrackInfo::TrackType aType)
  : mParent(aParent)
  , mType(aType)
  , mMonitor("HLSTrackDemuxer")
  , mReset(true)
  , mPreRoll(TimeUnit::FromMicroseconds(0))
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
  TimeUnit seekTime = TimeUnit::FromMicroseconds(0);
  return SeekPromise::CreateAndResolve(seekTime, __func__);
}

RefPtr<HLSTrackDemuxer::SamplesPromise>
HLSTrackDemuxer::GetSamples(int32_t aNumSamples)
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  if (!aNumSamples) {
    return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                                           __func__);
  }
  RefPtr<SamplesHolder> samples = new SamplesHolder;
  //TRACK_AUDIO = 1;
  //TRACK_VIDEO = 2;
  mozilla::jni::ObjectArray::LocalRef demuxedSamples =
      (mType == TrackInfo::kAudioTrack) ?
        mParent->mHlsDemuxerWrapper->GetSamples(1, aNumSamples):
        mParent->mHlsDemuxerWrapper->GetSamples(2, aNumSamples);
  nsTArray<jni::Object::LocalRef> sampleObjectArray(demuxedSamples->GetElements());

  if (sampleObjectArray.IsEmpty()) {
    return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
  }
  for (auto&& demuxedSample : sampleObjectArray) {
    java::Sample::LocalRef sample(mozilla::Move(demuxedSample));
    java::sdk::BufferInfo::LocalRef info = sample->Info();
    // TODO: Check which is essential material for MediaRawData.
    // Currently extract PTS, Size and Data without Crypto information.
    // Transform java Sample into MediaRawData
    RefPtr<MediaRawData> mrd = new MediaRawData();
    int64_t presentationTimeUs = 0;
    bool ok = NS_SUCCEEDED(info->PresentationTimeUs(&presentationTimeUs));
    mrd->mTime = presentationTimeUs;
    mrd->mTimecode = presentationTimeUs;
    int32_t flags = 0;
    ok = NS_SUCCEEDED(info->Flags(&flags));
    mrd->mKeyframe = flags & MediaCodec::BUFFER_FLAG_KEY_FRAME;
    // TODO : Fix this when we know how to calculate from ExoPlayer.
    mrd->mDuration = (mType == TrackInfo::kVideoTrack) ? 33000 : 0;

    int32_t size = 0;
    ok &= NS_SUCCEEDED(info->Size(&size));
    if (!ok) {
      HLS_DEBUG("HLSTrackDemuxer", "Error occurred during extraction from Sample java object.");
      return nullptr;
    }
    // Write payload into MediaRawData
    UniquePtr<MediaRawDataWriter> writer(mrd->CreateWriter());
    if (!writer->SetSize(size)) {
      HLS_DEBUG("HLSTrackDemuxer", "Exit failed to allocated media buffer");
      return nullptr;
    }
    jni::ByteBuffer::LocalRef dest =
        jni::ByteBuffer::New(writer->Data(), writer->Size());
    sample->WriteToByteBuffer(dest);

    // Write Crypto information
    java::sdk::CryptoInfo::LocalRef cryptoInfo = sample->CryptoInfo();
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
      // TODO: Need to validate.
      // Inverse the work by http://searchfox.org/mozilla-central/rev/ca7015fa45b30b29176fbaa70ba0a36fe9263c38/dom/media/platforms/android/AndroidDecoderModule.cpp#92
      int subSamplesNum = 0;
      ok &= NS_SUCCEEDED(cryptoInfo->NumSubSamples(&subSamplesNum));
      writer->mCrypto.mPlainSizes[0] -= (writer->Size() - subSamplesNum);

      if (!ok) {
        // TODO: maybe print a warning msg.
        HLS_DEBUG("HLSTrackDemuxer", "Error occurred during extraction from CryptoInfo java object.");
      }
    }
    samples->mSamples.AppendElement(mrd);
  }
  return SamplesPromise::CreateAndResolve(samples, __func__);
}

void
HLSTrackDemuxer::Reset()
{
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
}

nsresult
HLSTrackDemuxer::GetNextRandomAccessPoint(media::TimeUnit* aTime)
{
  MonitorAutoLock mon(mMonitor);
  *aTime = mNextRandomAccessPoint;
  return NS_OK;
}

RefPtr<HLSTrackDemuxer::SkipAccessPointPromise>
HLSTrackDemuxer::SkipToNextRandomAccessPoint(const media::TimeUnit& aTimeThreshold)
{
  uint32_t parsed = 0;
  SkipFailureHolder holder(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, parsed);
  return SkipAccessPointPromise::CreateAndReject(holder, __func__);
}

media::TimeIntervals
HLSTrackDemuxer::GetBuffered()
{
  TimeIntervals buffered;
  return buffered;
}

void
HLSTrackDemuxer::BreakCycles()
{
}

} // namespace mozilla
