/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTArray.h"
#include "ezlogger.h"
#include "MediaDrmAdaptor.h"

#ifdef __CLASS__
#undef __CLASS__
#endif
#define __CLASS__ "MediaDrmAdaptor"

using namespace std;
using namespace android;
using namespace mozilla::widget::sdk;
using namespace mozilla;

extern const uint8_t kClearKeyUUID[16];
extern const uint8_t kWidevineUUID[16];

extern std::string byteArrayToHexString(const unsigned char *bytes, int len);

static
UUID::LocalRef GenDrmUUID(int64_t aFront, int64_t aRear)
{
  UUID::LocalRef uuid;
  auto rv = UUID::New(aFront, aRear, &uuid);
  PR(rv);
  return uuid;
}

static UUID::GlobalRef clearkeyUUID;
static UUID::GlobalRef widevineUUID;

template<class T>
auto ToStringHelper(T&& arg) -> nsString
{
  mozilla::jni::String::LocalRef str;
  arg->ToString(&str);
  return str->ToString();
}
template<class T>
auto ToCStringHelper(T&& arg) -> nsCString
{
  mozilla::jni::String::LocalRef str;
  arg->ToString(&str);
  return str->ToCString();
}
template<class T>
static jbyteArray FillJByteArray(const T& data, jsize length)
{
  JNIEnv* const jenv = mozilla::jni::GetEnvForThread();
  jbyteArray result = jenv->NewByteArray(length);
  jenv->SetByteArrayRegion(result, 0, length, reinterpret_cast<const jbyte*>(const_cast<T>(data)));
  return result;
}
template<class T>
static jintArray FillJIntArray(const T& data, jsize length)
{
  JNIEnv* const jenv = mozilla::jni::GetEnvForThread();
  jintArray result = jenv->NewIntArray(length);
  jenv->SetIntArrayRegion(result, 0, length, reinterpret_cast<const jint*>(const_cast<T>(data)));
  return result;
}

namespace mozilla {

class DecryptTask : public GMPTask {
public:
  DecryptTask(MediaDrmAdaptor* aDrmSessionManager,
              void (MediaDrmAdaptor::*aDecryptFunction)(GMPBuffer*, GMPEncryptedBufferMetadata*),
              GMPBuffer* aEncryptedBuffer,
              GMPEncryptedBufferMetadata* aMetadata)
    : mDrmSessionManager(aDrmSessionManager),
      mDecryptFunction(aDecryptFunction),
      mEncryptedBuffer(aEncryptedBuffer),
      mMetadata(aMetadata) {}
  virtual void Destroy()
  {
    delete this;
  }
  virtual void Run()
  {
    (mDrmSessionManager->*mDecryptFunction)(mEncryptedBuffer, mMetadata);
  }
private:
  MediaDrmAdaptor* const mDrmSessionManager;
  void (MediaDrmAdaptor::*mDecryptFunction)(GMPBuffer*, GMPEncryptedBufferMetadata*);
  GMPBuffer* mEncryptedBuffer;
  GMPEncryptedBufferMetadata* mMetadata;
};

MediaDrmAdaptor::MediaDrmAdaptor()
  : mKeySystem{0}
  , mProvisioning(false)
  , mMediaDrm(nullptr)
  , mMediaCrypto(nullptr)
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
}

MediaDrmAdaptor::~MediaDrmAdaptor()
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
}

uint32_t
MediaDrmAdaptor::GetPluginId() const
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
  return 0;
}

nsresult
MediaDrmAdaptor::Init(GMPDecryptorProxyCallback* aCallback)
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);

  mCallback = aCallback;

  if (!EnsureMediaDRMCreated()) {
    DRMLOG("[%s][%s] Init error !! ", __CLASS__, __FUNCTION__);
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

void
MediaDrmAdaptor::CreateSession(uint32_t aCreateSessionToken,
                               uint32_t aPromiseId,
                               const nsCString& aInitDataType,
                               const nsTArray<uint8_t>& aInitData,
                               GMPSessionType aSessionType)
/*
   uint32_t aCreateSessionToken,
   uint32_t aPromiseId,
   const char* aInitDataType,
   uint32_t aInitDataTypeSize,
   const uint8_t* aInitData,
   uint32_t aInitDataSize,
   GMPSessionType aSessionType*/
{
  DRMLOG("[%s][%s] type:%s", __CLASS__, __FUNCTION__, aInitDataType.get());

  if (!aInitDataType.EqualsLiteral("cenc") &&
      !aInitDataType.EqualsLiteral("keyids") &&
      !aInitDataType.EqualsLiteral("webm")) {
    string type(aInitDataType.get());
    string message = "'" + type + "' is an unsupported type for EME.";
    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_NOT_SUPPORTED_ERR,
                             nsCString(message.c_str(), message.size()));
    return;
  }
//
//  Vector<uint8_t> uInitData;
//  MediaDrmAdaptorUtils::Assign(uInitData, aInitData, aInitDataSize);
//  String8 mimeType(initDataType.data());
//
//  if (mProvisioning) {
//    DRMLOG("[%s][%s] Provisioning - Token(%d)/Promise(%d) pending ",
//           __CLASS__, __FUNCTION__, aCreateSessionToken, aPromiseId);
//    SavePendingInitData(aCreateSessionToken, aPromiseId, mimeType,
//                        uInitData, aSessionType);
//    return;
//  }
//
  bool created = EnsureMediaDRMCreated();
  if (!created) {
    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_NOT_SUPPORTED_ERR,
      NS_LITERAL_CSTRING(" DRM scheme NOT supported !! Plugin NOT created !!"));
    return;
  }

  // TODO : Need to handle NOT Provisioned exception
  mozilla::jni::ByteArray::LocalRef sessionId;
  nsresult rv = mMediaDrm->OpenSession(&sessionId);
  if (NS_FAILED(rv)) {
    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_NOT_SUPPORTED_ERR,
      NS_LITERAL_CSTRING(" MediaDrm OpenSession Failed !! "));
    return;
  }
  //
  //  Vector<uint8_t> uSessionId;
  //  // TODO: check more error code from openSession.
  //  status_t err = mDRMInstance->openSession(uSessionId);
  //  if (err == android::ERROR_DRM_NOT_PROVISIONED) {
  //    SavePendingInitData(aCreateSessionToken, aPromiseId, mimeType,
  //                        uInitData, aSessionType);
  //    StartProvisioning(aCreateSessionToken);
  //    return;
  //  }

  NS_ENSURE_SUCCESS_VOID(rv);
  PG(sessionId->GetElements());

  auto sidarr = sessionId->GetElements();
  JNIEnv* const jenv = mozilla::jni::GetEnvForThread();
  jbyteArray sessionIdArray = FillJByteArray(&sidarr[0], sidarr.Length());

  GenerateKeyRequest(sessionId, aInitData, aInitDataType,
                     aCreateSessionToken, aPromiseId);
}


void
MediaDrmAdaptor::LoadSession(uint32_t aPromiseId, const nsCString& aSessionId)
/*
 * uint32_t aPromiseId,
   const char* aSessionId,
   uint32_t aSessionIdLength
 */
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
  // NOT IMPLEMENTED
  MOZ_ASSERT(false);
}

//void
//MediaDrmAdaptor::PersistentSessionDataLoaded(GMPErr aStatus,
//                                             uint32_t aPromiseId,
//                                             const string& aSessionId,
//                                             const uint8_t* aKeyData,
//                                             uint32_t aKeyDataSize)
//{
//  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  MOZ_ASSERT(false);
//}

void
MediaDrmAdaptor::UpdateSession(uint32_t aPromiseId,
                               const nsCString& aSessionId,
                               const nsTArray<uint8_t>& aResponse)
/*
 * uint32_t aPromiseId,
   const char* aSessionId,
   uint32_t aSessionIdLength,
   const uint8_t* aResponse,
   uint32_t aResponseSize
 */
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
  MOZ_ASSERT(mMediaDrm);

  nsresult rv;

  mozilla::jni::ByteArray::LocalRef keysetid;
  auto jBASessionId = FillJByteArray((uint8_t*)aSessionId.Data(), aSessionId.Length());
  auto jBAReponse = FillJByteArray(aResponse.Elements(), aResponse.Length());

  rv = mMediaDrm->ProvideKeyResponse(mozilla::jni::ByteArray::Ref::From(jBASessionId),
                                     mozilla::jni::ByteArray::Ref::From(jBAReponse),
                                     &keysetid);
  if (NS_FAILED(rv)) {
    DRMLOG("[%s][%s] provideKeyResponse Err(%d).", __CLASS__, __FUNCTION__, rv);
    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                             NS_LITERAL_CSTRING(" ProvideKeyResponse error !!!"));
    return;
  }

  HashMap::LocalRef lHashMap;
  rv= mMediaDrm->QueryKeyStatus(mozilla::jni::ByteArray::Ref::From(jBASessionId),
                                ReturnTo(&lHashMap));
  if (NS_FAILED(rv)) {
    DRMLOG("[%s][%s] QueryKeyStatus Err(%d).", __CLASS__, __FUNCTION__, rv);
  }
  mCallback->ResolvePromise(aPromiseId);

//
//  // TODO : Need to notify key status via Drm::getKeyStatus!!
//  Vector<uint8_t> uSessionId;
//  MediaDrmAdaptorUtils::Assign(uSessionId,
//                               reinterpret_cast<const uint8_t*>(aSessionId),
//                               aSessionIdLength);
//
//  Vector<uint8_t> uKeySetId;
//  Vector<uint8_t> uResponse;
//  basic_string<uint8_t, char_traits<uint8_t>, allocator<uint8_t>> rep(aResponse,
//                                                                      aResponseSize);
//  if (!memcmp(mKeySystem, kClearKeyUUID, sizeof(kClearKeyUUID))) {
//    // Specifically handling the response for aosp-clearkey-plugin
//    std::replace(rep.begin(), rep.end(), static_cast<uint8_t>('-'),
//                 static_cast<uint8_t>('+'));
//    std::replace(rep.begin(), rep.end(), static_cast<uint8_t>('_'),
//                 static_cast<uint8_t>('/'));
//  }
//  MediaDrmAdaptorUtils::Assign(uResponse, rep.data(), rep.size());
//
//  string hexSessionId = byteArrayToHexString(uSessionId.array(), uSessionId.size());
//  string hexRepsonse = byteArrayToHexString(uResponse.array(), uResponse.size());
//  DRMLOG("[%s][%s] sessionidHex(%s) / aResponse(%s)-(%d)bytes ", __CLASS__,
//         __FUNCTION__, hexSessionId.c_str(), hexRepsonse.c_str(), rep.size());
//
//  status_t err = mDRMInstance->provideKeyResponse(uSessionId, uResponse, uKeySetId);
//  auto keySetId = byteArrayToHexString(uKeySetId.array(), uKeySetId.size());
//  DRMLOG("[%s][%s] provideKeyResponse keySetId = %s.", __CLASS__, __FUNCTION__,
//         keySetId.c_str());
//  if (err != OK) {
//    DRMLOG("[%s][%s] provideKeyResponse Err(%d).", __CLASS__, __FUNCTION__, err);
//    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_ACCESS_ERR, nullptr, 0);
//    return;
//  }
//
//  //TODO: KeyStatusChanged should use a standard way.
//  KeyedVector<String8, String8> infoMap;
//  err = mDRMInstance->queryKeyStatus(uSessionId, infoMap);
//  DRMLOG("[%s][%s] queryKeyStatus result(%d) / SizeOfInfoMap(%d) ",
//         __CLASS__, __FUNCTION__, err, infoMap.size());
//  if (err == OK) {
//    for (size_t i = 0; i < infoMap.size(); i++) {
//      auto key = infoMap.keyAt(i);
//      auto value = infoMap.valueAt(i);
//      DRMLOG("[%s][%s] key = (%s) / value = (%s) ", __CLASS__, __FUNCTION__,
//             key.string(), value.string());
//    }
//  }
//  for (auto it = mKeyIds.begin(); it != mKeyIds.end(); it++) {
//    auto str = byteArrayToHexString( &((*it)[0]), (*it).size());
//    DRMLOG("[%s][%s] keyIdHex = (%s)", __CLASS__, __FUNCTION__, str.c_str());
//
//    mCallback->KeyStatusChanged(aSessionId, aSessionIdLength,
//                                &((*it)[0]), (*it).size(), kGMPUsable);
//  }
//  ResumeDecryptProcedure();
//  mCallback->ResolvePromise(aPromiseId);
}

void
MediaDrmAdaptor::CloseSession(uint32_t aPromiseId, const nsCString& aSessionId)
/*
 * uint32_t aPromiseId,
   const char* aSessionId,
   uint32_t aSessionIdLength
 */
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  assert(mDRMInstance.get());
//
//  Vector<uint8_t> uSessionId;
//  MediaDrmAdaptorUtils::Assign(uSessionId,
//                               reinterpret_cast<const uint8_t*>(aSessionId),
//                               aSessionIdLength);
//
//  status_t err = mDRMInstance->closeSession(uSessionId);
//  if (err != OK) {
//    auto sessionId = byteArrayToHexString(uSessionId.array(), uSessionId.size());
//    DRMLOG("[%s][%s] Session(%s) can NOT be closed correctly.",
//           __CLASS__, __FUNCTION__, sessionId.c_str());
//    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_NOT_FOUND_ERR, nullptr, 0);
//    return;
//  }
//
//  mCallback->ResolvePromise(aPromiseId);
//  mCallback->SessionClosed(aSessionId, aSessionIdLength);
}

void
MediaDrmAdaptor::RemoveSession(uint32_t aPromiseId,
                               const nsCString& aSessionId)
/*
 * uint32_t aPromiseId,
   const char* aSessionId,
   uint32_t aSessionIdLength
 */
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);

//  string sessionId(aSessionId, aSessionId + aSessionIdLength);
//  // TODO : remove session from DRM
//  //mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_NOT_FOUND_ERR, nullptr, 0);
//  mCallback->ResolvePromise(aPromiseId);
}

void
MediaDrmAdaptor::SetServerCertificate(uint32_t aPromiseId,
                                      const nsTArray<uint8_t>& aServerCert)
/*
 * uint32_t aPromiseId,
   const uint8_t* aServerCert,
   uint32_t aServerCertSize
 */
{
  MOZ_ASSERT(mProvisioning);
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
  // 1. Provide individualization response to drmInstance
//  android::Vector<uint8_t> uResponse;
//  MediaDrmAdaptorUtils::Assign(uResponse, aServerCert, aServerCertSize);
//
//  android::Vector<uint8_t> certificate;
//  android::Vector<uint8_t> wrappedKey;
//  status_t err = mDRMInstance->provideProvisionResponse(uResponse,
//                                                        certificate,
//                                                        wrappedKey);
//  // 2. Restore data for last pending CreateSession
//  if (err == android::OK) {
//    mCallback->ResolvePromise(aPromiseId);
//    ResumePendingInitProcedure();
//  } else {
//    string message = " Provide provision Error.";
//    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
//                             message.c_str(), message.size());
//    ReleaseInitDataPackages();
//  }
}

void
MediaDrmAdaptor::ResumeDecryptProcedure()
{
//  assert(mCryptoInstance.get());
//
//  while (!mDecryptTasks.empty()) {
//    auto item = mDecryptTasks.front();
//    mDecryptTasks.pop();
//    mThread->Post(item);
//    DRMLOG(" Popping decrypt task out of mDecryptTasks. size(%d)",
//           mDecryptTasks.size());
//  }
}

void
MediaDrmAdaptor::Decrypt(uint32_t aId,
                         const mozilla::CryptoSample& aCrypto,
                         const nsTArray<uint8_t>& aBuffer)
/*
 * GMPBuffer* aBuffer,
   GMPEncryptedBufferMetadata* aMetadata
 */
{
//  if (!mThread) {
//    DRMLOG("No decrypt thread");
//    mCallback->Decrypted(aBuffer, GMPGenericErr);
//    return;
//  }
//  // DecryptTask::Destroyed() will be invoked after it's been executed.
//  auto task = new DecryptTask(this, &MediaDrmAdaptor::DoDecrypt,
//                              aBuffer, aMetadata);
//
//  if (!mCryptoInstance.get()) {
//    mDecryptTasks.push(task);
//    DRMLOG(" Storing decrypt task into mDecryptTasks. size(%d)",
//           mDecryptTasks.size());
//    return;
//  } else {
//    ResumeDecryptProcedure();
//  }
//
//  mThread->Post(task);
}

void
MediaDrmAdaptor::Close()
{

}

//void
//MediaDrmAdaptor::DoDecrypt(GMPBuffer* aBuffer,
//                           GMPEncryptedBufferMetadata* aMetadata)
//{
//  // aBuffer and aMetadata will be deleted after callback.
//  //DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  assert(mCryptoInstance.get());
//
//  // TODO:
//  // 1. check the usage of secure=true in other drm mechanism...
//  // 2. CryptoPlugin::kMode_AES_CTR for all other mechanism?
//  uint8_t* buffer = aBuffer->Data();
//  uint32_t bufferSize = aBuffer->Size();
//  const CryptoMetaData metaData = CryptoMetaData(aMetadata);
//
//  android::AString errMsg;
//  uint8_t iv[16] = {0};
//  uint8_t keyid[16] = {0};
//  android::CryptoPlugin::Mode mode = android::CryptoPlugin::kMode_Unencrypted;
//  bool secure = false;
//
//  if(metaData.IsValid()) {
//    memcpy(keyid, &metaData.mKeyId[0], metaData.mKeyId.size());
//    memcpy(iv, &metaData.mIV[0], metaData.mIV.size());
//    mode = android::CryptoPlugin::kMode_AES_CTR;
//  }
//  auto result = mCryptoInstance->decrypt(secure,
//                                         keyid,
//                                         iv,
//                                         mode,
//                                         buffer,
//                                         &metaData.mSubSamples[0],
//                                         metaData.NumSubsamples(),
//                                         buffer,
//                                         &errMsg);
// DRMLOG("[%s][%s] isEncrypted(%d)/size(%d)-result(%d)/errorMsg(%s)",
//        __CLASS__, __FUNCTION__, metaData.IsValid(), bufferSize,
//        result, errMsg.c_str());
//
//  GMPErr rv = result < 0 ? MediaDrmAdaptorUtils::ToGMPErr(result) : GMPNoErr;
//  mCallback->Decrypted(aBuffer, rv);
//}

void
MediaDrmAdaptor::Shutdown()
{
  DRMLOG("[%s][%s] %p", __CLASS__, __FUNCTION__, this);

//  // TODO : remove all sessions in DRM
//  mCryptoInstance = nullptr;
//  mDRMInstance = nullptr;
}

//void
//MediaDrmAdaptor::DecryptingComplete()
//{
//  DRMLOG("[%s][%s] %p", __CLASS__, __FUNCTION__, this);
//
//  GMPThread* thread = mThread;
//  thread->Join();
//
//  Shutdown();
//  Release();
//}

bool
MediaDrmAdaptor::EnsureMediaDRMCreated()
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
  if (!mMediaDrm) {
    MediaDrm::LocalRef mediaDrm;
    clearkeyUUID = GenDrmUUID(0x1077efecc0b24d02ll, 0xace33c1e52e2fb4bll);
    //widevineUUID = GenDrmUUID(0xedef8ba979d64acell, 0xa3c827dcd51d21edll);
    auto rvMediaDrmNew = MediaDrm::New(clearkeyUUID, &mediaDrm);
    PG(rvMediaDrmNew);
    mMediaDrm = mediaDrm;
    PG(mMediaDrm);
    if (mMediaDrm) {
      auto rvSecurityLevel = mMediaDrm->SetPropertyString("securityLevel", "L3");
      PG(rvSecurityLevel);
    }
  }

  if (!mMediaDrm) {
    DRMLOG("[%s][%s] ERROR !! ", __CLASS__, __FUNCTION__);
    return false;
  }
  return true;
}

bool
MediaDrmAdaptor::EnsureMediaCryptoCreated(mozilla::jni::ByteArray::Param aSessionId)
{
  if (!mMediaCrypto) {
    nsresult rv;

    MediaCrypto::LocalRef mediacrypto;
    rv = MediaCrypto::New(clearkeyUUID, aSessionId, &mediacrypto);

    mMediaCrypto = mediacrypto;
    if (mMediaCrypto) {
      bool isCryptoSchemeSupported = false;
      rv = mediacrypto->IsCryptoSchemeSupported(clearkeyUUID, &isCryptoSchemeSupported);
      PG(isCryptoSchemeSupported);
    }
  }

  if (!mMediaCrypto) {
    DRMLOG("[%s][%s] ERROR !! ", __CLASS__, __FUNCTION__);
    return false;
  }
  return true;
}

void
MediaDrmAdaptor::GenerateKeyRequest(mozilla::jni::ByteArray::Param aSessionId,
                                    const nsTArray<uint8_t>& aInitData,
                                    mozilla::jni::String::Param aMIMEType,
                                    uint32_t aCreateSessionToken,
                                    uint32_t aPromiseId)
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
  MOZ_ASSERT(mMediaDrm);

  bool created = EnsureMediaCryptoCreated(aSessionId);
  if (!created) {
    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_NOT_SUPPORTED_ERR,
                             NS_LITERAL_CSTRING(" MediaCrypto creation failed !!"));
    return;
  }

  nsresult rv;
  KeyRequest::LocalRef keyReq;
  HashMap::LocalRef lHashMap;
  auto rvNewHashMap = HashMap::New(&lHashMap);
  auto jBAInitData = FillJByteArray(&aInitData[0], aInitData.Length());

  // TODO: Figure out how to identify the KeyType
  rv = mMediaDrm->GetKeyRequest(aSessionId,
                                mozilla::jni::ByteArray::Ref::From(jBAInitData),
                                aMIMEType,
                                MediaDrm::KEY_TYPE_STREAMING,
                                lHashMap,
                                ReturnTo(&keyReq));
  if (NS_FAILED(rv)) {
    mCallback->RejectPromise(aPromiseId, NS_ERROR_DOM_INVALID_STATE_ERR,
                             NS_LITERAL_CSTRING(" GetKeyRequest Error"));
    return;
  }

  mozilla::jni::ByteArray::LocalRef reqData;
  mozilla::jni::String::LocalRef urlString;
  keyReq->GetData(&reqData);
  keyReq->GetDefaultUrl(&urlString);

  auto reqDataArray = reqData->GetElements();
  auto url = urlString->ToCString();
  PR(reqDataArray);
  PR(url.Length(), url);

  nsCString sessionId((char*) (&(aSessionId->GetElements()[0])), aSessionId->Length());
  mCallback->SetSessionId(aCreateSessionToken, sessionId);
  mCallback->ResolvePromise(aPromiseId);

  nsTArray<uint8_t> retRequest;
  retRequest.AppendElements((unsigned char*)(&reqDataArray[0]), reqDataArray.Length());
  mCallback->SessionMessage(sessionId,
                            kGMPLicenseRequest,
                            retRequest);

  DRMLOG("[%s][%s] <<<<<<<<<<<< leave", __CLASS__, __FUNCTION__);
}

//void
//MediaDrmAdaptor::SavePendingInitData(uint32_t aCreateSessionToken,
//                                     uint32_t aPromiseId,
//                                     const android::String8& aMIMEType,
//                                     const android::Vector<uint8_t>& aInitData,
//                                     GMPSessionType aSessionType)
//{
//  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//
//  PendingInitPack* initPack =
//    new PendingInitPack(aCreateSessionToken, aPromiseId, aMIMEType,
//                        aInitData, aSessionType);
//  mPendingInitPacks.push_back(initPack);
//}

void
MediaDrmAdaptor::StartProvisioning(uint32_t aCreateSessionToken)
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  assert(!mProvisioning);
//  assert(mDRMInstance.get());
//
//  mProvisioning = true;
//  // 1. Generate ProvisionReq
//  /*
//   * http://androidxref.com/5.1.1_r6/xref/frameworks/base/media/jni/android_media_MediaDrm.cpp#897
//  String8 certType;
//  if (jcertType == gCertificateTypes.kCertificateTypeX509) {
//      certType = "X.509";
//  } else if (jcertType == gCertificateTypes.kCertificateTypeNone) {
//      certType = "none";
//  } else {
//      certType = "invalid";
//  }
//  */
//  android::Vector<uint8_t> uRequest;
//  android::String8 defaultUrl;
//  android::String8 certType("none");
//  android::String8 certAuthority("");
//  status_t err =
//    mDRMInstance->getProvisionRequest(certType,
//                                      certAuthority,
//                                      uRequest, defaultUrl);
//  if (err != android::OK) {
//    DRMLOG("[%s][%s] getProvisionRequest failed !! ", __CLASS__, __FUNCTION__);
//    ReleaseInitDataPackages();
//    return;
//  }
//
//  // 2. Send to gecko via SessionMessage
//  string sessionId = "tempSessionId";
//  string request(uRequest.begin(), uRequest.end());
//
//  string finalURL = string(defaultUrl.string()) + "&signedRequest=" + request;
//  DRMLOG("[%s][%s] Provisioning finalURL - (%s)", __CLASS__, __FUNCTION__,
//         finalURL.c_str());
//
//  mCallback->SessionMessage(&sessionId[0], sessionId.length(),
//                            kGMPIndividualizationRequest,
//                            (uint8_t*)&finalURL[0], finalURL.length(),
//                            aCreateSessionToken);
}

void
MediaDrmAdaptor::ResumePendingInitProcedure()
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  assert(mProvisioning);
//  for (auto item : mPendingInitPacks) {
//    Vector<uint8_t> uSessionId;
//    status_t err = mDRMInstance->openSession(uSessionId);
//    if (err != android::OK) {
//      DRMLOG("[%s][%s] Cannot obtain sessionId, error(%d).",
//             __CLASS__, __FUNCTION__, err);
//      assert(false);
//    }
//    string hexSessionId = byteArrayToHexString(uSessionId.array(), uSessionId.size());
//    DRMLOG("[%s][%s] openSession obtain SesionID(%s).",
//           __CLASS__, __FUNCTION__, hexSessionId.c_str());
//
//    GenerateKeyRequest(uSessionId, item->mToken, item->mPromiseId,
//                       item->mMIMEType, item->mInitData,
//                       item->mSessionType);
//  }
//  ReleaseInitDataPackages();
}

void
MediaDrmAdaptor::ReleaseInitDataPackages()
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  assert(mProvisioning);
//  while (!mPendingInitPacks.empty()) {
//    auto item = mPendingInitPacks.back();
//    mPendingInitPacks.pop_back();
//    delete item;
//  }
//  mProvisioning = false;
}

} // namespace mozilla
