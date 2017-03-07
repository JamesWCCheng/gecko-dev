/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <limits>
#include <stdint.h>

#include "HLSDemuxer.h"
#include "nsPrintfCString.h"

namespace mozilla {

typedef TrackInfo::TrackType TrackType;
using media::TimeUnit;
using media::TimeIntervals;

HLSDemuxer::HLSDemuxer(AbstractThread* aAbstractMainThread)
  : mTaskQueue(new AutoTaskQueue(GetMediaThreadPool(MediaThreadType::PLAYBACK),
                                 aAbstractMainThread,
                                 /* aSupportsTailDispatch = */ false))
  , mMonitor("HLSDemuxer")
{
  MOZ_ASSERT(NS_IsMainThread());
  mHlsSampleGetter = GeckoHlsSampleGetter::Create();
}

// Due to inaccuracies in determining buffer end
// frames (Bug 1065207). This value is based on videos seen in the wild.
const TimeUnit HLSDemuxer::EOS_FUZZ = media::TimeUnit::FromMicroseconds(500000);

RefPtr<HLSDemuxer::InitPromise>
HLSDemuxer::Init()
{
  RefPtr<HLSDemuxer> self = this;
  return InitPromise::CreateAndResolve(NS_OK, __func__);
}

void HLSDemuxer::NotifyDataArrived()
{
}

bool
HLSDemuxer::HasTrackType(TrackType aType) const
{
  MonitorAutoLock mon(mMonitor);

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
  return HasTrackType(aType) ? 1u : 0;
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
    case TrackType::kAudioTrack:
      return &mInfo.mAudio;
    case TrackType::kVideoTrack:
      return &mInfo.mVideo;
    default:
      return nullptr;
  }
}

HLSDemuxer::~HLSDemuxer()
{
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
