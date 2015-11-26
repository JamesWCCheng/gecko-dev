/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPMediaRendererChild.h"
#include "GMPVideoi420FrameImpl.h"
#include "GMPContentChild.h"
#include <stdio.h>
#include "mozilla/Logging.h"
#include "mozilla/unused.h"
#include "GMPVideoEncodedFrameImpl.h"
#include "runnable_utils.h"

namespace mozilla {

#ifdef LOG
#undef LOG
#endif
#ifdef LOGD
#undef LOGD
#endif

extern LogModule* GetGMPLog();

#define LOGD(msg) MOZ_LOG(GetGMPLog(), mozilla::LogLevel::Debug, msg)
#define LOG(level, msg) MOZ_LOG(GetGMPLog(), (level), msg)

#ifdef __CLASS__
#undef __CLASS__
#endif
#define __CLASS__ "GMPMediaRendererChild"

namespace gmp {

GMPMediaRendererChild::GMPMediaRendererChild(GMPContentChild* aPlugin)
  : GMPSharedMemManager(aPlugin)
  , mPlugin(aPlugin)
  , mMediaRenderer(nullptr)
  , mVideoHost(this)
  , mNeedShmemIntrCount(0)
  , mPendingDecodeComplete(false)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  MOZ_ASSERT(mPlugin);
}

GMPMediaRendererChild::~GMPMediaRendererChild()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  MOZ_COUNT_DTOR(GMPMediaRendererChild);
  MOZ_ASSERT(!mNeedShmemIntrCount);
}

//Called by GMPContentChild, injecting the concrete renderer instance.
void
GMPMediaRendererChild::Init(GMPMediaRenderer* aRenderer)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  MOZ_ASSERT(aRenderer, "Cannot initialize media renderer child without a media renderer!");
  mMediaRenderer = aRenderer;
  mMediaRenderer->SetRendererCallback(this);
}

GMPVideoHostImpl&
GMPMediaRendererChild::Host()
{
  return mVideoHost;
}

void
GMPMediaRendererChild::TimeUpdate(int64_t aElapsedTime)
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  SendTimeUpdate(aElapsedTime);
}

void
GMPMediaRendererChild::AudioRenderReachedEOS()
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  Unused << SendAudioRenderReachedEOS();
}

void
GMPMediaRendererChild::VideoRenderReachedEOS()
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  Unused << SendVideoRenderReachedEOS();
}

void
GMPMediaRendererChild::AudioInputDataExhausted()
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  Unused << SendAudioInputDataExhausted();
}

void
GMPMediaRendererChild::VideoInputDataExhausted()
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  Unused << SendVideoInputDataExhausted();
}

void
GMPMediaRendererChild::ShutdownComplete()
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  Unused << SendShutdownComplete();
}

void
GMPMediaRendererChild::StopComplete()
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  Unused << SendStopComplete();
}

void
GMPMediaRendererChild::Error(GMPErr aError, const char* aMessage, uint32_t aLength)
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p GMPErr = %d ErrorMessage = %s", __CLASS__, __FUNCTION__, this, aError, aMessage));
  Unused << SendError(aError);
}

void
GMPMediaRendererChild::SetOverlayImageID(const uint32_t aId)
{
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  Unused << SendSetOverlayImageID(aId);
}

bool
GMPMediaRendererChild::RecvInitVideoRenderer(const GMPVideoCodec& aVideoCodecSettings,
                                             InfallibleTArray<uint8_t>&& aVideoCodecSpecific,
                                             const int32_t& aCoreCount,
                                             const GMPRect& aDisplayRect)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }


  // Ignore any return code. It is OK for this to fail without killing the process.
  mMediaRenderer->InitVideoRenderer(aVideoCodecSettings,
                                    aVideoCodecSpecific.Elements(),
                                    aVideoCodecSpecific.Length(),
                                    aCoreCount,
                                    aDisplayRect);
  return true;
}

bool
GMPMediaRendererChild::RecvInitAudioRenderer(const GMPAudioCodecData& aAudioCodecSettings)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  GMPAudioCodec audioCodecSettings;
  audioCodecSettings.mCodecType = aAudioCodecSettings.mCodecType();
  audioCodecSettings.mChannelCount = aAudioCodecSettings.mChannelCount();
  audioCodecSettings.mBitsPerChannel = aAudioCodecSettings.mBitsPerChannel();
  audioCodecSettings.mSamplesPerSecond = aAudioCodecSettings.mSamplesPerSecond();
  audioCodecSettings.mExtraData = aAudioCodecSettings.mExtraData().Elements();
  audioCodecSettings.mExtraDataLen = aAudioCodecSettings.mExtraData().Length();

  // Ignore any return code. It is OK for this to fail without killing the process.
  mMediaRenderer->InitAudioRenderer(audioCodecSettings);
  return true;
}

bool
GMPMediaRendererChild::RecvRenderVideoPacket(const GMPVideoEncodedFrameData& aInputFrame,
                                             const bool& aMissingFrames,
                                             InfallibleTArray<uint8_t>&& aCodecSpecificInfo)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  auto f = new GMPVideoEncodedFrameImpl(aInputFrame, &mVideoHost);
  mMediaRenderer->RenderVideoPacket(f,
                                    aMissingFrames,
                                    aCodecSpecificInfo.Elements(),
                                    aCodecSpecificInfo.Length());
  return true;
}

bool
GMPMediaRendererChild::RecvRenderAudioPacket(const GMPAudioEncodedSampleData& aEncodedSamples)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  GMPAudioSamples* samples = new GMPAudioSamplesImpl(aEncodedSamples);
  mMediaRenderer->RenderAudioPacket(samples);
  return true;
}

bool
GMPMediaRendererChild::RecvSetPlaybackRate(const double& aRate)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  mMediaRenderer->SetPlaybackRate(aRate);
  return true;
}

bool
GMPMediaRendererChild::RecvSetVolume(const double& aVolume)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  mMediaRenderer->SetVolume(aVolume);
  return true;
}

bool
GMPMediaRendererChild::RecvSetPreservesPitch(const bool& aPreservesPitch)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  mMediaRenderer->SetPreservesPitch(aPreservesPitch);
  return true;
}

bool
GMPMediaRendererChild::RecvShutdown()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  if (mNeedShmemIntrCount) {
    // There's a GMP blocked in Alloc() waiting for the CallNeedShem() to
    // return a frame they can use. Don't call the GMP's DecodingComplete()
    // now and don't delete the GMPVideoDecoderChild, defer processing the
    // DecodingComplete() until once the Alloc() finishes.
    mPendingDecodeComplete = true;
    return true;
  }

  // Ignore any return code. It is OK for this to fail without killing the process.
  mMediaRenderer->Shutdown();
  mMediaRenderer = nullptr;

  mVideoHost.DoneWithAPI();

  mPlugin = nullptr;

  Unused << Send__delete__(this);
  return true;
}

bool
GMPMediaRendererChild::RecvStart(const int64_t& aStartTime)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  mMediaRenderer->Start(aStartTime);
  return true;
}

bool
GMPMediaRendererChild::RecvStop()
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  mMediaRenderer->Stop();
  return true;
}

bool
GMPMediaRendererChild::RecvContentChanged(const int64_t& aTime)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  mMediaRenderer->ContentChanged(aTime);
  return true;
}

bool
GMPMediaRendererChild::RecvSetDisplayPosition(const GMPRect& aDisplayRect)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (!mMediaRenderer) {
    return false;
  }

  mMediaRenderer->SetDisplayPosition(aDisplayRect);
  return true;
}

bool
GMPMediaRendererChild::RecvChildShmemForPool(Shmem&& aFrameBuffer)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  if (aFrameBuffer.IsWritable()) {
    mVideoHost.SharedMemMgr()->MgrDeallocShmem(GMPSharedMem::kGMPFrameData,
                                               aFrameBuffer);
  }
  return true;
}

bool
GMPMediaRendererChild::Alloc(size_t aSize,
                            Shmem::SharedMemory::SharedMemoryType aType,
                            Shmem* aMem)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  bool rv;
#ifndef SHMEM_ALLOC_IN_CHILD
  ++mNeedShmemIntrCount;
  rv = CallNeedShmem(aSize, aMem);
  --mNeedShmemIntrCount;
  if (mPendingDecodeComplete) {
    auto t = NewRunnableMethod(this, &GMPMediaRendererChild::RecvShutdown);
    mPlugin->GMPMessageLoop()->PostTask(FROM_HERE, t);
  }
#else
#ifdef GMP_SAFE_SHMEM
  rv = AllocShmem(aSize, aType, aMem);
#else
  rv = AllocUnsafeShmem(aSize, aType, aMem);
#endif
#endif
  return rv;
}

void
GMPMediaRendererChild::Dealloc(Shmem& aMem)
{
  LOGD(("%s::%s: %p", __CLASS__, __FUNCTION__, this));
#ifndef SHMEM_ALLOC_IN_CHILD
  SendParentShmemForPool(aMem);
#else
  DeallocShmem(aMem);
#endif
}

} // namespace gmp
} // namespace mozilla