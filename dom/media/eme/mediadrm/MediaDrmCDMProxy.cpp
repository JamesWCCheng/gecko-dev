/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MediaKeySession.h"
#include "mozilla/MediaDrmCDMProxy.h"
#include "MediaDrmCDMCallbackProxy.h"

using namespace mozilla::java::sdk;

namespace mozilla {

MediaDrmSessionType
ToMediaDrmSessionType(dom::MediaKeySessionType aSessionType)
{
  switch (aSessionType) {
    case dom::MediaKeySessionType::Temporary: return kKeyStreaming;
    case dom::MediaKeySessionType::Persistent_license: return kKeyOffline;
    default: return kKeyStreaming;
  };
}

MediaDrmCDMProxy::MediaDrmCDMProxy(dom::MediaKeys* aKeys,
                                   const nsAString& aKeySystem,
                                   bool aDistinctiveIdentifierRequired,
                                   bool aPersistentStateRequired)
  : CDMProxy(aKeys,
             aKeySystem,
             aDistinctiveIdentifierRequired,
             aPersistentStateRequired)
  , mCDM(nullptr)
  , mShutdownCalled(false)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_COUNT_CTOR(MediaDrmCDMProxy);
}

MediaDrmCDMProxy::~MediaDrmCDMProxy()
{
  MOZ_COUNT_DTOR(MediaDrmCDMProxy);
}

void
MediaDrmCDMProxy::Init(PromiseId aPromiseId,
                       const nsAString& aOrigin,
                       const nsAString& aTopLevelOrigin,
                       const nsAString& aGMPName,
                       bool aInPrivateBrowsing)
// [TODO] Refactor the aGMPName related code
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  EME_LOG("MediaDrmCDMProxy::Init (%s, %s) %s",
          NS_ConvertUTF16toUTF8(aOrigin).get(),
          NS_ConvertUTF16toUTF8(aTopLevelOrigin).get(),
          (aInPrivateBrowsing ? "PrivateBrowsing" : "NonPrivateBrowsing"));

  // Create a thread to work with cdm.
  if (!mOwnerThread) {
    nsresult rv = NS_NewNamedThread("MDrmCDMThread", getter_AddRefs(mOwnerThread));
    if (NS_FAILED(rv)) {
      RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                    NS_LITERAL_CSTRING("Couldn't create CDM thread MediaDrmCDMProxy::Init"));
      return;
    }
  }

//  [TODO] Check the necessity of InitData structure
//  nsAutoPtr<InitData> data(new InitData());
//  data->mPromiseId = aPromiseId;
//  data->mOrigin = aOrigin;
//  data->mTopLevelOrigin = aTopLevelOrigin;
//  data->mGMPName = aGMPName;
//  data->mInPrivateBrowsing = aInPrivateBrowsing;
//  [TODO] For MediaDrm, how to verirfy Orgin/TopLevelOrgin stuff
  mCDM = mozilla::MakeUnique<MediaDrmBridgeNative>(mKeySystem);

//  [TODO] Check the necessity of InitData structure
//  nsCOMPtr<nsIRunnable> task(
//    NewRunnableMethod<nsAutoPtr<InitData>>(this, &MediaDrmCDMProxy::md_Init,
//                                           Move(data)));
  nsCOMPtr<nsIRunnable> task(NewRunnableMethod<uint32_t>(this,
                                                         &MediaDrmCDMProxy::md_Init,
                                                         aPromiseId));
  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
MediaDrmCDMProxy::CreateSession(uint32_t aCreateSessionToken,
                                MediaKeySessionType aSessionType,
                                PromiseId aPromiseId,
                                const nsAString& aInitDataType,
                                nsTArray<uint8_t>& aInitData)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerThread);

  nsAutoPtr<CreateSessionData> data(new CreateSessionData());
  data->mSessionType = aSessionType;
  data->mCreateSessionToken = aCreateSessionToken;
  data->mPromiseId = aPromiseId;
  data->mInitDataType = NS_ConvertUTF16toUTF8(aInitDataType);
  data->mInitData = Move(aInitData);

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<CreateSessionData>>(this,
                                                    &MediaDrmCDMProxy::md_CreateSession,
                                                    data));
  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
MediaDrmCDMProxy::LoadSession(PromiseId aPromiseId,
                            const nsAString& aSessionId)
{
  // [TODO] Need confirmation. Should not enter here.
  MOZ_ASSERT(false);
}

void
MediaDrmCDMProxy::SetServerCertificate(PromiseId aPromiseId,
                                     nsTArray<uint8_t>& aCert)
{
  // [TODO] Need confirmation. Should not enter here.
  MOZ_ASSERT(false);
}

void
MediaDrmCDMProxy::UpdateSession(const nsAString& aSessionId,
                              PromiseId aPromiseId,
                              nsTArray<uint8_t>& aResponse)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerThread);
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  nsAutoPtr<UpdateSessionData> data(new UpdateSessionData());
  data->mPromiseId = aPromiseId;
  data->mSessionId = NS_ConvertUTF16toUTF8(aSessionId);
  data->mResponse = Move(aResponse);

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<UpdateSessionData>>(this,
                                                    &MediaDrmCDMProxy::md_UpdateSession,
                                                    data));
  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
MediaDrmCDMProxy::CloseSession(const nsAString& aSessionId,
                             PromiseId aPromiseId)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerThread);
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  nsAutoPtr<SessionOpData> data(new SessionOpData());
  data->mPromiseId = aPromiseId;
  data->mSessionId = NS_ConvertUTF16toUTF8(aSessionId);

  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<nsAutoPtr<SessionOpData>>(this,
                                                &MediaDrmCDMProxy::md_CloseSession,
                                                data));
  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void
MediaDrmCDMProxy::RemoveSession(const nsAString& aSessionId,
                              PromiseId aPromiseId)
{
//  [TODO] Need confirmation. Should not enter here.
  MOZ_ASSERT(false);
}

void
MediaDrmCDMProxy::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerThread);
  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod(this, &MediaDrmCDMProxy::md_Shutdown));

  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
//  [TODO] Check if the shutdowm task is handled
  mOwnerThread->Shutdown();
  mOwnerThread = nullptr;
}

void
MediaDrmCDMProxy::Terminated()
{
//+
}

const nsCString&
MediaDrmCDMProxy::GetNodeId() const
{
  return mNodeId;
}

void
MediaDrmCDMProxy::OnSetSessionId(uint32_t aCreateSessionToken,
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
MediaDrmCDMProxy::OnResolveLoadSessionPromise(uint32_t aPromiseId, bool aSuccess)
{
//  [TODO] Need confirmation. Should not enter here.
  MOZ_ASSERT(false);
}

void
MediaDrmCDMProxy::OnSessionMessage(const nsAString& aSessionId,
                                   dom::MediaKeyMessageType aMessageType,
                                   nsTArray<uint8_t>& aMessage)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  RefPtr<dom::MediaKeySession> session(mKeys->GetSession(aSessionId));
  if (session) {
    session->DispatchKeyMessage(aMessageType, aMessage);
  }
}

// [TODO] Replace GMPTimestamp ?
void
MediaDrmCDMProxy::OnExpirationChange(const nsAString& aSessionId,
                                     GMPTimestamp aExpiryTime)
{
//+
}

void
MediaDrmCDMProxy::OnSessionClosed(const nsAString& aSessionId)
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
MediaDrmCDMProxy::OnSessionError(const nsAString& aSessionId,
                                 nsresult aException,
                                 uint32_t aSystemCode,
                                 const nsAString& aMsg)
{
// [TODO] Not Implement yet.
}

void
MediaDrmCDMProxy::OnRejectPromise(uint32_t aPromiseId,
                                  nsresult aDOMException,
                                  const nsCString& aMsg)
{
  MOZ_ASSERT(NS_IsMainThread());
  RejectPromise(aPromiseId, aDOMException, aMsg);
}

RefPtr<MediaDrmCDMProxy::DecryptPromise>
MediaDrmCDMProxy::Decrypt(MediaRawData* aSample)
{
  // [TODO] Need confirmation. Should not enter here.
  MOZ_ASSERT(false);
  return nullptr;
}

void
MediaDrmCDMProxy::OnDecrypted(uint32_t aId,
                              DecryptStatus aResult,
                              const nsTArray<uint8_t>& aDecryptedData)
{
  // [TODO] Need confirmation. Should not enter here.
  MOZ_ASSERT(false);
}

//void
//MediaDrmCDMProxy::OnSessionKeysChanged()
//{
//+
//}

void
MediaDrmCDMProxy::RejectPromise(PromiseId aId, nsresult aCode,
                                const nsCString& aReason)
{
//  [TODO] Merge the following with GMPCDMProxy::RejectPromise
  if (NS_IsMainThread()) {
    if (!mKeys.IsNull()) {
      mKeys->RejectPromise(aId, aCode, aReason);
    }
  } else {
    nsCOMPtr<nsIRunnable> task(new RejectPromiseTask(this, aId, aCode,
                                                     aReason));
    NS_DispatchToMainThread(task);
  }
}

void
MediaDrmCDMProxy::ResolvePromise(PromiseId aId)
{
//  [TODO] Merge the following with GMPDCDMProxy::ResolvePromise
  if (NS_IsMainThread()) {
    if (!mKeys.IsNull()) {
      mKeys->ResolvePromise(aId);
    } else {
      NS_WARNING("MediaDrmCDMProxy unable to resolve promise!");
    }
  } else {
    nsCOMPtr<nsIRunnable> task;
    task = NewRunnableMethod<PromiseId>(this,
                                        &MediaDrmCDMProxy::ResolvePromise,
                                        aId);
    NS_DispatchToMainThread(task);
  }
}

const nsString&
MediaDrmCDMProxy::KeySystem() const
{
  return mKeySystem;
}

CDMCaps&
MediaDrmCDMProxy::Capabilites()
{
//  [TODO] Check the necessity
  return mCapabilites;
}

void
MediaDrmCDMProxy::OnKeyStatusesChange(const nsAString& aSessionId)
{
//  [TODO] Implement the key status behavior to notify JS.
  MOZ_ASSERT(NS_IsMainThread());
}

void
MediaDrmCDMProxy::GetSessionIdsForKeyId(const nsTArray<uint8_t>& aKeyId,
                                      nsTArray<nsCString>& aSessionIds)
{
//  [TODO] Implementation
}

#ifdef DEBUG
bool
MediaDrmCDMProxy::IsOnOwnerThread()
{
  return NS_GetCurrentThread() == mOwnerThread;
}
#endif

MediaCrypto::LocalRef
MediaDrmCDMProxy::GetMediaCrypto()
{
  if (mCDM) {
    //return mCDM->GetMediaCrypto();
  }
  return nullptr;
}

bool
MediaDrmCDMProxy::IsSecureDecoderComponentRequired(const nsCString& aMIMEType)
{
  MOZ_ASSERT(mCDM);
  return mCDM->IsSecureDecoderComponentRequired(aMIMEType);
}

//+bool
//+MediaDrmCDMProxy::IsAllowPlayback()
//+{
//+  MOZ_ASSERT(mCDM);
//+  return mCDM->IsAllowPlayback();
//+}

// ======== Private Implementation ========
void
MediaDrmCDMProxy::OnCDMCreated(uint32_t aPromiseId)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
//  [TODO] Check if NodeId is required for MediaDrm
//  MOZ_ASSERT(!GetNodeId().IsEmpty());
  if (mCDM) {
//    [TODO] Provide PluginId, and Node Id, GetNodeId(), mCDM->GetPluginId()
    mKeys->OnCDMCreated(aPromiseId, GetNodeId(), 0);
    return;
  }

  // No CDM? Just reject the promise.
  mKeys->RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                       NS_LITERAL_CSTRING("Null CDM in OnCDMCreated()"));
}

void
MediaDrmCDMProxy::md_Init(uint32_t aPromiseId)
{
  MOZ_ASSERT(IsOnOwnerThread());
  MOZ_ASSERT(mCDM);

  mCallback = new MediaDrmCDMCallbackProxy(this);
  mCDM->Init(mCallback);
  nsCOMPtr<nsIRunnable> task(
    NewRunnableMethod<uint32_t>(this,
                                &MediaDrmCDMProxy::OnCDMCreated,
                                aPromiseId));
  NS_DispatchToMainThread(task);
}

void
MediaDrmCDMProxy::md_CreateSession(nsAutoPtr<CreateSessionData> aData)
{
  MOZ_ASSERT(IsOnOwnerThread());

  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_CreateSession"));
    return;
  }

  mCDM->CreateSession(aData->mCreateSessionToken,
                      aData->mPromiseId,
                      aData->mInitDataType,
                      aData->mInitData,
                      ToMediaDrmSessionType(aData->mSessionType));
}

void
MediaDrmCDMProxy::md_UpdateSession(nsAutoPtr<UpdateSessionData> aData)
{
  MOZ_ASSERT(IsOnOwnerThread());

  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_UpdateSession"));
    return;
  }
  bool success = mCDM->UpdateSession(aData->mPromiseId,
                                     aData->mSessionId,
                                     aData->mResponse);
  if (!success) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING(" ProvideKeyResponse error !!!"));
  }
}

void
MediaDrmCDMProxy::md_CloseSession(nsAutoPtr<SessionOpData> aData)
{
  MOZ_ASSERT(IsOnOwnerThread());

  if (!mCDM) {
    RejectPromise(aData->mPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                  NS_LITERAL_CSTRING("Null CDM in fmd_CloseSession"));
    return;
  }
  mCDM->CloseSession(aData->mPromiseId, aData->mSessionId);
}

void
MediaDrmCDMProxy::md_Shutdown()
{
  MOZ_ASSERT(IsOnOwnerThread());
  MOZ_ASSERT(mCDM);
  if (mShutdownCalled) {
    return;
  }
  mShutdownCalled = true;
  mCDM->Shutdown();
}

} // namespace mozilla
