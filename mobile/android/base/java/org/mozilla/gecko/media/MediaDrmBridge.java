// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.mozilla.gecko.media;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.lang.InterruptedException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.UUID;
import java.util.ArrayDeque;
import java.util.concurrent.atomic.AtomicInteger;

import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.annotation.RobocopTarget;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoAppShell;

import android.annotation.TargetApi;
import static android.os.Build.VERSION.SDK_INT;
import static android.os.Build.VERSION_CODES.JELLY_BEAN_MR2;
import static android.os.Build.VERSION_CODES.LOLLIPOP;
import static android.os.Build.VERSION_CODES.M;
import android.os.AsyncTask;
import android.os.Handler;
import android.media.MediaCrypto;
import android.media.MediaCryptoException;
import android.media.MediaDrm;
import android.media.MediaDrmException;
import android.util.Log;
import java.util.List;

public class MediaDrmBridge extends JNIObject {

    private static final String LOGTAG = "\033[1;41mMediaDrmBridge\033[m";
    private static final UUID CLEARKEY_SCHEME_UUID =
      new UUID(0x1077efecc0b24d02L, 0xace33c1e52e2fb4bL);
    private static final UUID WIDEVINE_SCHEME_UUID =
      new UUID(0xedef8ba979d64aceL, 0xa3c827dcd51d21edL);
    private static final UUID PLAYREADY_SCHEME_UUID =
      new UUID(0x9a04f07998404286L, 0xab92e658e0885f95L);
    private static final String INVALID_SESSION_ID = "Invalid";
    private static final String WIDEVINE_KEY_SYSTEM = "com.widevine.alpha";
    private MediaDrm mDrm;
    private MediaCrypto mCrypto;
    private UUID mSchemeUUID;
    private Handler mHandler;
    private ByteBuffer mCryptoSessionId;
    private boolean mProvisioning;
    private boolean mSingleSessionMode;
    private boolean mAllowPlay;
    private int mReqCount;
    private HashMap<ByteBuffer, String> mSessionIds;
    private HashMap<ByteBuffer, String> mSessionMIMETypes;
    private ArrayDeque<PendingCreateSessionData> mPendingCreateSessionDataQueue;
    // This flag is to protect using the native objected that has been destroyed.
    private boolean mDestroyed;

    private static class PendingCreateSessionData {
        private final int mToken;
        private final int mPromiseId;
        private final byte[] mInitData;
        private final String mMimeType;

        private PendingCreateSessionData(int aToken, int aPromiseId,
                                         byte[] aInitData, String aMimeType) {
            mToken = aToken;
            mPromiseId = aPromiseId;
            mInitData = aInitData;
            mMimeType = aMimeType;
        }

        private int token() { return mToken; }
        private int promiseId() { return mPromiseId; }
        private byte[] initData() { return mInitData; }
        private String mimeType() { return mMimeType; }
    }

    @Override protected native void disposeNative();

    public Object getLock() {
        return this;
    }

    public void calculateAllowState(int aDelta) {
        // [TODO] Considering provision ?
        mReqCount += aDelta;
        if (mReqCount == 0) {
            mAllowPlay = true;
        } else {
          mAllowPlay = false;
        }
    }

    static public boolean passMiniumSupportVersionCheck() {
      // Support versions >= LOLLIPOP
      if (SDK_INT < LOLLIPOP) {
          Log.e(LOGTAG, "CheckMiniumSupportVersion Not supported !! ");
          return false;
      }
      return true;
    }

    @WrapForJNI
    public boolean isAllowPlayback() {
      return mAllowPlay;
    }

    @WrapForJNI
    static public boolean isSchemeSupported(String aKeySystem) {
        if (!passMiniumSupportVersionCheck()) {
            return false;
        }
        if (aKeySystem.equals("com.widevine.alpha")){
            return MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID) &&
                MediaCrypto.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID);
        }
        return false;
    }

    @WrapForJNI
    static public boolean isSchemeMIMESupported(String aKeySystem, String aMIME) {
        if (!passMiniumSupportVersionCheck()) {
            return false;
        }
        Log.d(LOGTAG, "isSchemeMIMESupported " + aMIME);
        if (aKeySystem.equals("com.widevine.alpha")){
            return MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID, aMIME);
        }
        return false;
    }

    @WrapForJNI
    public boolean isSecureDecoderComonentRequired(String aMIME) {
      if (mCrypto != null) {
          return mCrypto.requiresSecureDecoderComponent(aMIME);
      }
      return false;
    }

    @WrapForJNI
    MediaDrmBridge(String akeySystem) {
        Log.d(LOGTAG, "MediaDrmBridge");
        mReqCount = 0;
        mAllowPlay = false;
        mProvisioning = false;

        // [TODO] Not sure in which case this is ture.
        mSingleSessionMode = false;

        mDestroyed = false;

        mSessionIds = new HashMap<ByteBuffer, String>();
        mSessionMIMETypes = new HashMap<ByteBuffer, String>();
        mPendingCreateSessionDataQueue = new ArrayDeque<PendingCreateSessionData>();
        mHandler = new Handler();
        mSchemeUUID = convertKeySystemToSchemeUUID(akeySystem);
        mCryptoSessionId = null;
        Log.d(LOGTAG, "mSchemeUUID : " + mSchemeUUID.toString());
        try {
            mDrm = new MediaDrm(mSchemeUUID);
            try {
                String currentSecurityLevel = mDrm.getPropertyString("securityLevel");
                Log.d(LOGTAG, "Security level: current level = " + currentSecurityLevel);
                mDrm.setPropertyString("securityLevel", "L3");
                currentSecurityLevel = mDrm.getPropertyString("securityLevel");
                Log.d(LOGTAG, "Security level: after set securityLevel = " + currentSecurityLevel);

                // [TODO]
                // Reference chromium, if widevine, using multiple session mode.
                // https://cs.chromium.org/chromium/src/media/base/android/java/src/org/chromium/media/MediaDrmBridge.java?q=mediadrmbridge&sq=package:chromium&dr=CSs&l=217

                if (isWidevine()) {
                    //mDrm.setPropertyString("privacyMode", "enable");
                    mDrm.setPropertyString("sessionSharing", "enable");
                }
            }
            catch (Exception e) {
                Log.e(LOGTAG, "Failed to setPropertyString: " + e.getMessage());
            }
            mDrm.setOnEventListener(new MediaDrmListener());
            if (SDK_INT >= M) {
                // [TODO] Implement ExpirationUpdateListener for M.
                // https://cs.chromium.org/chromium/src/media/base/android/java/src/org/chromium/media/MediaDrmBridge.java?q=mediadrmbridge.java&sq=package:chromium&dr&l=1037
                mDrm.setOnKeyStatusChangeListener(new KeyStatusChangeListener(), null);
            }
        } catch (MediaDrmException e) {
            Log.e(LOGTAG, "Failed to create MediaDrm: " + e.getMessage());
        }
    }

    @WrapForJNI
    public boolean init(String akeySystem) {
        return true;
    }

    @WrapForJNI
    private boolean createSession(int aCreateSessionToken, int aPromiseId,
                                  String aInitDataType, byte[] aInitData) {
        Log.d(LOGTAG, "createSession");
        if (mDrm == null) {
            Log.d(LOGTAG, "mDrm not exists !");
            return false;
        }
        synchronized (getLock()) {
            if (mProvisioning) {
                Log.d(LOGTAG, "Pending createsession because of provisioning !");
                assert mCrypto == null;
                savePendingCreateSessionData(aCreateSessionToken, aPromiseId,
                                             aInitData, aInitDataType);
                return true;
            }
            ByteBuffer sessionId = null;
            String strSessionId = null;
            try {
                ensureMediaCryptoCreated();

                sessionId = openSession();
                if (sessionId == null) {
                    return false;
                }
                strSessionId = new String(sessionId.array());
                mSessionIds.put(sessionId, strSessionId);

                MediaDrm.KeyRequest request = null;
                try {
                    request = getKeyRequest(sessionId, aInitData, aInitDataType);
                } catch (Exception e) {
                    Log.e(LOGTAG, "Exception intentionally caught when calling getKeyRequest()", e);
                }

                if (request == null) {
                    if (sessionId != null) {
                        closeSession(aPromiseId, strSessionId);
                    }
                    return false;
                }
                if (!mDestroyed) {
                    onSessionCreated(aCreateSessionToken, aPromiseId, sessionId.array(),
                                     request.getData());
                    onSessionMessage(sessionId.array(), 0/*MediaKeyMessageType::License_request = 0*/, request.getData());
                }
                mSessionMIMETypes.put(sessionId, aInitDataType);
                Log.d(LOGTAG, " StringID : " + strSessionId + " is put into mSessionIds ");
                return true;
            } catch (android.media.NotProvisionedException e) {
                Log.e(LOGTAG, "Device not provisioned", e);
                if (sessionId != null) {
                    closeSession(0, strSessionId);
                }
                savePendingCreateSessionData(aCreateSessionToken, aPromiseId,
                                             aInitData, aInitDataType);
                startProvisioning();
                return true;
            }
        }
    }

    @WrapForJNI
    private boolean updateSession(int aPromiseId, String aSessionId,
                                  byte[] aResponse) {
        Log.d(LOGTAG, "updateSession, sessionId = " + aSessionId);

        if (mDrm == null) {
            Log.e(LOGTAG, "updateSession() called when MediaDrm is null.");
            return false;
        }

        ByteBuffer session = getSession(aSessionId);
        if (session == null) {
            Log.e(LOGTAG, "updateSession : found no session for Id="+aSessionId);
        }

        if (!sessionExists(session)) {
            Log.e(LOGTAG, "Invalid session in updateSession.");
            if (!mDestroyed) {
                onSessionError(session.array());
            }
            return false;
        }

        try {
            try {
                final byte [] keySetId = mDrm.provideKeyResponse(session.array(), aResponse);
                calculateAllowState(-1);
                HashMap<String, String> infoMap = mDrm.queryKeyStatus(session.array());
                for (String strKey : infoMap.keySet()) {
                    String strValue = infoMap.get(strKey);
                    Log.e(LOGTAG, "InfoMap : key("+strKey+")/value("+strValue+")");
                }
            } catch (java.lang.IllegalStateException e) {
                // [TODO] remove the comment from google.
                // This is not really an exception. Some error code are incorrectly
                // reported as an exception.
                // TODO(qinmin): remove this exception catch when b/10495563 is fixed.
                Log.e(LOGTAG, "Exception intentionally caught when calling provideKeyResponse()", e);
            }
            if (!mDestroyed) {
                onSessionUpdated(aPromiseId, session.array());
            }
            Log.d(LOGTAG, "Key successfully added for session " + aSessionId);
            return true;
        } catch (android.media.NotProvisionedException e) {
            // [TODO] remove the comment from google.
            // TODO(xhwang): Should we handle this?
            Log.e(LOGTAG, "failed to provide key response", e);
        } catch (android.media.DeniedByServerException e) {
            Log.e(LOGTAG, "failed to provide key response", e);
        }
        if (!mDestroyed) {
            onSessionError(session.array());
        }
        release();
        return false;
    }

    @WrapForJNI
    private void closeSession(int aPromiseId, String aSessionId) {
        Log.d(LOGTAG, "closeSession");
        assert mDrm != null;
        ByteBuffer session = removeSession(aSessionId);
        mDrm.closeSession(session.array());
        if (aPromiseId > 0 && !mDestroyed) {
          onSessionClosed(aPromiseId, session.array());
        }
    }

    @WrapForJNI
    private MediaCrypto getMediaCrypto() {
      synchronized (getLock()) {
          Log.d(LOGTAG, "getMediaCrypto");
          if (mCrypto == null) {
              try {
                  getLock().wait();
              } catch (InterruptedException e){
                  Log.e(LOGTAG, " Wait for MediaCrypto !!!");
                  return null;
              }
          }
          return mCrypto;
      }
    }

    @WrapForJNI
    private native void onSessionCreated(int aCreateSessionToken,
                                         int aPromiseId,
                                         byte[] aSessionId,
                                         byte[] aRequest);

    @WrapForJNI
    private native void onSessionUpdated(int aPromiseId, byte[] aSessionId);

    @WrapForJNI
    private native void onSessionClosed(int aPromiseId, byte[] aSessionId);

    @WrapForJNI
    private native void onSessionMessage(byte[] aSessionId,
                                         int aSessionMessageType,
                                         byte[] aRequest);

    @WrapForJNI
    private native void onSessionError(byte[] aSessionId);

    // Key status change callback.
    // MediaDrm.KeyStatus is available in API level 23(M)
    // https://developer.android.com/reference/android/media/MediaDrm.KeyStatus.html
    // Just unwrap the structure and pass the keyid and status code to native for
    // compatibility between L and M.
    // Reference: https://cs.chromium.org/chromium/src/media/base/android/java/src/org/chromium/media/MediaDrmBridge.java?q=mediadrmbridge.java&sq=package:chromium&dr&l=96
    @WrapForJNI
    private native void  onSessionKeyChanged(byte[] aSessionId,
                                             byte[] aKeyId,
                                             int aStatusCode);

    @WrapForJNI // Called when natvie object is destroyed.
    public void destroy() {
        Log.d(LOGTAG, "MediaDrmBridge::destroy!! Natvie object is destroyed.");
        if (mDestroyed) {
            return;
        }
        mDestroyed = true;
        release();
    }

    private boolean isWidevine() {
        return mSchemeUUID.equals(WIDEVINE_SCHEME_UUID);
    }

    /*
     * Refers to Android Java Implementation
     */

    private MediaDrm.KeyRequest getKeyRequest(ByteBuffer aSession,
                                              byte[] aData,
                                              String aMime)
        throws android.media.NotProvisionedException {
        assert mDrm != null;
        assert mCrypto != null;
        assert !mProvisioning;

        HashMap<String, String> optionalParameters = new HashMap<String, String>();
        MediaDrm.KeyRequest request =
            mDrm.getKeyRequest(aSession.array(), aData, aMime,
                             MediaDrm.KEY_TYPE_STREAMING, optionalParameters);
        String result = (request != null) ? "successed" : "failed";
        if (request != null) {
          calculateAllowState(1);
        }
        Log.d(LOGTAG, "getKeyRequest " + result + "!");
        return request;
    }

    @TargetApi(M)
    private class KeyStatusChangeListener implements MediaDrm.OnKeyStatusChangeListener {
        @Override
        public void onKeyStatusChange(MediaDrm aMediaDrm,
                                      byte[] aSessionId,
                                      List<MediaDrm.KeyStatus> aKeyInformation,
                                      boolean aHasNewUsableKey) {
            // [TODO] Check all the parameter is dealing properly.
            // aHasNewUsableKey seems not use but only log it.
            // https://cs.chromium.org/chromium/src/third_party/WebKit/Source/modules/encryptedmedia/MediaKeySession.cpp?cl=GROK&l=853
            Log.d(LOGTAG, "KeyStatusChangeListener.onKeyStatusChange(), aHasNewUsableKey = " + aHasNewUsableKey);
            // Iterate to callback the status change event for all key in this session.
            // Update the key status map in
            // https://dxr.mozilla.org/mozilla-central/rev/4c05938a64a7fde3ac2d7f4493aee1c5f2ad8a0a/dom/media/gmp/GMPCDMCallbackProxy.cpp#293
            for (MediaDrm.KeyStatus keyStatus : aKeyInformation) {
                if (!mDestroyed) {
                    onSessionKeyChanged(aSessionId, keyStatus.getKeyId(), keyStatus.getStatusCode());
                }
            }
        }
    }

    private class MediaDrmListener implements MediaDrm.OnEventListener {
        @Override
        public void onEvent(MediaDrm mediaDrm, byte[] sessionArray, int event,
                            int extra, byte[] data) {
            Log.e(LOGTAG, "MediaDrmListener.onEvent() is called !!!!!!!!!! ");
            if (sessionArray == null) {
                Log.e(LOGTAG, "MediaDrmListener: Null session.");
                return;
            }
            ByteBuffer session = ByteBuffer.wrap(sessionArray);
            if (!sessionExists(session)) {
                Log.e(LOGTAG, "MediaDrmListener: Invalid session.");
                return;
            }
            String sessionId = mSessionIds.get(session);
            if (sessionId == null || sessionId == INVALID_SESSION_ID) {
               Log.e(LOGTAG, "MediaDrmListener: Invalid session ID.");
               return;
            }
            switch(event) {
                case MediaDrm.EVENT_PROVISION_REQUIRED:
                    // Maybe handle provisition here ?
                    Log.d(LOGTAG, "MediaDrm.EVENT_PROVISION_REQUIRED");
                    break;
                case MediaDrm.EVENT_KEY_REQUIRED:
                    Log.d(LOGTAG, "MediaDrm.EVENT_KEY_REQUIRED");
                    if (mProvisioning) {
                        return;
                    }
                    String initDataType = mSessionMIMETypes.get(session);
                    Log.d(LOGTAG, "MediaDrm.EVENT_KEY_REQUIRED initDataType = " + initDataType + ", sessionId = " + sessionId);
                    MediaDrm.KeyRequest request = null;
                    try {
                        request = getKeyRequest(session, data, initDataType);
                    } catch (android.media.NotProvisionedException e) {
                        // Will this ever happen here ?
                        Log.e(LOGTAG, "Device not provisioned", e);
                        startProvisioning();
                        return;
                    }
                    if (request != null) {
                        if (!mDestroyed) {
                            onSessionMessage(session.array(), 0/*MediaKeyMessageType::License_request = 0*/, request.getData());
                        }
                    } else {
                        if (!mDestroyed) {
                            onSessionError(session.array());
                        }
                    }
                    break;
                case MediaDrm.EVENT_KEY_EXPIRED:
                    Log.d(LOGTAG, "MediaDrm.EVENT_KEY_EXPIRED");
                    if (!mDestroyed) {
                        onSessionError(session.array());
                    }
                    break;
                case MediaDrm.EVENT_VENDOR_DEFINED:
                    Log.e(LOGTAG, "MediaDrm.EVENT_VENDOR_DEFINED");
                    assert false;  // Should never happen.
                    break;
                default:
                    Log.e(LOGTAG, "Invalid DRM event " + event);
                    return;
            }
        }
    }

    private ByteBuffer openSession() throws android.media.NotProvisionedException {
        assert mDrm != null;
        try {
            byte[] sessionId = mDrm.openSession();
            // ByteBuffer.wrap() is backed by the byte[]. Make a clone here in
            // case the underlying byte[] is modified.
            return ByteBuffer.wrap(sessionId.clone());
        } catch (java.lang.RuntimeException e) {  // TODO(xhwang): Drop this?
            Log.e(LOGTAG, "Cannot open a new session", e);
            release();
            return null;
        } catch (android.media.NotProvisionedException e) {
            // Throw NotProvisionedException so that we can startProvisioning().
            throw e;
        } catch (android.media.MediaDrmException e) {
            // Other MediaDrmExceptions (e.g. ResourceBusyException) are not
            // recoverable.
            release();
            return null;
        }
    }

    private ByteBuffer getSession(String aSessionId) {
        for (ByteBuffer session : mSessionIds.keySet()) {
            if (mSessionIds.get(session).equals(aSessionId)) {
                return session;
            }
        }
        return null;
    }

    private ByteBuffer removeSession(String aSessionId) {
        for (ByteBuffer session : mSessionIds.keySet()) {
            if (mSessionIds.get(session).equals(aSessionId)) {
                mSessionIds.remove(session);
                return session;
            }
        }
        return null;
    }

    private boolean sessionExists(ByteBuffer aSession) {
        if (mCryptoSessionId == null) {
            assert mSessionIds.isEmpty();
            Log.e(LOGTAG, "Session doesn't exist because media crypto session is not created.");
            return false;
        }
        assert mSessionIds.containsKey(mCryptoSessionId);
        if (mSingleSessionMode) {
            return mCryptoSessionId.equals(aSession);
        }
        return !aSession.equals(mCryptoSessionId) && mSessionIds.containsKey(aSession);
    }

    // TODO : release the resources
    private void release() {
        assert mDrm != null;
        mPendingCreateSessionDataQueue.clear();
        mPendingCreateSessionDataQueue = null;

        for (ByteBuffer session : mSessionIds.keySet()) {
            if (session != null && mDrm != null) {
                mDrm.closeSession(session.array());
            }
        }
        mSessionIds.clear();
        mSessionIds = null;
        mSessionMIMETypes.clear();
        mSessionMIMETypes = null;

        mCryptoSessionId = null;
        if (mCrypto != null) {
            mCrypto.release();
            mCrypto = null;
        }
        if (mDrm != null) {
            mDrm.release();
            mDrm = null;
        }
    }

    private class PostRequestTask extends AsyncTask<String, Void, Void> {
        private static final String LOGTAG = "PostRequestTask";

        private byte[] mDrmRequest;
        private byte[] mResponseBody;

        public PostRequestTask(byte[] drmRequest) {
            mDrmRequest = drmRequest;
        }

        @Override
        protected Void doInBackground(String... urls) {
            try {
                mResponseBody = postRequest(urls[0], mDrmRequest);
                if (mResponseBody != null) {
                    Log.d(LOGTAG, "response length=" + mResponseBody.length);
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
            return null;
        }

        private byte[] postRequest(String url, byte[] drmRequest) throws IOException {
            // TODO : Check the correctness
            URL finalURL = new URL(url + "&signedRequest=" + new String(drmRequest));
            HttpURLConnection urlConnection = (HttpURLConnection) finalURL.openConnection();
            urlConnection.setRequestMethod("POST");
            Log.d(LOGTAG, "Provisioning : postRequest=" + finalURL.toString());
            try {
                // Add data
                urlConnection.setRequestProperty("Accept", "*/*");
                urlConnection.setRequestProperty("User-Agent", "Widevine CDM v1.0");
                urlConnection.setRequestProperty("Content-Type", "application/json");

                // Execute HTTP Post Request
                urlConnection.connect();

                int responseCode = urlConnection.getResponseCode();
                if (responseCode == HttpURLConnection.HTTP_OK) {
                    BufferedReader in =
                      new BufferedReader(new InputStreamReader(urlConnection.getInputStream()));
                    String inputLine;
                    StringBuffer response = new StringBuffer();

                    while ((inputLine = in.readLine()) != null) {
                        response.append(inputLine);
                    }
                    in.close();
                    Log.d(LOGTAG, "Provisioning : postRequest - done");
                    return String.valueOf(response).getBytes();
                } else {
                    Log.d(LOGTAG, "Server returned HTTP error code " + responseCode);
                    return null;
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
            return null;
        }

        @Override
        protected void onPostExecute(Void v) {
            onProvisionResponse(mResponseBody);
        }
    }

    boolean provideProvisionResponse(byte[] response) {
        if (response == null || response.length == 0) {
            Log.e(LOGTAG, "Invalid provision response.");
            return false;
        }

        try {
            mDrm.provideProvisionResponse(response);
            return true;
        } catch (android.media.DeniedByServerException e) {
            Log.e(LOGTAG, "failed to provide provision response", e);
        } catch (java.lang.IllegalStateException e) {
            Log.e(LOGTAG, "failed to provide provision response", e);
        }
        return false;
    }

    private void savePendingCreateSessionData(int aToken, int aPromiseId,
                                              byte[] initData, String mime) {
        Log.d(LOGTAG, "savePendingCreateSessionData() - promiseId : " + aPromiseId);
        mPendingCreateSessionDataQueue.offer(new PendingCreateSessionData(aToken, aPromiseId, initData, mime));
    }

    private void processPendingCreateSessionData() {
        Log.d(LOGTAG, "processPendingCreateSessionData()");
        assert mDrm != null;

        // Check !mProvisioning because NotProvisionedException may be
        // thrown in createSession().
        while (!mProvisioning && !mPendingCreateSessionDataQueue.isEmpty()) {
            PendingCreateSessionData pendingData = mPendingCreateSessionDataQueue.poll();
            int token = pendingData.token();
            int promiseId = pendingData.promiseId();
            Log.d(LOGTAG, "processPendingCreateSessionData() >>>> promiseId : " + promiseId + " ... ");
            byte[] initData = pendingData.initData();
            String mime = pendingData.mimeType();
            createSession(token, promiseId, mime, initData);
        }
    }

    private void resumePendingOperations() {
        mHandler.post(new Runnable(){
            @Override
            public void run() {
                processPendingCreateSessionData();
            }
        });
    }

    // Triggered only when failed on {openSession, getKeyRequest}
    private void startProvisioning() {
        Log.d(LOGTAG, "startProvisioning");
        assert mDrm != null;
        assert !mProvisioning;
        mProvisioning = true;
        MediaDrm.ProvisionRequest request = mDrm.getProvisionRequest();
        PostRequestTask postTask = new PostRequestTask(request.getData());
        postTask.execute(request.getDefaultUrl());
    }

    private void onProvisionResponse(byte[] response) {
        Log.d(LOGTAG, "onProvisionResponse()");
        assert mProvisioning;
        mProvisioning = false;

        // If |mDrm| is released, there is no need to callback native.
        if (mDrm == null) {
            return;
        }

        boolean success = provideProvisionResponse(response);

        if (success) {
            resumePendingOperations();
        } else {
            Log.e(LOGTAG, "onProvisionResponse() Failed.");
        }
    }

    // ===============================================================================
    private boolean ensureMediaCryptoCreated() throws android.media.NotProvisionedException {
        if (mCrypto != null) {
            return true;
        }
        try {
            mCryptoSessionId = openSession();
            if (mCryptoSessionId == null) {
                Log.e(LOGTAG, " Cannot open session for MediaCrypto");
                return false;
            }

            if (MediaCrypto.isCryptoSchemeSupported(mSchemeUUID)) {
                final byte [] cryptoSessionId = mCryptoSessionId.array();
                mCrypto = new MediaCrypto(mSchemeUUID, cryptoSessionId);
                String strCrypteSessionId = new String(mCryptoSessionId.array());
                mSessionIds.put(mCryptoSessionId, INVALID_SESSION_ID);
                Log.d(LOGTAG, "MediaCrypto successfully created! - SId " + INVALID_SESSION_ID + ", " + strCrypteSessionId);
                getLock().notify();
                return true;
            } else {
                Log.e(LOGTAG, "Cannot create MediaCrypto for unsupported scheme.");
                return false;
            }
        } catch (android.media.MediaCryptoException e) {
            Log.e(LOGTAG, "Cannot create MediaCrypto", e);
            release();
            return false;
        } catch (android.media.NotProvisionedException e) {
            Log.e(LOGTAG, "ensureMediaCryptoCreated::Device not provisioned", e);
            throw e;
        }
    }

    private UUID convertKeySystemToSchemeUUID(String keySystem) {
      if (keySystem.equals(WIDEVINE_KEY_SYSTEM)) {
          return WIDEVINE_SCHEME_UUID;
      }
      Log.e(LOGTAG, "Cannot convert unsupported key system : " + keySystem);
      return null;
    }
}
