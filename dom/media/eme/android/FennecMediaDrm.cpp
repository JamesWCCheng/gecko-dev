/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/FennecMediaDrm.h"

using namespace std;
using namespace mozilla::widget::sdk;

#ifdef __CLASS__
#undef __CLASS__
#endif
#define __CLASS__ "FennecMediaDrm"

template<class T>
static jbyteArray FillJByteArray(const T& data, jsize length)
{
  JNIEnv* const jenv = mozilla::jni::GetEnvForThread();
  jbyteArray result = jenv->NewByteArray(length);
  jenv->SetByteArrayRegion(result, 0, length, reinterpret_cast<const jbyte*>(const_cast<T>(data)));
  return result;
}

static UUID::GlobalRef clearkeyUUID;
static UUID::GlobalRef widevineUUID;
static UUID::GlobalRef playreadyUUID;

namespace mozilla {

LogModule* GetFMDRLog() {
  static LazyLogModule log("FMDRM");
  return log;
}

static
UUID::LocalRef GenDrmUUID(int64_t aFront, int64_t aRear)
{
  UUID::LocalRef uuid;
  UUID::New(aFront, aRear, &uuid);
  return uuid;
}

FennecMediaDrm::FennecMediaDrm(const nsAString& aKeySystem)
  : mBridge(nullptr)
  , mCrypto(nullptr)
  , mCallback(nullptr)
  , mKeySystem(aKeySystem)
{
  clearkeyUUID = GenDrmUUID(0x1077efecc0b24d02ll, 0xace33c1e52e2fb4bll);
  widevineUUID = GenDrmUUID(0xedef8ba979d64acell, 0xa3c827dcd51d21edll);
  playreadyUUID = GenDrmUUID(0x9a04f07998404286ll, 0xab92e658e0885f95ll);

  if (mKeySystem.EqualsLiteral("org.w3.clearkey")) {
    mKeySystemUUID = clearkeyUUID;
  } else if (mKeySystem.EqualsLiteral("com.widevine.alpha")) {
    mKeySystemUUID = widevineUUID;
  } else if (mKeySystem.EqualsLiteral("com.microsoft.playready")) {
    mKeySystemUUID = playreadyUUID;
  } else {
    MOZ_ASSERT(false);
  }

  mBridge = widget::MediaDrmBridge::New(mKeySystemUUID);
  MDBridge::AttachNative(mBridge, this);
}

uint32_t
FennecMediaDrm::GetPluginId() const
{
  return 0;
}

nsresult
FennecMediaDrm::Init(GMPDecryptorProxyCallback* aCallback)
{
  FMDRM_LOG("FennecMediaDrm::Init >>>>> ");
  MOZ_ASSERT(mBridge);
  mCallback = aCallback;

  bool success = mBridge->Init(mKeySystemUUID);
  FMDRM_LOG("FennecMediaDrm::Init <<<<< ");
  return success ? NS_OK : NS_ERROR_FAILURE;
}

void
FennecMediaDrm::CreateSession(uint32_t aCreateSessionToken,
                              uint32_t aPromiseId,
                              const nsCString& aInitDataType,
                              const nsTArray<uint8_t>& aInitData,
                              GMPSessionType aSessionType)
{
  FMDRM_LOG("FennecMediaDrm::CreateSession >>>>> ");
  MOZ_ASSERT(mBridge);
  NS_ConvertUTF8toUTF16 initDataType(aInitDataType);

  uint32_t len = aInitData.Length();
  if (!len) {
    return;
  }
  auto initDataBytes = FillJByteArray(&aInitData[0], len);

  mBridge->CreateSession(aCreateSessionToken, aPromiseId, initDataType,
                         mozilla::jni::ByteArray::Ref::From(initDataBytes));


  FMDRM_LOG("FennecMediaDrm::CreateSession <<<<< ");
}

void
FennecMediaDrm::LoadSession(uint32_t aPromiseId,
                            const nsCString& aSessionId)
{

}

void
FennecMediaDrm::UpdateSession(uint32_t aPromiseId,
                              const nsCString& aSessionId,
                              const nsTArray<uint8_t>& aResponse)
{
  FMDRM_LOG("FennecMediaDrm::UpdateSession >>>>> ");
  MOZ_ASSERT(mBridge);

  bool isClearkey = false;
  mKeySystemUUID->Equals(clearkeyUUID, &isClearkey);

  nsTArray<uint8_t> newResponse;
  if (isClearkey) {
    basic_string<uint8_t, char_traits<uint8_t>, allocator<uint8_t>> rep(aResponse.Elements(),
                                                                          aResponse.Length());
    std::replace(rep.begin(), rep.end(), static_cast<uint8_t>('-'), static_cast<uint8_t>('+'));
    std::replace(rep.begin(), rep.end(), static_cast<uint8_t>('_'), static_cast<uint8_t>('/'));
    newResponse.AppendElements(rep.data(), rep.size());
  } else {
    newResponse = aResponse;
  }

  NS_ConvertUTF8toUTF16 sessionId(aSessionId);
  auto jBAReponse = FillJByteArray(newResponse.Elements(), newResponse.Length());

  bool success = mBridge->UpdateSession(aPromiseId, sessionId,
                                          jni::ByteArray::Ref::From(jBAReponse));
  if (!success) {
    FMDRM_LOG("[%s][%s] provideKeyResponse Err.", __CLASS__, __FUNCTION__);
    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                             NS_LITERAL_CSTRING(" ProvideKeyResponse error !!!"));
    return;
  }

// // TODO : Implement this part after receiving information from listener.
//  for (auto it = mKeyIds.begin(); it != mKeyIds.end(); it++) {
//    auto str = byteArrayToHexString( &((*it)[0]), (*it).size());
//    DRMLOG("[%s][%s] keyIdHex = (%s)", __CLASS__, __FUNCTION__, str.c_str());
//
//    mCallback->KeyStatusChanged(aSessionId, aSessionIdLength,
//                                &((*it)[0]), (*it).size(), kGMPUsable);
//  }

  FMDRM_LOG("FennecMediaDrm::UpdateSession <<<<< ");
}

void
FennecMediaDrm::CloseSession(uint32_t aPromiseId,
                             const nsCString& aSessionId)
{
  FMDRM_LOG("FennecMediaDrm::CloseSession >>>>> ");
  MOZ_ASSERT(mBridge);

//  auto jBASessionId = FillJByteArray((uint8_t*)aSessionId.Data(),
//                                      aSessionId.Length());

  //mBridge->CloseSession(mozilla::jni::ByteArray::Ref::From(jBASessionId));
  mBridge->CloseSession(aSessionId);
  mCallback->ResolvePromise(aPromiseId);
  mCallback->SessionClosed(aSessionId);
  FMDRM_LOG("FennecMediaDrm::CloseSession <<<<< ");
}

void
FennecMediaDrm::RemoveSession(uint32_t aPromiseId,
                              const nsCString& aSessionId)
{

}

void
FennecMediaDrm::SetServerCertificate(uint32_t aPromiseId,
                                     const nsTArray<uint8_t>& aServerCert)
{

}

void
FennecMediaDrm::Decrypt(uint32_t aId,
                        const mozilla::CryptoSample& aCrypto,
                        const nsTArray<uint8_t>& aBuffer)
{

}

void
FennecMediaDrm::Close()
{

}

MediaCrypto::LocalRef
FennecMediaDrm::GetMediaCrypto()
{
  MOZ_ASSERT(mBridge);
  if (!mCrypto) {
    mCrypto = mBridge->GetMediaCrypto();
  }
  return mCrypto;
}

bool
FennecMediaDrm::IsSecureDecoderComponentRequired(const nsCString& aMIMEType)
{
  MOZ_ASSERT(mBridge);
  NS_ConvertUTF8toUTF16 mimeType(aMIMEType);
  return mBridge->IsSecureDecoderComonentRequired(mimeType);
}

bool
FennecMediaDrm::IsAllowPlayback()
{
  MOZ_ASSERT(mBridge);
  return mBridge->IsAllowPlayback();
}

void
FennecMediaDrm::OnSessionCreated(int aCreateSessionToken, int aPromiseId,
                                 jni::ByteArray::Param aSessionId,
                                 jni::ByteArray::Param aRequest)
{
  FMDRM_LOG("FennecMediaDrm::OnSessionCreated >>>>> ");
  FMDRM_LOG("FennecMediaDrm::OnSessionCreated : aCreateSessionToken(%d) / aPromiseId(%d)",
    aCreateSessionToken, aPromiseId);

  //jni::String::LocalRef urlString;
  //keyReq->GetDefaultUrl(&urlString);

  auto reqDataArray = aRequest->GetElements();
  //auto url = urlString->ToCString();
  //PR(reqDataArray);
  //PR(url.Length(), url);

  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()), aSessionId->Length());
  FMDRM_LOG("FennecMediaDrm::OnSessionCreated : aSessionId(%s)", sessionId.get());
  mCallback->SetSessionId(aCreateSessionToken, sessionId);
  mCallback->ResolvePromise(aPromiseId);

  FMDRM_LOG("FennecMediaDrm::OnSessionCreated <<<<< ");
}

void
FennecMediaDrm::OnSessionUpdated(int aPromiseId, jni::ByteArray::Param aSessionId)
{
  FMDRM_LOG("FennecMediaDrm::OnSessionUpdated >>>>> ");

  //nsCString sessionId((char*) (&(aSessionId->GetElements()[0])), aSessionId->Length());

  mCallback->ResolvePromise(aPromiseId);
  FMDRM_LOG("FennecMediaDrm::OnSessionUpdated <<<<< ");
}

void
FennecMediaDrm::OnSessionClosed(int aPromiseId, jni::ByteArray::Param aSessionId)
{
  FMDRM_LOG("FennecMediaDrm::OnSessionClosed >>>>> ");
}

void
FennecMediaDrm::OnSessionMessage(jni::ByteArray::Param aSessionId,
                                 int aSessionMessageType,
                                 jni::ByteArray::Param aRequest)
{
  FMDRM_LOG("FennecMediaDrm::OnSessionMessage >>>>> ");

  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()), aSessionId->Length());
  FMDRM_LOG("FennecMediaDrm::OnSessionMessage : aSessionId(%s) / aSessionMessageType(%d)",
    sessionId.get(), aSessionMessageType);
  auto reqDataArray = aRequest->GetElements();
  nsTArray<uint8_t> retRequest;
  retRequest.AppendElements(reinterpret_cast<uint8_t*>(reqDataArray.Elements()), reqDataArray.Length());
  mCallback->SessionMessage(sessionId,
                            static_cast<GMPSessionMessageType>(aSessionMessageType),
                            retRequest);
  FMDRM_LOG("FennecMediaDrm::OnSessionMessage <<<<< ");
}

void
FennecMediaDrm::OnSessionError(jni::ByteArray::Param aSessionId)
{
  FMDRM_LOG("FennecMediaDrm::OnSessionError >>>>> ");
  FMDRM_LOG("FennecMediaDrm::OnSessionError <<<<< ");
}

/*

TODO: Delete this if code is stable and make GMPMediaKeyStatus to FennecMediaKeyStatus to get rid of GMP.
A Helper to translate MediaDrm.KeyStatus into
GMPMediaKeyStatus(FennecMediaKeyStatus)


int STATUS_EXPIRED Constant Value: 1 (0x00000001)
The key is no longer usable to decrypt media data because its expiration time has passed.
int STATUS_INTERNAL_ERROR Constant Value: 4 (0x00000004)
The key is not currently usable to decrypt media data because of an internal error in processing unrelated to input parameters.
int STATUS_OUTPUT_NOT_ALLOWED Constant Value: 2 (0x00000002)
The key is not currently usable to decrypt media data because its output requirements cannot currently be met.
int STATUS_PENDING Constant Value: 3 (0x00000003)
The status of the key is not yet known and is being determined.
int STATUS_USABLE Constant Value: 0 (0x00000000)
The key is currently usable to decrypt media data.

*/
GMPMediaKeyStatus
ToGMPMediaKeyStatus(int aKeyStatusCode)
{
  switch(aKeyStatusCode) {

    case 0x00000000/*STATUS_USABLE*/: return kGMPUsable;
    case 0x00000001/*STATUS_EXPIRED*/: return kGMPExpired;
    // TODO: MediaDrm.Status cannot tell kGMPOutputDownscaled.
    // Need to find a way to deal with this case.
    // https://w3c.github.io/encrypted-media/#mediakeystatusmap-interface
    case 0x00000002/*STATUS_OUTPUT_NOT_ALLOWED*/: return kGMPOutputRestricted;
    case 0x00000003/*STATUS_PENDING*/: return kGMPStatusPending;
    case 0x00000004/*STATUS_INTERNAL_ERROR*/: return kGMPInternalError;
    default: return kGMPUnknown;
  };
}
void
FennecMediaDrm::OnSessionKeyChanged(jni::ByteArray::Param aSessionId,
                                    jni::ByteArray::Param aKeyId,
                                    int aStatusCode)
{
  FMDRM_LOG("FennecMediaDrm::OnSessionKeyChanged >>>>> ");
  nsCString sessionId(reinterpret_cast<char*>(aSessionId->GetElements().Elements()), aSessionId->Length());
  FMDRM_LOG("FennecMediaDrm::OnSessionKeyChanged : aSessionId(%s) / aStatusCode(%d)",
    sessionId.get(), aStatusCode);
  auto keyIdArray = aKeyId->GetElements();
  nsTArray<uint8_t> keyId;
  keyId.AppendElements(reinterpret_cast<uint8_t*>(keyIdArray.Elements()), keyIdArray.Length());
  mCallback->KeyStatusChanged(sessionId,
                              keyId,
                              ToGMPMediaKeyStatus(aStatusCode));
  FMDRM_LOG("FennecMediaDrm::OnSessionKeyChanged <<<<< ");
}

} // namespace mozilla
