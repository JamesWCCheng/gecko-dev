/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPMediaRendererChild_h_
#define GMPMediaRendererChild_h_

#include "nsString.h"
#include "mozilla/gmp/PGMPMediaRendererChild.h"
#include "gmp-media-render.h"
#include "GMPAudioHost.h"
#include "GMPSharedMemManager.h"
#include "GMPVideoHost.h"
#include "mozilla/gmp/GMPTypes.h"

namespace mozilla {
namespace gmp {

class GMPContentChild;

class GMPMediaRendererChild : public PGMPMediaRendererChild,
                              public GMPMediaRendererCallback,
                              public GMPSharedMemManager
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GMPMediaRendererChild);

  explicit GMPMediaRendererChild(GMPContentChild* aPlugin);

  void Init(GMPMediaRenderer* aRenderer);

  GMPVideoHostImpl& Host();

  // GMPMediaRendererCallback
  virtual void TimeUpdate(int64_t aElapsedTime) override;
  virtual void AudioRenderReachedEOS() override;
  virtual void VideoRenderReachedEOS() override;
  virtual void AudioInputDataExhausted() override;
  virtual void VideoInputDataExhausted() override;
  virtual void ShutdownComplete() override;
  virtual void StopComplete() override;
  virtual void Error(GMPErr aError, const char* aMessage, uint32_t aLength) override;
  virtual void SetOverlayImageID(const uint32_t aId) override;

  // GMPSharedMemManager
  virtual bool Alloc(size_t aSize, Shmem::SharedMemory::SharedMemoryType aType, Shmem* aMem) override;
  virtual void Dealloc(Shmem& aMem) override;

private:
  virtual ~GMPMediaRendererChild();

  // PGMPMediaRendererChild
  virtual bool RecvInitVideoRenderer(const GMPVideoCodec& aVideoCodecSettings,
                                     InfallibleTArray<uint8_t>&& aVideoCodecSpecific,
                                     const int32_t& aCoreCount,
                                     const GMPRect& aDisplayRect) override;

  virtual bool RecvInitAudioRenderer(const GMPAudioCodecData& aAudioCodecSettings) override;

  virtual bool RecvRenderVideoPacket(const GMPVideoEncodedFrameData& aInputFrame,
                                     const bool& aMissingFrames,
                                     InfallibleTArray<uint8_t>&& aCodecSpecificInfo) override;

  virtual bool RecvRenderAudioPacket(const GMPAudioEncodedSampleData& aInput) override;

  virtual bool RecvSetPlaybackRate(const double& aRate) override;

  virtual bool RecvSetVolume(const double& aVolume) override;

  virtual bool RecvSetPreservesPitch(const bool& aPreservesPitch) override;

  virtual bool RecvShutdown() override;

  virtual bool RecvStart(const int64_t& aStartTime) override;

  virtual bool RecvStop() override;

  virtual bool RecvContentChanged(const int64_t& aTime) override;

  virtual bool RecvSetDisplayPosition(const GMPRect& aDisplayRect) override;

  virtual bool RecvChildShmemForPool(Shmem&& aFrameBuffer) override;

  GMPContentChild* mPlugin;
  GMPMediaRenderer* mMediaRenderer;
  //[TODO] Audio Host seems no use....
  GMPVideoHostImpl mVideoHost;

  // Non-zero when a GMP is blocked spinning the IPC message loop while
  // waiting on an NeedShmem to complete.
  int mNeedShmemIntrCount;
  bool mPendingDecodeComplete;
};

} // namespace gmp
} // namespace mozilla

#endif // GMPMediaRendererChild_h_
