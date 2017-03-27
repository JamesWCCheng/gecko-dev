/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HLSResource_h_
#define HLSResource_h_

#include "FennecJNIWrappers.h"
#include "HLSUtils.h"
#include "nsContentUtils.h"

#define UNIMPLEMENTED() HLS_DEBUG("HLSResource", "UNIMPLEMENTED FUNCTION")

namespace mozilla {
class HLSResource final : public BaseMediaResource
{
public:
  HLSResource(MediaResourceCallback* aCallback,
              nsIChannel* aChannel,
              nsIURI* aURI,
              const MediaContainerType& aContainerType) :
    BaseMediaResource(aCallback, aChannel, aURI, aContainerType)
    , mMonitor("HLSResource")
    , mEnded(false)
  {
    nsCString spec;
    nsresult rv = aURI->GetSpec(spec);
    (void)rv;
    HLS_DEBUG("HLSResource", "aContainerType = %s, aURI->GetSpec = %s", aContainerType.OriginalString().Data(), spec.get());
    mHlsResourceWrapper = java::GeckoHlsResourceWrapper::Create(NS_ConvertUTF8toUTF16(spec));
    MOZ_ASSERT(mHlsResourceWrapper);
  }

  ~HLSResource()
  {
  }
  nsresult Close() override { return NS_OK; }
  void Suspend(bool aCloseImmediately) override { UNIMPLEMENTED(); }
  void Resume() override { UNIMPLEMENTED(); }
  bool CanClone() override { UNIMPLEMENTED(); return false; }
  already_AddRefed<MediaResource> CloneData(MediaResourceCallback*) override { UNIMPLEMENTED(); return nullptr; }
  void SetReadMode(MediaCacheStream::ReadMode aMode) override { UNIMPLEMENTED(); }
  void SetPlaybackRate(uint32_t aBytesPerSecond) override  { UNIMPLEMENTED(); }
  nsresult ReadAt(int64_t aOffset, char* aBuffer, uint32_t aCount, uint32_t* aBytes) override { UNIMPLEMENTED(); return NS_ERROR_FAILURE; }
  int64_t Tell() override { UNIMPLEMENTED(); return -1; }
  void Pin() override { UNIMPLEMENTED(); }
  void Unpin() override { UNIMPLEMENTED(); }
  double GetDownloadRate(bool* aIsReliable) override { UNIMPLEMENTED(); *aIsReliable = false; return 0; }
  int64_t GetLength() override { UNIMPLEMENTED(); return -1; }
  int64_t GetNextCachedData(int64_t aOffset) override { UNIMPLEMENTED(); return -1; }
  int64_t GetCachedDataEnd(int64_t aOffset) override { UNIMPLEMENTED(); return -1; }
  bool IsDataCachedToEndOfResource(int64_t aOffset) override { UNIMPLEMENTED(); return false; }
  bool IsSuspendedByCache() override { UNIMPLEMENTED(); return false; }
  bool IsSuspended() override { UNIMPLEMENTED(); return false; }
  nsresult ReadFromCache(char* aBuffer, int64_t aOffset, uint32_t aCount) override { UNIMPLEMENTED(); return NS_ERROR_FAILURE; }
  nsresult Open(nsIStreamListener** aStreamListener) override { UNIMPLEMENTED(); return NS_OK; }

  already_AddRefed<nsIPrincipal> GetCurrentPrincipal() override
  {
    // TODO: Referenced from FileMediaResource::GetCurrentPrincipal(), should ensure what's that.
    NS_ASSERTION(NS_IsMainThread(), "Only call on main thread");

    nsCOMPtr<nsIPrincipal> principal;
    nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
    if (!secMan || !mChannel)
      return nullptr;
    secMan->GetChannelResultPrincipal(mChannel, getter_AddRefs(principal));
    return principal.forget();
  }

  nsresult GetCachedRanges(MediaByteRangeSet& aRanges) override
  {
    UNIMPLEMENTED();
    return NS_OK;
  }

  bool IsTransportSeekable() override { return true; }
  const MediaContainerType& GetContentType() const override { return mContainerType; }

  bool IsLiveStream() override
  {
    MonitorAutoLock mon(mMonitor);
    return !mEnded;
  }
  void SetEnded(bool aEnded)
  {
    MonitorAutoLock mon(mMonitor);
    mEnded = aEnded;
  }

  bool IsExpectingMoreData() override
  {
    MonitorAutoLock mon(mMonitor);
    // TODO: find the correct way to handle this API.
    //return !mEnded;
    return false;
  }

  java::GeckoHlsResourceWrapper::GlobalRef GetResourceWrapper() {
    return mHlsResourceWrapper;
  }

private:
  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const override
  {
    size_t size = MediaResource::SizeOfExcludingThis(aMallocSizeOf);
    size += mContainerType.SizeOfExcludingThis(aMallocSizeOf);

    return size;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override
  {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  Monitor mMonitor;
  bool mEnded; // protected by mMonitor
  java::GeckoHlsResourceWrapper::GlobalRef mHlsResourceWrapper;
};

} // namespace mozilla
#endif /* HLSResource_h_ */
