/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(HLSDemuxer_h_)
#define HLSDemuxer_h_

#include "AutoTaskQueue.h"
#include "GeneratedJNIWrappers.h"
#include "MediaDataDemuxer.h"
#include "MediaDecoder.h"
#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/Monitor.h"

#include "VideoUtils.h"

namespace mozilla {

class AbstractThread;
class MediaResult;
class HLSTrackDemuxer;

class HLSDemuxer : public MediaDataDemuxer
{
public:
  explicit HLSDemuxer(MediaResource* aResource);

  RefPtr<InitPromise> Init() override;

  bool HasTrackType(TrackInfo::TrackType aType) const override;

  uint32_t GetNumberTracks(TrackInfo::TrackType aType) const override;

  already_AddRefed<MediaTrackDemuxer>
  GetTrackDemuxer(TrackInfo::TrackType aType, uint32_t aTrackNumber) override;

  bool IsSeekable() const override;

  UniquePtr<EncryptionInfo> GetCrypto() override;

  bool ShouldComputeStartTime() const override { return true; }

  void NotifyDataArrived() override;

  AutoTaskQueue* GetTaskQueue() { return mTaskQueue; }
  void OnInitialized(bool aHasAudio, bool aHasVideo);

protected:
  int64_t GetNextKeyFrameTime();
  void UpdateVideoInfo(int index);
  void UpdateAudioInfo(int index);

private:
  ~HLSDemuxer();
  RefPtr<MediaResource> mResource;
  friend class HLSTrackDemuxer;
  TrackInfo* GetTrackInfo(TrackInfo::TrackType);

  RefPtr<AutoTaskQueue> mTaskQueue;
  nsTArray<RefPtr<HLSTrackDemuxer>> mDemuxers;

  MozPromiseHolder<InitPromise> mInitPromise;

  // Monitor to protect members below across multiple threads.
  mutable Monitor mMonitor;
  MediaInfo mInfo;

  // java::GeckoHlsDemuxerWrapper::HlsDemuxerCallbacks::GlobalRef mJavaCallbacks;
  // java::GeckoHlsDemuxerWrapper::GlobalRef mHlsDemuxerWrapper;
};

class HLSTrackDemuxer : public MediaTrackDemuxer
{
public:
  HLSTrackDemuxer(HLSDemuxer* aParent,
                  TrackInfo::TrackType aType);
  ~HLSTrackDemuxer();
  UniquePtr<TrackInfo> GetInfo() const override;

  RefPtr<SeekPromise> Seek(const media::TimeUnit& aTime) override;

  RefPtr<SamplesPromise> GetSamples(int32_t aNumSamples = 1) override;

  void Reset() override;

  nsresult GetNextRandomAccessPoint(media::TimeUnit* aTime) override;

  RefPtr<SkipAccessPointPromise> SkipToNextRandomAccessPoint(
    const media::TimeUnit& aTimeThreshold) override;

  media::TimeIntervals GetBuffered() override;

  void BreakCycles() override;

  bool GetSamplesMayBlock() const override
  {
    return false;
  }

private:
  // Update the timestamp of the next keyframe if there's one.
  void UpdateNextKeyFrameTime();
  RefPtr<SeekPromise> DoSeek(const media::TimeUnit& aTime);
  RefPtr<SamplesPromise> DoGetSamples(int32_t aNumSamples);

  RefPtr<HLSDemuxer> mParent;

  TrackInfo::TrackType mType;
  // Monitor protecting members below accessed from multiple threads.
  Monitor mMonitor;
  Maybe<media::TimeUnit> mNextKeyframeTime;
  int32_t mLastFormatIndex = -1;
};

} // namespace mozilla

#endif
