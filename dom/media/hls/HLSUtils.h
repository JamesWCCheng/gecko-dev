/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HLSUtils_h_
#define HLSUtils_h_

#include "mozilla/Logging.h"
// Logger

inline
mozilla::LogModule* GetHLSLog()
{
  static mozilla::LazyLogModule sLogModule("HLS");
  return sLogModule;
}

// TODO: Uncolor
#define HLS_DEBUG(TAG, format, ...) MOZ_LOG(GetHLSLog(), mozilla::LogLevel::Debug, ("\033[0;32;31m " TAG "(%p)::%s: " format " \033[m", this, __func__, ##__VA_ARGS__))
#define HLS_DEBUG_NON_MEMBER(TAG, format, ...) MOZ_LOG(GetHLSLog(), mozilla::LogLevel::Debug, ("\033[0;32;31m " TAG "%s: " format " \033[m", __func__, ##__VA_ARGS__))
//

#endif // HLSUtils_h_
