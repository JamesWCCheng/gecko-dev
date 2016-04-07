/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* Copyright (c) 2011, The WebRTC project authors. All rights reserved.
 * Copyright (c) 2015, Mozilla
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 ** Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 ** Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in
 *  the documentation and/or other materials provided with the
 *  distribution.
 *
 ** Neither the name of Google nor the names of its contributors may
 *  be used to endorse or promote products derived from this software
 *  without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef GMP_MEDIA_RENDER_h_
#define GMP_MEDIA_RENDER_h_

#include "gmp-errors.h"
#include "gmp-audio-decode.h"
#include "gmp-audio-samples.h"
#include "gmp-audio-codec.h"
#include "gmp-video-frame-encoded.h"
#include "gmp-video-codec.h"

#include <stdint.h>

struct GMPRect
{
  int32_t mTop;
  int32_t mLeft;
  int32_t mWidth;
  int32_t mHeight;
};

class GMPMediaRendererCallback
{
public:
  virtual ~GMPMediaRendererCallback() {}

  // Notify current media time in HW_CDM in microseconds.
  // aElapsedTime is in microseconds.
  virtual void TimeUpdate(int64_t aElapsedTime) = 0;
  // Notify last audio frame is rendered (EOS encountered)
  virtual void AudioRenderReachedEOS() = 0;
  // Notify last video frame is rendered (EOS encountered)
  virtual void VideoRenderReachedEOS() = 0;

  // Notify parent-side to request more data and send to child.
  // AudioInputDataExhausted() / VideoInputDataExhausted() is expected to be
  // called repeatedly to get more audio / video frame when needed.
  virtual void AudioInputDataExhausted() = 0;

  virtual void VideoInputDataExhausted() = 0;

 // Notify parent that task of shutdown is done.
  virtual void ShutdownComplete() = 0;

  // Notify parent that Stop command is completed.
  virtual void StopComplete() = 0;

  // Notify parent side and parent side should notify layout engine to
  // reserve this region for HW_CDM rendering.
  virtual void SetOverlayImageID(const uint32_t aId) = 0;

  // Called when the renderer encounters a catestrophic error and cannot
  // continue.
  virtual void Error(GMPErr aError, const char* aMessage, uint32_t aLength) = 0;
};

#define GMP_API_MEDIA_RENDERER "render-media"

// Media Rendering for a single stream. A GMP may be asked to create multiple
// GMPMediaRenderer concurrently.
//
// API name macro: GMP_API_MEDIA_RENDERER
// Host API: GMPMediaHost
//
// ALL METHODS MUST BE CALLED ON THE MAIN THREAD
class GMPMediaRenderer
{
public:
  virtual ~GMPMediaRenderer() {}

  // Init Renderer for video, passing the video spec to child.
  // - aVideoCodecSettings: Details of video codec info to create.
  // - aVideoCodecSpecific: codec specific data, cast to a GMPVideoCodecXXX 
  //                        struct to get codec specific config data.
  // - aCodecSpecificLength: number of bytes in aCodecSpecific.
  // - aCoreCount: number of CPU cores.
  // - aDisplayRect: the initial display region on screen.
  virtual void InitVideoRenderer(const GMPVideoCodec& aVideoCodecSettings,
                                 const uint8_t* aVideoCodecSpecific,
                                 uint32_t aVideoCodecSpecificLength,
                                 int32_t aCoreCount,
                                 GMPRect aDisplayRect) = 0;
  // Init Renderer for audio, passing the audio spec to child.
  // - aAudioCodecSettings: Details of audio codec info to create.
  virtual void InitAudioRenderer(const GMPAudioCodec& aAudioCodecSettings) = 0;

  // Set the renderer callback
  // - aCallback: Subclass should retain reference to it until RenderingComplete
  //              is called. Do not attempt to delete it, host retains ownership.
  virtual void SetRendererCallback(GMPMediaRendererCallback* aCallback) = 0;

  // Render encoded frame (as a part of a video stream). Should callback through
  // VideoInputDataExhausted for requesting video data.
  // When rendering to the end of stream, callback through VideoRenderReachedEOS
  // to notify the parent side.
  // - aInputFrame: Frame to render. Call Destroy() on frame when it's rendered.
  // - aMissingFrames: True if one or more frames have been lost since the
  //                   previous decode call.
  // - aCodecSpecificInfo : codec specific data, pointer to a
  //                        GMPCodecSpecificInfo structure appropriate for
  //                        this codec type.
  // - aCodecSpecificInfoLength : number of bytes in aCodecSpecificInfo
  virtual void RenderVideoPacket(GMPVideoEncodedFrame* aInputFrame,
                                 bool aMissingFrames,
                                 const uint8_t* aCodecSpecificInfo,
                                 uint32_t aCodecSpecificInfoLength) = 0;

  // Render encoded audio frames (as a part of an audio stream).
  // - aEncodedSamples: Frame to render.
  //                    Call Destroy() on frame when it's rendered.
  virtual void RenderAudioPacket(GMPAudioSamples* aEncodedSamples) = 0;

  virtual void SetPlaybackRate(double aRate) = 0;

  // Set the current playback volume, as a number in the range 0.0 to 1.0,
  // -aVolume : 0.0 is the quietest and 1.0 the loudest.
  virtual void SetVolume(double aVolume) = 0;

  // Set if we are preserving the pitch.
  virtual void SetPreservesPitch(bool aPreservesPitch) = 0;

  // Should callback ShutdownComplete when completing the shutdown task.
  virtual void Shutdown() = 0;

  // Begin a playback session with the provided start time
  // aStartTime is in microseconds.
  virtual void Start(int64_t aStartTime) = 0;

  // Should callback StopComplete when completing the stop task.
  virtual void Stop() = 0;

  // Notify CDM that the new stream content is received.
  // The next sample to come is guaranteed to be a keyframe and
  // that it needs to decode all frames and drop all those whose time is < aTime
  // and to start to redisplay from aTime only.
  virtual void ContentChanged(int64_t aTime) = 0;

  // Dynamically set the display region on the screen.
  // - aDisplayRect: Contained (Top, Left) and the width/height of the display.
  virtual void SetDisplayPosition(GMPRect aDisplayRect) = 0;
};

#endif // GMP_MEDIA_RENDER_h_
