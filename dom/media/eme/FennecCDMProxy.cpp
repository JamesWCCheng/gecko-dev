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
#include "mozilla/FennecMediaDrm.h"

namespace mozilla {

extern GMPSessionType ToGMPSessionType(dom::SessionType aSessionType);
extern dom::MediaKeyMessageType ToMediaKeyMessageType(GMPSessionMessageType aMessageType);

FennecCDMProxy::FennecCDMProxy(dom::MediaKeys* aKeys, const nsAString& aKeySystem)
  : mKeys(aKeys)
  , mKeySystem(aKeySystem)
  , mGMPThread(nullptr)
  , mCDM(nullptr)
  , mJavaMDrm(false)
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

  auto ptrFunc = mJavaMDrm ?
    &FennecCDMProxy::fmd_Init :
    &FennecCDMProxy::mediaDrm_Init;

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<InitData>>(this, ptrFunc, Move(data)));
  mGMPThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
FennecCDMProxy::CreateSession(uint32_t aCreateSessionToken,
                              dom::SessionType aSessionType,
                              PromiseId aPromiseId,
                              const nsAString& aInitDataType,
                              nsTArray<uint8_t>& aInitData)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mGMPThread);
  EME_LOG("FennecCDMProxy::CreateSession >>>>>>>> ");

  nsAutoPtr<CreateSessionData> data(new CreateSessionData());
  data->mSessionType = aSessionType;
  data->mCreateSessionToken = aCreateSessionToken;
  data->mPromiseId = aPromiseId;
  data->mInitDataType = NS_ConvertUTF16toUTF8(aInitDataType);
  data->mInitData = Move(aInitData);

  auto ptrFunc = mJavaMDrm ?
    &FennecCDMProxy::fmd_CreateSession :
    &FennecCDMProxy::mediaDrm_CreateSession;

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<CreateSessionData>>(this, ptrFunc, data));
  mGMPThread->Dispatch(task, NS_DISPATCH_NORMAL);
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
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mGMPThread);
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  nsAutoPtr<UpdateSessionData> data(new UpdateSessionData());
  data->mPromiseId = aPromiseId;
  data->mSessionId = NS_ConvertUTF16toUTF8(aSessionId);
  data->mResponse = Move(aResponse);

  auto ptrFunc = mJavaMDrm ?
    &FennecCDMProxy::fmd_UpdateSession :
    &FennecCDMProxy::mediaDrm_UpdateSession;

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<UpdateSessionData>>(this, ptrFunc, data));
  mGMPThread->Dispatch(task, NS_DISPATCH_NORMAL);
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

  auto ptrFunc = mJavaMDrm ?
    &FennecCDMProxy::fmd_CloseSession :
    &FennecCDMProxy::mediaDrm_CloseSession;

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<SessionOpData>>(this, ptrFunc, data));
  mGMPThread->Dispatch(task, NS_DISPATCH_NORMAL);
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
    MediaDrmAdaptor* mda = static_cast<MediaDrmAdaptor*>(mCDM);
    return mda->GetMediaCrypto();
  } else if (mJavaMDrm && mMediaDrmCDM) {
    return mMediaDrmCDM->GetMediaCrypto();
  }
  return nullptr;
}

// ======== Private Implementation ========

void
FennecCDMProxy::mediaDrm_Init(nsAutoPtr<InitData>&& aData)
{
  EME_LOG("FennecCDMProxy::mediaDrm_Init ====> ");

  mCDM = new MediaDrmAdaptor();
  EME_LOG("FennecCDMProxy::mediaDrm_Init ====> mCDM(%x)", mCDM);
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
FennecCDMProxy::mediaDrm_CreateSession(nsAutoPtr<CreateSessionData> aData)
{
  MOZ_ASSERT(IsOnGMPThread());
  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in mediaDrm_CreateSession"));
    return;
  }
  mCDM->CreateSession(aData->mCreateSessionToken,
                      aData->mPromiseId,
                      aData->mInitDataType,
                      aData->mInitData,
                      ToGMPSessionType(aData->mSessionType));
}

void
FennecCDMProxy::mediaDrm_UpdateSession(nsAutoPtr<UpdateSessionData> aData)
{
  MOZ_ASSERT(IsOnGMPThread());
  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in mediaDrm_UpdateSession"));
    return;
  }
  mCDM->UpdateSession(aData->mPromiseId,
                      aData->mSessionId,
                      aData->mResponse);
}

void
FennecCDMProxy::mediaDrm_CloseSession(nsAutoPtr<SessionOpData> aData)
{
  MOZ_ASSERT(IsOnGMPThread());
  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in mediaDrm_CloseSession"));
    return;
  }
  mCDM->CloseSession(aData->mPromiseId, aData->mSessionId);
}

void
FennecCDMProxy::OnCDMCreated(uint32_t aPromiseId)
{
  EME_LOG("FennecCDMProxy::OnCDMCreated ====> ");
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  //MOZ_ASSERT(!GetNodeId().IsEmpty());
  if (mJavaMDrm && mMediaDrmCDM) {
    EME_LOG("FennecCDMProxy::OnCDMCreated ====> Ok mMediaDrmCDM(%x)", mMediaDrmCDM.get());
    mKeys->OnCDMCreated(aPromiseId, GetNodeId(), mMediaDrmCDM->GetPluginId());
    return;
  } else if (mCDM) {
    EME_LOG("FennecCDMProxy::OnCDMCreated ====> Ok mCDM(%x)", mCDM);
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
  mMediaDrmCDM = mozilla::MakeUnique<FennecMediaDrm>();
  MOZ_ASSERT(mMediaDrmCDM);

  mCallback = new CDMCallbackProxy(this);
  mMediaDrmCDM->Init(mCallback);
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

  if (!mMediaDrmCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_CreateSession"));
    return;
  }
  mMediaDrmCDM->CreateSession(aData->mCreateSessionToken,
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

  if (!mMediaDrmCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_UpdateSession"));
    return;
  }
  mMediaDrmCDM->UpdateSession(aData->mPromiseId,
                              aData->mSessionId,
                              aData->mResponse);
  EME_LOG("FennecCDMProxy::fmd_UpdateSession ====< ");
}
void
FennecCDMProxy::fmd_CloseSession(nsAutoPtr<SessionOpData> aData)
{
  EME_LOG("FennecCDMProxy::fmd_CloseSession ====> ");
  MOZ_ASSERT(IsOnGMPThread());

  if (!mMediaDrmCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_CloseSession"));
    return;
  }
  mMediaDrmCDM->CloseSession(aData->mPromiseId, aData->mSessionId);
  EME_LOG("FennecCDMProxy::fmd_CloseSession ====< ");
}

}
