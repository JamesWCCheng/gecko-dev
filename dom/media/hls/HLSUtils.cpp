/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HLSUtils.h"
namespace mozilla {

bool
IsHttpLiveStreamingType(const mozilla::MediaContainerType& aType)
{
  return // For m3u8.
         // https://tools.ietf.org/html/draft-pantos-http-live-streaming-19#section-10
         aType.Type() == MEDIAMIMETYPE("application/vnd.apple.mpegurl")
         // Some sites serve these as the informal m3u type.
         || aType.Type() == MEDIAMIMETYPE("application/x-mpegurl")
         || aType.Type() == MEDIAMIMETYPE("audio/x-mpegurl");
}

} // namespace mozilla
