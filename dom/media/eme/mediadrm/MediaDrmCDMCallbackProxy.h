/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MediaDrmCDMCallbackProxy_h_
#define MediaDrmCDMCallbackProxy_h_

namespace mozilla {
class CDMProxy;
// Proxies call backs from the CDM on the worker thread back to the MediaKeys
// object on the main thread.
class MediaDrmCDMCallbackProxy : public DecryptorProxyCallback {
public:
  void SetSessionId(uint32_t aCreateSessionToken,
                    const nsCString& aSessionId) override;

  void ResolveLoadSessionPromise(uint32_t aPromiseId,
                                 bool aSuccess) override;

  void ResolvePromise(uint32_t aPromiseId) override;

  void RejectPromise(uint32_t aPromiseId,
                     nsresult aException,
                     const nsCString& aSessionId) override;

  void SessionMessage(const nsCString& aSessionId,
                      dom::MediaKeyMessageType aMessageType,
                      const nsTArray<uint8_t>& aMessage) override;

  void ExpirationChange(const nsCString& aSessionId,
                        int64_t aExpiryTime) override;

  void SessionClosed(const nsCString& aSessionId) override;

  void SessionError(const nsCString& aSessionId,
                    nsresult aException,
                    uint32_t aSystemCode,
                    const nsCString& aMessage) override;

  void KeyStatusChanged(const nsCString& aSessionId,
                        const nsTArray<uint8_t>& aKeyId,
                        dom::MediaKeyStatus aStatus) override;

  void ForgetKeyStatus(const nsCString& aSessionId,
                       const nsTArray<uint8_t>& aKeyId) override;

  void Decrypted(uint32_t aId,
                 DecryptStatus aResult,
                 const nsTArray<uint8_t>& aDecryptedData) override;

   ~MediaDrmCDMCallbackProxy() {}

private:
  friend class MediaDrmCDMProxy;
  explicit MediaDrmCDMCallbackProxy(CDMProxy* aProxy);
  void KeyStatusChangedInternal(const nsCString& aSessionId,
                                const nsTArray<uint8_t>& aKeyId,
                                const dom::Optional<dom::MediaKeyStatus>& aStatus);
  // Warning: Weak ref.
  CDMProxy* mProxy;
};

} // namespace mozilla
#endif