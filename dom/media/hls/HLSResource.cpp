/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HLSResource.h"

#include <algorithm>

#include "nsISeekableStream.h"
#include "nsISupports.h"
#include "mozilla/Logging.h"
#include "mozilla/SizePrintfMacros.h"
#include "MediaData.h"

mozilla::LogModule* GetHLSResourceLog()
{
  static mozilla::LazyLogModule sLogModule("HLSResource");
  return sLogModule;
}

#define HLSR_DEBUG(arg, ...) MOZ_LOG(GetHLSResourceLog(), mozilla::LogLevel::Debug, ("HLSResource(%p:%s)::%s: " arg, this, mType.OriginalString().Data(), __func__, ##__VA_ARGS__))
#define HLSR_DEBUGV(arg, ...) MOZ_LOG(GetHLSResourceLog(), mozilla::LogLevel::Verbose, ("HLSResource(%p:%s)::%s: " arg, this, mType.OriginalString().Data(), __func__, ##__VA_ARGS__))

namespace mozilla {

nsresult
HLSResource::Close()
{
  ReentrantMonitorAutoEnter mon(mMonitor);
  HLSR_DEBUG("Close");
  //MOZ_ASSERT(!mClosed);
  mClosed = true;
  mon.NotifyAll();
  return NS_OK;
}

nsresult
HLSResource::ReadAt(int64_t aOffset, char* aBuffer, uint32_t aCount, uint32_t* aBytes)
{
  HLSR_DEBUG("ReadAt(aOffset=%" PRId64 ", aBuffer=%p, aCount=%u, aBytes=%p)",
            aOffset, aBytes, aCount, aBytes);
  ReentrantMonitorAutoEnter mon(mMonitor);
  return ReadAtInternal(aOffset, aBuffer, aCount, aBytes, /* aMayBlock = */ true);
}

nsresult
HLSResource::ReadAtInternal(int64_t aOffset, char* aBuffer, uint32_t aCount, uint32_t* aBytes,
                                     bool aMayBlock)
{
  mMonitor.AssertCurrentThreadIn();

  MOZ_ASSERT_IF(!aMayBlock, aBytes);

  if (mClosed ||
      aOffset < 0 ||
//      uint64_t(aOffset) < mInputBuffer.GetOffset() ||
      aOffset > GetLength()) {
    return NS_ERROR_FAILURE;
  }

  while (aMayBlock &&
         !mEnded &&
         aOffset + aCount > GetLength()) {
    HLSR_DEBUGV("waiting for data");
    mMonitor.Wait();
    // The callers of this function should have checked this, but it's
    // possible that we had an eviction while waiting on the monitor.
//    if (uint64_t(aOffset) < mInputBuffer.GetOffset()) {
//      return NS_ERROR_FAILURE;
//    }
  }

  uint32_t available = GetLength() - aOffset;
  uint32_t count = std::min(aCount, available);

  // Keep the position of the last read to have Tell() approximately give us
  // the position we're up to in the stream.
  mOffset = aOffset + count;

  HLSR_DEBUGV("offset=%" PRId64 " GetLength()=%" PRId64 " available=%u count=%u mEnded=%d",
             aOffset, GetLength(), available, count, mEnded);
  if (available == 0) {
    HLSR_DEBUGV("reached EOF");
    *aBytes = 0;
    return NS_OK;
  }

//  mInputBuffer.CopyData(aOffset, count, aBuffer);
  *aBytes = count;

  return NS_OK;
}

nsresult
HLSResource::ReadFromCache(char* aBuffer, int64_t aOffset, uint32_t aCount)
{
  HLSR_DEBUG("ReadFromCache(aBuffer=%p, aOffset=%" PRId64 ", aCount=%u)",
            aBuffer, aOffset, aCount);
  ReentrantMonitorAutoEnter mon(mMonitor);
  uint32_t bytesRead;
  nsresult rv = ReadAtInternal(aOffset, aBuffer, aCount, &bytesRead, /* aMayBlock = */ false);
  NS_ENSURE_SUCCESS(rv, rv);

  // ReadFromCache return failure if not all the data is cached.
  return bytesRead == aCount ? NS_OK : NS_ERROR_FAILURE;
}

HLSResource::~HLSResource()
{
  HLSR_DEBUG("");
}

HLSResource::HLSResource(const MediaContainerType& aType)
  : mType(aType)
  , mMonitor("mozilla::HLSResource::mMonitor")
  , mOffset(0)
  , mClosed(false)
  , mEnded(false)
{
  HLSR_DEBUG("");
}

#undef HLSR_DEBUG
#undef HLSR_DEBUGV
} // namespace mozilla
