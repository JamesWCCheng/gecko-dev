/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CDMProxy_h_
#define CDMProxy_h_

#include "mozilla/CDMCaps.h"
#include "mozilla/MozPromise.h"
#include "mozilla/dom/MediaKeys.h"
#include "mozilla/dom/MediaKeySession.h"
#include "nsString.h"

namespace mozilla {
class MediaRawData;

struct DecryptResult {
  DecryptResult(GMPErr aStatus, MediaRawData* aSample)
    : mStatus(aStatus)
    , mSample(aSample)
  {}
  GMPErr mStatus;
  RefPtr<MediaRawData> mSample;
};

extern GMPSessionType ToGMPSessionType(dom::SessionType aSessionType);

extern dom::MediaKeyMessageType ToMediaKeyMessageType(GMPSessionMessageType aMessageType);

// Proxies calls GMP/CDM, and proxies calls back.
// Note: Promises are passed in via a PromiseId, so that the ID can be
// passed via IPC to the CDM, which can then signal when to reject or
// resolve the promise using its PromiseId.
class CDMProxy {
  typedef dom::PromiseId PromiseId;
  typedef dom::SessionType SessionType;
public:

  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) = 0;
  NS_IMETHOD_(MozExternalRefCountType) Release(void) = 0;

  typedef MozPromise<DecryptResult, DecryptResult, /* IsExclusive = */ true> DecryptPromise;

  // Main thread only.
  // Loads the CDM corresponding to mKeySystem.
  // Calls MediaKeys::OnCDMCreated() when the CDM is created.
  virtual void Init(PromiseId aPromiseId,
                    const nsAString& aOrigin,
                    const nsAString& aTopLevelOrigin,
                    const nsAString& aGMPName,
                    bool aInPrivateBrowsing) = 0;

  // Main thread only.
  // Uses the CDM to create a key session.
  // Calls MediaKeys::OnSessionActivated() when session is created.
  // Assumes ownership of (Move()s) aInitData's contents.
  virtual void CreateSession(uint32_t aCreateSessionToken,
                             dom::SessionType aSessionType,
                             PromiseId aPromiseId,
                             const nsAString& aInitDataType,
                             nsTArray<uint8_t>& aInitData) = 0;

  // Main thread only.
  // Uses the CDM to load a presistent session stored on disk.
  // Calls MediaKeys::OnSessionActivated() when session is loaded.
  virtual void LoadSession(PromiseId aPromiseId,
                           const nsAString& aSessionId) = 0;

  // Main thread only.
  // Sends a new certificate to the CDM.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  // Assumes ownership of (Move()s) aCert's contents.
  virtual void SetServerCertificate(PromiseId aPromiseId,
                                    nsTArray<uint8_t>& aCert) = 0;

  // Main thread only.
  // Sends an update to the CDM.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  // Assumes ownership of (Move()s) aResponse's contents.
  virtual void UpdateSession(const nsAString& aSessionId,
                             PromiseId aPromiseId,
                             nsTArray<uint8_t>& aResponse) = 0;

  // Main thread only.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  // If processing this operation results in the session actually closing,
  // we also call MediaKeySession::OnClosed(), which in turn calls
  // MediaKeys::OnSessionClosed().
  virtual void CloseSession(const nsAString& aSessionId,
                            PromiseId aPromiseId) = 0;

  // Main thread only.
  // Removes all data for a persisent session.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  virtual void RemoveSession(const nsAString& aSessionId,
                             PromiseId aPromiseId) = 0;

  // Main thread only.
  virtual void Shutdown() = 0;

  // Main thread only.
  virtual void Terminated() = 0;

  // Threadsafe.
  virtual const nsCString& GetNodeId() const = 0;

  // Main thread only.
  virtual void OnSetSessionId(uint32_t aCreateSessionToken,
                              const nsAString& aSessionId) = 0;

  // Main thread only.
  virtual void OnResolveLoadSessionPromise(uint32_t aPromiseId, bool aSuccess) = 0;

  // Main thread only.
  virtual void OnSessionMessage(const nsAString& aSessionId,
                                GMPSessionMessageType aMessageType,
                                nsTArray<uint8_t>& aMessage) = 0;

  // Main thread only.
  virtual void OnExpirationChange(const nsAString& aSessionId,
                                  GMPTimestamp aExpiryTime) = 0;

  // Main thread only.
  virtual void OnSessionClosed(const nsAString& aSessionId) = 0;

  // Main thread only.
  virtual void OnSessionError(const nsAString& aSessionId,
                              nsresult aException,
                              uint32_t aSystemCode,
                              const nsAString& aMsg) = 0;

  // Main thread only.
  virtual void OnRejectPromise(uint32_t aPromiseId,
                               nsresult aDOMException,
                               const nsCString& aMsg) = 0;

  virtual RefPtr<DecryptPromise> Decrypt(MediaRawData* aSample) = 0;

  // Reject promise with DOMException corresponding to aExceptionCode.
  // Can be called from any thread.
  virtual void RejectPromise(PromiseId aId, nsresult aExceptionCode,
                             const nsCString& aReason) = 0;

  // Resolves promise with "undefined".
  // Can be called from any thread.
  virtual void ResolvePromise(PromiseId aId) = 0;

  // Threadsafe.
  virtual const nsString& KeySystem() const = 0;

  // GMP thread only.
  virtual void gmp_Decrypted(uint32_t aId,
                             GMPErr aResult,
                             const nsTArray<uint8_t>& aDecryptedData) = 0;

  virtual CDMCaps& Capabilites() = 0;

  // Main thread only.
  virtual void OnKeyStatusesChange(const nsAString& aSessionId) = 0;

  virtual void GetSessionIdsForKeyId(const nsTArray<uint8_t>& aKeyId,
                                     nsTArray<nsCString>& aSessionIds) = 0;

#ifdef DEBUG
  virtual bool IsOnGMPThread() = 0;
#endif

  struct CreateSessionData {
    dom::SessionType mSessionType;
    uint32_t mCreateSessionToken;
    PromiseId mPromiseId;
    nsCString mInitDataType;
    nsTArray<uint8_t> mInitData;
  };

protected:

  struct InitData {
    uint32_t mPromiseId;
    nsString mOrigin;
    nsString mTopLevelOrigin;
    nsString mGMPName;
    bool mInPrivateBrowsing;
  };

  struct UpdateSessionData {
    PromiseId mPromiseId;
    nsCString mSessionId;
    nsTArray<uint8_t> mResponse;
  };

  struct SessionOpData {
    PromiseId mPromiseId;
    nsCString mSessionId;
  };
};

} // namespace mozilla

#endif // CDMProxy_h_
