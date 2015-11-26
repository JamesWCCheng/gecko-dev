/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(MediaDataRendererProxy_h_)
#define MediaDataRendererProxy_h_

#include "gmp-media-render.h"
#include "PlatformDecoderModule.h"
#include "mozilla/RefPtr.h"
#include "nsThreadUtils.h"
#include "nscore.h"

namespace mozilla {

//[TODO] MediaDataDecoder is in PlatformDecoderModule.h, should move to the right location?
class MediaDataRenderer {
protected:
  virtual ~MediaDataRenderer() {};

public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDataRenderer)
  // [TODO]
  virtual RefPtr<MediaDataDecoder::InitPromise>  Init() = 0;

  virtual nsresult RenderVideoPacket(MediaRawData* aSample) = 0;

  virtual nsresult RenderAudioPacket(MediaRawData* aSample) = 0;

  virtual nsresult SetPlaybackRate(double aRate) = 0;

  virtual nsresult SetVolume(double aVolume) = 0;

  virtual nsresult SetPreservesPitch(bool aPreservesPitch) = 0;

  virtual nsresult Shutdown() = 0;

  virtual nsresult Start(int64_t aStartTime) = 0;

  virtual nsresult Stop() = 0;

  virtual nsresult ContentChanged(int64_t aTime) = 0;

  virtual nsresult SetDisplayPosition(GMPRect aDisplayRect) = 0;

  virtual bool IsHardwareAccelerated() const { return false; }
  virtual nsresult ConfigurationChanged(const TrackInfo& aConfig)
  {
    return NS_OK;
  }
};

class MediaDataRendererCallback {
public:
  virtual ~MediaDataRendererCallback() {}

  virtual void TimeUpdate(int64_t aElapsedTime) = 0;

  virtual void AudioRenderReachedEOS() = 0;

  virtual void VideoRenderReachedEOS() = 0;

  virtual void AudioInputDataExhausted() = 0;

  virtual void VideoInputDataExhausted() = 0;

  virtual void ShutdownComplete() = 0;

  virtual void StopComplete() = 0;

  virtual void SetOverlayImageID(const uint32_t aId) = 0;

  virtual void Error() = 0;
};

class MediaDataRendererCallbackProxy : public MediaDataRendererCallback {
public:
  explicit MediaDataRendererCallbackProxy(MediaDataRendererCallback* aCallback)
    : mProxyCallback(aCallback)
  {
  }

  virtual void TimeUpdate(int64_t aElapsedTime) override
  {
    mProxyCallback->TimeUpdate(aElapsedTime);
  }

  virtual void AudioRenderReachedEOS() override
  {
    mProxyCallback->AudioRenderReachedEOS();
  }

  virtual void VideoRenderReachedEOS() override
  {
    mProxyCallback->VideoRenderReachedEOS();
  }

  virtual void AudioInputDataExhausted() override
  {
    mProxyCallback->AudioInputDataExhausted();
  }

  virtual void VideoInputDataExhausted() override
  {
    mProxyCallback->VideoInputDataExhausted();
  }

  virtual void ShutdownComplete() override
  {
    mProxyCallback->ShutdownComplete();
  }

  virtual void StopComplete() override
  {
    mProxyCallback->StopComplete();
  }

  virtual void SetOverlayImageID(const uint32_t aId) override
  {
    mProxyCallback->SetOverlayImageID(aId);
  }

  virtual void Error() override
  {
    mProxyCallback->Error();
  }

private:
  MediaDataRendererCallback* mProxyCallback;
};

template<typename Method>
class RenderTask : public nsRunnable {
public:
  RenderTask(MediaDataRenderer* aRenderer,
            Method aFunction,
            MediaRawData* aSample)
   : mRenderer(aRenderer)
   , mFunction(mozilla::Move(aFunction))
   , mSample(aSample)
  {}

  NS_IMETHOD Run() {
    ((*mRenderer).*mFunction)(mSample);
    return NS_OK;
  }

private:

  RefPtr<MediaDataRenderer> mRenderer;
  Method mFunction;
  RefPtr<MediaRawData> mSample;
};

class MediaDataRendererProxy : public MediaDataRenderer {
public:
  MediaDataRendererProxy(nsIThread* aProxyThread, MediaDataRendererCallback* aCallback)
   : mProxyThread(aProxyThread)
   , mProxyCallback(aCallback)
#if defined(DEBUG)
   , mIsShutdown(false)
#endif
  {
    mProxyThreadWrapper = CreateXPCOMAbstractThreadWrapper(aProxyThread, false);
  }

  MediaDataRendererCallbackProxy* Callback()
  {
    return &mProxyCallback;
  }

  void SetProxyTarget(MediaDataRenderer* aProxyRenderer)
  {
    MOZ_ASSERT(aProxyRenderer);
    mProxyRenderer = aProxyRenderer;
  }

  // [TODO]
  virtual RefPtr<MediaDataDecoder::InitPromise> Init() override;

  virtual nsresult RenderVideoPacket(MediaRawData* aSample) override;

  virtual nsresult RenderAudioPacket(MediaRawData* aSample) override;

  virtual nsresult SetPreservesPitch(bool aPreservesPitch) override;

  virtual nsresult SetPlaybackRate(double aRate) override;

  virtual nsresult SetVolume(double aVolume) override;

  virtual nsresult Shutdown() override;

  virtual nsresult Start(int64_t aStartTime) override;

  virtual nsresult Stop() override;

  virtual nsresult ContentChanged(int64_t aTime) override;

  virtual nsresult SetDisplayPosition(GMPRect aDisplayRect) override;

private:
  // [TODO]
  RefPtr<MediaDataDecoder::InitPromise> InternalInit();

#ifdef DEBUG
  bool IsOnProxyThread() {
    return NS_GetCurrentThread() == mProxyThread;
  }
#endif

  RefPtr<MediaDataRenderer> mProxyRenderer;
  nsCOMPtr<nsIThread> mProxyThread;
  RefPtr<AbstractThread> mProxyThreadWrapper;

  MediaDataRendererCallbackProxy mProxyCallback;

#if defined(DEBUG)
  bool mIsShutdown;
#endif
};

} // namespace mozilla

#endif // MediaDataRendererProxy_h_
