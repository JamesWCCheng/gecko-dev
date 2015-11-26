/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPMediaRendererParent.h"
#include "mozilla/Logging.h"
#include "mozilla/unused.h"
#include "nsAutoRef.h"
#include "nsThreadUtils.h"
#include "GMPUtils.h"
#include "GMPVideoEncodedFrameImpl.h"
//#include "GMPVideoi420FrameImpl.h"
#include "GMPContentParent.h"
#include "GMPMessageUtils.h"
#include "mozilla/gmp/GMPTypes.h"

namespace mozilla {

#ifdef LOG
#undef LOG
#endif

extern LogModule* GetGMPLog(); //definition is in GMPService.cpp.

#define LOGD(msg) MOZ_LOG(GetGMPLog(), mozilla::LogLevel::Debug, msg) //PR_NewLogModule("GMP");
#define LOG(level, msg) MOZ_LOG(GetGMPLog(), (level), msg)

#ifdef __CLASS__
#undef __CLASS__
#endif
#define __CLASS__ "GMPMediaRendererParent"

namespace gmp {

// States:
// Initial: mIsAudioOpen&&mIsVideoOpen == false
//    on InitDecode success -> Open
//    on Shutdown -> Dead
// Open: mIsAudioOpen&&mIsVideoOpen == true
//    on Close -> Dead
//    on ActorDestroy -> Dead
//    on Shutdown -> Dead
// Dead: mIsAudioOpen&&mIsVideoOpen == false

GMPMediaRendererParent::GMPMediaRendererParent(GMPContentParent* aPlugin)
  : GMPSharedMemManager(aPlugin)
    , mIsVideoOpen(false)
    , mIsAudioOpen(false)
    , mShuttingDown(false)
    , mShuttingDownComplete(false)
    , mActorDestroyed(false)
    , mPlugin(aPlugin)
    , mCallback(nullptr)
    , mRenderHost(this)
    , mPluginId(aPlugin->GetPluginId())
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  MOZ_ASSERT(mPlugin);
}


GMPMediaRendererParent::~GMPMediaRendererParent()
{
  MOZ_COUNT_DTOR(GMPMediaRendererParent);
}

GMPVideoHostImpl&
GMPMediaRendererParent::Host()
{
  return mRenderHost;
}

void
GMPMediaRendererParent::Close()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  MOZ_ASSERT(!mPlugin || mPlugin->GMPThread() == NS_GetCurrentThread());
  // Consumer is done with us; we can shut down.  No more callbacks should
  // be made to mCallback.  Note: do this before Shutdown()!
  mCallback = nullptr;
  // Let Shutdown mark us as dead so it knows if we had been alive

  // In case this is the last reference
  RefPtr<GMPMediaRendererParent> kungfudeathgrip(this);
  Release();
  Shutdown();
}

nsresult
GMPMediaRendererParent::InitVideoRenderer(const GMPVideoCodec& aVideoCodecSettings,
                                          const nsTArray<uint8_t>& aCodecSpecific,
                                          int32_t aCoreCount,
                                          GMPRect aDisplayRect)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (mActorDestroyed) {
    NS_WARNING("Trying to use a destroyed GMP media renderer!");
    return NS_ERROR_FAILURE;
  }
  if (mIsVideoOpen) {
    NS_WARNING("Trying to re-init an in-use GMP media renderer!");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!SendInitVideoRenderer(aVideoCodecSettings,
                             aCodecSpecific,
                             aCoreCount,
                             aDisplayRect)) {
    return NS_ERROR_FAILURE;
  }
  mIsVideoOpen = true;

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::InitAudioRenderer(GMPAudioCodecType aCodecType,
                                          uint32_t aChannelCount,
                                          uint32_t aBitsPerChannel,
                                          uint32_t aSamplesPerSecond,
                                          nsTArray<uint8_t>& aExtraData)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (mActorDestroyed) {
    NS_WARNING("Trying to use a destroyed GMP media renderer!");
    return NS_ERROR_FAILURE;
  }
  if (mIsAudioOpen) {
    NS_WARNING("Trying to re-init an in-use GMP media renderer!");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  GMPAudioCodecData data;
  data.mCodecType() = aCodecType;
  data.mChannelCount() = aChannelCount;
  data.mBitsPerChannel() = aBitsPerChannel;
  data.mSamplesPerSecond() = aSamplesPerSecond;
  data.mExtraData() = aExtraData;

  if (!SendInitAudioRenderer(data)) {
    return NS_ERROR_FAILURE;
  }
  mIsAudioOpen = true;

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::SetRendererCallback(GMPMediaRendererCallbackProxy* aCallback)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (mActorDestroyed) {
    NS_WARNING("Trying to use a destroyed GMP media renderer!");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!aCallback) {
    return NS_ERROR_FAILURE;
  }
  mCallback = aCallback;

  return NS_OK;
}

nsresult
GMPMediaRendererParent::RenderVideoPacket(GMPUniquePtr<GMPVideoEncodedFrame> aInputFrame,
                                          bool aMissingFrames,
                                          const nsTArray<uint8_t>& aCodecSpecificInfo)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer for video");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  GMPUniquePtr<GMPVideoEncodedFrameImpl> inputFrameImpl(
    static_cast<GMPVideoEncodedFrameImpl*>(aInputFrame.release()));

  // Very rough kill-switch if the plugin stops processing.  If it's merely
  // hung and continues, we'll come back to life eventually.
  // 3* is because we're using 3 buffers per frame for i420 data for now.
  if ((NumInUse(GMPSharedMem::kGMPFrameData) > 3*GMPSharedMem::kGMPBufLimit) ||
      (NumInUse(GMPSharedMem::kGMPEncodedData) > GMPSharedMem::kGMPBufLimit)) {
    return NS_ERROR_FAILURE;
  }

  GMPVideoEncodedFrameData frameData;
  inputFrameImpl->RelinquishFrameData(frameData);

  if (!SendRenderVideoPacket(frameData,
                             aMissingFrames,
                             aCodecSpecificInfo)) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::RenderAudioPacket(GMPAudioSamplesImpl& aEncodedSamples)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen) {
    NS_WARNING("Trying to use a dead GMP Media Renderer for audio!");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  GMPAudioEncodedSampleData samples;
  aEncodedSamples.RelinquishData(samples);

  if (!SendRenderAudioPacket(samples)) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::SetPlaybackRate(double aRate)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!SendSetPlaybackRate(aRate)) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::SetVolume(double aVolume)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!SendSetVolume(aVolume)) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::SetPreservesPitch(bool aPreservesPitch)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!SendSetPreservesPitch(aPreservesPitch)) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

// Note: Consider keeping ActorDestroy sync'd up when making changes here.
nsresult
GMPMediaRendererParent::Shutdown()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  //[TODO] ??? two flag can merger?
  if (mShuttingDownComplete || mShuttingDown) {
    return NS_OK;
  }
  mShuttingDown = true;

  // Notify client we're gone!  Won't occur after Close()
  if (mCallback) {
    mCallback->Terminated();
    //cannot set to null since we need to notify shutdown complete through callback.
    //mCallback = nullptr;
  }

  mIsAudioOpen = false;
  mIsVideoOpen = false;

  if (!mActorDestroyed) {
    Unused << SendShutdown();
  }

  return NS_OK;
}

nsresult
GMPMediaRendererParent::Start(int64_t aStartTime)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!SendStart(aStartTime)) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::Stop()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!SendStop()) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::ContentChanged(int64_t aTime)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!SendContentChanged(aTime)) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

nsresult
GMPMediaRendererParent::SetDisplayPosition(GMPRect aDisplayRect)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  if (!SendSetDisplayPosition(aDisplayRect)) {
    return NS_ERROR_FAILURE;
  }

  // Async IPC, we don't have access to a return value.
  return NS_OK;
}

const nsCString&
GMPMediaRendererParent::GetDisplayName() const
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mIsAudioOpen || !mIsVideoOpen) {
    NS_WARNING("Trying to use an dead GMP Media Renderer");
  }

  MOZ_ASSERT(mPlugin->GMPThread() == NS_GetCurrentThread());

  return mPlugin->GetDisplayName();
}

bool
GMPMediaRendererParent::RecvTimeUpdate(const int64_t& aElapsedTime)
{
  LOGD(("%s::%s: %p, aElapsedTime = %ld", __CLASS__, __FUNCTION__, this, aElapsedTime));
  mCallback->TimeUpdate(aElapsedTime);
  return true;
}

bool
GMPMediaRendererParent::RecvAudioRenderReachedEOS()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mCallback) {
    return false;
  }

  mCallback->AudioRenderReachedEOS();
  return true;
}

bool
GMPMediaRendererParent::RecvVideoRenderReachedEOS()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mCallback) {
    return false;
  }

  mCallback->VideoRenderReachedEOS();
  return true;
}

bool
GMPMediaRendererParent::RecvAudioInputDataExhausted()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mCallback) {
    return false;
  }

  mCallback->AudioInputDataExhausted();
  return true;
}

bool
GMPMediaRendererParent::RecvVideoInputDataExhausted()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mCallback) {
    return false;
  }

  mCallback->VideoInputDataExhausted();
  return true;
}

bool
GMPMediaRendererParent::RecvShutdownComplete()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mCallback) {
    return false;
  }

  mShuttingDownComplete = true;

  mCallback->ShutdownComplete();

  //[TODO] timming?
  mCallback = nullptr;
  return true;
}

bool
GMPMediaRendererParent::RecvStopComplete()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mCallback) {
    return false;
  }

  mCallback->StopComplete();

  return true;
}

bool
GMPMediaRendererParent::RecvError(const GMPErr& aError)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mCallback) {
    return false;
  }
  LOGD(("%s::%s: %p, GMPErr = %d", __CLASS__, __FUNCTION__, this, aError));
  // Ignore any return code. It is OK for this to fail without killing the process.
  // Create an error message indicating that child GMPRendererChild have encountered
  // any error.
  auto errorMsg = nsCString("GMPRendererChild Internal Error.");
  mCallback->Error(aError, errorMsg.get(), errorMsg.Length());

  return true;
}

bool
GMPMediaRendererParent::RecvSetOverlayImageID(const uint32_t& aId)
{
  LOGD(("%s::%s: %p aId = %lu", __CLASS__, __FUNCTION__, this, aId));
  if (!mCallback) {
    return false;
  }

  mCallback->SetOverlayImageID(aId);
  return true;
}

// Note: Keep this sync'd up with Shutdown
void
GMPMediaRendererParent::ActorDestroy(ActorDestroyReason aWhy)
{
  LOGD(("%s::%s: %p, aWhy = %d", __CLASS__, __FUNCTION__, this, aWhy));
  mIsVideoOpen = false;
  mIsAudioOpen = false;
  mActorDestroyed = true;
  mRenderHost.DoneWithAPI();

  if (mCallback) {
    // May call Close() (and Shutdown()) immediately or with a delay
    mCallback->Terminated();
    mCallback = nullptr;
  }
  if (mPlugin) {
    // Ignore any return code. It is OK for this to fail without killing the process.
    mPlugin->MediaRendererDestroyed(this);
    mPlugin = nullptr;
  }
  mRenderHost.ActorDestroyed();
}

//[TODO] What's this for??
bool
GMPMediaRendererParent::RecvParentShmemForPool(Shmem&& aEncodedBuffer)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (aEncodedBuffer.IsWritable()) {
    mRenderHost.SharedMemMgr()->MgrDeallocShmem(GMPSharedMem::kGMPEncodedData,
                                               aEncodedBuffer);
  }
  return true;
}

//[TODO] What's this for??
bool
GMPMediaRendererParent::AnswerNeedShmem(const uint32_t& aFrameBufferSize,
                                       Shmem* aMem)
{
  LOGD(("%s::%s: %p, aFrameBufferSize = %lu", __CLASS__, __FUNCTION__, this, aFrameBufferSize));
  ipc::Shmem mem;

  if (!mRenderHost.SharedMemMgr()->MgrAllocShmem(GMPSharedMem::kGMPFrameData,
                                                aFrameBufferSize,
                                                ipc::SharedMemory::TYPE_BASIC, &mem))
  {
    LOG(LogLevel::Error, ("%s: Failed to get a shared mem buffer for Child! size %u",
                       __FUNCTION__, aFrameBufferSize));
    return false;
  }
  *aMem = mem;
  mem = ipc::Shmem();
  return true;
}

bool
GMPMediaRendererParent::Recv__delete__()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (mPlugin) {
    // Ignore any return code. It is OK for this to fail without killing the process.
    mPlugin->MediaRendererDestroyed(this);
    mPlugin = nullptr;
  }

  return true;
}

} // namespace gmp
} // namespace mozilla