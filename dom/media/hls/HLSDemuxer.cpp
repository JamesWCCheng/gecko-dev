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
#include "nsPrintfCString.h"
#include "mozilla/Unused.h"

using namespace mozilla::java;

namespace mozilla {

typedef TrackInfo::TrackType TrackType;
using media::TimeUnit;
using media::TimeIntervals;

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

  void OnAudioFormatChanged(jni::Object::Param aParam) {
    MOZ_ASSERT(mDemuxer);
    mDemuxer->onAudioFormatChanged();
    mDemuxer->onCheckInitDone();
  }

  void OnVideoFormatChanged(jni::Object::Param aParam) {
    MOZ_ASSERT(mDemuxer);
    mDemuxer->onVideoFormatChanged();
    mDemuxer->onCheckInitDone();
  }
private:
  HLSDemuxer* mDemuxer;
};

HLSDemuxer::HLSDemuxer(MediaResource* aResource, AbstractThread* aAbstractMainThread)
  : mResource(aResource)
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
      jni::Object::LocalRef hlsAInfoObj = mHlsDemuxerWrapper->GetAudioInfo(0);
      return &mInfo.mAudio;
    }
    case TrackType::kVideoTrack: {
      jni::Object::LocalRef hlsVInfoObj = mHlsDemuxerWrapper->GetVideoInfo(0);
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
  RefPtr<SamplesHolder> samples = new SamplesHolder;
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
