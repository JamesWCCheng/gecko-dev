/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AutoTaskQueue.h"
#include "mozilla/SizePrintfMacros.h"
#include "Layers.h"
#include "MediaData.h"
#include "MediaInfo.h"
#include "MediaFormatReader.h"
#include "MediaPrefs.h"
#include "MediaResource.h"
#include "VideoUtils.h"
#include "VideoFrameContainer.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/layers/ShadowLayers.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/CDMProxy.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/Telemetry.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/Unused.h"
#include "nsContentUtils.h"
#include "nsPrintfCString.h"
#include "nsSize.h"

#include <algorithm>
#include <queue>

using namespace mozilla::media;

using mozilla::layers::Image;
using mozilla::layers::LayerManager;
using mozilla::layers::LayersBackend;

static mozilla::LazyLogModule sFormatDecoderLog("MediaFormatReader");
mozilla::LazyLogModule gMediaDemuxerLog("MediaDemuxer");

#define LOG(arg, ...) MOZ_LOG(sFormatDecoderLog, mozilla::LogLevel::Debug, ("MediaFormatReader(%p)::%s: " arg, this, __func__, ##__VA_ARGS__))
#define LOGV(arg, ...) MOZ_LOG(sFormatDecoderLog, mozilla::LogLevel::Verbose, ("MediaFormatReader(%p)::%s: " arg, this, __func__, ##__VA_ARGS__))

namespace mozilla {

/**
 * This is a singleton which controls the number of decoders that can be
 * created concurrently. Before calling PDMFactory::CreateDecoder(), Alloc()
 * must be called to get a token object as a permission to create a decoder.
 * The token should stay alive until Shutdown() is called on the decoder.
 * The destructor of the token will restore the decoder count so it is available
 * for next calls of Alloc().
 */
class GlobalAllocPolicy
{
  using TrackType = TrackInfo::TrackType;

public:
  class Token
  {
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Token)
  protected:
    virtual ~Token() {}
  };

  using Promise = MozPromise<RefPtr<Token>, bool, true>;

  // Acquire a token for decoder creation. Thread-safe.
  auto Alloc() -> RefPtr<Promise>;

  // Called by ClearOnShutdown() to delete the singleton.
  void operator=(decltype(nullptr));

  // Get the singleton for the given track type. Thread-safe.
  static GlobalAllocPolicy& Instance(TrackType aTrack);

private:
  class AutoDeallocToken;
  using PromisePrivate = Promise::Private;
  GlobalAllocPolicy();
  ~GlobalAllocPolicy();
  // Called by the destructor of TokenImpl to restore the decoder limit.
  void Dealloc();
  // Decrement the decoder limit and resolve a promise if available.
  void ResolvePromise(ReentrantMonitorAutoEnter& aProofOfLock);

  // Protect access to Instance().
  static StaticMutex sMutex;

  ReentrantMonitor mMonitor;
  // The number of decoders available for creation.
  int mDecoderLimit;
  // Requests to acquire tokens.
  std::queue<RefPtr<PromisePrivate>> mPromises;
};

StaticMutex GlobalAllocPolicy::sMutex;

class GlobalAllocPolicy::AutoDeallocToken : public Token
{
public:
  explicit AutoDeallocToken(GlobalAllocPolicy& aPolicy) : mPolicy(aPolicy) { }

private:
  ~AutoDeallocToken()
  {
    mPolicy.Dealloc();
  }

  GlobalAllocPolicy& mPolicy; // reference to a singleton object.
};

GlobalAllocPolicy::GlobalAllocPolicy()
  : mMonitor("DecoderAllocPolicy::mMonitor")
  , mDecoderLimit(MediaPrefs::MediaDecoderLimit())
{
  // Non DocGroup-version AbstractThread::MainThread is fine for
  // ClearOnShutdown().
  AbstractThread::MainThread()->Dispatch(NS_NewRunnableFunction([this] () {
    ClearOnShutdown(this, ShutdownPhase::ShutdownThreads);
  }));
}

GlobalAllocPolicy::~GlobalAllocPolicy()
{
  while (!mPromises.empty()) {
    RefPtr<PromisePrivate> p = mPromises.front().forget();
    mPromises.pop();
    p->Reject(true, __func__);
  }
}

GlobalAllocPolicy&
GlobalAllocPolicy::Instance(TrackType aTrack)
{
  StaticMutexAutoLock lock(sMutex);
  if (aTrack == TrackType::kAudioTrack) {
    static auto sAudioPolicy = new GlobalAllocPolicy();
    return *sAudioPolicy;
  } else {
    static auto sVideoPolicy = new GlobalAllocPolicy();
    return *sVideoPolicy;
  }
}

auto
GlobalAllocPolicy::Alloc() -> RefPtr<Promise>
{
  // No decoder limit set.
  if (mDecoderLimit < 0) {
    return Promise::CreateAndResolve(new Token(), __func__);
  }

  ReentrantMonitorAutoEnter mon(mMonitor);
  RefPtr<PromisePrivate> p = new PromisePrivate(__func__);
  mPromises.push(p);
  ResolvePromise(mon);
  return p.forget();
}

void
GlobalAllocPolicy::Dealloc()
{
  ReentrantMonitorAutoEnter mon(mMonitor);
  ++mDecoderLimit;
  ResolvePromise(mon);
}

void
GlobalAllocPolicy::ResolvePromise(ReentrantMonitorAutoEnter& aProofOfLock)
{
  MOZ_ASSERT(mDecoderLimit >= 0);

  if (mDecoderLimit > 0 && !mPromises.empty()) {
    --mDecoderLimit;
    RefPtr<PromisePrivate> p = mPromises.front().forget();
    mPromises.pop();
    p->Resolve(new AutoDeallocToken(*this), __func__);
  }
}

void
GlobalAllocPolicy::operator=(std::nullptr_t)
{
  delete this;
}

/**
 * This class addresses the concern of bug 1339310 comment 4 where the Widevine
 * CDM doesn't support running multiple instances of a video decoder at once per
 * CDM instance by sequencing the order of decoder creation and shutdown. Note
 * this class addresses a different concern from that of GlobalAllocPolicy which
 * controls a system-wide number of decoders while this class control a per-MFR
 * number (which is one per CDM requirement).
 */
class LocalAllocPolicy
{
  using TrackType = TrackInfo::TrackType;
  using Promise = GlobalAllocPolicy::Promise;
  using Token = GlobalAllocPolicy::Token;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(LocalAllocPolicy)

public:
  LocalAllocPolicy(TrackType aTrack, TaskQueue* aOwnerThread)
    : mTrack(aTrack)
    , mOwnerThread(aOwnerThread)
  {
  }

  // Acquire a token for decoder creation. Note the resolved token will
  // aggregate a GlobalAllocPolicy token to comply to its policy. Note
  // this function shouldn't be called again until the returned promise
  // is resolved or rejected.
  RefPtr<Promise> Alloc();

  // Cancel the request to GlobalAllocPolicy and reject the current token
  // request. Note this must happen before mOwnerThread->BeginShutdown().
  void Cancel();

private:
  /*
   * An RAII class to manage LocalAllocPolicy::mDecoderLimit.
   */
  class AutoDeallocToken : public Token
  {
  public:
    explicit AutoDeallocToken(LocalAllocPolicy* aOwner)
      : mOwner(aOwner)
    {
      MOZ_DIAGNOSTIC_ASSERT(mOwner->mDecoderLimit > 0);
      --mOwner->mDecoderLimit;
    }
    // Aggregate a GlobalAllocPolicy token to present a single instance of
    // Token to the client so the client doesn't have to deal with
    // GlobalAllocPolicy and LocalAllocPolicy separately.
    void Append(Token* aToken)
    {
      mToken = aToken;
    }
  private:
    // Release tokens allocated from GlobalAllocPolicy and LocalAllocPolicy
    // and process next token request if any.
    ~AutoDeallocToken()
    {
      mToken = nullptr; // Dealloc the global token.
      ++mOwner->mDecoderLimit; // Dealloc the local token.
      mOwner->ProcessRequest(); // Process next pending request.
    }
    RefPtr<LocalAllocPolicy> mOwner;
    RefPtr<Token> mToken;
  };

  ~LocalAllocPolicy() { }
  void ProcessRequest();

  int mDecoderLimit = 1;
  const TrackType mTrack;
  RefPtr<TaskQueue> mOwnerThread;
  MozPromiseHolder<Promise> mPendingPromise;
  MozPromiseRequestHolder<Promise> mTokenRequest;
};

RefPtr<LocalAllocPolicy::Promise>
LocalAllocPolicy::Alloc()
{
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());
  MOZ_DIAGNOSTIC_ASSERT(mPendingPromise.IsEmpty());
  RefPtr<Promise> p = mPendingPromise.Ensure(__func__);
  if (mDecoderLimit > 0) {
    ProcessRequest();
  }
  return p.forget();
}

void
LocalAllocPolicy::ProcessRequest()
{
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());
  MOZ_DIAGNOSTIC_ASSERT(mDecoderLimit > 0);

  // No pending request.
  if (mPendingPromise.IsEmpty()) {
    return;
  }

  RefPtr<AutoDeallocToken> token = new AutoDeallocToken(this);
  RefPtr<LocalAllocPolicy> self = this;

  GlobalAllocPolicy::Instance(mTrack).Alloc()->Then(
    mOwnerThread, __func__,
    [self, token](Token* aToken) {
      self->mTokenRequest.Complete();
      token->Append(aToken);
      self->mPendingPromise.Resolve(token, __func__);
    },
    [self, token]() {
      self->mTokenRequest.Complete();
      self->mPendingPromise.Reject(true, __func__);
    })->Track(mTokenRequest);
}

void
LocalAllocPolicy::Cancel()
{
  MOZ_ASSERT(mOwnerThread->IsCurrentThreadIn());
  mPendingPromise.RejectIfExists(true, __func__);
  mTokenRequest.DisconnectIfExists();
}

/**
 * This class tracks shutdown promises to ensure all decoders are shut down
 * completely before MFR continues the rest of the shutdown procedure.
 */
class MediaFormatReader::ShutdownPromisePool
{
public:
  ShutdownPromisePool()
    : mOnShutdownComplete(new ShutdownPromise::Private(__func__))
  {
  }

  // Return a promise which will be resolved when all the tracking promises
  // are resolved. Note no more promises should be added for tracking once
  // this function is called.
  RefPtr<ShutdownPromise> Shutdown();

   // Track a shutdown promise.
  void Track(RefPtr<ShutdownPromise> aPromise);

   // Shut down a decoder and track its shutdown promise.
  void ShutdownDecoder(already_AddRefed<MediaDataDecoder> aDecoder)
  {
    Track(RefPtr<MediaDataDecoder>(aDecoder)->Shutdown());
  }

private:
  bool mShutdown = false;
  const RefPtr<ShutdownPromise::Private> mOnShutdownComplete;
  nsTHashtable<nsRefPtrHashKey<ShutdownPromise>> mPromises;
};

RefPtr<ShutdownPromise>
MediaFormatReader::ShutdownPromisePool::Shutdown()
{
  MOZ_DIAGNOSTIC_ASSERT(!mShutdown);
  mShutdown = true;
  if (mPromises.Count() == 0) {
    mOnShutdownComplete->Resolve(true, __func__);
  }
  return mOnShutdownComplete;
}

void
MediaFormatReader::ShutdownPromisePool::Track(RefPtr<ShutdownPromise> aPromise)
{
  MOZ_DIAGNOSTIC_ASSERT(!mShutdown);
  MOZ_DIAGNOSTIC_ASSERT(!mPromises.Contains(aPromise));
  mPromises.PutEntry(aPromise);
  aPromise->Then(
    AbstractThread::GetCurrent(), __func__,
    [aPromise, this]() {
      MOZ_DIAGNOSTIC_ASSERT(mPromises.Contains(aPromise));
      mPromises.RemoveEntry(aPromise);
      if (mShutdown && mPromises.Count() == 0) {
        mOnShutdownComplete->Resolve(true, __func__);
      }
    });
}

class MediaFormatReader::DecoderFactory
{
  using InitPromise = MediaDataDecoder::InitPromise;
  using TokenPromise = GlobalAllocPolicy::Promise;
  using Token = GlobalAllocPolicy::Token;

public:
  explicit DecoderFactory(MediaFormatReader* aOwner)
    : mAudio(aOwner->mAudio, TrackInfo::kAudioTrack, aOwner->OwnerThread())
    , mVideo(aOwner->mVideo, TrackInfo::kVideoTrack, aOwner->OwnerThread())
    , mOwner(WrapNotNull(aOwner)) { }

  void CreateDecoder(TrackType aTrack);

  // Shutdown any decoder pending initialization and reset mAudio/mVideo to its
  // pristine state so CreateDecoder() is ready to be called again immediately.
  void ShutdownDecoder(TrackType aTrack)
  {
    MOZ_ASSERT(aTrack == TrackInfo::kAudioTrack
               || aTrack == TrackInfo::kVideoTrack);
    auto& data = aTrack == TrackInfo::kAudioTrack ? mAudio : mVideo;
    data.mPolicy->Cancel();
    data.mTokenRequest.DisconnectIfExists();
    data.mInitRequest.DisconnectIfExists();
    if (data.mDecoder) {
      mOwner->mShutdownPromisePool->ShutdownDecoder(data.mDecoder.forget());
    }
    data.mStage = Stage::None;
    MOZ_ASSERT(!data.mToken);
  }

private:
  class Wrapper;

  enum class Stage : int8_t
  {
    None,
    WaitForToken,
    CreateDecoder,
    WaitForInit
  };

  struct Data
  {
    Data(DecoderData& aOwnerData, TrackType aTrack, TaskQueue* aThread)
      : mOwnerData(aOwnerData)
      , mTrack(aTrack)
      , mPolicy(new LocalAllocPolicy(aTrack, aThread)) { }
    DecoderData& mOwnerData;
    const TrackType mTrack;
    RefPtr<LocalAllocPolicy> mPolicy;
    Stage mStage = Stage::None;
    RefPtr<Token> mToken;
    RefPtr<MediaDataDecoder> mDecoder;
    MozPromiseRequestHolder<TokenPromise> mTokenRequest;
    MozPromiseRequestHolder<InitPromise> mInitRequest;
  } mAudio, mVideo;

  void RunStage(Data& aData);
  MediaResult DoCreateDecoder(Data& aData);
  void DoInitDecoder(Data& aData);

  // guaranteed to be valid by the owner.
  const NotNull<MediaFormatReader*> mOwner;
};

void
MediaFormatReader::DecoderFactory::CreateDecoder(TrackType aTrack)
{
  MOZ_ASSERT(aTrack == TrackInfo::kAudioTrack
             || aTrack == TrackInfo::kVideoTrack);
  RunStage(aTrack == TrackInfo::kAudioTrack ? mAudio : mVideo);
}

class MediaFormatReader::DecoderFactory::Wrapper : public MediaDataDecoder
{
  using Token = GlobalAllocPolicy::Token;

public:
  Wrapper(already_AddRefed<MediaDataDecoder> aDecoder,
          already_AddRefed<Token> aToken)
    : mDecoder(aDecoder), mToken(aToken) {}

  RefPtr<InitPromise> Init() override { return mDecoder->Init(); }
  RefPtr<DecodePromise> Decode(MediaRawData* aSample) override
  {
    return mDecoder->Decode(aSample);
  }
  RefPtr<DecodePromise> Drain() override { return mDecoder->Drain(); }
  RefPtr<FlushPromise> Flush() override { return mDecoder->Flush(); }
  bool IsHardwareAccelerated(nsACString& aFailureReason) const override
  {
    return mDecoder->IsHardwareAccelerated(aFailureReason);
  }
  const char* GetDescriptionName() const override
  {
    return mDecoder->GetDescriptionName();
  }
  void SetSeekThreshold(const media::TimeUnit& aTime) override
  {
    mDecoder->SetSeekThreshold(aTime);
  }
  bool SupportDecoderRecycling() const override
  {
    return mDecoder->SupportDecoderRecycling();
  }
  void ConfigurationChanged(const TrackInfo& aConfig) override
  {
    mDecoder->ConfigurationChanged(aConfig);
  }
  RefPtr<ShutdownPromise> Shutdown() override
  {
    RefPtr<MediaDataDecoder> decoder = mDecoder.forget();
    RefPtr<Token> token = mToken.forget();
    return decoder->Shutdown()->Then(
      AbstractThread::GetCurrent(), __func__,
      [token]() {},
      [token]() { MOZ_RELEASE_ASSERT(false, "Can't reach here"); });
  }

private:
  RefPtr<MediaDataDecoder> mDecoder;
  RefPtr<Token> mToken;
};

void
MediaFormatReader::DecoderFactory::RunStage(Data& aData)
{
  switch (aData.mStage) {
    case Stage::None: {
      MOZ_ASSERT(!aData.mToken);
      aData.mPolicy->Alloc()->Then(
        mOwner->OwnerThread(), __func__,
        [this, &aData] (Token* aToken) {
          aData.mTokenRequest.Complete();
          aData.mToken = aToken;
          aData.mStage = Stage::CreateDecoder;
          RunStage(aData);
        },
        [&aData] () {
          aData.mTokenRequest.Complete();
          aData.mStage = Stage::None;
        })->Track(aData.mTokenRequest);
      aData.mStage = Stage::WaitForToken;
      break;
    }

    case Stage::WaitForToken: {
      MOZ_ASSERT(!aData.mToken);
      MOZ_ASSERT(aData.mTokenRequest.Exists());
      break;
    }

    case Stage::CreateDecoder: {
      MOZ_ASSERT(aData.mToken);
      MOZ_ASSERT(!aData.mDecoder);
      MOZ_ASSERT(!aData.mInitRequest.Exists());

      MediaResult rv = DoCreateDecoder(aData);
      if (NS_FAILED(rv)) {
        NS_WARNING("Error constructing decoders");
        aData.mToken = nullptr;
        aData.mStage = Stage::None;
        mOwner->NotifyError(aData.mTrack, rv);
        return;
      }

      aData.mDecoder = new Wrapper(aData.mDecoder.forget(), aData.mToken.forget());
      DoInitDecoder(aData);
      aData.mStage = Stage::WaitForInit;
      break;
    }

    case Stage::WaitForInit: {
      MOZ_ASSERT(aData.mDecoder);
      MOZ_ASSERT(aData.mInitRequest.Exists());
      break;
    }
  }
}

MediaResult
MediaFormatReader::DecoderFactory::DoCreateDecoder(Data& aData)
{
  auto& ownerData = aData.mOwnerData;

  auto decoderCreatingError = "error creating audio decoder";
  MediaResult result =
    MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, decoderCreatingError);

  if (!mOwner->mPlatform) {
    mOwner->mPlatform = new PDMFactory();
    if (mOwner->IsEncrypted()) {
      MOZ_ASSERT(mOwner->mCDMProxy);
      mOwner->mPlatform->SetCDMProxy(mOwner->mCDMProxy);
    }
  }

  switch (aData.mTrack) {
    case TrackInfo::kAudioTrack: {
      aData.mDecoder = mOwner->mPlatform->CreateDecoder({
        ownerData.mInfo
        ? *ownerData.mInfo->GetAsAudioInfo()
        : *ownerData.mOriginalInfo->GetAsAudioInfo(),
        ownerData.mTaskQueue,
        mOwner->mCrashHelper,
        ownerData.mIsBlankDecode,
        &result,
        TrackInfo::kAudioTrack,
        &mOwner->OnTrackWaitingForKeyProducer()
      });
      break;
    }

    case TrackType::kVideoTrack: {
      // Decoders use the layers backend to decide if they can use hardware decoding,
      // so specify LAYERS_NONE if we want to forcibly disable it.
      aData.mDecoder = mOwner->mPlatform->CreateDecoder({
        ownerData.mInfo
        ? *ownerData.mInfo->GetAsVideoInfo()
        : *ownerData.mOriginalInfo->GetAsVideoInfo(),
        ownerData.mTaskQueue,
        mOwner->mKnowsCompositor,
        mOwner->GetImageContainer(),
        mOwner->mCrashHelper,
        ownerData.mIsBlankDecode,
        &result,
        TrackType::kVideoTrack,
        &mOwner->OnTrackWaitingForKeyProducer()
      });
      break;
    }

    default:
      break;
  }

  if (aData.mDecoder) {
    return NS_OK;
  }

  ownerData.mDescription = decoderCreatingError;
  return result;
}

void
MediaFormatReader::DecoderFactory::DoInitDecoder(Data& aData)
{
  auto& ownerData = aData.mOwnerData;

  aData.mDecoder->Init()
    ->Then(mOwner->OwnerThread(), __func__,
           [this, &aData, &ownerData](TrackType aTrack) {
             aData.mInitRequest.Complete();
             aData.mStage = Stage::None;
             MutexAutoLock lock(ownerData.mMutex);
             ownerData.mDecoder = aData.mDecoder.forget();
             ownerData.mDescription = ownerData.mDecoder->GetDescriptionName();
             mOwner->SetVideoDecodeThreshold();
             mOwner->ScheduleUpdate(aTrack);
           },
           [this, &aData, &ownerData](const MediaResult& aError) {
             aData.mInitRequest.Complete();
             MOZ_RELEASE_ASSERT(!ownerData.mDecoder,
                                "Can't have a decoder already set");
             aData.mStage = Stage::None;
             mOwner->mShutdownPromisePool->ShutdownDecoder(aData.mDecoder.forget());
             mOwner->NotifyError(aData.mTrack, aError);
           })
    ->Track(aData.mInitRequest);
}

// DemuxerProxy ensures that the original main demuxer is only ever accessed
// via its own dedicated task queue.
// This ensure that the reader's taskqueue will never blocked while a demuxer
// is itself blocked attempting to access the MediaCache or the MediaResource.
class MediaFormatReader::DemuxerProxy
{
  using TrackType = TrackInfo::TrackType;
  class Wrapper;

public:
  explicit DemuxerProxy(MediaDataDemuxer* aDemuxer, AbstractThread* aMainThread)
    : mTaskQueue(new AutoTaskQueue(
                   GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
                   aMainThread))
    , mData(new Data(aDemuxer))
  {
    MOZ_COUNT_CTOR(DemuxerProxy);
  }

  ~DemuxerProxy()
  {
    MOZ_COUNT_DTOR(DemuxerProxy);
  }

  RefPtr<ShutdownPromise> Shutdown()
  {
    mData->mAudioDemuxer = nullptr;
    mData->mVideoDemuxer = nullptr;
    RefPtr<Data> data = mData.forget();
    return InvokeAsync(mTaskQueue, __func__, [data]() {
      // We need to clear our reference to the demuxer now. So that in the event
      // the init promise wasn't resolved, such as what can happen with the
      // mediasource demuxer that is waiting on more data, it will force the
      // init promise to be rejected.
      data->mDemuxer = nullptr;
      return ShutdownPromise::CreateAndResolve(true, __func__);
    });
  }

  RefPtr<MediaDataDemuxer::InitPromise> Init();

  Wrapper*
  GetTrackDemuxer(TrackType aTrack, uint32_t aTrackNumber)
  {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    switch (aTrack) {
      case TrackInfo::kAudioTrack:
        return mData->mAudioDemuxer;
      case TrackInfo::kVideoTrack:
        return mData->mVideoDemuxer;
      default:
        return nullptr;
    }
  }

  uint32_t GetNumberTracks(TrackType aTrack) const
  {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    switch (aTrack) {
      case TrackInfo::kAudioTrack:
        return mData->mNumAudioTrack;
      case TrackInfo::kVideoTrack:
        return mData->mNumVideoTrack;
      default:
        return 0;
    }
  }

  bool IsSeekable() const
  {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    return mData->mSeekable;
  }

  bool IsSeekableOnlyInBufferedRanges() const
  {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    return mData->mSeekableOnlyInBufferedRange;
  }

  UniquePtr<EncryptionInfo> GetCrypto() const
  {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    if (!mData->mCrypto) {
      return nullptr;
    }
    auto crypto = MakeUnique<EncryptionInfo>();
    *crypto = *mData->mCrypto;
    return crypto;
  }

  RefPtr<NotifyDataArrivedPromise> NotifyDataArrived();

  bool ShouldComputeStartTime() const
  {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    return mData->mShouldComputeStartTime;
  }

private:
  const RefPtr<AutoTaskQueue> mTaskQueue;
  struct Data
  {
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Data)

    explicit Data(MediaDataDemuxer* aDemuxer)
      : mInitDone(false)
      , mDemuxer(aDemuxer)
    {
    }

    Atomic<bool> mInitDone;
    // Only ever accessed over mTaskQueue once.
    RefPtr<MediaDataDemuxer> mDemuxer;
    // Only accessed once InitPromise has been resolved and immutable after.
    // So we can safely access them without the use of the mutex.
    uint32_t mNumAudioTrack = 0;
    RefPtr<Wrapper> mAudioDemuxer;
    uint32_t mNumVideoTrack = 0;
    RefPtr<Wrapper> mVideoDemuxer;
    bool mSeekable = false;
    bool mSeekableOnlyInBufferedRange = false;
    bool mShouldComputeStartTime = true;
    UniquePtr<EncryptionInfo> mCrypto;
  private:
    ~Data() { }
  };
  RefPtr<Data> mData;
};

class MediaFormatReader::DemuxerProxy::Wrapper : public MediaTrackDemuxer
{
public:
  Wrapper(MediaTrackDemuxer* aTrackDemuxer, AutoTaskQueue* aTaskQueue)
    : mMutex("TrackDemuxer Mutex")
    , mTaskQueue(aTaskQueue)
    , mGetSamplesMayBlock(aTrackDemuxer->GetSamplesMayBlock())
    , mInfo(aTrackDemuxer->GetInfo())
    , mTrackDemuxer(aTrackDemuxer)
  {
  }

  UniquePtr<TrackInfo> GetInfo() const override
  {
    if (!mInfo) {
      return nullptr;
    }
    return mInfo->Clone();
  }

  RefPtr<SeekPromise> Seek(const media::TimeUnit& aTime) override
  {
    RefPtr<Wrapper> self = this;
    return InvokeAsync(
             mTaskQueue, __func__,
             [self, aTime]() { return self->mTrackDemuxer->Seek(aTime); })
      ->Then(mTaskQueue, __func__,
             [self]() { self->UpdateRandomAccessPoint(); },
             [self]() { self->UpdateRandomAccessPoint(); });
  }

  RefPtr<SamplesPromise> GetSamples(int32_t aNumSamples) override
  {
    RefPtr<Wrapper> self = this;
    return InvokeAsync(mTaskQueue, __func__,
                       [self, aNumSamples]() {
                         return self->mTrackDemuxer->GetSamples(aNumSamples);
                       })
      ->Then(mTaskQueue, __func__,
             [self]() { self->UpdateRandomAccessPoint(); },
             [self]() { self->UpdateRandomAccessPoint(); });
  }

  bool GetSamplesMayBlock() const override
  {
    return mGetSamplesMayBlock;
  }

  void Reset() override
  {
    RefPtr<Wrapper> self = this;
    mTaskQueue->Dispatch(NS_NewRunnableFunction([self]() {
      self->mTrackDemuxer->Reset();
    }));
  }

  nsresult GetNextRandomAccessPoint(TimeUnit* aTime) override
  {
    MutexAutoLock lock(mMutex);
    if (NS_SUCCEEDED(mNextRandomAccessPointResult)) {
      *aTime = mNextRandomAccessPoint;
    }
    return mNextRandomAccessPointResult;
  }

  RefPtr<SkipAccessPointPromise>
  SkipToNextRandomAccessPoint(const media::TimeUnit& aTimeThreshold) override
  {
    RefPtr<Wrapper> self = this;
    return InvokeAsync(
             mTaskQueue, __func__,
             [self, aTimeThreshold]()  {
               return self->mTrackDemuxer->SkipToNextRandomAccessPoint(
                 aTimeThreshold);
             })
      ->Then(mTaskQueue, __func__,
             [self]() { self->UpdateRandomAccessPoint(); },
             [self]() { self->UpdateRandomAccessPoint(); });
  }

  TimeIntervals GetBuffered() override
  {
    MutexAutoLock lock(mMutex);
    return mBuffered;
  }

  void BreakCycles() override { }

private:
  Mutex mMutex;
  const RefPtr<AutoTaskQueue> mTaskQueue;
  const bool mGetSamplesMayBlock;
  const UniquePtr<TrackInfo> mInfo;
  // mTrackDemuxer is only ever accessed on demuxer's task queue.
  RefPtr<MediaTrackDemuxer> mTrackDemuxer;
  // All following members are protected by mMutex
  nsresult mNextRandomAccessPointResult = NS_OK;
  TimeUnit mNextRandomAccessPoint;
  TimeIntervals mBuffered;
  friend class DemuxerProxy;

  ~Wrapper()
  {
    RefPtr<MediaTrackDemuxer> trackDemuxer = mTrackDemuxer.forget();
    mTaskQueue->Dispatch(NS_NewRunnableFunction(
      [trackDemuxer]() { trackDemuxer->BreakCycles(); }));
  }

  void UpdateRandomAccessPoint()
  {
    MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());
    if (!mTrackDemuxer) {
      // Detached.
      return;
    }
    MutexAutoLock lock(mMutex);
    mNextRandomAccessPointResult =
      mTrackDemuxer->GetNextRandomAccessPoint(&mNextRandomAccessPoint);
  }

  void UpdateBuffered()
  {
    MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());
    if (!mTrackDemuxer) {
      // Detached.
      return;
    }
    MutexAutoLock lock(mMutex);
    mBuffered = mTrackDemuxer->GetBuffered();
  }
};

RefPtr<MediaDataDemuxer::InitPromise>
MediaFormatReader::DemuxerProxy::Init()
{
  RefPtr<Data> data = mData;
  RefPtr<AutoTaskQueue> taskQueue = mTaskQueue;
  return InvokeAsync(mTaskQueue, __func__,
                     [data, taskQueue]() {
                       if (!data->mDemuxer) {
                         return MediaDataDemuxer::InitPromise::CreateAndReject(
                           NS_ERROR_DOM_MEDIA_CANCELED, __func__);
                       }
                       return data->mDemuxer->Init();
                     })
    ->Then(taskQueue, __func__,
           [data, taskQueue]() {
             if (!data->mDemuxer) { // Was shutdown.
               return;
             }
             data->mNumAudioTrack =
               data->mDemuxer->GetNumberTracks(TrackInfo::kAudioTrack);
             if (data->mNumAudioTrack) {
               RefPtr<MediaTrackDemuxer> d =
                 data->mDemuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
               if (d) {
                 RefPtr<Wrapper> wrapper =
                   new DemuxerProxy::Wrapper(d, taskQueue);
                 wrapper->UpdateBuffered();
                 data->mAudioDemuxer = wrapper;
               }
             }
             data->mNumVideoTrack =
               data->mDemuxer->GetNumberTracks(TrackInfo::kVideoTrack);
             if (data->mNumVideoTrack) {
               RefPtr<MediaTrackDemuxer> d =
                 data->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
               if (d) {
                 RefPtr<Wrapper> wrapper =
                   new DemuxerProxy::Wrapper(d, taskQueue);
                 wrapper->UpdateBuffered();
                 data->mVideoDemuxer = wrapper;
               }
             }
             data->mCrypto = data->mDemuxer->GetCrypto();
             data->mSeekable = data->mDemuxer->IsSeekable();
             data->mSeekableOnlyInBufferedRange =
               data->mDemuxer->IsSeekableOnlyInBufferedRanges();
             data->mShouldComputeStartTime =
               data->mDemuxer->ShouldComputeStartTime();
             data->mInitDone = true;
           },
           []() {});
}

RefPtr<MediaFormatReader::NotifyDataArrivedPromise>
MediaFormatReader::DemuxerProxy::NotifyDataArrived()
{
  RefPtr<Data> data = mData;
  return InvokeAsync(mTaskQueue, __func__, [data]() {
    if (!data->mDemuxer) {
      // Was shutdown.
      return NotifyDataArrivedPromise::CreateAndReject(
        NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    }
    data->mDemuxer->NotifyDataArrived();
    if (data->mAudioDemuxer) {
      data->mAudioDemuxer->UpdateBuffered();
    }
    if (data->mVideoDemuxer) {
      data->mVideoDemuxer->UpdateBuffered();
    }
    return NotifyDataArrivedPromise::CreateAndResolve(true, __func__);
  });
}

MediaFormatReader::MediaFormatReader(AbstractMediaDecoder* aDecoder,
                                     MediaDataDemuxer* aDemuxer,
                                     VideoFrameContainer* aVideoFrameContainer)
  : MediaDecoderReader(aDecoder)
  , mAudio(this, MediaData::AUDIO_DATA,
           Preferences::GetUint("media.audio-max-decode-error", 3))
  , mVideo(this, MediaData::VIDEO_DATA,
           Preferences::GetUint("media.video-max-decode-error", 2))
  , mDemuxer(new DemuxerProxy(aDemuxer, aDecoder
                                        ? aDecoder->AbstractMainThread()
                                        : AbstractThread::MainThread()))
  , mDemuxerInitDone(false)
  , mLastReportedNumDecodedFrames(0)
  , mPreviousDecodedKeyframeTime_us(sNoPreviousDecodedKeyframe)
  , mInitDone(false)
  , mTrackDemuxersMayBlock(false)
  , mSeekScheduled(false)
  , mVideoFrameContainer(aVideoFrameContainer)
  , mDecoderFactory(new DecoderFactory(this))
  , mShutdownPromisePool(new ShutdownPromisePool())
{
  MOZ_ASSERT(aDemuxer);
  MOZ_COUNT_CTOR(MediaFormatReader);

  if (aDecoder && aDecoder->CompositorUpdatedEvent()) {
    mCompositorUpdatedListener =
      aDecoder->CompositorUpdatedEvent()->Connect(
        mTaskQueue, this, &MediaFormatReader::NotifyCompositorUpdated);
  }
  mOnTrackWaitingForKeyListener = OnTrackWaitingForKey().Connect(
    mTaskQueue, this, &MediaFormatReader::NotifyWaitingForKey);
}

MediaFormatReader::~MediaFormatReader()
{
  MOZ_COUNT_DTOR(MediaFormatReader);
}

RefPtr<ShutdownPromise>
MediaFormatReader::Shutdown()
{
  MOZ_ASSERT(OnTaskQueue());
  LOG("");

  mDemuxerInitRequest.DisconnectIfExists();
  mNotifyDataArrivedPromise.DisconnectIfExists();
  mMetadataPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mSeekPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mSkipRequest.DisconnectIfExists();

  if (mAudio.HasPromise()) {
    mAudio.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  }
  if (mVideo.HasPromise()) {
    mVideo.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  }

  if (HasAudio()) {
    mAudio.ResetDemuxer();
    mAudio.mTrackDemuxer->BreakCycles();
    mAudio.mTrackDemuxer = nullptr;
    mAudio.ResetState();
    mShutdownPromisePool->Track(ShutdownDecoderWithPromise(TrackInfo::kAudioTrack));
  }

  if (HasVideo()) {
    mVideo.ResetDemuxer();
    mVideo.mTrackDemuxer->BreakCycles();
    mVideo.mTrackDemuxer = nullptr;
    mVideo.ResetState();
    mShutdownPromisePool->Track(ShutdownDecoderWithPromise(TrackInfo::kVideoTrack));
  }

  mShutdownPromisePool->Track(mDemuxer->Shutdown());
  mDemuxer = nullptr;

  mCompositorUpdatedListener.DisconnectIfExists();
  mOnTrackWaitingForKeyListener.Disconnect();

  mShutdown = true;
  return mShutdownPromisePool->Shutdown()
    ->Then(OwnerThread(), __func__, this,
           &MediaFormatReader::TearDownDecoders,
           &MediaFormatReader::TearDownDecoders);
}

RefPtr<ShutdownPromise>
MediaFormatReader::ShutdownDecoderWithPromise(TrackType aTrack)
{
  LOGV("%s", TrackTypeToStr(aTrack));

  auto& decoder = GetDecoderData(aTrack);
  if (!decoder.mFlushed && decoder.mDecoder) {
    // The decoder has yet to be flushed.
    // We always flush the decoder prior to a shutdown to ensure that all the
    // potentially pending operations on the decoder are completed.
    decoder.Flush();
    return decoder.mShutdownPromise.Ensure(__func__);
  }

  if (decoder.mFlushRequest.Exists() || decoder.mShutdownRequest.Exists()) {
    // Let the current flush or shutdown operation complete, Flush will continue
    // shutting down the current decoder now that the shutdown promise is set.
    return decoder.mShutdownPromise.Ensure(__func__);
  }

  if (!decoder.mDecoder) {
    // Shutdown any decoders that may be in the process of being initialized
    // in the Decoder Factory.
    // This will be a no-op until we're processing the final decoder shutdown
    // prior to the MediaFormatReader being shutdown.
    mDecoderFactory->ShutdownDecoder(aTrack);
    return ShutdownPromise::CreateAndResolve(true, __func__);
  }

  // Finally, let's just shut down the currently active decoder.
  decoder.ShutdownDecoder();
  return decoder.mShutdownPromise.Ensure(__func__);
}

void
MediaFormatReader::ShutdownDecoder(TrackType aTrack)
{
  LOG("%s", TrackTypeToStr(aTrack));
  auto& decoder = GetDecoderData(aTrack);
  if (!decoder.mDecoder) {
    LOGV("Already shut down");
    return;
  }
  if (!decoder.mShutdownPromise.IsEmpty()) {
    LOGV("Shutdown already in progress");
    return;
  }
  Unused << ShutdownDecoderWithPromise(aTrack);
}

RefPtr<ShutdownPromise>
MediaFormatReader::TearDownDecoders()
{
  if (mAudio.mTaskQueue) {
    mAudio.mTaskQueue->BeginShutdown();
    mAudio.mTaskQueue->AwaitShutdownAndIdle();
    mAudio.mTaskQueue = nullptr;
  }
  if (mVideo.mTaskQueue) {
    mVideo.mTaskQueue->BeginShutdown();
    mVideo.mTaskQueue->AwaitShutdownAndIdle();
    mVideo.mTaskQueue = nullptr;
  }

  mDecoderFactory = nullptr;
  mPlatform = nullptr;
  mVideoFrameContainer = nullptr;

  return MediaDecoderReader::Shutdown();
}

void
MediaFormatReader::InitLayersBackendType()
{
  // Extract the layer manager backend type so that platform decoders
  // can determine whether it's worthwhile using hardware accelerated
  // video decoding.
  if (!mDecoder) {
    return;
  }
  MediaDecoderOwner* owner = mDecoder->GetOwner();
  if (!owner) {
    NS_WARNING("MediaFormatReader without a decoder owner, can't get HWAccel");
    return;
  }

  dom::HTMLMediaElement* element = owner->GetMediaElement();
  NS_ENSURE_TRUE_VOID(element);

  RefPtr<LayerManager> layerManager =
    nsContentUtils::LayerManagerForDocument(element->OwnerDoc());
  NS_ENSURE_TRUE_VOID(layerManager);

  mKnowsCompositor = layerManager->AsShadowForwarder();
}

nsresult
MediaFormatReader::InitInternal()
{
  MOZ_ASSERT(NS_IsMainThread(), "Must be on main thread.");

  InitLayersBackendType();

  mAudio.mTaskQueue =
    new TaskQueue(GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER));
  mVideo.mTaskQueue =
    new TaskQueue(GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER));

  if (mDecoder) {
    // Note: GMPCrashHelper must be created on main thread, as it may use
    // weak references, which aren't threadsafe.
    mCrashHelper = mDecoder->GetCrashHelper();
  }
  return NS_OK;
}

class DispatchKeyNeededEvent : public Runnable
{
public:
  DispatchKeyNeededEvent(AbstractMediaDecoder* aDecoder,
                         nsTArray<uint8_t>& aInitData,
                         const nsString& aInitDataType)
    : mDecoder(aDecoder)
    , mInitData(aInitData)
    , mInitDataType(aInitDataType)
  {
  }
  NS_IMETHOD Run() override
  {
    // Note: Null check the owner, as the decoder could have been shutdown
    // since this event was dispatched.
    MediaDecoderOwner* owner = mDecoder->GetOwner();
    if (owner) {
      owner->DispatchEncrypted(mInitData, mInitDataType);
    }
    mDecoder = nullptr;
    return NS_OK;
  }
private:
  RefPtr<AbstractMediaDecoder> mDecoder;
  nsTArray<uint8_t> mInitData;
  nsString mInitDataType;
};

void
MediaFormatReader::SetCDMProxy(CDMProxy* aProxy)
{
  RefPtr<CDMProxy> proxy = aProxy;
  RefPtr<MediaFormatReader> self = this;
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction([=] () {
    MOZ_ASSERT(self->OnTaskQueue());
    self->mCDMProxy = proxy;
  });
  OwnerThread()->Dispatch(r.forget());
}

bool
MediaFormatReader::IsWaitingOnCDMResource()
{
  MOZ_ASSERT(OnTaskQueue());
  return IsEncrypted() && !mCDMProxy;
}

RefPtr<MediaDecoderReader::MetadataPromise>
MediaFormatReader::AsyncReadMetadata()
{
  MOZ_ASSERT(OnTaskQueue());

  MOZ_DIAGNOSTIC_ASSERT(mMetadataPromise.IsEmpty());

  if (mInitDone) {
    // We are returning from dormant.
    RefPtr<MetadataHolder> metadata = new MetadataHolder();
    metadata->mInfo = mInfo;
    metadata->mTags = nullptr;
    return MetadataPromise::CreateAndResolve(metadata, __func__);
  }

  RefPtr<MetadataPromise> p = mMetadataPromise.Ensure(__func__);

  mDemuxer->Init()
    ->Then(OwnerThread(), __func__, this,
           &MediaFormatReader::OnDemuxerInitDone,
           &MediaFormatReader::OnDemuxerInitFailed)
    ->Track(mDemuxerInitRequest);
  return p;
}

void
MediaFormatReader::OnDemuxerInitDone(nsresult)
{
  MOZ_ASSERT(OnTaskQueue());
  mDemuxerInitRequest.Complete();

  mDemuxerInitDone = true;

  UniquePtr<MetadataTags> tags(MakeUnique<MetadataTags>());

  RefPtr<PDMFactory> platform;
  if (!IsWaitingOnCDMResource()) {
    platform = new PDMFactory();
  }

  // To decode, we need valid video and a place to put it.
  bool videoActive =
    !!mDemuxer->GetNumberTracks(TrackInfo::kVideoTrack) && GetImageContainer();

  if (videoActive) {
    // We currently only handle the first video track.
    mVideo.mTrackDemuxer = mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    if (!mVideo.mTrackDemuxer) {
      mMetadataPromise.Reject(NS_ERROR_DOM_MEDIA_METADATA_ERR, __func__);
      return;
    }

    UniquePtr<TrackInfo> videoInfo = mVideo.mTrackDemuxer->GetInfo();
    videoActive = videoInfo && videoInfo->IsValid();
    if (videoActive) {
      if (platform
          && !platform->SupportsMimeType(videoInfo->mMimeType, nullptr)) {
        // We have no decoder for this track. Error.
        mMetadataPromise.Reject(NS_ERROR_DOM_MEDIA_METADATA_ERR, __func__);
        return;
      }
      mInfo.mVideo = *videoInfo->GetAsVideoInfo();
      for (const MetadataTag& tag : videoInfo->mTags) {
        tags->Put(tag.mKey, tag.mValue);
      }
      mVideo.mOriginalInfo = Move(videoInfo);
      mTrackDemuxersMayBlock |= mVideo.mTrackDemuxer->GetSamplesMayBlock();
    } else {
      mVideo.mTrackDemuxer->BreakCycles();
      mVideo.mTrackDemuxer = nullptr;
    }
  }

  bool audioActive = !!mDemuxer->GetNumberTracks(TrackInfo::kAudioTrack);
  if (audioActive) {
    mAudio.mTrackDemuxer = mDemuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
    if (!mAudio.mTrackDemuxer) {
      mMetadataPromise.Reject(NS_ERROR_DOM_MEDIA_METADATA_ERR, __func__);
      return;
    }

    UniquePtr<TrackInfo> audioInfo = mAudio.mTrackDemuxer->GetInfo();
    // We actively ignore audio tracks that we know we can't play.
    audioActive =
      audioInfo
      && audioInfo->IsValid()
      && (!platform || platform->SupportsMimeType(audioInfo->mMimeType,
                                                  nullptr));

    if (audioActive) {
      mInfo.mAudio = *audioInfo->GetAsAudioInfo();
      for (const MetadataTag& tag : audioInfo->mTags) {
        tags->Put(tag.mKey, tag.mValue);
      }
      mAudio.mOriginalInfo = Move(audioInfo);
      mTrackDemuxersMayBlock |= mAudio.mTrackDemuxer->GetSamplesMayBlock();
    } else {
      mAudio.mTrackDemuxer->BreakCycles();
      mAudio.mTrackDemuxer = nullptr;
    }
  }

  UniquePtr<EncryptionInfo> crypto = mDemuxer->GetCrypto();
  if (mDecoder && crypto && crypto->IsEncrypted()) {
    // Try and dispatch 'encrypted'. Won't go if ready state still HAVE_NOTHING.
    for (uint32_t i = 0; i < crypto->mInitDatas.Length(); i++) {
      NS_DispatchToMainThread(
        new DispatchKeyNeededEvent(mDecoder, crypto->mInitDatas[i].mInitData,
                                   crypto->mInitDatas[i].mType));
    }
    mInfo.mCrypto = *crypto;
  }

  int64_t videoDuration = HasVideo() ? mInfo.mVideo.mDuration : 0;
  int64_t audioDuration = HasAudio() ? mInfo.mAudio.mDuration : 0;

  int64_t duration = std::max(videoDuration, audioDuration);
  if (duration != -1) {
    mInfo.mMetadataDuration = Some(TimeUnit::FromMicroseconds(duration));
  }

  mInfo.mMediaSeekable = mDemuxer->IsSeekable();
  mInfo.mMediaSeekableOnlyInBufferedRanges =
    mDemuxer->IsSeekableOnlyInBufferedRanges();

  if (!videoActive && !audioActive) {
    mMetadataPromise.Reject(NS_ERROR_DOM_MEDIA_METADATA_ERR, __func__);
    return;
  }

  mTags = Move(tags);
  mInitDone = true;

  // Try to get the start time.
  // For MSE case, the start time of each track is assumed to be 0.
  // For others, we must demux the first sample to know the start time for each
  // track.
  if (!mDemuxer->ShouldComputeStartTime()) {
    mAudio.mFirstDemuxedSampleTime.emplace(TimeUnit::FromMicroseconds(0));
    mVideo.mFirstDemuxedSampleTime.emplace(TimeUnit::FromMicroseconds(0));
  } else {
    if (HasAudio()) {
      RequestDemuxSamples(TrackInfo::kAudioTrack);
    }

    if (HasVideo()) {
      RequestDemuxSamples(TrackInfo::kVideoTrack);
    }
  }

  MaybeResolveMetadataPromise();
}

void
MediaFormatReader::MaybeResolveMetadataPromise()
{
  MOZ_ASSERT(OnTaskQueue());

  if ((HasAudio() && mAudio.mFirstDemuxedSampleTime.isNothing())
      || (HasVideo() && mVideo.mFirstDemuxedSampleTime.isNothing())) {
    return;
  }

  TimeUnit startTime =
    std::min(mAudio.mFirstDemuxedSampleTime.refOr(TimeUnit::FromInfinity()),
             mVideo.mFirstDemuxedSampleTime.refOr(TimeUnit::FromInfinity()));

  if (!startTime.IsInfinite()) {
    mInfo.mStartTime = startTime; // mInfo.mStartTime is initialized to 0.
  }

  RefPtr<MetadataHolder> metadata = new MetadataHolder();
  metadata->mInfo = mInfo;
  metadata->mTags = mTags->Count() ? mTags.release() : nullptr;

  // We now have all the informations required to calculate the initial buffered
  // range.
  mHasStartTime = true;
  UpdateBuffered();

  mMetadataPromise.Resolve(metadata, __func__);
}

bool
MediaFormatReader::IsEncrypted() const
{
  return (HasAudio() && mInfo.mAudio.mCrypto.mValid)
         || (HasVideo() && mInfo.mVideo.mCrypto.mValid);
}

void
MediaFormatReader::OnDemuxerInitFailed(const MediaResult& aError)
{
  mDemuxerInitRequest.Complete();
  mMetadataPromise.Reject(aError, __func__);
}

void
MediaFormatReader::ReadUpdatedMetadata(MediaInfo* aInfo)
{
  *aInfo = mInfo;
}

MediaFormatReader::DecoderData&
MediaFormatReader::GetDecoderData(TrackType aTrack)
{
  MOZ_ASSERT(aTrack == TrackInfo::kAudioTrack
             || aTrack == TrackInfo::kVideoTrack);
  if (aTrack == TrackInfo::kAudioTrack) {
    return mAudio;
  }
  return mVideo;
}

bool
MediaFormatReader::ShouldSkip(bool aSkipToNextKeyframe,
                              media::TimeUnit aTimeThreshold)
{
  MOZ_ASSERT(HasVideo());
  media::TimeUnit nextKeyframe;
  nsresult rv = mVideo.mTrackDemuxer->GetNextRandomAccessPoint(&nextKeyframe);
  if (NS_FAILED(rv)) {
    return aSkipToNextKeyframe;
  }
  return (nextKeyframe < aTimeThreshold
          || (mVideo.mTimeThreshold
              && mVideo.mTimeThreshold.ref().EndTime() < aTimeThreshold))
         && nextKeyframe.ToMicroseconds() >= 0
         && !nextKeyframe.IsInfinite();
}

RefPtr<MediaDecoderReader::MediaDataPromise>
MediaFormatReader::RequestVideoData(bool aSkipToNextKeyframe,
                                    int64_t aTimeThreshold)
{
  MOZ_ASSERT(OnTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT(mSeekPromise.IsEmpty(),
                        "No sample requests allowed while seeking");
  MOZ_DIAGNOSTIC_ASSERT(!mVideo.HasPromise(), "No duplicate sample requests");
  MOZ_DIAGNOSTIC_ASSERT(!mVideo.mSeekRequest.Exists()
                        || mVideo.mTimeThreshold.isSome());
  MOZ_DIAGNOSTIC_ASSERT(!IsSeeking(), "called mid-seek");
  LOGV("RequestVideoData(%d, %" PRId64 ")", aSkipToNextKeyframe, aTimeThreshold);

  if (!HasVideo()) {
    LOG("called with no video track");
    return MediaDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                             __func__);
  }

  if (IsSeeking()) {
    LOG("called mid-seek. Rejecting.");
    return MediaDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
  }

  if (mShutdown) {
    NS_WARNING("RequestVideoData on shutdown MediaFormatReader!");
    return MediaDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
  }

  media::TimeUnit timeThreshold{ media::TimeUnit::FromMicroseconds(
    aTimeThreshold) };
  // Ensure we have no pending seek going as ShouldSkip could return out of date
  // information.
  if (!mVideo.HasInternalSeekPending()
      && ShouldSkip(aSkipToNextKeyframe, timeThreshold)) {
    RefPtr<MediaDataPromise> p = mVideo.EnsurePromise(__func__);
    SkipVideoDemuxToNextKeyFrame(timeThreshold);
    return p;
  }

  RefPtr<MediaDataPromise> p = mVideo.EnsurePromise(__func__);
  ScheduleUpdate(TrackInfo::kVideoTrack);

  return p;
}

void
MediaFormatReader::OnDemuxFailed(TrackType aTrack, const MediaResult& aError)
{
  MOZ_ASSERT(OnTaskQueue());
  LOG("Failed to demux %s, failure:%" PRIu32,
      aTrack == TrackType::kVideoTrack ? "video" : "audio",
      static_cast<uint32_t>(aError.Code()));
  auto& decoder = GetDecoderData(aTrack);
  decoder.mDemuxRequest.Complete();
  switch (aError.Code()) {
    case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
      if (!decoder.mWaitingForData) {
        decoder.RequestDrain();
      }
      NotifyEndOfStream(aTrack);
      break;
    case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
      if (!decoder.mWaitingForData) {
        decoder.RequestDrain();
      }
      NotifyWaitingForData(aTrack);
      break;
    case NS_ERROR_DOM_MEDIA_CANCELED:
      if (decoder.HasPromise()) {
        decoder.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
      }
      break;
    default:
      NotifyError(aTrack, aError);
      break;
  }
}

void
MediaFormatReader::DoDemuxVideo()
{
  auto p = mVideo.mTrackDemuxer->GetSamples(1);

  if (mVideo.mFirstDemuxedSampleTime.isNothing()) {
    RefPtr<MediaFormatReader> self = this;
    p = p->Then(OwnerThread(), __func__,
                [self] (RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples) {
                  self->OnFirstDemuxCompleted(TrackInfo::kVideoTrack, aSamples);
                },
                [self] (const MediaResult& aError) {
                  self->OnFirstDemuxFailed(TrackInfo::kVideoTrack, aError);
                });
  }

  p->Then(OwnerThread(), __func__, this,
          &MediaFormatReader::OnVideoDemuxCompleted,
          &MediaFormatReader::OnVideoDemuxFailed)
   ->Track(mVideo.mDemuxRequest);
}

void
MediaFormatReader::OnVideoDemuxCompleted(
  RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples)
{
  LOGV("%" PRIuSIZE " video samples demuxed (sid:%d)",
       aSamples->mSamples.Length(),
       aSamples->mSamples[0]->mTrackInfo
       ? aSamples->mSamples[0]->mTrackInfo->GetID()
       : 0);
  mVideo.mDemuxRequest.Complete();
  mVideo.mQueuedSamples.AppendElements(aSamples->mSamples);
  ScheduleUpdate(TrackInfo::kVideoTrack);
}

RefPtr<MediaDecoderReader::MediaDataPromise>
MediaFormatReader::RequestAudioData()
{
  MOZ_ASSERT(OnTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT(!mAudio.HasPromise(), "No duplicate sample requests");
  MOZ_DIAGNOSTIC_ASSERT(IsVideoSeeking() || mSeekPromise.IsEmpty(),
                        "No sample requests allowed while seeking");
  MOZ_DIAGNOSTIC_ASSERT(IsVideoSeeking()
                        || !mAudio.mSeekRequest.Exists()
                        || mAudio.mTimeThreshold.isSome());
  MOZ_DIAGNOSTIC_ASSERT(IsVideoSeeking() || !IsSeeking(), "called mid-seek");
  LOGV("");

  if (!HasAudio()) {
    LOG("called with no audio track");
    return MediaDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                             __func__);
  }

  if (IsSeeking()) {
    LOG("called mid-seek. Rejecting.");
    return MediaDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
  }

  if (mShutdown) {
    NS_WARNING("RequestAudioData on shutdown MediaFormatReader!");
    return MediaDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
  }

  RefPtr<MediaDataPromise> p = mAudio.EnsurePromise(__func__);
  ScheduleUpdate(TrackInfo::kAudioTrack);

  return p;
}

void
MediaFormatReader::DoDemuxAudio()
{
  auto p = mAudio.mTrackDemuxer->GetSamples(1);

  if (mAudio.mFirstDemuxedSampleTime.isNothing()) {
    RefPtr<MediaFormatReader> self = this;
    p = p->Then(OwnerThread(), __func__,
                [self] (RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples) {
                  self->OnFirstDemuxCompleted(TrackInfo::kAudioTrack, aSamples);
                },
                [self] (const MediaResult& aError) {
                  self->OnFirstDemuxFailed(TrackInfo::kAudioTrack, aError);
                });
  }

  p->Then(OwnerThread(), __func__, this,
          &MediaFormatReader::OnAudioDemuxCompleted,
          &MediaFormatReader::OnAudioDemuxFailed)
   ->Track(mAudio.mDemuxRequest);
}

void
MediaFormatReader::OnAudioDemuxCompleted(
  RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples)
{
  LOGV("%" PRIuSIZE " audio samples demuxed (sid:%d)",
       aSamples->mSamples.Length(),
       aSamples->mSamples[0]->mTrackInfo
       ? aSamples->mSamples[0]->mTrackInfo->GetID()
       : 0);
  mAudio.mDemuxRequest.Complete();
  mAudio.mQueuedSamples.AppendElements(aSamples->mSamples);
  ScheduleUpdate(TrackInfo::kAudioTrack);
}

void
MediaFormatReader::NotifyNewOutput(
  TrackType aTrack, const MediaDataDecoder::DecodedData& aResults)
{
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  for (auto& sample : aResults) {
    LOGV("Received new %s sample time:%" PRId64 " duration:%" PRId64,
        TrackTypeToStr(aTrack), sample->mTime, sample->mDuration);
    decoder.mOutput.AppendElement(sample);
    decoder.mNumSamplesOutput++;
    decoder.mNumOfConsecutiveError = 0;
  }
  LOG("Done processing new %s samples", TrackTypeToStr(aTrack));
  ScheduleUpdate(aTrack);
}

void
MediaFormatReader::NotifyError(TrackType aTrack, const MediaResult& aError)
{
  MOZ_ASSERT(OnTaskQueue());
  NS_WARNING(aError.Description().get());
  LOGV("%s Decoding error", TrackTypeToStr(aTrack));
  auto& decoder = GetDecoderData(aTrack);
  decoder.mError = decoder.HasFatalError() ? decoder.mError : Some(aError);
  ScheduleUpdate(aTrack);
}

void
MediaFormatReader::NotifyWaitingForData(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  decoder.mWaitingForData = true;
  if (decoder.mTimeThreshold) {
    decoder.mTimeThreshold.ref().mWaiting = true;
  }
  ScheduleUpdate(aTrack);
}

void
MediaFormatReader::NotifyWaitingForKey(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  if (mDecoder) {
    mDecoder->NotifyWaitingForKey();
  }
  if (!decoder.mDecodeRequest.Exists()) {
    LOGV("WaitingForKey received while no pending decode. Ignoring");
  }
  decoder.mWaitingForKey = true;
  ScheduleUpdate(aTrack);
}

void
MediaFormatReader::NotifyEndOfStream(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  decoder.mDemuxEOS = true;
  ScheduleUpdate(aTrack);
}

bool
MediaFormatReader::NeedInput(DecoderData& aDecoder)
{
  // The decoder will not be fed a new raw sample until the current decoding
  // requests has completed.
  return
    (aDecoder.HasPromise() || aDecoder.mTimeThreshold.isSome())
    && !aDecoder.HasPendingDrain()
    && !aDecoder.HasFatalError()
    && !aDecoder.mDemuxRequest.Exists()
    && !aDecoder.mOutput.Length()
    && !aDecoder.HasInternalSeekPending()
    && !aDecoder.mDecodeRequest.Exists();
}

void
MediaFormatReader::ScheduleUpdate(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());
  if (mShutdown) {
    return;
  }
  auto& decoder = GetDecoderData(aTrack);
  if (decoder.mUpdateScheduled) {
    return;
  }
  LOGV("SchedulingUpdate(%s)", TrackTypeToStr(aTrack));
  decoder.mUpdateScheduled = true;
  RefPtr<nsIRunnable> task(
    NewRunnableMethod<TrackType>(this, &MediaFormatReader::Update, aTrack));
  OwnerThread()->Dispatch(task.forget());
}

bool
MediaFormatReader::UpdateReceivedNewData(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);

  if (!decoder.mReceivedNewData) {
    return false;
  }

  // We do not want to clear mWaitingForData while there are pending
  // demuxing or seeking operations that could affect the value of this flag.
  // This is in order to ensure that we will retry once they complete as we may
  // now have new data that could potentially allow those operations to
  // successfully complete if tried again.
  if (decoder.mSeekRequest.Exists()) {
    // Nothing more to do until this operation complete.
    return true;
  }

  if (aTrack == TrackType::kVideoTrack && mSkipRequest.Exists()) {
    LOGV("Skipping in progress, nothing more to do");
    return true;
  }

  if (decoder.mDemuxRequest.Exists()) {
    // We may have pending operations to process, so we want to continue
    // after UpdateReceivedNewData returns.
    return false;
  }

  if (decoder.HasPendingDrain()) {
    // We do not want to clear mWaitingForData or mDemuxEOS while
    // a drain is in progress in order to properly complete the operation.
    return false;
  }

  decoder.mReceivedNewData = false;
  if (decoder.mTimeThreshold) {
    decoder.mTimeThreshold.ref().mWaiting = false;
  }
  decoder.mWaitingForData = false;

  if (decoder.HasFatalError()) {
    return false;
  }

  if (!mSeekPromise.IsEmpty()
      && (!IsVideoSeeking() || aTrack == TrackInfo::kVideoTrack)) {
    MOZ_ASSERT(!decoder.HasPromise());
    MOZ_DIAGNOSTIC_ASSERT(
      (IsVideoSeeking() || !mAudio.mTimeThreshold) && !mVideo.mTimeThreshold,
      "InternalSeek must have been aborted when Seek was first called");
    MOZ_DIAGNOSTIC_ASSERT(
      (IsVideoSeeking() || !mAudio.HasWaitingPromise())
      && !mVideo.HasWaitingPromise(),
      "Waiting promises must have been rejected when Seek was first called");
    if (mVideo.mSeekRequest.Exists()
        || (!IsVideoSeeking() && mAudio.mSeekRequest.Exists())) {
      // Already waiting for a seek to complete. Nothing more to do.
      return true;
    }
    LOG("Attempting Seek");
    ScheduleSeek();
    return true;
  }
  if (decoder.HasInternalSeekPending() || decoder.HasWaitingPromise()) {
    if (decoder.HasInternalSeekPending()) {
      LOG("Attempting Internal Seek");
      InternalSeek(aTrack, decoder.mTimeThreshold.ref());
    }
    if (decoder.HasWaitingPromise() && !decoder.IsWaiting()) {
      MOZ_ASSERT(!decoder.HasPromise());
      LOG("We have new data. Resolving WaitingPromise");
      decoder.mWaitingPromise.Resolve(decoder.mType, __func__);
    }
    return true;
  }
  return false;
}

void
MediaFormatReader::RequestDemuxSamples(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  MOZ_ASSERT(!decoder.mDemuxRequest.Exists());

  if (!decoder.mQueuedSamples.IsEmpty()) {
    // No need to demux new samples.
    return;
  }

  if (decoder.mDemuxEOS) {
    // Nothing left to demux.
    // We do not want to attempt to demux while in waiting for data mode
    // as it would retrigger an unecessary drain.
    return;
  }

  LOGV("Requesting extra demux %s", TrackTypeToStr(aTrack));
  if (aTrack == TrackInfo::kVideoTrack) {
    DoDemuxVideo();
  } else {
    DoDemuxAudio();
  }
}

void
MediaFormatReader::DecodeDemuxedSamples(TrackType aTrack,
                                        MediaRawData* aSample)
{
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  RefPtr<MediaFormatReader> self = this;
  decoder.mFlushed = false;
  decoder.mDecoder->Decode(aSample)
    ->Then(mTaskQueue, __func__,
           [self, this, aTrack, &decoder]
           (const MediaDataDecoder::DecodedData& aResults) {
             decoder.mDecodeRequest.Complete();
             NotifyNewOutput(aTrack, aResults);
           },
           [self, this, aTrack, &decoder](const MediaResult& aError) {
             decoder.mDecodeRequest.Complete();
             NotifyError(aTrack, aError);
           })
    ->Track(decoder.mDecodeRequest);
}

void
MediaFormatReader::HandleDemuxedSamples(
  TrackType aTrack, AbstractMediaDecoder::AutoNotifyDecoded& aA)
{
  MOZ_ASSERT(OnTaskQueue());

  auto& decoder = GetDecoderData(aTrack);

  if (decoder.mFlushRequest.Exists() || decoder.mShutdownRequest.Exists()) {
    LOGV("Decoder operation in progress, let it complete.");
    return;
  }

  if (decoder.mQueuedSamples.IsEmpty()) {
    return;
  }

  if (!decoder.mDecoder) {
    mDecoderFactory->CreateDecoder(aTrack);
    return;
  }

  LOGV("Giving %s input to decoder", TrackTypeToStr(aTrack));

  // Decode all our demuxed frames.
  while (decoder.mQueuedSamples.Length()) {
    RefPtr<MediaRawData> sample = decoder.mQueuedSamples[0];
    RefPtr<TrackInfoSharedPtr> info = sample->mTrackInfo;

    if (info && decoder.mLastStreamSourceID != info->GetID()) {
      if (decoder.mNextStreamSourceID.isNothing()
          || decoder.mNextStreamSourceID.ref() != info->GetID()) {
        LOG("%s stream id has changed from:%d to:%d, draining decoder.",
          TrackTypeToStr(aTrack), decoder.mLastStreamSourceID,
          info->GetID());
        decoder.RequestDrain();
        decoder.mNextStreamSourceID = Some(info->GetID());
        ScheduleUpdate(aTrack);
        return;
      }

      LOG("%s stream id has changed from:%d to:%d.",
          TrackTypeToStr(aTrack), decoder.mLastStreamSourceID,
          info->GetID());
      decoder.mLastStreamSourceID = info->GetID();
      decoder.mNextStreamSourceID.reset();

      if (!MediaPrefs::MediaDecoderCheckRecycling()
          || !decoder.mDecoder->SupportDecoderRecycling()) {
        LOG("Decoder does not support recycling, recreate decoder.");
        // If flushing is required, it will clear our array of queued samples.
        // So make a copy now.
        nsTArray<RefPtr<MediaRawData>> samples{ Move(decoder.mQueuedSamples) };
        ShutdownDecoder(aTrack);
        if (sample->mKeyframe) {
          decoder.mQueuedSamples.AppendElements(Move(samples));
        }
      } else if (decoder.mInfo && *decoder.mInfo != *info) {
        const TrackInfo* trackInfo = *info;
        decoder.mDecoder->ConfigurationChanged(*trackInfo);
      }

      decoder.mInfo = info;

      if (sample->mKeyframe) {
        ScheduleUpdate(aTrack);
      } else {
        TimeInterval time =
          TimeInterval(TimeUnit::FromMicroseconds(sample->mTime),
                       TimeUnit::FromMicroseconds(sample->GetEndTime()));
        InternalSeekTarget seekTarget =
          decoder.mTimeThreshold.refOr(InternalSeekTarget(time, false));
        LOG("Stream change occurred on a non-keyframe. Seeking to:%" PRId64,
            sample->mTime);
        InternalSeek(aTrack, seekTarget);
      }
      return;
    }

    LOGV("Input:%" PRId64 " (dts:%" PRId64 " kf:%d)",
         sample->mTime, sample->mTimecode, sample->mKeyframe);
    decoder.mNumSamplesInput++;
    decoder.mSizeOfQueue++;
    if (aTrack == TrackInfo::kVideoTrack) {
      aA.mStats.mParsedFrames++;
    }

    DecodeDemuxedSamples(aTrack, sample);

    decoder.mQueuedSamples.RemoveElementAt(0);
    break;
  }
}

void
MediaFormatReader::InternalSeek(TrackType aTrack,
                                const InternalSeekTarget& aTarget)
{
  MOZ_ASSERT(OnTaskQueue());
  LOG("%s internal seek to %f",
      TrackTypeToStr(aTrack), aTarget.Time().ToSeconds());

  auto& decoder = GetDecoderData(aTrack);
  decoder.Flush();
  decoder.ResetDemuxer();
  decoder.mTimeThreshold = Some(aTarget);
  RefPtr<MediaFormatReader> self = this;
  decoder.mTrackDemuxer->Seek(decoder.mTimeThreshold.ref().Time())
    ->Then(OwnerThread(), __func__,
           [self, aTrack] (media::TimeUnit aTime) {
             auto& decoder = self->GetDecoderData(aTrack);
             decoder.mSeekRequest.Complete();
             MOZ_ASSERT(
               decoder.mTimeThreshold,
               "Seek promise must be disconnected when timethreshold is reset");
             decoder.mTimeThreshold.ref().mHasSeeked = true;
             self->SetVideoDecodeThreshold();
             self->ScheduleUpdate(aTrack);
           },
           [self, aTrack] (const MediaResult& aError) {
             auto& decoder = self->GetDecoderData(aTrack);
             decoder.mSeekRequest.Complete();
             switch (aError.Code()) {
               case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
                 self->NotifyWaitingForData(aTrack);
                 break;
               case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
                 decoder.mTimeThreshold.reset();
                 self->NotifyEndOfStream(aTrack);
                 break;
               case NS_ERROR_DOM_MEDIA_CANCELED:
                 decoder.mTimeThreshold.reset();
                 break;
               default:
                 decoder.mTimeThreshold.reset();
                 self->NotifyError(aTrack, aError);
                 break;
             }
           })
    ->Track(decoder.mSeekRequest);
}

void
MediaFormatReader::DrainDecoder(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());

  auto& decoder = GetDecoderData(aTrack);
  if (decoder.mDrainState == DrainState::Draining) {
    return;
  }
  if (!decoder.mDecoder
      || (decoder.mDrainState != DrainState::PartialDrainPending
          && decoder.mNumSamplesInput == decoder.mNumSamplesOutput)) {
    // No frames to drain.
    LOGV("Draining %s with nothing to drain", TrackTypeToStr(aTrack));
    decoder.mDrainState = DrainState::DrainAborted;
    ScheduleUpdate(aTrack);
    return;
  }

  decoder.mDrainState = DrainState::Draining;

  RefPtr<MediaFormatReader> self = this;
  decoder.mDecoder->Drain()
    ->Then(mTaskQueue, __func__,
           [self, this, aTrack, &decoder]
           (const MediaDataDecoder::DecodedData& aResults) {
             decoder.mDrainRequest.Complete();
             if (aResults.IsEmpty()) {
               decoder.mDrainState = DrainState::DrainCompleted;
             } else {
               NotifyNewOutput(aTrack, aResults);
               // Let's see if we have any more data available to drain.
               decoder.mDrainState = DrainState::PartialDrainPending;
             }
             ScheduleUpdate(aTrack);
           },
           [self, this, aTrack, &decoder](const MediaResult& aError) {
             decoder.mDrainRequest.Complete();
             NotifyError(aTrack, aError);
           })
    ->Track(decoder.mDrainRequest);
  LOG("Requesting %s decoder to drain", TrackTypeToStr(aTrack));
}

void
MediaFormatReader::Update(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown) {
    return;
  }

  LOGV("Processing update for %s", TrackTypeToStr(aTrack));

  bool needOutput = false;
  auto& decoder = GetDecoderData(aTrack);
  decoder.mUpdateScheduled = false;

  if (!mInitDone) {
    return;
  }

  if (aTrack == TrackType::kVideoTrack && mSkipRequest.Exists()) {
    LOGV("Skipping in progress, nothing more to do");
    return;
  }

  if (UpdateReceivedNewData(aTrack)) {
    LOGV("Nothing more to do");
    return;
  }

  if (decoder.mSeekRequest.Exists()) {
    LOGV("Seeking hasn't completed, nothing more to do");
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(
    !decoder.HasInternalSeekPending()
    || (!decoder.mOutput.Length() && !decoder.mQueuedSamples.Length()),
    "No frames can be demuxed or decoded while an internal seek is pending");

  // Record number of frames decoded and parsed. Automatically update the
  // stats counters using the AutoNotifyDecoded stack-based class.
  AbstractMediaDecoder::AutoNotifyDecoded a(mDecoder);

  // Drop any frames found prior our internal seek target.
  while (decoder.mTimeThreshold && decoder.mOutput.Length()) {
    RefPtr<MediaData>& output = decoder.mOutput[0];
    InternalSeekTarget target = decoder.mTimeThreshold.ref();
    media::TimeUnit time = media::TimeUnit::FromMicroseconds(output->mTime);
    if (time >= target.Time()) {
      // We have reached our internal seek target.
      decoder.mTimeThreshold.reset();
      // We might have dropped some keyframes.
      mPreviousDecodedKeyframeTime_us = sNoPreviousDecodedKeyframe;
    }
    if (time < target.Time() || (target.mDropTarget && target.Contains(time))) {
      LOGV("Internal Seeking: Dropping %s frame time:%f wanted:%f (kf:%d)",
           TrackTypeToStr(aTrack),
           media::TimeUnit::FromMicroseconds(output->mTime).ToSeconds(),
           target.Time().ToSeconds(),
           output->mKeyframe);
      decoder.mOutput.RemoveElementAt(0);
      decoder.mSizeOfQueue -= 1;
    }
  }

  while (decoder.mOutput.Length()
         && decoder.mOutput[0]->mType == MediaData::NULL_DATA) {
    LOGV("Dropping null data. Time: %" PRId64, decoder.mOutput[0]->mTime);
    decoder.mOutput.RemoveElementAt(0);
    decoder.mSizeOfQueue -= 1;
  }

  if (decoder.HasPromise()) {
    needOutput = true;
    if (decoder.mOutput.Length()) {
      RefPtr<MediaData> output = decoder.mOutput[0];
      decoder.mOutput.RemoveElementAt(0);
      decoder.mSizeOfQueue -= 1;
      decoder.mLastSampleTime =
        Some(TimeInterval(TimeUnit::FromMicroseconds(output->mTime),
                          TimeUnit::FromMicroseconds(output->GetEndTime())));
      decoder.mNumSamplesOutputTotal++;
      ReturnOutput(output, aTrack);
      // We have a decoded sample ready to be returned.
      if (aTrack == TrackType::kVideoTrack) {
        uint64_t delta =
          decoder.mNumSamplesOutputTotal - mLastReportedNumDecodedFrames;
        a.mStats.mDecodedFrames = static_cast<uint32_t>(delta);
        mLastReportedNumDecodedFrames = decoder.mNumSamplesOutputTotal;
        if (output->mKeyframe) {
          if (mPreviousDecodedKeyframeTime_us < output->mTime) {
            // There is a previous keyframe -> Record inter-keyframe stats.
            uint64_t segment_us =
              output->mTime - mPreviousDecodedKeyframeTime_us;
            a.mStats.mInterKeyframeSum_us += segment_us;
            a.mStats.mInterKeyframeCount += 1;
            if (a.mStats.mInterKeyFrameMax_us < segment_us) {
              a.mStats.mInterKeyFrameMax_us = segment_us;
            }
          }
          mPreviousDecodedKeyframeTime_us = output->mTime;
        }
        nsCString error;
        mVideo.mIsHardwareAccelerated =
          mVideo.mDecoder && mVideo.mDecoder->IsHardwareAccelerated(error);
      }
    } else if (decoder.HasFatalError()) {
      LOG("Rejecting %s promise: DECODE_ERROR", TrackTypeToStr(aTrack));
      decoder.RejectPromise(decoder.mError.ref(), __func__);
      return;
    } else if (decoder.mDrainState == DrainState::DrainCompleted
               || decoder.mDrainState == DrainState::DrainAborted) {
      if (decoder.mDemuxEOS) {
        LOG("Rejecting %s promise: EOS", TrackTypeToStr(aTrack));
        decoder.RejectPromise(NS_ERROR_DOM_MEDIA_END_OF_STREAM, __func__);
      } else if (decoder.mWaitingForData) {
        if (decoder.mDrainState == DrainState::DrainCompleted
            && decoder.mLastSampleTime
            && !decoder.mNextStreamSourceID) {
          // We have completed draining the decoder following WaitingForData.
          // Set up the internal seek machinery to be able to resume from the
          // last sample decoded.
          LOG("Seeking to last sample time: %" PRId64,
              decoder.mLastSampleTime.ref().mStart.ToMicroseconds());
          InternalSeek(aTrack,
                       InternalSeekTarget(decoder.mLastSampleTime.ref(), true));
        }
        if (!decoder.mReceivedNewData) {
          LOG("Rejecting %s promise: WAITING_FOR_DATA", TrackTypeToStr(aTrack));
          decoder.RejectPromise(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
        }
      }

      decoder.mDrainState = DrainState::None;

      // Now that draining has completed, we check if we have received
      // new data again as the result may now be different from the earlier
      // run.
      if (UpdateReceivedNewData(aTrack) || decoder.mSeekRequest.Exists()) {
        LOGV("Nothing more to do");
        return;
      }
    } else if (decoder.mDemuxEOS
               && !decoder.HasPendingDrain()
               && decoder.mQueuedSamples.IsEmpty()) {
      // It is possible to transition from WAITING_FOR_DATA directly to EOS
      // state during the internal seek; in which case no draining would occur.
      // There is no more samples left to be decoded and we are already in
      // EOS state. We can immediately reject the data promise.
      LOG("Rejecting %s promise: EOS", TrackTypeToStr(aTrack));
      decoder.RejectPromise(NS_ERROR_DOM_MEDIA_END_OF_STREAM, __func__);
    } else if (decoder.mWaitingForKey) {
      LOG("Rejecting %s promise: WAITING_FOR_DATA due to waiting for key",
          TrackTypeToStr(aTrack));
      decoder.RejectPromise(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    }
  }

  if (decoder.mDrainState == DrainState::DrainRequested
      || decoder.mDrainState == DrainState::PartialDrainPending) {
    if (decoder.mOutput.IsEmpty()) {
      DrainDecoder(aTrack);
    }
    return;
  }

  if (decoder.mError && !decoder.HasFatalError()) {
    bool needsNewDecoder =
      decoder.mError.ref() == NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER;
    if (!needsNewDecoder
        && ++decoder.mNumOfConsecutiveError > decoder.mMaxConsecutiveError) {
      NotifyError(aTrack, decoder.mError.ref());
      return;
    }
    decoder.mError.reset();
    LOG("%s decoded error count %d", TrackTypeToStr(aTrack),
                                     decoder.mNumOfConsecutiveError);
    media::TimeUnit nextKeyframe;
    if (aTrack == TrackType::kVideoTrack && !decoder.HasInternalSeekPending()
        && NS_SUCCEEDED(
             decoder.mTrackDemuxer->GetNextRandomAccessPoint(&nextKeyframe))) {
      if (needsNewDecoder) {
        ShutdownDecoder(aTrack);
      }
      SkipVideoDemuxToNextKeyFrame(
        decoder.mLastSampleTime.refOr(TimeInterval()).Length());
    } else if (aTrack == TrackType::kAudioTrack) {
      decoder.Flush();
    }
    return;
  }

  bool needInput = NeedInput(decoder);

  LOGV("Update(%s) ni=%d no=%d in:%" PRIu64 " out:%" PRIu64
       " qs=%u decoding:%d flushing:%d "
       "shutdown:%d pending:%u waiting:%d promise:%d sid:%u",
       TrackTypeToStr(aTrack), needInput, needOutput, decoder.mNumSamplesInput,
       decoder.mNumSamplesOutput, uint32_t(size_t(decoder.mSizeOfQueue)),
       decoder.mDecodeRequest.Exists(), decoder.mFlushRequest.Exists(),
       decoder.mShutdownRequest.Exists(), uint32_t(decoder.mOutput.Length()),
       decoder.mWaitingForData, decoder.HasPromise(),
       decoder.mLastStreamSourceID);

  if ((decoder.mWaitingForData
       && (!decoder.mTimeThreshold || decoder.mTimeThreshold.ref().mWaiting))
      || (decoder.mWaitingForKey && decoder.mDecodeRequest.Exists())) {
    // Nothing more we can do at present.
    LOGV("Still waiting for data or key.");
    return;
  }

  if (decoder.mWaitingForKey) {
    decoder.mWaitingForKey = false;
    if (decoder.HasWaitingPromise() && !decoder.IsWaiting()) {
      LOGV("No longer waiting for key. Resolving waiting promise");
      decoder.mWaitingPromise.Resolve(decoder.mType, __func__);
      return;
    }
  }

  if (!needInput) {
    LOGV("No need for additional input (pending:%u)",
         uint32_t(decoder.mOutput.Length()));
    return;
  }

  // Demux samples if we don't have some.
  RequestDemuxSamples(aTrack);

  HandleDemuxedSamples(aTrack, a);
}

void
MediaFormatReader::ReturnOutput(MediaData* aData, TrackType aTrack)
{
  MOZ_ASSERT(GetDecoderData(aTrack).HasPromise());
  MOZ_DIAGNOSTIC_ASSERT(aData->mType != MediaData::NULL_DATA);
  LOG("Resolved data promise for %s [%" PRId64 ", %" PRId64 "]", TrackTypeToStr(aTrack),
      aData->mTime, aData->GetEndTime());

  if (aTrack == TrackInfo::kAudioTrack) {
    AudioData* audioData = static_cast<AudioData*>(aData);

    if (audioData->mChannels != mInfo.mAudio.mChannels
        || audioData->mRate != mInfo.mAudio.mRate) {
      LOG("change of audio format (rate:%d->%d). "
          "This is an unsupported configuration",
          mInfo.mAudio.mRate, audioData->mRate);
      mInfo.mAudio.mRate = audioData->mRate;
      mInfo.mAudio.mChannels = audioData->mChannels;
    }
    mAudio.ResolvePromise(aData, __func__);
  } else if (aTrack == TrackInfo::kVideoTrack) {
    VideoData* videoData = static_cast<VideoData*>(aData);

    if (videoData->mDisplay != mInfo.mVideo.mDisplay) {
      LOG("change of video display size (%dx%d->%dx%d)",
          mInfo.mVideo.mDisplay.width, mInfo.mVideo.mDisplay.height,
          videoData->mDisplay.width, videoData->mDisplay.height);
      mInfo.mVideo.mDisplay = videoData->mDisplay;
    }
    mVideo.ResolvePromise(aData, __func__);
  }
}

size_t
MediaFormatReader::SizeOfVideoQueueInFrames()
{
  return SizeOfQueue(TrackInfo::kVideoTrack);
}

size_t
MediaFormatReader::SizeOfAudioQueueInFrames()
{
  return SizeOfQueue(TrackInfo::kAudioTrack);
}

size_t
MediaFormatReader::SizeOfQueue(TrackType aTrack)
{
  auto& decoder = GetDecoderData(aTrack);
  return decoder.mSizeOfQueue;
}

RefPtr<MediaDecoderReader::WaitForDataPromise>
MediaFormatReader::WaitForData(MediaData::Type aType)
{
  MOZ_ASSERT(OnTaskQueue());
  TrackType trackType = aType == MediaData::VIDEO_DATA ?
    TrackType::kVideoTrack : TrackType::kAudioTrack;
  auto& decoder = GetDecoderData(trackType);
  if (!decoder.IsWaiting()) {
    // We aren't waiting for anything.
    return WaitForDataPromise::CreateAndResolve(decoder.mType, __func__);
  }
  RefPtr<WaitForDataPromise> p = decoder.mWaitingPromise.Ensure(__func__);
  ScheduleUpdate(trackType);
  return p;
}

nsresult
MediaFormatReader::ResetDecode(TrackSet aTracks)
{
  MOZ_ASSERT(OnTaskQueue());
  LOGV("");

  mSeekPromise.RejectIfExists(NS_OK, __func__);
  mSkipRequest.DisconnectIfExists();

  // Do the same for any data wait promises.
  if (aTracks.contains(TrackInfo::kAudioTrack)) {
    mAudio.mWaitingPromise.RejectIfExists(
        WaitForDataRejectValue(MediaData::AUDIO_DATA,
                               WaitForDataRejectValue::CANCELED), __func__);
  }

  if (aTracks.contains(TrackInfo::kVideoTrack)) {
    mVideo.mWaitingPromise.RejectIfExists(
        WaitForDataRejectValue(MediaData::VIDEO_DATA,
                               WaitForDataRejectValue::CANCELED), __func__);
  }

  // Reset miscellaneous seeking state.
  mPendingSeekTime.reset();

  if (HasVideo() && aTracks.contains(TrackInfo::kVideoTrack)) {
    mVideo.ResetDemuxer();
    Reset(TrackInfo::kVideoTrack);
    if (mVideo.HasPromise()) {
      mVideo.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    }
  }

  if (HasAudio() && aTracks.contains(TrackInfo::kAudioTrack)) {
    mAudio.ResetDemuxer();
    Reset(TrackInfo::kAudioTrack);
    if (mAudio.HasPromise()) {
      mAudio.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    }
  }

  return MediaDecoderReader::ResetDecode(aTracks);
}

void
MediaFormatReader::Reset(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());
  LOG("Reset(%s) BEGIN", TrackTypeToStr(aTrack));

  auto& decoder = GetDecoderData(aTrack);

  decoder.ResetState();
  decoder.Flush();

  LOG("Reset(%s) END", TrackTypeToStr(aTrack));
}

void
MediaFormatReader::DropDecodedSamples(TrackType aTrack)
{
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  size_t lengthDecodedQueue = decoder.mOutput.Length();
  if (lengthDecodedQueue && decoder.mTimeThreshold.isSome()) {
    TimeUnit time =
      TimeUnit::FromMicroseconds(decoder.mOutput.LastElement()->mTime);
    if (time >= decoder.mTimeThreshold.ref().Time()) {
      // We would have reached our internal seek target.
      decoder.mTimeThreshold.reset();
    }
  }
  decoder.mOutput.Clear();
  decoder.mSizeOfQueue -= lengthDecodedQueue;
  if (aTrack == TrackInfo::kVideoTrack && mDecoder) {
    mDecoder->NotifyDecodedFrames({ 0, 0, lengthDecodedQueue });
  }
}

void
MediaFormatReader::SkipVideoDemuxToNextKeyFrame(media::TimeUnit aTimeThreshold)
{
  MOZ_ASSERT(OnTaskQueue());
  LOG("Skipping up to %" PRId64, aTimeThreshold.ToMicroseconds());

  // We've reached SkipVideoDemuxToNextKeyFrame when our decoding is late.
  // As such we can drop all already decoded samples and discard all pending
  // samples.
  DropDecodedSamples(TrackInfo::kVideoTrack);

  mVideo.mTrackDemuxer->SkipToNextRandomAccessPoint(aTimeThreshold)
    ->Then(OwnerThread(), __func__, this,
           &MediaFormatReader::OnVideoSkipCompleted,
           &MediaFormatReader::OnVideoSkipFailed)
    ->Track(mSkipRequest);
  return;
}

void
MediaFormatReader::VideoSkipReset(uint32_t aSkipped)
{
  MOZ_ASSERT(OnTaskQueue());

  // Some frames may have been output by the decoder since we initiated the
  // videoskip process and we know they would be late.
  DropDecodedSamples(TrackInfo::kVideoTrack);
  // Report the pending frames as dropped.
  if (mDecoder) {
    mDecoder->NotifyDecodedFrames({ 0, 0, SizeOfVideoQueueInFrames() });
  }

  // Cancel any pending demux request and pending demuxed samples.
  mVideo.mDemuxRequest.DisconnectIfExists();
  Reset(TrackType::kVideoTrack);

  if (mDecoder) {
    mDecoder->NotifyDecodedFrames({ aSkipped, 0, aSkipped });
  }

  mVideo.mNumSamplesSkippedTotal += aSkipped;
}

void
MediaFormatReader::OnVideoSkipCompleted(uint32_t aSkipped)
{
  MOZ_ASSERT(OnTaskQueue());
  LOG("Skipping succeeded, skipped %u frames", aSkipped);
  mSkipRequest.Complete();

  VideoSkipReset(aSkipped);

  ScheduleUpdate(TrackInfo::kVideoTrack);
}

void
MediaFormatReader::OnVideoSkipFailed(
  MediaTrackDemuxer::SkipFailureHolder aFailure)
{
  MOZ_ASSERT(OnTaskQueue());
  LOG("Skipping failed, skipped %u frames", aFailure.mSkipped);
  mSkipRequest.Complete();

  switch (aFailure.mFailure.Code()) {
    case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
    case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
      // Some frames may have been output by the decoder since we initiated the
      // videoskip process and we know they would be late.
      DropDecodedSamples(TrackInfo::kVideoTrack);
      // We can't complete the skip operation, will just service a video frame
      // normally.
      ScheduleUpdate(TrackInfo::kVideoTrack);
      break;
    case NS_ERROR_DOM_MEDIA_CANCELED:
      if (mVideo.HasPromise()) {
        mVideo.RejectPromise(aFailure.mFailure, __func__);
      }
      break;
    default:
      NotifyError(TrackType::kVideoTrack, aFailure.mFailure);
      break;
  }
}

RefPtr<MediaDecoderReader::SeekPromise>
MediaFormatReader::Seek(const SeekTarget& aTarget)
{
  MOZ_ASSERT(OnTaskQueue());

  LOG("aTarget=(%" PRId64 ")", aTarget.GetTime().ToMicroseconds());

  MOZ_DIAGNOSTIC_ASSERT(mSeekPromise.IsEmpty());
  MOZ_DIAGNOSTIC_ASSERT(!mVideo.HasPromise());
  MOZ_DIAGNOSTIC_ASSERT(aTarget.IsVideoOnly() || !mAudio.HasPromise());
  MOZ_DIAGNOSTIC_ASSERT(mPendingSeekTime.isNothing());
  MOZ_DIAGNOSTIC_ASSERT(mVideo.mTimeThreshold.isNothing());
  MOZ_DIAGNOSTIC_ASSERT(aTarget.IsVideoOnly()
                        || mAudio.mTimeThreshold.isNothing());

  if (!mInfo.mMediaSeekable && !mInfo.mMediaSeekableOnlyInBufferedRanges) {
    LOG("Seek() END (Unseekable)");
    return SeekPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  if (mShutdown) {
    return SeekPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  SetSeekTarget(aTarget);

  RefPtr<SeekPromise> p = mSeekPromise.Ensure(__func__);

  ScheduleSeek();

  return p;
}

void
MediaFormatReader::SetSeekTarget(const SeekTarget& aTarget)
{
  MOZ_ASSERT(OnTaskQueue());

  mOriginalSeekTarget = aTarget;
  mFallbackSeekTime = mPendingSeekTime = Some(aTarget.GetTime());
}

void
MediaFormatReader::ScheduleSeek()
{
  if (mSeekScheduled) {
    return;
  }
  mSeekScheduled = true;
  OwnerThread()->Dispatch(
    NewRunnableMethod(this, &MediaFormatReader::AttemptSeek));
}

void
MediaFormatReader::AttemptSeek()
{
  MOZ_ASSERT(OnTaskQueue());

  mSeekScheduled = false;

  if (mPendingSeekTime.isNothing()) {
    return;
  }

  if (HasVideo()) {
    mVideo.ResetDemuxer();
    mVideo.ResetState();
  }

  // Don't reset the audio demuxer not state when seeking video only
  // as it will cause the audio to seek back to the beginning
  // resulting in out-of-sync audio from video.
  if (HasAudio() && !mOriginalSeekTarget.IsVideoOnly()) {
    mAudio.ResetDemuxer();
    mAudio.ResetState();
  }

  if (HasVideo()) {
    DoVideoSeek();
  } else if (HasAudio()) {
    DoAudioSeek();
  } else {
    MOZ_CRASH();
  }
}

void
MediaFormatReader::OnSeekFailed(TrackType aTrack, const MediaResult& aError)
{
  MOZ_ASSERT(OnTaskQueue());
  LOGV("%s failure:%" PRIu32, TrackTypeToStr(aTrack), static_cast<uint32_t>(aError.Code()));
  if (aTrack == TrackType::kVideoTrack) {
    mVideo.mSeekRequest.Complete();
  } else {
    mAudio.mSeekRequest.Complete();
  }

  if (aError == NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) {
    if (HasVideo()
        && aTrack == TrackType::kAudioTrack
        && mFallbackSeekTime.isSome()
        && mPendingSeekTime.ref() != mFallbackSeekTime.ref()) {
      // We have failed to seek audio where video seeked to earlier.
      // Attempt to seek instead to the closest point that we know we have in
      // order to limit A/V sync discrepency.

      // Ensure we have the most up to date buffered ranges.
      UpdateReceivedNewData(TrackType::kAudioTrack);
      Maybe<media::TimeUnit> nextSeekTime;
      // Find closest buffered time found after video seeked time.
      for (const auto& timeRange : mAudio.mTimeRanges) {
        if (timeRange.mStart >= mPendingSeekTime.ref()) {
          nextSeekTime.emplace(timeRange.mStart);
          break;
        }
      }
      if (nextSeekTime.isNothing()
          || nextSeekTime.ref() > mFallbackSeekTime.ref()) {
        nextSeekTime = Some(mFallbackSeekTime.ref());
        LOG("Unable to seek audio to video seek time. A/V sync may be broken");
      } else {
        mFallbackSeekTime.reset();
      }
      mPendingSeekTime = nextSeekTime;
      DoAudioSeek();
      return;
    }
    NotifyWaitingForData(aTrack);
  }
  MOZ_ASSERT(!mVideo.mSeekRequest.Exists() && !mAudio.mSeekRequest.Exists());
  mPendingSeekTime.reset();

  auto type = aTrack == TrackType::kAudioTrack ? MediaData::AUDIO_DATA
                                               : MediaData::VIDEO_DATA;
  mSeekPromise.Reject(SeekRejectValue(type, aError), __func__);
}

void
MediaFormatReader::DoVideoSeek()
{
  MOZ_ASSERT(mPendingSeekTime.isSome());
  LOGV("Seeking video to %" PRId64, mPendingSeekTime.ref().ToMicroseconds());
  media::TimeUnit seekTime = mPendingSeekTime.ref();
  mVideo.mTrackDemuxer->Seek(seekTime)
    ->Then(OwnerThread(), __func__, this,
           &MediaFormatReader::OnVideoSeekCompleted,
           &MediaFormatReader::OnVideoSeekFailed)
    ->Track(mVideo.mSeekRequest);
}

void
MediaFormatReader::OnVideoSeekCompleted(media::TimeUnit aTime)
{
  MOZ_ASSERT(OnTaskQueue());
  LOGV("Video seeked to %" PRId64, aTime.ToMicroseconds());
  mVideo.mSeekRequest.Complete();

  mPreviousDecodedKeyframeTime_us = sNoPreviousDecodedKeyframe;

  SetVideoDecodeThreshold();

  if (HasAudio() && !mOriginalSeekTarget.IsVideoOnly()) {
    MOZ_ASSERT(mPendingSeekTime.isSome());
    if (mOriginalSeekTarget.IsFast()) {
      // We are performing a fast seek. We need to seek audio to where the
      // video seeked to, to ensure proper A/V sync once playback resume.
      mPendingSeekTime = Some(aTime);
    }
    DoAudioSeek();
  } else {
    mPendingSeekTime.reset();
    mSeekPromise.Resolve(aTime, __func__);
  }
}

void
MediaFormatReader::OnVideoSeekFailed(const MediaResult& aError)
{
  mPreviousDecodedKeyframeTime_us = sNoPreviousDecodedKeyframe;
  OnSeekFailed(TrackType::kVideoTrack, aError);
}

void
MediaFormatReader::SetVideoDecodeThreshold()
{
  MOZ_ASSERT(OnTaskQueue());

  if (!HasVideo() || !mVideo.mDecoder) {
    return;
  }

  if (!mVideo.mTimeThreshold && !IsSeeking()) {
    return;
  }

  TimeUnit threshold;
  if (mVideo.mTimeThreshold) {
    // For internalSeek.
    threshold = mVideo.mTimeThreshold.ref().Time();
  } else if (IsSeeking()) {
    // If IsSeeking() is true, then video seek must have completed already.
    TimeUnit keyframe;
    if (NS_FAILED(mVideo.mTrackDemuxer->GetNextRandomAccessPoint(&keyframe))) {
      return;
    }

    // If the key frame is invalid/infinite, it means the target position is
    // closing to end of stream. We don't want to skip any frame at this point.
    if (!keyframe.IsValid() || keyframe.IsInfinite()) {
      return;
    }
    threshold = mOriginalSeekTarget.GetTime();
  } else {
    return;
  }

  LOG("Set seek threshold to %" PRId64, threshold.ToMicroseconds());
  mVideo.mDecoder->SetSeekThreshold(threshold);
}

void
MediaFormatReader::DoAudioSeek()
{
  MOZ_ASSERT(mPendingSeekTime.isSome());
  LOGV("Seeking audio to %" PRId64, mPendingSeekTime.ref().ToMicroseconds());
  media::TimeUnit seekTime = mPendingSeekTime.ref();
  mAudio.mTrackDemuxer->Seek(seekTime)
    ->Then(OwnerThread(), __func__, this,
           &MediaFormatReader::OnAudioSeekCompleted,
           &MediaFormatReader::OnAudioSeekFailed)
    ->Track(mAudio.mSeekRequest);
}

void
MediaFormatReader::OnAudioSeekCompleted(media::TimeUnit aTime)
{
  MOZ_ASSERT(OnTaskQueue());
  LOGV("Audio seeked to %" PRId64, aTime.ToMicroseconds());
  mAudio.mSeekRequest.Complete();
  mPendingSeekTime.reset();
  mSeekPromise.Resolve(aTime, __func__);
}

void
MediaFormatReader::OnAudioSeekFailed(const MediaResult& aError)
{
  OnSeekFailed(TrackType::kAudioTrack, aError);
}

void MediaFormatReader::ReleaseResources()
{
  LOGV("");
  if (mShutdown) {
    return;
  }
  ShutdownDecoder(TrackInfo::kAudioTrack);
  ShutdownDecoder(TrackInfo::kVideoTrack);
}

bool
MediaFormatReader::VideoIsHardwareAccelerated() const
{
  return mVideo.mIsHardwareAccelerated;
}

void
MediaFormatReader::NotifyTrackDemuxers()
{
  MOZ_ASSERT(OnTaskQueue());

  LOGV("");

  if (!mInitDone) {
    return;
  }

  if (HasVideo()) {
    mVideo.mReceivedNewData = true;
    ScheduleUpdate(TrackType::kVideoTrack);
  }
  if (HasAudio()) {
    mAudio.mReceivedNewData = true;
    ScheduleUpdate(TrackType::kAudioTrack);
  }
}

void
MediaFormatReader::NotifyDataArrived()
{
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown
      || !mDemuxer
      || (!mDemuxerInitDone && !mDemuxerInitRequest.Exists())) {
    return;
  }

  if (mNotifyDataArrivedPromise.Exists()) {
    // Already one in progress. Reschedule for later.
    RefPtr<nsIRunnable> task(
        NewRunnableMethod(this, &MediaFormatReader::NotifyDataArrived));
    OwnerThread()->Dispatch(task.forget());
    return;
  }

  RefPtr<MediaFormatReader> self = this;
  mDemuxer->NotifyDataArrived()
    ->Then(OwnerThread(), __func__,
           [self]() {
             self->mNotifyDataArrivedPromise.Complete();
             self->UpdateBuffered();
             self->NotifyTrackDemuxers();
           },
           [self]() { self->mNotifyDataArrivedPromise.Complete(); })
    ->Track(mNotifyDataArrivedPromise);
}

void
MediaFormatReader::UpdateBuffered()
{
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown) {
    return;
  }

  if (!mInitDone || !mHasStartTime) {
    mBuffered = TimeIntervals();
    return;
  }

  if (HasVideo()) {
    mVideo.mTimeRanges = mVideo.mTrackDemuxer->GetBuffered();
    bool hasLastEnd;
    media::TimeUnit lastEnd = mVideo.mTimeRanges.GetEnd(&hasLastEnd);
    if (hasLastEnd) {
      if (mVideo.mLastTimeRangesEnd
          && mVideo.mLastTimeRangesEnd.ref() < lastEnd) {
        // New data was added after our previous end, we can clear the EOS flag.
        mVideo.mDemuxEOS = false;
        ScheduleUpdate(TrackInfo::kVideoTrack);
      }
      mVideo.mLastTimeRangesEnd = Some(lastEnd);
    }
  }
  if (HasAudio()) {
    mAudio.mTimeRanges = mAudio.mTrackDemuxer->GetBuffered();
    bool hasLastEnd;
    media::TimeUnit lastEnd = mAudio.mTimeRanges.GetEnd(&hasLastEnd);
    if (hasLastEnd) {
      if (mAudio.mLastTimeRangesEnd
          && mAudio.mLastTimeRangesEnd.ref() < lastEnd) {
        // New data was added after our previous end, we can clear the EOS flag.
        mAudio.mDemuxEOS = false;
        ScheduleUpdate(TrackInfo::kAudioTrack);
      }
      mAudio.mLastTimeRangesEnd = Some(lastEnd);
    }
  }

  media::TimeIntervals intervals;
  if (HasAudio() && HasVideo()) {
    intervals = media::Intersection(mVideo.mTimeRanges, mAudio.mTimeRanges);
  } else if (HasAudio()) {
    intervals = mAudio.mTimeRanges;
  } else if (HasVideo()) {
    intervals = mVideo.mTimeRanges;
  }

  if (!intervals.Length()
      || intervals.GetStart() == media::TimeUnit::FromMicroseconds(0)) {
    // IntervalSet already starts at 0 or is empty, nothing to shift.
    mBuffered = intervals;
  } else {
    mBuffered =
      intervals.Shift(media::TimeUnit() - mInfo.mStartTime);
  }
}

layers::ImageContainer*
MediaFormatReader::GetImageContainer()
{
  return mVideoFrameContainer ? mVideoFrameContainer->GetImageContainer()
                              : nullptr;
}

void
MediaFormatReader::GetMozDebugReaderData(nsACString& aString)
{
  nsAutoCString result;
  const char* audioName = "unavailable";
  const char* videoName = audioName;

  if (HasAudio()) {
    MutexAutoLock lock(mAudio.mMutex);
    audioName = mAudio.mDescription;
  }
  if (HasVideo()) {
    MutexAutoLock mon(mVideo.mMutex);
    videoName = mVideo.mDescription;
  }

  result += nsPrintfCString("audio decoder: %s\n", audioName);
  result += nsPrintfCString("audio frames decoded: %" PRIu64 "\n",
                            mAudio.mNumSamplesOutputTotal);
  if (HasAudio()) {
    result += nsPrintfCString(
      "audio state: ni=%d no=%d demuxr:%d demuxq:%d tt:%f tths:%d in:%" PRIu64
      " out:%" PRIu64 " qs=%u pending:%u waiting:%d sid:%u\n",
      NeedInput(mAudio), mAudio.HasPromise(), mAudio.mDemuxRequest.Exists(),
      int(mAudio.mQueuedSamples.Length()),
      mAudio.mTimeThreshold ? mAudio.mTimeThreshold.ref().Time().ToSeconds()
                            : -1.0,
      mAudio.mTimeThreshold ? mAudio.mTimeThreshold.ref().mHasSeeked : -1,
      mAudio.mNumSamplesInput, mAudio.mNumSamplesOutput,
      unsigned(size_t(mAudio.mSizeOfQueue)), unsigned(mAudio.mOutput.Length()),
      mAudio.mWaitingForData, mAudio.mLastStreamSourceID);
  }
  result += nsPrintfCString("video decoder: %s\n", videoName);
  result +=
    nsPrintfCString("hardware video decoding: %s\n",
                    VideoIsHardwareAccelerated() ? "enabled" : "disabled");
  result += nsPrintfCString("video frames decoded: %" PRIu64 " (skipped:%" PRIu64 ")\n",
                            mVideo.mNumSamplesOutputTotal,
                            mVideo.mNumSamplesSkippedTotal);
  if (HasVideo()) {
    result += nsPrintfCString(
      "video state: ni=%d no=%d demuxr:%d demuxq:%d tt:%f tths:%d in:%" PRIu64
      " out:%" PRIu64 " qs=%u pending:%u waiting:%d sid:%u\n",
      NeedInput(mVideo), mVideo.HasPromise(), mVideo.mDemuxRequest.Exists(),
      int(mVideo.mQueuedSamples.Length()),
      mVideo.mTimeThreshold ? mVideo.mTimeThreshold.ref().Time().ToSeconds()
                            : -1.0,
      mVideo.mTimeThreshold ? mVideo.mTimeThreshold.ref().mHasSeeked : -1,
      mVideo.mNumSamplesInput, mVideo.mNumSamplesOutput,
      unsigned(size_t(mVideo.mSizeOfQueue)), unsigned(mVideo.mOutput.Length()),
      mVideo.mWaitingForData, mVideo.mLastStreamSourceID);
  }
  aString += result;
}

void
MediaFormatReader::SetVideoBlankDecode(bool aIsBlankDecode)
{
  MOZ_ASSERT(OnTaskQueue());
  return SetBlankDecode(TrackType::kVideoTrack, aIsBlankDecode);
}

void
MediaFormatReader::SetBlankDecode(TrackType aTrack, bool aIsBlankDecode)
{
  MOZ_ASSERT(OnTaskQueue());

  auto& decoder = GetDecoderData(aTrack);
  if (decoder.mIsBlankDecode == aIsBlankDecode) {
    return;
  }

  LOG("%s, decoder.mIsBlankDecode = %d => aIsBlankDecode = %d",
      TrackTypeToStr(aTrack), decoder.mIsBlankDecode, aIsBlankDecode);

  decoder.mIsBlankDecode = aIsBlankDecode;
  ShutdownDecoder(aTrack);
}

void
MediaFormatReader::OnFirstDemuxCompleted(
  TrackInfo::TrackType aType, RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples)
{
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown) {
    return;
  }

  auto& decoder = GetDecoderData(aType);
  MOZ_ASSERT(decoder.mFirstDemuxedSampleTime.isNothing());
  decoder.mFirstDemuxedSampleTime.emplace(
    TimeUnit::FromMicroseconds(aSamples->mSamples[0]->mTime));
  MaybeResolveMetadataPromise();
}

void
MediaFormatReader::OnFirstDemuxFailed(TrackInfo::TrackType aType,
                                      const MediaResult& aError)
{
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown) {
    return;
  }

  auto& decoder = GetDecoderData(aType);
  MOZ_ASSERT(decoder.mFirstDemuxedSampleTime.isNothing());
  decoder.mFirstDemuxedSampleTime.emplace(TimeUnit::FromInfinity());
  MaybeResolveMetadataPromise();
}

} // namespace mozilla
