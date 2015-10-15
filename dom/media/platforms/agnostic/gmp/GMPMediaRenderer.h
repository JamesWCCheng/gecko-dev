/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPMediaRenderer_h_
#define GMPMediaRenderer_h_

#include "GMPMediaRendererProxy.h"
#include "ImageContainer.h"
#include "MediaDataRendererProxy.h"
#include "PlatformDecoderModule.h"
#include "mozIGeckoMediaPluginService.h"
#include "MediaInfo.h"

namespace mozilla {

class MediaRendererCallbackAdapter : public GMPMediaRendererCallbackProxy {
public:
  MediaRendererCallbackAdapter(MediaDataRendererCallbackProxy* aCallback,
                               VideoInfo aVideoInfo,
                               layers::ImageContainer* aImageContainer)
   : mCallback(aCallback)
   , mLastAudioStreamOffset(0)
   , mLastVideoStreamOffset(0)
   , mVideoInfo(aVideoInfo)
   , mImageContainer(aImageContainer)
  {}

  // GMPMediaRendererCallbackProxy
  virtual void TimeUpdate(int64_t aElapsedTime) override;
  virtual void AudioRenderReachedEOS() override;
  virtual void VideoRenderReachedEOS() override;
  virtual void AudioInputDataExhausted() override;
  virtual void VideoInputDataExhausted() override;
  virtual void ShutdownComplete() override;
  virtual void StopComplete() override;
  virtual void SetOverlayImageID(const uint32_t aId) override;
  virtual void Error(GMPErr aError, const char* aMessage, uint32_t aLength) override;
  virtual void Terminated() override;

  void SetLastAudioStreamOffset(int64_t aStreamOffset) {
    mLastAudioStreamOffset = aStreamOffset;
  }

  void SetLastVideoStreamOffset(int64_t aStreamOffset) {
    mLastVideoStreamOffset = aStreamOffset;
  }

private:
  MediaDataRendererCallbackProxy* mCallback;
  int64_t mLastAudioStreamOffset;
  int64_t mLastVideoStreamOffset;

  VideoInfo mVideoInfo;
  RefPtr<layers::ImageContainer> mImageContainer;
}; // MediaRendererCallbackAdapter

class GMPMediaRenderer : public MediaDataRenderer {
protected:
  GMPMediaRenderer(const AudioInfo& aAudioConfig,
                   const VideoInfo& aVideoConfig,
                   layers::LayersBackend aLayersBackend,
                   layers::ImageContainer* aImageContainer,
                   const GMPRect& aDisplayRect,
                   TaskQueue* aTaskQueue,
                   MediaDataRendererCallbackProxy* aCallback,
                   MediaRendererCallbackAdapter* aAdapter)
   : mVideoConfig(aVideoConfig)
   , mAudioConfig(aAudioConfig)
   , mCallback(aCallback)
   , mGMP(nullptr)
   , mHost(nullptr)
   , mAdapter(aAdapter)
   , mDisplayInfo(aDisplayRect)
   , mConvertNALUnitLengths(false)
  {
  }

public:
  GMPMediaRenderer(const AudioInfo& aAudioConfig,
                   const VideoInfo& aVideoConfig,
                   layers::LayersBackend aLayersBackend,
                   layers::ImageContainer* aImageContainer,
                   const GMPRect& aDisplayRect,
                   TaskQueue* aTaskQueue,
                   MediaDataRendererCallbackProxy* aCallback)
   : mVideoConfig(aVideoConfig)
   , mAudioConfig(aAudioConfig)
   , mCallback(aCallback)
   , mGMP(nullptr)
   , mHost(nullptr)
   , mAdapter(new MediaRendererCallbackAdapter(aCallback,
                                               VideoInfo(aVideoConfig.mDisplay.width,
                                                         aVideoConfig.mDisplay.height),
                                               aImageContainer))
   , mDisplayInfo(aDisplayRect)
   , mConvertNALUnitLengths(false)
  {
  }

  //[TODO]: Use InitPromise instead of MediaDataDecoder
  RefPtr<MediaDataDecoder::InitPromise> Init() override;

  nsresult RenderVideoPacket(MediaRawData* aSample) override;

  nsresult RenderAudioPacket(MediaRawData* aSample) override;

  nsresult SetPlaybackRate(double aRate) override;

  nsresult SetVolume(double aVolume) override;

  nsresult SetPreservesPitch(bool aPreservesPitch) override;

  nsresult Shutdown() override;

  nsresult Start(int64_t aStartTime) override;

  nsresult Stop() override;

  nsresult ContentChanged(int64_t aTime) override;

  nsresult SetDisplayPosition(GMPRect aDisplayRect) override;

protected:
  virtual void InitTags(nsTArray<nsCString>& aTags);
  virtual nsCString GetNodeId();
  virtual GMPUniquePtr<GMPVideoEncodedFrame> CreateFrame(MediaRawData* aSample);

private:

  class GMPInitDoneCallback : public GetGMPMediaRendererCallback
  {
  public:
    GMPInitDoneCallback(GMPMediaRenderer* aRenderer)
      : mRenderer(aRenderer)
    {
    }

    virtual void Done(GMPMediaRendererProxy* aGMP, GMPVideoHost* aHost)
    {
      if (aGMP) {
        mRenderer->GMPInitDone(aGMP, aHost);
      }
    }

  private:
    RefPtr<GMPMediaRenderer> mRenderer;
  }; // GMPInitDoneCallback

  void GMPInitDone(GMPMediaRendererProxy* aGMP, GMPVideoHost* aHost);

  const VideoInfo& mVideoConfig;
  const AudioInfo& mAudioConfig;
  MediaDataRendererCallbackProxy* mCallback;
  nsCOMPtr<mozIGeckoMediaPluginService> mMPS;
  GMPMediaRendererProxy* mGMP;
  GMPVideoHost* mHost;
  nsAutoPtr<MediaRendererCallbackAdapter> mAdapter;
  GMPRect mDisplayInfo;
  bool mConvertNALUnitLengths;
  //[TODO]: Use InitPromise instead of MediaDataDecoder
  MozPromiseHolder<MediaDataDecoder::InitPromise> mInitPromise;
};

} // namespace mozilla

#endif // GMPMediaRenderer_h_
