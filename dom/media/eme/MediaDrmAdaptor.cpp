/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
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

static UUID::LocalRef GenDrmUUID(int64_t aFront, int64_t aRear)
{
  UUID::LocalRef uuid;
  auto rv = UUID::New(aFront, aRear, &uuid);
  PR(rv);
  return uuid;
}

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
{
//  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  AddRef();
//
//  if (GetPlatform()->createthread(&mThread) != GMPNoErr) {
//    DRMLOG("failed to create thread in key cdm");
//    mThread = nullptr;
//  }
}

MediaDrmAdaptor::~MediaDrmAdaptor()
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
}

uint32_t
MediaDrmAdaptor::GetPluginId() const
{
  return 0;
}

nsresult
MediaDrmAdaptor::Init(GMPDecryptorProxyCallback* aCallback)
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);

  mCallback = aCallback;

  //Clearkey: 0x1077efecc0b24d02L, 0xace33c1e52e2fb4bL
  auto clearkeyUUID = GenDrmUUID(0x1077efecc0b24d02ll, 0xace33c1e52e2fb4bll);

  auto uuidStr = ToStringHelper(clearkeyUUID);
  PB(uuidStr);
  MediaDrm::LocalRef mediadrm;
  auto rvmediadrm = MediaDrm::New(clearkeyUUID, &mediadrm);
  PG(rvmediadrm);

//  if (CanDecode()) {
//    // TODO : Find a way obtain capabilities from android mediadrm plugin.
//  } else {
//    mCallback->SetCapabilities(GMP_EME_CAP_DECRYPT_AUDIO |
//                               GMP_EME_CAP_DECRYPT_VIDEO);
//  }
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

//  string initDataType(aInitDataType, aInitDataType + aInitDataTypeSize);
//
//  // initDataType must be "cenc", "keyids", or "webm".
//  if (initDataType != "cenc" && initDataType != "keyids" &&
//      initDataType != "webm") {
//    string message = "'" + initDataType +
//      "' is an unsupported type for EME.";
//    mCallback->RejectPromise(aPromiseId, kGMPNotSupportedError,
//                             message.c_str(), message.size());
//    return;
//  }
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
//  GMPErr gmpErr = EnsureDRMInstanceCreated(initDataType,
//                                           aInitData,
//                                           aInitDataSize);
//  if (GMP_FAILED(gmpErr)) {
//    string message = " DRM scheme NOT supported !! Plugin NOT created !!";
//    mCallback->RejectPromise(aPromiseId, kGMPNotSupportedError,
//                             message.c_str(), message.size());
//    return;
//  }
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
//  string hexSessionId = byteArrayToHexString(uSessionId.array(), uSessionId.size());
//  DRMLOG("[%s][%s] openSession obtain SesionID(%s).",
//         __CLASS__, __FUNCTION__, hexSessionId.c_str());
//
//  GenerateKeyRequest(uSessionId, aCreateSessionToken, aPromiseId,
//                     mimeType, uInitData, aSessionType);
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
//  assert(mDRMInstance.get());
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
//    mCallback->RejectPromise(aPromiseId, kGMPInvalidAccessError, nullptr, 0);
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
//    mCallback->RejectPromise(aPromiseId, kGMPNotFoundError, nullptr, 0);
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
//  //mCallback->RejectPromise(aPromiseId, kGMPNotFoundError, nullptr, 0);
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
//    mCallback->RejectPromise(aPromiseId, kGMPInvalidStateError,
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

GMPErr
MediaDrmAdaptor::EnsureDRMInstanceCreated(const string& aInitDataType,
                                          const uint8_t* aInitData,
                                          uint32_t aInitDataSize)
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  assert(!aInitDataType.empty());
//
//  if (!mDRMInstance.get()) {
//    mDRMInstance = new android::Drm();
//  }
//  assert(mDRMInstance.get());
//
//  if (aInitDataType == "cenc") {
//    // TODO : How to identify this, is it possible for adaptor to know for which
//    //        key system it's loaded for ?
//    memcpy(mKeySystem, kWidevineUUID, sizeof(kWidevineUUID));
//
//    // NOTE : Parsing keyIds is only for CDMProxy continuing the process after
//    //        receiving keyStatusChanged. If we don't check the keyId consistency
//    //        in CDMProxy, we shall not need this function and the whole adaptor
//    //        will become much simpler !!
//    MediaDrmAdaptorUtils::ParseCENCInitDataForKeySystem(mKeySystem,
//                                                        aInitData,
//                                                        aInitDataSize,
//                                                        mKeyIds);
//
//  } else if (aInitDataType == "webm" && aInitDataSize == 16) {
//    // "webm" initData format is simply the raw bytes of the keyId.
//    // TODO: Check if there is a better way to get system key id.
//    memcpy(mKeySystem, kWidevineUUID, sizeof(kWidevineUUID));
//
//    vector<uint8_t> keyId;
//    keyId.assign(aInitData, aInitData+aInitDataSize);
//    mKeyIds.push_back(keyId);
//    auto keyidstr = byteArrayToHexString(&keyId[0], keyId.size());
//    DRMLOG("[%s][%s] webm keyid = %s", __CLASS__, __FUNCTION__, keyidstr.c_str());
//  } else {
//    return GMPNotImplementedErr;
//  }
//
//  String8 mimeType(aInitDataType.data());
//  bool pluginExist = mDRMInstance->isCryptoSchemeSupported(mKeySystem,
//                                                           mimeType);
//  if (pluginExist) {
//    status_t err = mDRMInstance->createPlugin(mKeySystem);
//    if (err == android::OK ||
//        (err == -EINVAL &&
//         mDRMInstance->initCheck() == android::OK)) {
//      String8 securityLevel;
//      String8 key("securityLevel");
//      mDRMInstance->getPropertyString(key, securityLevel);
//      DRMLOG("[%s][%s] DRM plugin created, securityLevel(%s)",
//             __CLASS__, __FUNCTION__, securityLevel.string());
//      return GMPNoErr;
//    }
//  }
//  DRMLOG("[%s][%s] DRM plugin can NOT be created.", __CLASS__, __FUNCTION__);
  return GMPGenericErr;
}

GMPErr
MediaDrmAdaptor::EnsureCryptoInstanceCreated(uint32_t aPromiseId,
                                             void* aData,
                                             size_t aDataSize)
{
  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//  if (!mCryptoInstance.get()) {
//    mCryptoInstance = new android::Crypto();
//  }
//
//  bool pluginExist = mCryptoInstance->isCryptoSchemeSupported(mKeySystem);
//  if (pluginExist) {
//    auto err = mCryptoInstance->createPlugin(mKeySystem, aData, aDataSize);
//    if (err == android::OK ||
//        (err == -EINVAL && mCryptoInstance->initCheck() == android::OK)) {
//      DRMLOG("[%s][%s] Crypto plugin loaded successfully", __CLASS__, __FUNCTION__);
//      return GMPNoErr;
//    }
//  }
//  DRMLOG("[%s][%s] Crypto plugin could NOT be created.", __CLASS__, __FUNCTION__);
  return GMPGenericErr;
}

//void
//MediaDrmAdaptor::GenerateKeyRequest(const android::Vector<uint8_t>& aSessionId,
//                                    uint32_t aCreateSessionToken,
//                                    uint32_t aPromiseId,
//                                    const android::String8& aMIMEType,
//                                    const android::Vector<uint8_t>& aInitData,
//                                    GMPSessionType aSessionType)
//{
//  DRMLOG("[%s][%s]", __CLASS__, __FUNCTION__);
//
//  auto gmpErr = EnsureCryptoInstanceCreated(aPromiseId,
//                                            (void*)aSessionId.array(),
//                                            aSessionId.size());
//  if (GMP_FAILED(gmpErr)) {
//    string message = " Crypto plugin NOT supported.";
//    mCallback->RejectPromise(aPromiseId, kGMPNotSupportedError,
//                             message.c_str(), message.size());
//    return;
//  }
//
//  if (aMIMEType == "cenc" || aMIMEType == "webm") {
//    KeyedVector<String8, String8> optionPara;
//    String8 defaultURL;
//    Vector<uint8_t> uRequest;
//
//    // TODO: Figure out how to identify the KeyType
//    status_t err =
//      mDRMInstance->getKeyRequest(aSessionId, aInitData, aMIMEType,
//                                  DrmPlugin::KeyType::kKeyType_Streaming,
//                                  optionPara, uRequest, defaultURL);
//    if (err != OK) {
//      string message = " GetKeyRequest Error";
//      mCallback->RejectPromise(aPromiseId, kGMPInvalidStateError,
//                               message.c_str(), message.size());
//      return;
//    }
//
//    string sessionId(aSessionId.begin(), aSessionId.end());
//    string hexSessionId = byteArrayToHexString(aSessionId.array(), aSessionId.size());
//    string hexRequest = byteArrayToHexString(uRequest.array(), uRequest.size());
//    DRMLOG("[%s][%s] SeesionIDHex(%s) / KeyRequestHex - (%s) ",
//           __CLASS__, __FUNCTION__, hexSessionId.c_str(),
//           hexRequest.c_str());
//
//    mCallback->SetSessionId(aCreateSessionToken, &sessionId[0],
//                            sessionId.length());
//    mCallback->ResolvePromise(aPromiseId);
//    mCallback->SessionMessage(&sessionId[0], sessionId.length(),
//                              kGMPLicenseRequest,
//                              (uint8_t*)&(uRequest.array())[0], uRequest.size());
//  } else {
//    string message = " Not handled yet !";
//    mCallback->RejectPromise(aPromiseId, kGMPNotSupportedError,
//                             message.c_str(), message.size());
//  }
//}

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
