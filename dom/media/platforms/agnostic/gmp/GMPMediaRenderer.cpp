/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPMediaRenderer.h"
#include "GMPVideoHost.h"
#include "mozilla/Endian.h"
#include "mozilla/Logging.h"
#include "prsystem.h"
#include "MediaData.h"

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
#define __CLASS__ "GMPMediaRenderer"

#if defined(DEBUG)
extern bool IsOnGMPThread();
#endif

void
MediaRendererCallbackAdapter::TimeUpdate(int64_t aElapsedTime)
{
  LOGD(("%s::%s: %p, aElapsedTime = %ld", __CLASS__, __func__, this, aElapsedTime));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->TimeUpdate(aElapsedTime);
}

void
MediaRendererCallbackAdapter::AudioRenderReachedEOS()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->AudioRenderReachedEOS();
}

void
MediaRendererCallbackAdapter::VideoRenderReachedEOS()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->VideoRenderReachedEOS();
}

void
MediaRendererCallbackAdapter::VideoInputDataExhausted()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->VideoInputDataExhausted();
}

void
MediaRendererCallbackAdapter::AudioInputDataExhausted()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->AudioInputDataExhausted();
}

void
MediaRendererCallbackAdapter::ShutdownComplete()
{
  LOGD(("%s::%s: %p, aErr = %d", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->ShutdownComplete();
}

void
MediaRendererCallbackAdapter::StopComplete()
{
  LOGD(("%s::%s: %p, aErr = %d", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->StopComplete();
}

void
MediaRendererCallbackAdapter::SetOverlayImageID(const uint32_t aId)
{
  LOGD(("%s::%s: %p, aId = %d", __CLASS__, __func__, this, aId));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->SetOverlayImageID(aId);
}

void
MediaRendererCallbackAdapter::Error(GMPErr aError, const char* aMessage, uint32_t aLength)
{
  LOGD(("%s::%s: %p GMPErr = %d ErrorMessage = %s", __CLASS__, __func__, this, aError, aMessage));
  MOZ_ASSERT(IsOnGMPThread());
  mCallback->Error();
}

void
MediaRendererCallbackAdapter::Terminated()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  // Note that this *may* be called from the proxy thread also.
  NS_WARNING("GMP Renderer terminated.");
  mCallback->Error();
}

void
GMPMediaRenderer::InitTags(nsTArray<nsCString>& aTags)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  aTags.AppendElement(NS_LITERAL_CSTRING("external.renderer"));
}

nsCString
GMPMediaRenderer::GetNodeId()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  return NS_LITERAL_CSTRING("");
}

GMPUniquePtr<GMPVideoEncodedFrame>
GMPMediaRenderer::CreateFrame(MediaRawData* aSample)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  GMPVideoFrame* ftmp = nullptr;
  GMPErr err = mHost->CreateFrame(kGMPEncodedVideoFrame, &ftmp);
  if (GMP_FAILED(err)) {
    mCallback->Error();
    return nullptr;
  }

  GMPUniquePtr<GMPVideoEncodedFrame> frame(static_cast<GMPVideoEncodedFrame*>(ftmp));
  err = frame->CreateEmptyFrame(aSample->Size());
  if (GMP_FAILED(err)) {
    mCallback->Error();
    return nullptr;
  }

  memcpy(frame->Buffer(), aSample->Data(), frame->Size());

  // Convert 4-byte NAL unit lengths to host-endian 4-byte buffer lengths to
  // suit the GMP API.
  if (mConvertNALUnitLengths) {
    const int kNALLengthSize = 4;
    uint8_t* buf = frame->Buffer();
    while (buf < frame->Buffer() + frame->Size() - kNALLengthSize) {
      uint32_t length = BigEndian::readUint32(buf) + kNALLengthSize;
      *reinterpret_cast<uint32_t *>(buf) = length;
      buf += length;
    }
  }

  frame->SetBufferType(GMP_BufferLength32);

  frame->SetEncodedWidth(mVideoConfig.mDisplay.width);
  frame->SetEncodedHeight(mVideoConfig.mDisplay.height);
  frame->SetTimeStamp(aSample->mTime);
  frame->SetCompleteFrame(true);
  frame->SetDuration(aSample->mDuration);
  frame->SetFrameType(aSample->mKeyframe ? kGMPKeyFrame : kGMPDeltaFrame);

  return frame;
}

void
GMPMediaRenderer::GMPInitDone(GMPMediaRendererProxy* aGMP, GMPVideoHost* aHost)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(aHost && aGMP);

  //Audio
  nsTArray<uint8_t> audioCodecSpecific;
  audioCodecSpecific.AppendElements(mAudioConfig.mCodecSpecificConfig->Elements(),
                                    mAudioConfig.mCodecSpecificConfig->Length());
  //Video
  GMPVideoCodec codec;
  memset(&codec, 0, sizeof(codec));

  codec.mGMPApiVersion = kGMPVersion33;

  codec.mCodecType = kGMPVideoCodecH264;
  codec.mWidth = mVideoConfig.mDisplay.width;
  codec.mHeight = mVideoConfig.mDisplay.height;

  nsTArray<uint8_t> videoCodecSpecific;
  videoCodecSpecific.AppendElement(0); // mPacketizationMode.
  videoCodecSpecific.AppendElements(mVideoConfig.mExtraData->Elements(),
                                    mVideoConfig.mExtraData->Length());
  nsresult rv = aGMP->InitAudioRenderer(kGMPAudioCodecAAC,
                                        mAudioConfig.mChannels,
                                        mAudioConfig.mBitDepth,
                                        mAudioConfig.mRate,
                                        audioCodecSpecific);

  nsresult rv2 = aGMP->InitVideoRenderer(codec,
                                         videoCodecSpecific,
                                         PR_GetNumberOfProcessors(),
                                         mDisplayInfo);

  nsresult rv3 = aGMP->SetRendererCallback(mAdapter);

  if (NS_FAILED(rv) || NS_FAILED(rv2) || NS_FAILED(rv3)) {
    aGMP->Close();
    mInitPromise.Reject(MediaDataDecoder::DecoderFailureReason::INIT_ERROR, __func__);
    return;
  }
  mGMP = aGMP;
  mHost = aHost;

  // TODO
  // GMP implementations have interpreted the meaning of GMP_BufferLength32
  // differently.  The OpenH264 GMP expects GMP_BufferLength32 to behave as
  // specified in the GMP API, where each buffer is prefixed by a 32-bit
  // host-endian buffer length that includes the size of the buffer length
  // field.  Other existing GMPs currently expect GMP_BufferLength32 (when
  // combined with kGMPVideoCodecH264) to mean "like AVCC but restricted to
  // 4-byte NAL lengths" (i.e. buffer lengths are specified in big-endian
  // and do not include the length of the buffer length field.
  mConvertNALUnitLengths = mGMP->GetDisplayName().EqualsLiteral("gmpopenh264");
  LOGD(("%s::%s: %p, GetDisplayName = %s", __CLASS__, __func__, this, mGMP->GetDisplayName().get()));

  // [TODO] get rid of trackinfo
  mInitPromise.Resolve(TrackInfo::kVideoTrack, __func__);
}

RefPtr<MediaDataDecoder::InitPromise>
GMPMediaRenderer::Init()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());

  mMPS = do_GetService("@mozilla.org/gecko-media-plugin-service;1");
  MOZ_ASSERT(mMPS);

  RefPtr<MediaDataDecoder::InitPromise> promise(mInitPromise.Ensure(__func__));

  nsTArray<nsCString> tags;
  InitTags(tags);
  UniquePtr<GetGMPMediaRendererCallback> callback(new GMPInitDoneCallback(this));
  if (NS_FAILED(mMPS->GetGMPMediaRenderer(&tags, GetNodeId(), Move(callback)))) {
    mInitPromise.Reject(MediaDataDecoder::DecoderFailureReason::INIT_ERROR, __func__);
  }

  return promise;
}

nsresult
GMPMediaRenderer::RenderAudioPacket(MediaRawData* aSample)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());

  RefPtr<MediaRawData> sample(aSample);
  if (!mGMP) {
    mCallback->Error();
    return NS_ERROR_FAILURE;
  }

  mAdapter->SetLastAudioStreamOffset(sample->mOffset);

  gmp::GMPAudioSamplesImpl samples(sample, mAudioConfig.mChannels, mAudioConfig.mRate);
  nsresult rv = mGMP->RenderAudioPacket(samples);
  if (NS_FAILED(rv)) {
    mCallback->Error();
    return rv;
  }

  return NS_OK;
}

nsresult
GMPMediaRenderer::RenderVideoPacket(MediaRawData* aSample)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());

  RefPtr<MediaRawData> sample(aSample);
  if (!mGMP) {
    mCallback->Error();
    return NS_ERROR_FAILURE;
  }

  mAdapter->SetLastVideoStreamOffset(sample->mOffset);

  GMPUniquePtr<GMPVideoEncodedFrame> frame = CreateFrame(sample);
  nsTArray<uint8_t> info; // No codec specific per-frame info to pass.
  nsresult rv = mGMP->RenderVideoPacket(Move(frame), false, info);
  if (NS_FAILED(rv)) {
    mCallback->Error();
    return rv;
  }

  return NS_OK;
}


nsresult
GMPMediaRenderer::SetPlaybackRate(double aRate)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  if (!mGMP || NS_FAILED(mGMP->SetPlaybackRate(aRate))) {
    mCallback->Error();
  }
  return NS_OK;
}

nsresult
GMPMediaRenderer::SetVolume(double aVolume)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  if (!mGMP || NS_FAILED(mGMP->SetVolume(aVolume))) {
    mCallback->Error();
  }
  return NS_OK;
}

nsresult
GMPMediaRenderer::SetPreservesPitch(bool aPreservesPitch)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  if (!mGMP || NS_FAILED(mGMP->SetPreservesPitch(aPreservesPitch))) {
    mCallback->Error();
  }
  return NS_OK;
}

nsresult
GMPMediaRenderer::Shutdown()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  mInitPromise.RejectIfExists(MediaDataDecoder::DecoderFailureReason::CANCELED, __func__);
  // Note that this *may* be called from the proxy thread also.
  if (!mGMP) {
    return NS_ERROR_FAILURE;
  }
  mGMP->Close();
  mGMP = nullptr;

  return NS_OK;
}

nsresult
GMPMediaRenderer::Start(int64_t aStartTime)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  if (!mGMP || NS_FAILED(mGMP->Start(aStartTime))) {
    mCallback->Error();
  }
  return NS_OK;
}

nsresult
GMPMediaRenderer::Stop()
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  if (!mGMP || NS_FAILED(mGMP->Stop())) {
    mCallback->Error();
  }
  return NS_OK;
}

nsresult
GMPMediaRenderer::ContentChanged(int64_t aTime)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  if (!mGMP || NS_FAILED(mGMP->ContentChanged(aTime))) {
    mCallback->Error();
  }
  return NS_OK;
}

nsresult
GMPMediaRenderer::SetDisplayPosition(GMPRect aDisplayRect)
{
  LOGD(("%s::%s: %p", __CLASS__, __func__, this));
  MOZ_ASSERT(IsOnGMPThread());
  if (!mGMP || NS_FAILED(mGMP->SetDisplayPosition(aDisplayRect))) {
    mCallback->Error();
  }
  return NS_OK;
}

} // namespace mozilla
