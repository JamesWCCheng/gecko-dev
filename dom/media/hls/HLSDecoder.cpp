/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HLSDecoder.h"
#include "MediaContainerType.h"
#include "MediaDecoderStateMachine.h"
#include "MediaFormatReader.h"
#include "MediaPrefs.h"
#include "HLSDemuxer.h"
#include "HLSUtils.h"

namespace mozilla {

MediaDecoderStateMachine*
HLSDecoder::CreateStateMachine()
{
  MOZ_ASSERT(NS_IsMainThread());
  HLS_DEBUG("HLSDecoder", "");
  //TODO: check hls demuxer constructor's parameter
  mReader =
    new MediaFormatReader(this,
                          new HLSDemuxer(this, GetResource(), AbstractMainThread()),
                          GetVideoFrameContainer());

  return new MediaDecoderStateMachine(this, mReader);
}

MediaDecoder*
HLSDecoder::Clone(MediaDecoderOwner* aOwner)
{
  if (!IsEnabled()) {
    return nullptr;
  }
  return new HLSDecoder(aOwner);
}

bool
HLSDecoder::IsEnabled()
{
  // TODO: Currently default return true for testing.
  HLS_DEBUG_NON_MEMBER("HLSDecoder", "HlsEnabled = %d", MediaPrefs::HLSEnabled());
  return MediaPrefs::HLSEnabled();
}

bool
HLSDecoder::IsSupportedType(const MediaContainerType& aContainerType)
{
  // TODO: Copy from DecoderTraits
  HLS_DEBUG_NON_MEMBER("HLSDecoder", "HLSDecoder::aContainerType = %s", aContainerType.OriginalString().Data());
  HLS_DEBUG_NON_MEMBER("HLSDecoder", "HLSDecoder::IsSupportedType = %d", IsHttpLiveStreamingType(aContainerType));
  return IsEnabled()
         && IsHttpLiveStreamingType(aContainerType);
}

} // namespace mozilla
