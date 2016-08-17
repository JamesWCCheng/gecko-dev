/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDrmBridgeNative.h"
#include "mozilla/EMEUtils.h"
#include "MediaCodec.h" //For MediaDrm::KeyStatus

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
  static LazyLogModule log("MediaDrmBridgeNative");
  return log;
}

MediaDrmBridgeNative::MediaDrmBridgeNative(const nsAString& aKeySystem)
  : mKeySystem(aKeySystem)
  , mDestroyed(false)
{
  mBridge = MediaDrmBridge::New(mKeySystem);
  MDBridge::AttachNative(mBridge, this);
}

MediaDrmBridgeNative::~MediaDrmBridgeNative()
{
  MOZ_ASSERT(mDestroyed, "Shutdown() should be called before !!");
}

void
MediaDrmBridgeNative::OnSessionCreated(int aCreateSessionToken,
                                       int aPromiseId,
                                       jni::ByteArray::Param aSessionId,
                                       jni::ByteArray::Param aRequest)
{
  auto reqDataArray = aRequest->GetElements();
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()), aSessionId->Length());

  mCallback->SetSessionId(aCreateSessionToken, sessionId);
  mCallback->ResolvePromise(aPromiseId);
}

void
MediaDrmBridgeNative::OnSessionUpdated(int aPromiseId,
                                       jni::ByteArray::Param aSessionId)
{
  mCallback->ResolvePromise(aPromiseId);
}

void
MediaDrmBridgeNative::OnSessionClosed(int aPromiseId,
                                      jni::ByteArray::Param aSessionId)
{
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                      aSessionId->Length());
  MDRMN_LOG("SessionId(%s) closed", sessionId.get());
  mCallback->ResolvePromise(aPromiseId);
  mCallback->SessionClosed(sessionId);
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
MediaDrmBridgeNative::OnSessionMessage(jni::ByteArray::Param aSessionId,
                                       int/*mozilla::dom::MediaKeyMessageType*/ aSessionMessageType,
                                       jni::ByteArray::Param aRequest)
{
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                      aSessionId->Length());
  auto reqDataArray = aRequest->GetElements();

  nsTArray<uint8_t> retRequest;
  retRequest.AppendElements(reinterpret_cast<uint8_t*>(reqDataArray.Elements()),
                            reqDataArray.Length());

  mCallback->SessionMessage(sessionId,
                            static_cast<dom::MediaKeyMessageType>(aSessionMessageType),
                            retRequest);
}

void
MediaDrmBridgeNative::OnSessionError(jni::ByteArray::Param aSessionId)
{
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                      aSessionId->Length());
  MDRMN_LOG("SessionId(%s) ", sessionId.get());
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
MediaDrmBridgeNative::OnSessionKeyChanged(jni::ByteArray::Param aSessionId,
                                          jni::ByteArray::Param aKeyId,
                                          int aStatusCode)
{
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                      aSessionId->Length());

  auto keyIdArray = aKeyId->GetElements();
  nsTArray<uint8_t> keyId;
  keyId.AppendElements(reinterpret_cast<uint8_t*>(keyIdArray.Elements()),
                       keyIdArray.Length());

  mCallback->KeyStatusChanged(sessionId,
                              keyId,
                              MediaDrmKeyStatusToMediaKeyStatus(aStatusCode));
}

nsresult
MediaDrmBridgeNative::Init(DecryptorProxyCallback* aCallback)
{
  MOZ_ASSERT(mBridge);
  mCallback = aCallback;

  bool isSuccess = mBridge->Init(mKeySystem);
  return isSuccess ? NS_OK : NS_ERROR_FAILURE;
}

void
MediaDrmBridgeNative::CreateSession(uint32_t aCreateSessionToken,
                                    uint32_t aPromiseId,
                                    const nsCString& aInitDataType,
                                    const nsTArray<uint8_t>& aInitData,
                                    MediaDrmSessionType aSessionType)
{
  MOZ_ASSERT(mBridge);
  NS_ConvertUTF8toUTF16 initDataType(aInitDataType);
  uint32_t len = aInitData.Length();
  if (!len) {
    return;
  }

  auto initDataBytes = FillJByteArray(&aInitData[0], len);
  mBridge->CreateSession(aCreateSessionToken, aPromiseId, initDataType,
                         mozilla::jni::ByteArray::Ref::From(initDataBytes));
}

bool
MediaDrmBridgeNative::UpdateSession(uint32_t aPromiseId,
                                    const nsCString& aSessionId,
                                    const nsTArray<uint8_t>& aResponse)
{
  MOZ_ASSERT(mBridge);

  nsTArray<uint8_t> newResponse(aResponse);

  NS_ConvertUTF8toUTF16 sessionId(aSessionId);
  auto jBAReponse = FillJByteArray(newResponse.Elements(), newResponse.Length());

  bool success = mBridge->UpdateSession(aPromiseId, sessionId,
                                        jni::ByteArray::Ref::From(jBAReponse));
  if (!success) {
    return false;
  }

//  [TODO] Implement this part after receiving information from listener.
//  for (auto it = mKeyIds.begin(); it != mKeyIds.end(); it++) {
//    auto str = byteArrayToHexString( &((*it)[0]), (*it).size());
//    DRMLOG("[%s][%s] keyIdHex = (%s)", __CLASS__, __FUNCTION__, str.c_str());
//
//    mCallback->KeyStatusChanged(aSessionId, aSessionIdLength,
//                                &((*it)[0]), (*it).size(), kGMPUsable);
//  }
  return true;
}

void
MediaDrmBridgeNative::CloseSession(uint32_t aPromiseId,
                                   const nsCString& aSessionId)
{
  MOZ_ASSERT(mBridge);

  NS_ConvertUTF8toUTF16 sessionId(aSessionId);
  mBridge->CloseSession(aPromiseId, sessionId);
}

void
MediaDrmBridgeNative::Shutdown()
{
  MOZ_ASSERT(mBridge);
  if (mDestroyed) {
    return;
  }
  mBridge->Destroy();
  mDestroyed = true;
}

bool
MediaDrmBridgeNative::IsSecureDecoderComponentRequired(const nsCString& aMIMEType)
{
  MOZ_ASSERT(mBridge);
  NS_ConvertUTF8toUTF16 mimeType(aMIMEType);
  return mBridge->IsSecureDecoderComonentRequired(mimeType);
}

} // namespace mozilla
