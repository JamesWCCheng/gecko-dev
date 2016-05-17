/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/FennecCDMProxy.h"
#include "mozIGeckoMediaPluginService.h"
#include "nsContentCID.h"
#include "nsString.h"
#include "mozilla/MediaDrmAdaptor.h"

namespace mozilla {

FennecCDMProxy::FennecCDMProxy(dom::MediaKeys* aKeys, const nsAString& aKeySystem)
  : mKeys(aKeys)
  , mKeySystem(aKeySystem)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_COUNT_CTOR(FennecCDMProxy);
}

FennecCDMProxy::~FennecCDMProxy()
{
  MOZ_COUNT_DTOR(FennecCDMProxy);
}

void
FennecCDMProxy::Init(PromiseId aPromiseId,
                     const nsAString& aOrigin,
                     const nsAString& aTopLevelOrigin,
                     const nsAString& aGMPName,
                     bool aInPrivateBrowsing)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  EME_LOG("FennecCDMProxy::Init (%s, %s) %s",
          NS_ConvertUTF16toUTF8(aOrigin).get(),
          NS_ConvertUTF16toUTF8(aTopLevelOrigin).get(),
          (aInPrivateBrowsing ? "PrivateBrowsing" : "NonPrivateBrowsing"));

  // Obtain GMPThread to work with OR we could create a new thread ?
  if (!mGMPThread) {
    nsCOMPtr<mozIGeckoMediaPluginService> mps =
      do_GetService("@mozilla.org/gecko-media-plugin-service;1");
    if (!mps) {
      RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                    NS_LITERAL_CSTRING("Couldn't get MediaPluginService in FennecCDMProxy::Init"));
      return;
    }
    mps->GetThread(getter_AddRefs(mGMPThread));
    if (!mGMPThread) {
      RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                    NS_LITERAL_CSTRING("Couldn't get GMP thread FennecCDMProxy::Init"));
      return;
    }
  }

  // =========================
  // TODO : Check there's DRM machinery existing via MediaDrm API
  // =========================

  // Initialize MediaDrmAdaptor
  nsAutoPtr<InitData> data(new InitData());
  data->mPromiseId = aPromiseId;
  data->mOrigin = aOrigin;
  data->mTopLevelOrigin = aTopLevelOrigin;
  data->mGMPName = aGMPName;
  data->mInPrivateBrowsing = aInPrivateBrowsing;
  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<InitData>>(this,
                                           &FennecCDMProxy::mediaDrm_Init,
                                           Move(data)));
  mGMPThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
FennecCDMProxy::CreateSession(uint32_t aCreateSessionToken,
                              dom::SessionType aSessionType,
                              PromiseId aPromiseId,
                              const nsAString& aInitDataType,
                              nsTArray<uint8_t>& aInitData)
{

}

void
FennecCDMProxy::LoadSession(PromiseId aPromiseId,
                            const nsAString& aSessionId)
{

}

void
FennecCDMProxy::SetServerCertificate(PromiseId aPromiseId,
                                     nsTArray<uint8_t>& aCert)
{

}

void
FennecCDMProxy::UpdateSession(const nsAString& aSessionId,
                              PromiseId aPromiseId,
                              nsTArray<uint8_t>& aResponse)
{

}

void
FennecCDMProxy::CloseSession(const nsAString& aSessionId,
                             PromiseId aPromiseId)
{

}

void
FennecCDMProxy::RemoveSession(const nsAString& aSessionId,
                              PromiseId aPromiseId)
{

}

void
FennecCDMProxy::Shutdown()
{

}

void
FennecCDMProxy::Terminated()
{

}

const nsCString&
FennecCDMProxy::GetNodeId() const
{
  return mNodeId;
}

void
FennecCDMProxy::OnSetSessionId(uint32_t aCreateSessionToken,
                               const nsAString& aSessionId)
{

}

void
FennecCDMProxy::OnResolveLoadSessionPromise(uint32_t aPromiseId, bool aSuccess)
{

}

void
FennecCDMProxy::OnSessionMessage(const nsAString& aSessionId,
                                 GMPSessionMessageType aMessageType,
                                 nsTArray<uint8_t>& aMessage)
{

}

void
FennecCDMProxy::OnExpirationChange(const nsAString& aSessionId,
                                   GMPTimestamp aExpiryTime)
{

}

void
FennecCDMProxy::OnSessionClosed(const nsAString& aSessionId)
{

}

void
FennecCDMProxy::OnSessionError(const nsAString& aSessionId,
                               nsresult aException,
                               uint32_t aSystemCode,
                               const nsAString& aMsg)
{

}

void
FennecCDMProxy::OnRejectPromise(uint32_t aPromiseId,
                                nsresult aDOMException,
                                const nsCString& aMsg)
{

}

RefPtr<FennecCDMProxy::DecryptPromise>
FennecCDMProxy::Decrypt(MediaRawData* aSample)
{
  RefPtr<DecryptJob> job(new DecryptJob(aSample));
  RefPtr<DecryptPromise> promise(job->Ensure());
  return promise;
}

void
FennecCDMProxy::RejectPromise(PromiseId aId, nsresult aExceptionCode,
                              const nsCString& aReason)
{

}

void
FennecCDMProxy::ResolvePromise(PromiseId aId)
{

}

const nsString&
FennecCDMProxy::KeySystem() const
{
  return mKeySystem;
}

void
FennecCDMProxy::gmp_Decrypted(uint32_t aId,
                              GMPErr aResult,
                              const nsTArray<uint8_t>& aDecryptedData)
{

}

CDMCaps&
FennecCDMProxy::Capabilites()
{
  return mCapabilites;
}

void
FennecCDMProxy::OnKeyStatusesChange(const nsAString& aSessionId)
{

}

void
FennecCDMProxy::GetSessionIdsForKeyId(const nsTArray<uint8_t>& aKeyId,
                                      nsTArray<nsCString>& aSessionIds)
{

}

#ifdef DEBUG
bool
FennecCDMProxy::IsOnGMPThread()
{
  return true;
}
#endif

// ======== Private Implementation ========

void
FennecCDMProxy::mediaDrm_Init(nsAutoPtr<InitData>&& aData)
{
  EME_LOG("FennecCDMProxy::mediaDrm_Init ====> ");

  auto mCDM = new MediaDrmAdaptor();
  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Failed to return a CDM (MediaDrmAdaptor)"));
    return;
  }

  mCallback = new CDMCallbackProxy(this);
  mCDM->Init(mCallback);
  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<uint32_t>(this,
                                &FennecCDMProxy::OnCDMCreated,
                                aData->mPromiseId));
  NS_DispatchToMainThread(task);
}

void
FennecCDMProxy::OnCDMCreated(uint32_t aPromiseId)
{
  EME_LOG("FennecCDMProxy::OnCDMCreated ====> ");
}

}
