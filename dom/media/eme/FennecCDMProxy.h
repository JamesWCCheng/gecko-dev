/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FennecCDMProxy_h_
#define FennecCDMProxy_h_

#include <jni.h>
#include "mozilla/jni/Types.h"
#include "GeneratedJNINatives.h"

#include "mozilla/CDMProxy.h"
#include "mozilla/CDMCaps.h"
#include "mozilla/dom/MediaKeys.h"
#include "mozilla/Monitor.h"
#include "mozilla/MozPromise.h"
#include "mozilla/UniquePtr.h"

#include "MediaCodec.h"
#include "nsIThread.h"
#include "nsString.h"
#include "nsAutoPtr.h"
#include "GMPDecryptorProxy.h"

using namespace mozilla::widget::sdk;

namespace mozilla {
class MediaRawData;
class CDMCallbackProxy;
class FennecMediaDrm;

namespace dom {
class MediaKeySession;
} // namespace dom

class FennecCDMProxy : public CDMProxy {
  typedef dom::PromiseId PromiseId;
  typedef dom::SessionType SessionType;
public:

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FennecCDMProxy)

  typedef MozPromise<DecryptResult, DecryptResult, /* IsExclusive = */ true> DecryptPromise;

  // Main thread only.
  FennecCDMProxy(dom::MediaKeys* aKeys, const nsAString& aKeySystem);

  // Main thread only.
  // Loads the CDM corresponding to mKeySystem.
  // Calls MediaKeys::OnCDMCreated() when the CDM is created.
  void Init(PromiseId aPromiseId,
            const nsAString& aOrigin,
            const nsAString& aTopLevelOrigin,
            const nsAString& aGMPName,
            bool aInPrivateBrowsing);

  // Main thread only.
  // Uses the CDM to create a key session.
  // Calls MediaKeys::OnSessionActivated() when session is created.
  // Assumes ownership of (Move()s) aInitData's contents.
  void CreateSession(uint32_t aCreateSessionToken,
                     dom::SessionType aSessionType,
                     PromiseId aPromiseId,
                     const nsAString& aInitDataType,
                     nsTArray<uint8_t>& aInitData);

  // Main thread only.
  // Uses the CDM to load a presistent session stored on disk.
  // Calls MediaKeys::OnSessionActivated() when session is loaded.
  void LoadSession(PromiseId aPromiseId,
                   const nsAString& aSessionId);

  // Main thread only.
  // Sends a new certificate to the CDM.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  // Assumes ownership of (Move()s) aCert's contents.
  void SetServerCertificate(PromiseId aPromiseId,
                            nsTArray<uint8_t>& aCert);

  // Main thread only.
  // Sends an update to the CDM.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  // Assumes ownership of (Move()s) aResponse's contents.
  void UpdateSession(const nsAString& aSessionId,
                     PromiseId aPromiseId,
                     nsTArray<uint8_t>& aResponse);

  // Main thread only.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  // If processing this operation results in the session actually closing,
  // we also call MediaKeySession::OnClosed(), which in turn calls
  // MediaKeys::OnSessionClosed().
  void CloseSession(const nsAString& aSessionId,
                    PromiseId aPromiseId);

  // Main thread only.
  // Removes all data for a persisent session.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  void RemoveSession(const nsAString& aSessionId,
                     PromiseId aPromiseId);

  // Main thread only.
  void Shutdown();

  // Main thread only.
  void Terminated();

  // Threadsafe.
  const nsCString& GetNodeId() const;

  // Main thread only.
  void OnSetSessionId(uint32_t aCreateSessionToken,
                      const nsAString& aSessionId);

  // Main thread only.
  void OnResolveLoadSessionPromise(uint32_t aPromiseId, bool aSuccess);

  // Main thread only.
  void OnSessionMessage(const nsAString& aSessionId,
                        GMPSessionMessageType aMessageType,
                        nsTArray<uint8_t>& aMessage);

  // Main thread only.
  void OnExpirationChange(const nsAString& aSessionId,
                          GMPTimestamp aExpiryTime);

  // Main thread only.
  void OnSessionClosed(const nsAString& aSessionId);

  // Main thread only.
  void OnSessionError(const nsAString& aSessionId,
                      nsresult aException,
                      uint32_t aSystemCode,
                      const nsAString& aMsg);

  // Main thread only.
  void OnSessionKeysChanged();

  // Main thread only.
  void OnRejectPromise(uint32_t aPromiseId,
                       nsresult aDOMException,
                       const nsCString& aMsg);

  RefPtr<DecryptPromise> Decrypt(MediaRawData* aSample);

  // Reject promise with DOMException corresponding to aExceptionCode.
  // Can be called from any thread.
  void RejectPromise(PromiseId aId, nsresult aExceptionCode,
                     const nsCString& aReason);

  // Resolves promise with "undefined".
  // Can be called from any thread.
  void ResolvePromise(PromiseId aId);

  // Threadsafe.
  const nsString& KeySystem() const;

  // GMP thread only.
  void gmp_Decrypted(uint32_t aId,
                     GMPErr aResult,
                     const nsTArray<uint8_t>& aDecryptedData);

  CDMCaps& Capabilites();

  // Main thread only.
  void OnKeyStatusesChange(const nsAString& aSessionId);

  void GetSessionIdsForKeyId(const nsTArray<uint8_t>& aKeyId,
                             nsTArray<nsCString>& aSessionIds);

#ifdef DEBUG
  bool IsOnGMPThread();
#endif

  mozilla::widget::sdk::MediaCrypto::LocalRef GetMediaCrypto();

  bool IsSecureDecoderComponentRequired(const nsCString& aMIMEType);
  bool IsAllowPlayback();

private:
  virtual ~FennecCDMProxy();

  // Main thread only.
  void OnCDMCreated(uint32_t aPromiseId);

  // Helper to enforce that a raw pointer is only accessed on the main thread.
  template<class Type>
  class MainThreadOnlyRawPtr {
  public:
    explicit MainThreadOnlyRawPtr(Type* aPtr)
      : mPtr(aPtr)
    {
      MOZ_ASSERT(NS_IsMainThread());
    }

    bool IsNull() const {
      MOZ_ASSERT(NS_IsMainThread());
      return !mPtr;
    }

    void Clear() {
      MOZ_ASSERT(NS_IsMainThread());
      mPtr = nullptr;
    }

    Type* operator->() const MOZ_NO_ADDREF_RELEASE_ON_RETURN {
      MOZ_ASSERT(NS_IsMainThread());
      return mPtr;
    }
  private:
    Type* mPtr;
  };

  // Our reference back to the MediaKeys object.
  // WARNING: This is a non-owning reference that is cleared by MediaKeys
  // destructor. only use on main thread, and always nullcheck before using!
  MainThreadOnlyRawPtr<dom::MediaKeys> mKeys;

  const nsString mKeySystem;

  // Gecko Media Plugin thread. All interactions with the out-of-process
  // EME plugin must come from this thread.
  RefPtr<nsIThread> mGMPThread;

  nsCString mNodeId;

  //FennecMediaDrm* mCDM;
  mozilla::UniquePtr<FennecMediaDrm> mCDM;

  nsAutoPtr<CDMCallbackProxy> mCallback;

  class DecryptJob {
  public:
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(DecryptJob)

    explicit DecryptJob(MediaRawData* aSample)
      : mId(0)
      , mSample(aSample)
    {
    }

    void PostResult(GMPErr aResult, const nsTArray<uint8_t>& aDecryptedData);
    void PostResult(GMPErr aResult);

    RefPtr<DecryptPromise> Ensure() {
      return mPromise.Ensure(__func__);
    }

    uint32_t mId;
    RefPtr<MediaRawData> mSample;
  private:
    ~DecryptJob() {}
    MozPromiseHolder<DecryptPromise> mPromise;
  };

  CDMCaps mCapabilites;

// =====================================================================
// For FennecMediaDrm
  void fmd_Init(nsAutoPtr<InitData>&& aData);
  void fmd_CreateSession(nsAutoPtr<CreateSessionData> aData);
  void fmd_UpdateSession(nsAutoPtr<UpdateSessionData> aData);
  void fmd_CloseSession(nsAutoPtr<SessionOpData> aData);
// =====================================================================

};


} // namespace mozilla

#endif // FennecCDMProxy_h_
