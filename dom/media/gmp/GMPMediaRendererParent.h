/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPMediaRendererParent_h_
#define GMPMediaRendererParent_h_

#include "mozilla/RefPtr.h"
#include "gmp-media-render.h"
#include "mozilla/gmp/PGMPMediaRendererParent.h"
#include "GMPMessageUtils.h"
#include "GMPSharedMemManager.h"
#include "GMPUtils.h"
#include "GMPVideoHost.h"
#include "GMPMediaRendererProxy.h"

namespace mozilla {
namespace gmp {

class GMPContentParent;

class GMPMediaRendererParent final : public PGMPMediaRendererParent,
                                     public GMPMediaRendererProxy,
                                     public GMPSharedMemManager
{
public:
  NS_INLINE_DECL_REFCOUNTING(GMPMediaRendererParent)

  explicit GMPMediaRendererParent(GMPContentParent *aPlugin);

  GMPVideoHostImpl& Host();

  // GMPMediaRendererProxy
  virtual void Close() override;
  virtual nsresult InitVideoRenderer(const GMPVideoCodec& aVideoCodecSettings,
                                     const nsTArray<uint8_t>& aCodecSpecific,
                                     int32_t aCoreCount,
                                     GMPRect aDisplayRect) override;
  virtual nsresult InitAudioRenderer(GMPAudioCodecType aCodecType,
                                     uint32_t aChannelCount,
                                     uint32_t aBitsPerChannel,
                                     uint32_t aSamplesPerSecond,
                                     nsTArray<uint8_t>& aExtraData) override;
  virtual nsresult SetRendererCallback(GMPMediaRendererCallbackProxy* aCallback) override;
  virtual nsresult RenderVideoPacket(GMPUniquePtr<GMPVideoEncodedFrame> aInputFrame,
                                     bool aMissingFrames,
                                     const nsTArray<uint8_t>& aCodecSpecificInfo) override;
  virtual nsresult RenderAudioPacket(GMPAudioSamplesImpl& aSamples) override;
  virtual nsresult SetPlaybackRate(double aRate) override;
  virtual nsresult SetVolume(double aVolume) override;
  virtual nsresult SetPreservesPitch(bool aPreservesPitch) override;
  virtual nsresult Shutdown() override;
  virtual nsresult Start(int64_t aStartTime) override;
  virtual nsresult Stop() override;
  virtual nsresult ContentChanged(int64_t aTime) override;
  virtual nsresult SetDisplayPosition(GMPRect aDisplayRect) override;
  virtual const uint32_t GetPluginId() const override { return mPluginId; }
  virtual const nsCString& GetDisplayName() const override;

  //[TODO] whats this for?
  // GMPSharedMemManager
  virtual bool Alloc(size_t aSize, Shmem::SharedMemory::SharedMemoryType aType, Shmem* aMem) override
  {
#ifdef GMP_SAFE_SHMEM
    return AllocShmem(aSize, aType, aMem);
#else
    return AllocUnsafeShmem(aSize, aType, aMem);
#endif
  }
  virtual void Dealloc(Shmem& aMem) override
  {
    DeallocShmem(aMem);
  }

private:
  ~GMPMediaRendererParent();

  // PGMPVideoDecoderParent
  virtual bool RecvTimeUpdate(const int64_t& aElapsedTimeMs) override;
  virtual bool RecvAudioRenderReachedEOS() override;
  virtual bool RecvVideoRenderReachedEOS() override;
  virtual bool RecvAudioInputDataExhausted() override;
  virtual bool RecvVideoInputDataExhausted()override;
  virtual bool RecvShutdownComplete() override;
  virtual bool RecvStopComplete() override;
  virtual bool RecvError(const GMPErr& aError) override;
  virtual bool RecvSetOverlayImageID(const uint32_t& aId) override;
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;
  virtual bool RecvParentShmemForPool(Shmem&& aEncodedBuffer) override;
  virtual bool AnswerNeedShmem(const uint32_t& aFrameBufferSize,
                               Shmem* aMem) override;
  virtual bool Recv__delete__() override;

  bool mIsVideoOpen;
  bool mIsAudioOpen;
  bool mShuttingDown;
  bool mShuttingDownComplete;
  bool mActorDestroyed;
  RefPtr<GMPContentParent> mPlugin;
  GMPMediaRendererCallbackProxy* mCallback;
  GMPVideoHostImpl mRenderHost;
  const uint32_t mPluginId;
};

} // namespace gmp
} // namespace mozilla

#endif // GMPMediaRendererParent_h_
