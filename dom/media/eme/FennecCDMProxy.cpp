/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/FennecCDMProxy.h"
#include "mozIGeckoMediaPluginService.h"
#include "nsContentCID.h"
#include "nsString.h"
#include "mozilla/FennecMediaDrm.h"

namespace mozilla {

extern GMPSessionType ToGMPSessionType(dom::SessionType aSessionType);
extern dom::MediaKeyMessageType ToMediaKeyMessageType(GMPSessionMessageType aMessageType);

FennecCDMProxy::FennecCDMProxy(dom::MediaKeys* aKeys, const nsAString& aKeySystem)
  : mKeys(aKeys)
  , mKeySystem(aKeySystem)
  , mCDMThread(nullptr)
  , mCDM(nullptr)
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

  // Create a CDM Thread to work with.
  if (!mCDMThread) {
    nsresult rv = NS_NewNamedThread("FennecCDMThread", getter_AddRefs(mCDMThread));
    if (NS_FAILED(rv)) {
      RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                    NS_LITERAL_CSTRING("Couldn't create CDM thread FennecCDMProxy::Init"));
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

  mCDM = mozilla::MakeUnique<FennecMediaDrm>(mKeySystem);

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<InitData>>(this, &FennecCDMProxy::fmd_Init,
                                           Move(data)));
  mCDMThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
FennecCDMProxy::CreateSession(uint32_t aCreateSessionToken,
                              dom::SessionType aSessionType,
                              PromiseId aPromiseId,
                              const nsAString& aInitDataType,
                              nsTArray<uint8_t>& aInitData)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mCDMThread);
  EME_LOG("FennecCDMProxy::CreateSession >>>>>>>> ");

  nsAutoPtr<CreateSessionData> data(new CreateSessionData());
  data->mSessionType = aSessionType;
  data->mCreateSessionToken = aCreateSessionToken;
  data->mPromiseId = aPromiseId;
  data->mInitDataType = NS_ConvertUTF16toUTF8(aInitDataType);
  data->mInitData = Move(aInitData);

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<CreateSessionData>>(this,
                                                    &FennecCDMProxy::fmd_CreateSession,
                                                    data));
  mCDMThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
FennecCDMProxy::LoadSession(PromiseId aPromiseId,
                            const nsAString& aSessionId)
{
  // NOT IMPLEMENTED
}

void
FennecCDMProxy::SetServerCertificate(PromiseId aPromiseId,
                                     nsTArray<uint8_t>& aCert)
{
  // NOT IMPLEMENTED
}

void
FennecCDMProxy::UpdateSession(const nsAString& aSessionId,
                              PromiseId aPromiseId,
                              nsTArray<uint8_t>& aResponse)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mCDMThread);
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  nsAutoPtr<UpdateSessionData> data(new UpdateSessionData());
  data->mPromiseId = aPromiseId;
  data->mSessionId = NS_ConvertUTF16toUTF8(aSessionId);
  data->mResponse = Move(aResponse);

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<UpdateSessionData>>(this,
                                                    &FennecCDMProxy::fmd_UpdateSession,
                                                    data));
  mCDMThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
FennecCDMProxy::CloseSession(const nsAString& aSessionId,
                             PromiseId aPromiseId)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  nsAutoPtr<SessionOpData> data(new SessionOpData());
  data->mPromiseId = aPromiseId;
  data->mSessionId = NS_ConvertUTF16toUTF8(aSessionId);

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<SessionOpData>>(this,
                                                &FennecCDMProxy::fmd_CloseSession,
                                                data));
  mCDMThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
FennecCDMProxy::RemoveSession(const nsAString& aSessionId,
                              PromiseId aPromiseId)
{

}

void
FennecCDMProxy::Shutdown()
{
  if (mCDMThread) {
    mCDMThread->Shutdown();
    mCDMThread = nullptr;
  }
}

void
FennecCDMProxy::Terminated()
{

}

const nsCString&
FennecCDMProxy::GetNodeId() const
{
  EME_LOG("FennecCDMProxy::GetNodeId ====> ");
  return mNodeId;
}

void
FennecCDMProxy::OnSetSessionId(uint32_t aCreateSessionToken,
                               const nsAString& aSessionId)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }

  RefPtr<dom::MediaKeySession> session(mKeys->GetPendingSession(aCreateSessionToken));
  if (session) {
    session->SetSessionId(aSessionId);
  }
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
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  RefPtr<dom::MediaKeySession> session(mKeys->GetSession(aSessionId));
  if (session) {
    session->DispatchKeyMessage(ToMediaKeyMessageType(aMessageType), aMessage);
  }
}

void
FennecCDMProxy::OnExpirationChange(const nsAString& aSessionId,
                                   GMPTimestamp aExpiryTime)
{

}

void
FennecCDMProxy::OnSessionClosed(const nsAString& aSessionId)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  RefPtr<dom::MediaKeySession> session(mKeys->GetSession(aSessionId));
  if (session) {
    session->OnClosed();
  }
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
  // TODO: Implment the key status behavior to notify JS.
  EME_LOG("FennecCDMProxy::OnKeyStatusesChange ====> ");
  MOZ_ASSERT(NS_IsMainThread());
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

MediaCrypto::LocalRef
FennecCDMProxy::GetMediaCrypto()
{
  if (mCDM) {
    return mCDM->GetMediaCrypto();
  }
  return nullptr;
}

bool
FennecCDMProxy::IsSecureDecoderComponentRequired(const nsCString& aMIMEType)
{
  MOZ_ASSERT(mCDM);
  return mCDM->IsSecureDecoderComponentRequired(aMIMEType);
}

bool
FennecCDMProxy::IsAllowPlayback()
{
  MOZ_ASSERT(mCDM);
  return mCDM->IsAllowPlayback();
}

// ======== Private Implementation ========
void
FennecCDMProxy::OnCDMCreated(uint32_t aPromiseId)
{
  EME_LOG("FennecCDMProxy::OnCDMCreated ====> ");
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  //MOZ_ASSERT(!GetNodeId().IsEmpty());
  if (mCDM) {
    EME_LOG("FennecCDMProxy::OnCDMCreated ====> Ok mCDM(%x)", mCDM.get());
    mKeys->OnCDMCreated(aPromiseId, GetNodeId(), mCDM->GetPluginId());
    return;
  }

  // No CDM? Just reject the promise.
  EME_LOG("FennecCDMProxy::OnCDMCreated ====> No cdm");
  mKeys->RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                       NS_LITERAL_CSTRING("Null CDM in OnCDMCreated()"));
}

// ===================================

void
FennecCDMProxy::fmd_Init(nsAutoPtr<InitData>&& aData)
{
  EME_LOG("FennecCDMProxy::fmd_Init ====> ");
  MOZ_ASSERT(mCDM);

  mCallback = new CDMCallbackProxy(this);
  mCDM->Init(mCallback);
  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<uint32_t>(this,
                                &FennecCDMProxy::OnCDMCreated,
                                aData->mPromiseId));
  NS_DispatchToMainThread(task);
  EME_LOG("FennecCDMProxy::fmd_Init ====< ");
}

void
FennecCDMProxy::fmd_CreateSession(nsAutoPtr<CreateSessionData> aData)
{
  EME_LOG("FennecCDMProxy::fmd_CreateSession ====> ");
  MOZ_ASSERT(IsOnGMPThread());

  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_CreateSession"));
    return;
  }
  mCDM->CreateSession(aData->mCreateSessionToken,
                      aData->mPromiseId,
                      aData->mInitDataType,
                      aData->mInitData,
                      ToGMPSessionType(aData->mSessionType));
  EME_LOG("FennecCDMProxy::fmd_CreateSession ====< ");
}
void
FennecCDMProxy::fmd_UpdateSession(nsAutoPtr<UpdateSessionData> aData)
{
  EME_LOG("FennecCDMProxy::fmd_UpdateSession ====> ");
  MOZ_ASSERT(IsOnGMPThread());

  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_UpdateSession"));
    return;
  }
  mCDM->UpdateSession(aData->mPromiseId,
                      aData->mSessionId,
                      aData->mResponse);
  EME_LOG("FennecCDMProxy::fmd_UpdateSession ====< ");
}
void
FennecCDMProxy::fmd_CloseSession(nsAutoPtr<SessionOpData> aData)
{
  EME_LOG("FennecCDMProxy::fmd_CloseSession ====> ");
  MOZ_ASSERT(IsOnGMPThread());

  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_CloseSession"));
    return;
  }
  mCDM->CloseSession(aData->mPromiseId, aData->mSessionId);
  EME_LOG("FennecCDMProxy::fmd_CloseSession ====< ");
}

}
