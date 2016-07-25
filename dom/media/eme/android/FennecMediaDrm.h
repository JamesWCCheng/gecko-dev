/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FennecMediaDrm_h__
#define FennecMediaDrm_h__

#include <jni.h>
#include "mozilla/jni/Types.h"
#include "mozilla/Logging.h"
#include "GeneratedJNINatives.h"
#include "GeneratedJNIWrappers.h"
#include "GMPDecryptorProxy.h"
#include "MediaCodec.h"
#include "nsString.h"

using namespace mozilla::widget::sdk;

namespace mozilla {

#ifndef FMDRM_LOG
  LogModule* GetFMDRLog();
  #define FMDRM_LOG(...) MOZ_LOG(GetFMDRLog(), mozilla::LogLevel::Debug, (__VA_ARGS__))
#endif

class FennecMediaDrm final
  : public widget::MediaDrmBridge::Natives<FennecMediaDrm>
  , public SupportsWeakPtr<FennecMediaDrm>
  , public GMPDecryptorProxy
{
public:
  typedef widget::MediaDrmBridge::Natives<FennecMediaDrm> MDBridge;

  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(FennecMediaDrm);

  FennecMediaDrm(const nsAString& aKeySystem);
  ~FennecMediaDrm() { mBridge->Destroy(); }

  // === Called from Java ===
  //void OnMediaDrmInited(int32_t aPromiseId);
  void OnSessionCreated(int aCreateSessionToken, int aPromiseId,
                        jni::ByteArray::Param aSessionId,
                        jni::ByteArray::Param aRequest);

  void OnSessionUpdated(int aPromiseId, jni::ByteArray::Param aSessionId);

  void OnSessionClosed(int aPromiseId, jni::ByteArray::Param aSessionId);

  void OnSessionMessage(jni::ByteArray::Param aSessionId,
                        int/*GMPSessionMessageType*/ aSessionMessageType,
                        jni::ByteArray::Param aRequest);

  void OnSessionError(jni::ByteArray::Param aSessionId);

  void OnSessionKeyChanged(jni::ByteArray::Param aSessionId,
                           jni::ByteArray::Param aKeyId,
                           int aStatusCode);
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

  bool IsSecureDecoderComponentRequired(const nsCString& aMIMEType);
  bool IsAllowPlayback();

private:
  widget::MediaDrmBridge::GlobalRef mBridge;
  widget::sdk::MediaCrypto::GlobalRef mCrypto;
  GMPDecryptorProxyCallback* mCallback;
  UUID::GlobalRef mKeySystemUUID;
  const nsString mKeySystem;

};

} // namespace mozilla

#endif
