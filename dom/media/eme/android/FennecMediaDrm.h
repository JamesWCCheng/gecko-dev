/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FennecMediaDrm_h__
#define FennecMediaDrm_h__

#include <jni.h>
#include "GeneratedJNINatives.h"
#include "GeneratedJNIWrappers.h"
#include "GMPDecryptorProxy.h"
#include "MediaCodec.h"
#include "mozilla/FennecCDMProxy.h"
#include "mozilla/jni/Types.h"
#include "nsString.h"

namespace mozilla {

class FennecMediaDrm final
  : public widget::MediaDrmBridge::Natives<FennecMediaDrm>
  , public SupportsWeakPtr<FennecMediaDrm>
  , public GMPDecryptorProxy
{
public:
  typedef widget::MediaDrmBridge::Natives<FennecMediaDrm> MDBridge;

  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(FennecMediaDrm);

  FennecMediaDrm();
  ~FennecMediaDrm() {}

  // === Called from Java ===
  //void OnMediaDrmInited(int32_t aPromiseId);
  void OnSessionCreated(int aCreateSessionToken, int aPromiseId,
                        jni::ByteArray::Param aSessionId,
                        jni::ByteArray::Param aRequest);
  void OnSessionUpdated(int aPromiseId, int aSessionId);
  void OnSessoinClosed(int aPromiseId, int aSessionId);

  // === GMPDecryptorProxy ===
  uint32_t GetPluginId() const override;
  nsresult Init(GMPDecryptorProxyCallback* aCallback) override;
  void CreateSession(uint32_t aCreateSessionToken,
                     uint32_t aPromiseId,
                     const nsCString& aInitDataType,
                     const nsTArray<uint8_t>& aInitData,
                     GMPSessionType aSessionType) override;

  void LoadSession(uint32_t aPromiseId,
                   const nsCString& aSessionId) override;

  void UpdateSession(uint32_t aPromiseId,
                     const nsCString& aSessionId,
                     const nsTArray<uint8_t>& aResponse) override;

  void CloseSession(uint32_t aPromiseId,
                    const nsCString& aSessionId) override;

  void RemoveSession(uint32_t aPromiseId,
                     const nsCString& aSessionId) override;

  void SetServerCertificate(uint32_t aPromiseId,
                            const nsTArray<uint8_t>& aServerCert) override;

  void Decrypt(uint32_t aId,
               const mozilla::CryptoSample& aCrypto,
               const nsTArray<uint8_t>& aBuffer) override;

  void Close() override;

  // Utility =============================
  MediaCrypto::LocalRef GetMediaCrypto();


private:
  widget::MediaDrmBridge::GlobalRef mBridge;
  widget::sdk::MediaCrypto::GlobalRef mCrypto;
  GMPDecryptorProxyCallback* mCallback;
  UUID::GlobalRef mKeySystem;

};

} // namespace mozilla

#endif
