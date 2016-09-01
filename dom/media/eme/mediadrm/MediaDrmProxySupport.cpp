/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDrmProxySupport.h"
#include "mozilla/EMEUtils.h"
#include "MediaCodec.h" //For MediaDrm::KeyStatus
#include "MediaPrefs.h"

using namespace mozilla::java;

template<class T>
static jbyteArray FillJByteArray(const T& aData, jsize aLength)
{
  JNIEnv* const jenv = mozilla::jni::GetEnvForThread();
  jbyteArray result = jenv->NewByteArray(aLength);
  jenv->SetByteArrayRegion(result, 0, aLength, reinterpret_cast<const jbyte*>(const_cast<T>(aData)));
  return result;
}

namespace mozilla {

LogModule* GetMDRMNLog() {
  static LazyLogModule log("MediaDrmProxySupport");
  return log;
}

class MediaDrmJavaCallbacksSupport
  : public MediaDrmProxy::NativeCallbacksToMediaDrmProxySupport::Natives<MediaDrmJavaCallbacksSupport>
{
public:
  typedef MediaDrmProxy::NativeCallbacksToMediaDrmProxySupport::Natives<MediaDrmJavaCallbacksSupport> MediaDrmProxyNativeCallbacks;
  using MediaDrmProxyNativeCallbacks::DisposeNative;
  using MediaDrmProxyNativeCallbacks::AttachNative;

  MediaDrmJavaCallbacksSupport(DecryptorProxyCallback* aDecryptorProxyCallback)
    : mDecryptorProxyCallback(aDecryptorProxyCallback)
  {
    MOZ_ASSERT(aDecryptorProxyCallback);
  }
  /*
   * Native implementation, called by Java.
   */
  void OnSessionCreated(int aCreateSessionToken, int aPromiseId,
                        jni::ByteArray::Param aSessionId,
                        jni::ByteArray::Param aRequest);

  void OnSessionUpdated(int aPromiseId, jni::ByteArray::Param aSessionId);

  void OnSessionClosed(int aPromiseId, jni::ByteArray::Param aSessionId);

  void OnSessionMessage(jni::ByteArray::Param aSessionId,
                        int /*mozilla::dom::MediaKeyMessageType*/ aSessionMessageType,
                        jni::ByteArray::Param aRequest);

  void OnSessionError(int aPromiseId, jni::ByteArray::Param aSessionId);

  void OnSessionKeyChanged(jni::ByteArray::Param aSessionId,
                           jni::ByteArray::Param aKeyId,
                           int aStatusCode);

  void OnError(jni::String::Param aMessage);

private:
  DecryptorProxyCallback* mDecryptorProxyCallback;
}; // MediaDrmJavaCallbacksSupport

void
MediaDrmJavaCallbacksSupport::OnSessionCreated(int aCreateSessionToken,
                                               int aPromiseId,
                                               jni::ByteArray::Param aSessionId,
                                               jni::ByteArray::Param aRequest)
{
  auto reqDataArray = aRequest->GetElements();
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()), aSessionId->Length());

  mDecryptorProxyCallback->SetSessionId(aCreateSessionToken, sessionId);
  mDecryptorProxyCallback->ResolvePromise(aPromiseId);
}

void
MediaDrmJavaCallbacksSupport::OnSessionUpdated(int aPromiseId,
                                               jni::ByteArray::Param aSessionId)
{
  mDecryptorProxyCallback->ResolvePromise(aPromiseId);
}

void
MediaDrmJavaCallbacksSupport::OnSessionClosed(int aPromiseId,
                                              jni::ByteArray::Param aSessionId)
{
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                      aSessionId->Length());
  MDRMN_LOG("SessionId(%s) closed", sessionId.get());
  mDecryptorProxyCallback->ResolvePromise(aPromiseId);
  mDecryptorProxyCallback->SessionClosed(sessionId);
}

// [TODO] Java implement should pass the same value as MediaKeyMessageType in gecko.
//        Any elegant way to sync up between this?
/*
* enum class MediaKeyMessageType : uint32_t {
*   License_request,
*   License_renewal,
*   License_release,
*   Individualization_request,
*   EndGuard_
* };
*/
void
MediaDrmJavaCallbacksSupport::OnSessionMessage(jni::ByteArray::Param aSessionId,
                                               int /*mozilla::dom::MediaKeyMessageType*/ aMessageType,
                                               jni::ByteArray::Param aRequest)
{
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                      aSessionId->Length());
  auto reqDataArray = aRequest->GetElements();

  nsTArray<uint8_t> retRequest;
  retRequest.AppendElements(reinterpret_cast<uint8_t*>(reqDataArray.Elements()),
                            reqDataArray.Length());

  mDecryptorProxyCallback->SessionMessage(sessionId,
                                          static_cast<dom::MediaKeyMessageType>(aMessageType),
                                          retRequest);
}

void
MediaDrmJavaCallbacksSupport::OnSessionError(int aPromiseId,
                                             jni::ByteArray::Param aSessionId)
{
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                      aSessionId->Length());
  MDRMN_LOG("SessionId(%s) ", sessionId.get());
  // [TODO] Need to callback or handle this.
}

// [TODO] MediaDrm.KeyStatus defined the status code not included
// dom::MediaKeyStatus::Released and dom::MediaKeyStatus::Output_downscaled.
// Should keep tracking for this if it will be changed in the future.

static dom::MediaKeyStatus
MediaDrmKeyStatusToMediaKeyStatus(int aStatusCode)
{
  using mozilla::java::sdk::KeyStatus;
  switch (aStatusCode) {
    case KeyStatus::STATUS_USABLE: return dom::MediaKeyStatus::Usable;
    case KeyStatus::STATUS_EXPIRED: return dom::MediaKeyStatus::Expired;
    case KeyStatus::STATUS_OUTPUT_NOT_ALLOWED: return dom::MediaKeyStatus::Output_restricted;
    case KeyStatus::STATUS_INTERNAL_ERROR: return dom::MediaKeyStatus::Internal_error;
    case KeyStatus::STATUS_PENDING: return dom::MediaKeyStatus::Status_pending;
    default: return dom::MediaKeyStatus::Internal_error;
  }
}
void
MediaDrmJavaCallbacksSupport::OnSessionKeyChanged(jni::ByteArray::Param aSessionId,
                                                  jni::ByteArray::Param aKeyId,
                                                  int aStatusCode)
{
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                      aSessionId->Length());

  auto keyIdArray = aKeyId->GetElements();
  nsTArray<uint8_t> keyId;
  keyId.AppendElements(reinterpret_cast<uint8_t*>(keyIdArray.Elements()),
                       keyIdArray.Length());

  mDecryptorProxyCallback->KeyStatusChanged(sessionId,
                                            keyId,
                                            MediaDrmKeyStatusToMediaKeyStatus(aStatusCode));
}

void
MediaDrmJavaCallbacksSupport::OnError(jni::String::Param aMessage)
{
  MDRMN_LOG("OnError aMessage(%s) ", aMessage->ToCString().get());
  // [TODO] Need to callback or handle this.
}

MediaDrmProxySupport::MediaDrmProxySupport(const nsAString& aKeySystem)
  : mKeySystem(aKeySystem)
  , mDestroyed(false)
{
  // Register native methods.
  //[TODO] Check what's the init purpose.
  mJavaCallbacks = MediaDrmProxy::NativeCallbacksToMediaDrmProxySupport::New();
  // Follow the pref flag PDMAndroidRemoteCodecEnabled returned to determine
  // it is crossing process CDM or not.
  mBridgeProxy =
    MediaDrmProxy::Create(mKeySystem,
                          mJavaCallbacks,
                          MediaPrefs::PDMAndroidRemoteCodecEnabled());
}

MediaDrmProxySupport::~MediaDrmProxySupport()
{
  MOZ_ASSERT(mDestroyed, "Shutdown() should be called before !!");
}

nsresult
MediaDrmProxySupport::Init(DecryptorProxyCallback* aCallback)
{
  MOZ_ASSERT(mJavaCallbacks);
  mCallback = aCallback;

  MediaDrmJavaCallbacksSupport::AttachNative(mJavaCallbacks,
                                             mozilla::MakeUnique<MediaDrmJavaCallbacksSupport>(mCallback));
  return mBridgeProxy != nullptr ? NS_OK : NS_ERROR_FAILURE;
}

void
MediaDrmProxySupport::CreateSession(uint32_t aCreateSessionToken,
                                    uint32_t aPromiseId,
                                    const nsCString& aInitDataType,
                                    const nsTArray<uint8_t>& aInitData,
                                    MediaDrmSessionType aSessionType)
{
  MOZ_ASSERT(mBridgeProxy);
  NS_ConvertUTF8toUTF16 initDataType(aInitDataType);
  uint32_t len = aInitData.Length();
  if (!len) {
    return;
  }

  auto initDataBytes = FillJByteArray(&aInitData[0], len);
  mBridgeProxy->CreateSession(aCreateSessionToken, aPromiseId, initDataType,
                              mozilla::jni::ByteArray::Ref::From(initDataBytes));
  // [TODO] aSessionType is not used here.
  // refer to
  // http://androidxref.com/5.1.1_r6/xref/external/chromium_org/media/base/android/java/src/org/chromium/media/MediaDrmBridge.java#420
  // and https://developer.android.com/reference/android/media/MediaDrm.html#getKeyRequest(byte[], byte[], java.lang.String, int, java.util.HashMap<java.lang.String, java.lang.String>)
}

void
MediaDrmProxySupport::UpdateSession(uint32_t aPromiseId,
                                    const nsCString& aSessionId,
                                    const nsTArray<uint8_t>& aResponse)
{
  MOZ_ASSERT(mBridgeProxy);

  nsTArray<uint8_t> newResponse(aResponse);

  NS_ConvertUTF8toUTF16 sessionId(aSessionId);
  auto jReponse = FillJByteArray(newResponse.Elements(), newResponse.Length());

  mBridgeProxy->UpdateSession(aPromiseId, sessionId,
                              jni::ByteArray::Ref::From(jReponse));


//  [TODO] Implement this part after receiving information from listener.
//  for (auto it = mKeyIds.begin(); it != mKeyIds.end(); it++) {
//    auto str = byteArrayToHexString( &((*it)[0]), (*it).size());
//    DRMLOG("[%s][%s] keyIdHex = (%s)", __CLASS__, __FUNCTION__, str.c_str());
//
//    mCallback->KeyStatusChanged(aSessionId, aSessionIdLength,
//                                &((*it)[0]), (*it).size(), kGMPUsable);
//  }
}

void
MediaDrmProxySupport::CloseSession(uint32_t aPromiseId,
                                   const nsCString& aSessionId)
{
  MOZ_ASSERT(mBridgeProxy);

  NS_ConvertUTF8toUTF16 sessionId(aSessionId);
  mBridgeProxy->CloseSession(aPromiseId, sessionId);
}

void
MediaDrmProxySupport::Shutdown()
{
  MOZ_ASSERT(mBridgeProxy);
  if (mDestroyed) {
    return;
  }
  mBridgeProxy->Destroy();
  mDestroyed = true;
}

} // namespace mozilla
