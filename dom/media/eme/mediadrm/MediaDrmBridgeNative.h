/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MediaDrmBridgeNative_h__
#define MediaDrmBridgeNative_h__

#include <jni.h>
#include "mozilla/DecryptorProxyCallback.h"
#include "mozilla/jni/Types.h"
#include "mozilla/Logging.h"
#include "GeneratedJNINatives.h"
#include "GeneratedJNIWrappers.h"
#include "nsString.h"

using namespace mozilla::java;

namespace mozilla {

enum MediaDrmSessionType {
  kKeyStreaming = 1,
  kKeyOffline = 2,
  kKeyRelease = 3,
};

#ifndef MDRMN_LOG
  LogModule* GetMDRMNLog();
  #define MDRMN_LOG(x, ...) MOZ_LOG(GetMDRMNLog(), mozilla::LogLevel::Debug,\
    ("[MediaDrmBridgeNative][%s]" x,__FUNCTION__, ##__VA_ARGS__))
#endif

class MediaDrmBridgeNative final
  : public MediaDrmBridge::Natives<MediaDrmBridgeNative>
  , public SupportsWeakPtr<MediaDrmBridgeNative>
{
public:
  typedef MediaDrmBridge::Natives<MediaDrmBridgeNative> MDBridge;

  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(MediaDrmBridgeNative);

  MediaDrmBridgeNative(const nsAString& aKeySystem);
  ~MediaDrmBridgeNative();

  /*
   * Native implementation, called by Java.
   */
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
  /*
  * APIs to act as GMPDecryptorAPI, discarding unnecessary calls.
  */
  nsresult Init(DecryptorProxyCallback* aCallback);

  void CreateSession(uint32_t aCreateSessionToken,
                     uint32_t aPromiseId,
                     const nsCString& aInitDataType,
                     const nsTArray<uint8_t>& aInitData,
                     MediaDrmSessionType aSessionType);

  bool UpdateSession(uint32_t aPromiseId,
                     const nsCString& aSessionId,
                     const nsTArray<uint8_t>& aResponse);

  void CloseSession(uint32_t aPromiseId,
                    const nsCString& aSessionId);

  void Shutdown();

//  void LoadSession(uint32_t aPromiseId,
//                   const nsCString& aSessionId);
//
//
//  void RemoveSession(uint32_t aPromiseId,
//                     const nsCString& aSessionId);
//
//  void Close();
//
//  uint32_t GetPluginId() const override;

  /*
   * Utility functions
   */
  bool IsSecureDecoderComponentRequired(const nsCString& aMIMEType);

  const java::MediaDrmBridge::Ref& GetMediaDrmBridge() const { return mBridge; }
private:
  const nsString mKeySystem;
  MediaDrmBridge::GlobalRef mBridge;
  DecryptorProxyCallback* mCallback;
  bool mDestroyed;

};

} // namespace mozilla
#endif // MediaDrmBridgeNative_h__
