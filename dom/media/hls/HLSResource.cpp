/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HLSResource.h"
#include "HLSUtils.h"

using namespace mozilla::java;

namespace mozilla {

HlsResourceCallbacksSupport::HlsResourceCallbacksSupport(HLSResource* aResource)
{
  MOZ_ASSERT(aResource);
  mResource = aResource;
}

void
HlsResourceCallbacksSupport::OnDataArrived()
{
  MOZ_ASSERT(mResource);
  mResource->onDataAvaiable();
}

void
HlsResourceCallbacksSupport::OnResourceError(int aErrorCode)
{
  MOZ_ASSERT(mResource);
}

HLSResource::HLSResource(MediaResourceCallback* aCallback,
                         nsIChannel* aChannel,
                         nsIURI* aURI,
                         const MediaContainerType& aContainerType)
  : BaseMediaResource(aCallback, aChannel, aURI, aContainerType)
  , mMonitor("HLSResource")
  , mEnded(false)
{
  nsCString spec;
  nsresult rv = aURI->GetSpec(spec);
  (void)rv;
  HLS_DEBUG("HLSResource", "aContainerType = %s, aURI->GetSpec = %s",
            aContainerType.OriginalString().Data(), spec.get());
  HlsResourceCallbacksSupport::Init();
  mJavaCallbacks = GeckoHlsResourceWrapper::HlsResourceCallbacks::New();
  HlsResourceCallbacksSupport::AttachNative(mJavaCallbacks,
                                            mozilla::MakeUnique<HlsResourceCallbacksSupport>(this));
  mHlsResourceWrapper = java::GeckoHlsResourceWrapper::Create(NS_ConvertUTF8toUTF16(spec),
                                                              mJavaCallbacks);
  MOZ_ASSERT(mHlsResourceWrapper);
}

void
HLSResource::onDataAvaiable()
{
  MOZ_ASSERT(mCallback);
  HLS_DEBUG("HLSResource", "onDataAvaiable");
  mCallback->NotifyDataArrived();
}

HLSResource::~HLSResource()
{
  if (mJavaCallbacks) {
    HlsResourceCallbacksSupport::DisposeNative(mJavaCallbacks);
    mJavaCallbacks = nullptr;
  }
  HLS_DEBUG("HLSResource", "Destructor");
}

} // namespace mozilla
