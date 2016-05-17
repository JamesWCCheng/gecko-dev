/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma GCC visibility push(default)

#ifndef __MediaDrmAdaptor_h__
#define __MediaDrmAdaptor_h__

#include <queue>
#include "nsISupportsImpl.h"
#include "GMPDecryptorProxy.h"
#include "MediaCodec.h"

using namespace mozilla::widget::sdk;

#define DRMLOG(...) __android_log_print(ANDROID_LOG_DEBUG,\
                                        "MediaDrmAdaptor",\
                                        __VA_ARGS__)

namespace mozilla {

class DecryptTask;

class MediaDrmAdaptor final : public GMPDecryptorProxy
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDrmAdaptor)

  MediaDrmAdaptor();

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

private:
  ~MediaDrmAdaptor();

  GMPErr
  EnsureDRMInstanceCreated(const std::string& aInitDataType,
                           const uint8_t* aInitData,
                           uint32_t aInitDataSize);
  GMPErr
  EnsureCryptoInstanceCreated(uint32_t aPromiseId,
                              void* aData,
                              size_t aDataSize);

//  void DoDecrypt(GMPBuffer* aBuffer, GMPEncryptedBufferMetadata* aMetadata);
  void Shutdown();

//  void GenerateKeyRequest(const android::Vector<uint8_t>& aSessionId,
//                          uint32_t aCreateSessionToken,
//                          uint32_t aPromiseId,
//                          const android::String8& aMIMEType,
//                          const android::Vector<uint8_t>& aInitData,
//                          GMPSessionType aSessionType);

  void StartProvisioning(uint32_t aCreateSessionToken);

//  void SavePendingInitData(uint32_t aCreateSessionToken,
//                           uint32_t aPromiseId,
//                           const android::String8& aMIMEType,
//                           const android::Vector<uint8_t>& aInitData,
//                           GMPSessionType aSessionType);

  void ResumePendingInitProcedure();

  void ReleaseInitDataPackages();

  void ResumeDecryptProcedure();

//  android::sp<android::Drm> mDRMInstance;
//  android::sp<android::Crypto> mCryptoInstance;

  GMPDecryptorProxyCallback* mCallback;
  //GMPDecryptorCallback* mCallback;
  GMPThread* mThread;

  uint8_t mKeySystem[16];
//  std::vector<KeyId> mKeyIds;
  bool mProvisioning;

  widget::sdk::MediaCodec::GlobalRef mDecoder;
//  std::queue<DecryptTask*> mDecryptTasks;
//
//  class PendingInitPack {
//  public:
//    PendingInitPack(uint32_t aToken,
//                    uint32_t aPromiseId,
//                    const android::String8 aMIMEType,
//                    const android::Vector<uint8_t> aInitData,
//                    GMPSessionType aSessionType)
//      : mToken(aToken)
//      , mPromiseId(aPromiseId)
//      , mMIMEType(aMIMEType)
//      , mInitData(aInitData)
//      , mSessionType(aSessionType) {}
//    uint32_t mToken;
//    uint32_t mPromiseId;
//    android::String8 mMIMEType;
//    android::Vector<uint8_t> mInitData;
//    GMPSessionType mSessionType;
//  };
//  std::vector<PendingInitPack*> mPendingInitPacks;
//
//  class CryptoMetaData {
//  public:
//    CryptoMetaData() {}
//
//    explicit CryptoMetaData(const GMPEncryptedBufferMetadata* aCrypto)
//    {
//      Init(aCrypto);
//    }
//
//    void Init(const GMPEncryptedBufferMetadata* aCrypto)
//    {
//      if (!aCrypto) {
//        assert(!IsValid());
//        return;
//      }
//
//      MediaDrmAdaptorUtils::Assign(mKeyId, aCrypto->KeyId(), aCrypto->KeyIdSize());
//
//      // AOSP - MediaDrm uses a IV vector with length of 16 bytes.
//      //        Need to padding zero if the IV from media files is less than 16.
//      MediaDrmAdaptorUtils::Assign(mIV, aCrypto->IV(), aCrypto->IVSize());
//      mIV.insert(mIV.end(), KEY_LEN - aCrypto->IVSize(), 0);
//      const uint16_t* clear = aCrypto->ClearBytes();
//      const uint32_t* cipher = aCrypto->CipherBytes();
//      //DRMLOG("[%s][%s] aCrypto->IV() size = %d, aCrypto->NumSubsamples() = %d,"
//      //          " mKeyId.size() = %d", "CryptoMetaData", __FUNCTION__,
//      //          aCrypto->IVSize(), aCrypto->NumSubsamples(), mKeyId.size());
//
//      // Fill the subsamples data.
//      for (size_t i = 0; i < aCrypto->NumSubsamples(); ++i) {
//        android::CryptoPlugin::SubSample subSample;
//        subSample.mNumBytesOfClearData = clear[i];
//        subSample.mNumBytesOfEncryptedData = cipher[i];
//        mSubSamples.push_back(subSample);
//      }
//
//    }
//
//    bool IsValid() const
//    {
//      return !mKeyId.empty() &&
//             !mIV.empty() &&
//             !mSubSamples.empty();
//    }
//
//    size_t NumSubsamples() const { return mSubSamples.size(); }
//
//    std::vector<uint8_t> mKeyId;
//    std::vector<uint8_t> mIV;
//    std::vector<android::CryptoPlugin::SubSample> mSubSamples;
//  };
};

} // namespace mozilla

#endif // __MediaDrmAdaptor_h__
#pragma GCC visibility pop
