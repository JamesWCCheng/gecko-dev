/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPMediaRendererProxy_h_
#define GMPMediaRendererProxy_h_

#include "nsTArray.h"
#include "gmp-media-render.h"
#include "GMPAudioHost.h"
#include "GMPCallbackBase.h"
#include "GMPUtils.h"

class GMPMediaRendererCallbackProxy : public GMPCallbackBase,
                                      public GMPMediaRendererCallback
{
public:
  virtual ~GMPMediaRendererCallbackProxy() {}
};

// A proxy to GMPMediaRenderer in the child process.
// GMPMediaRendererParent exposes this to users the GMP.
// This enables Gecko to pass nsTArrays to the child GMP and avoid
// an extra copy when doing so.

// The consumer must call Close() when done with the codec, or when
// Terminated() is called by the GMP plugin indicating an abnormal shutdown
// of the underlying plugin.  After calling Close(), the consumer must
// not access this again.

// This interface is not thread-safe and must only be used from GMPThread.
class GMPMediaRendererProxy {
public:
  virtual nsresult InitVideoRenderer(const GMPVideoCodec& aVideoCodecSettings,
                                     const nsTArray<uint8_t>& aCodecSpecific,
                                     int32_t aCoreCount,
                                     GMPRect aDisplayRect) = 0;
  virtual nsresult InitAudioRenderer(GMPAudioCodecType aCodecType,
                                     uint32_t aChannelCount,
                                     uint32_t aBitsPerChannel,
                                     uint32_t aSamplesPerSecond,
                                     nsTArray<uint8_t>& aExtraData) = 0;
  virtual nsresult SetRendererCallback(GMPMediaRendererCallbackProxy* aCallback) =0;
  virtual nsresult RenderVideoPacket(mozilla::GMPUniquePtr<GMPVideoEncodedFrame> aInputFrame,
                                     bool aMissingFrames,
                                     const nsTArray<uint8_t>& aCodecSpecificInfo) = 0;
  virtual nsresult RenderAudioPacket(mozilla::gmp::GMPAudioSamplesImpl& aSamples) = 0;
  virtual nsresult SetPlaybackRate(double aRate) = 0;
  virtual nsresult SetVolume(double aVolume) = 0;
  virtual nsresult SetPreservesPitch(bool aPreservesPitch) = 0;
  virtual nsresult Shutdown() = 0;
  virtual nsresult Start(int64_t aStartTime) = 0;
  virtual nsresult Stop() = 0;
  virtual nsresult ContentChanged(int64_t aTime) = 0;
  virtual nsresult SetDisplayPosition(GMPRect aDisplayRect) = 0;
  virtual const uint32_t GetPluginId() const = 0;

  // Call to tell GMP/plugin the consumer will no longer use this
  // interface/codec.
  virtual void Close() = 0;

  virtual const nsCString& GetDisplayName() const = 0;
};

#endif
