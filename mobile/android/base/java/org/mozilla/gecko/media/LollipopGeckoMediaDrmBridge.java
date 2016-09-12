/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.UUID;
import java.util.ArrayDeque;

import android.os.AsyncTask;
import android.os.Handler;
import android.os.Looper;
import android.media.MediaCrypto;
import android.media.MediaCryptoException;
import android.media.MediaDrm;
import android.media.MediaDrmException;
import android.util.Log;

public class LollipopGeckoMediaDrmBridge implements GeckoMediaDrm {
    private static final String LOGTAG = "LollipopGeckoMediaDrmBridge";
    private static final UUID WIDEVINE_SCHEME_UUID =
      new UUID(0xedef8ba979d64aceL, 0xa3c827dcd51d21edL);
    private static final String INVALID_SESSION_ID = "Invalid";
    private static final String WIDEVINE_KEY_SYSTEM = "com.widevine.alpha";
    protected MediaDrm mDrm;
    private MediaCrypto mCrypto;
    private UUID mSchemeUUID;
    private Handler mHandler;
    private ByteBuffer mCryptoSessionId;
    private boolean mProvisioning;
    private boolean mSingleSessionMode;
    private HashMap<ByteBuffer, String> mSessionIds;
    private HashMap<ByteBuffer, String> mSessionMIMETypes;
    private ArrayDeque<PendingCreateSessionData> mPendingCreateSessionDataQueue;
    private GeckoMediaDrm.Callbacks mCallbacks;

    private static class PendingCreateSessionData {
        private final int mToken;
        private final int mPromiseId;
        private final byte[] mInitData;
        private final String mMimeType;

        private PendingCreateSessionData(int token, int promiseId,
                                         byte[] initData, String mimeType) {
            mToken = token;
            mPromiseId = promiseId;
            mInitData = initData;
            mMimeType = mimeType;
        }

        private int token() { return mToken; }
        private int promiseId() { return mPromiseId; }
        private byte[] initData() { return mInitData; }
        private String mimeType() { return mMimeType; }
    }

    private void log(String msg) {
        Log.d(LOGTAG, msg);
    }

    public boolean isSecureDecoderComonentRequired(String mimeType) {
      if (mCrypto != null) {
          return mCrypto.requiresSecureDecoderComponent(mimeType);
      }
      return false;
    }

    @Override
    public void setCallbacks(GeckoMediaDrm.Callbacks callbacks) {
        if (callbacks == null) {
            return;
        }
        mCallbacks = callbacks;
    }

    LollipopGeckoMediaDrmBridge(String keySystem) {
        log("LollipopGeckoMediaDrmBridge");

        mProvisioning = false;
        // [TODO] Not sure in which case this is ture.
        mSingleSessionMode = false;
        mSessionIds = new HashMap<ByteBuffer, String>();
        mSessionMIMETypes = new HashMap<ByteBuffer, String>();
        mPendingCreateSessionDataQueue = new ArrayDeque<PendingCreateSessionData>();

        Looper myLoop = Looper.myLooper();
        if (myLoop == null) {
            Looper.prepare();
            mHandler = new Handler();
        }
        mSchemeUUID = convertKeySystemToSchemeUUID(keySystem);
        mCryptoSessionId = null;

        log("mSchemeUUID : " + mSchemeUUID.toString());
        try {
            mDrm = new MediaDrm(mSchemeUUID);
            try {
                String currentSecurityLevel = mDrm.getPropertyString("securityLevel");
                log("Security level: current level = " + currentSecurityLevel);
                mDrm.setPropertyString("securityLevel", "L3");
                currentSecurityLevel = mDrm.getPropertyString("securityLevel");
                log("Security level: after set securityLevel = " + currentSecurityLevel);

                // Reference chromium, using multiple session mode for Widevine.
                if (mSchemeUUID.equals(WIDEVINE_SCHEME_UUID)) {
                    // mDrm.setPropertyString("privacyMode", "enable");
                    mDrm.setPropertyString("sessionSharing", "enable");
                }
            }
            catch (Exception e) {
                log("Failed to setPropertyString: " + e.getMessage());
            }
            mDrm.setOnEventListener(new MediaDrmListener());
        } catch (MediaDrmException e) {
            log("Failed to create MediaDrm: " + e.getMessage());
        }
    }

    private void assertMediaCrypto() throws Exception {
        if (mCrypto == null) {
            throw new Exception(" mCrypto does not exist ! ");
        }
    }

    @Override
    public void createSession(int createSessionToken,
                              int promiseId,
                              String initDataType,
                              byte[] initData) {
        log("createSession");
        if (mDrm == null) {
            onError("mDrm does NOT exist");
            return;
        }

        if (mProvisioning && mCrypto == null) {
            log("Pending createsession because of provisioning !");
            savePendingCreateSessionData(createSessionToken, promiseId,
                                         initData, initDataType);
            return;
        }
        ByteBuffer sessionId = null;
        String strSessionId = null;
        try {
            ensureMediaCryptoCreated();

            sessionId = openSession();
            if (sessionId == null) {
                onError("Cannot get a session id from MediaDrm !");
                return;
            }
            strSessionId = new String(sessionId.array());
            mSessionIds.put(sessionId, strSessionId);

            MediaDrm.KeyRequest request = null;
            try {
                request = getKeyRequest(sessionId, initData, initDataType);
            } catch (Exception e) {
                log("Exception intentionally caught when calling getKeyRequest():" + e.getMessage());
            }

            if (request == null) {
                if (sessionId != null) {
                    closeSession(promiseId, strSessionId);
                }
                onError("Cannot get a key request from MediaDrm !");
                return;
            }
            onSessionCreated(createSessionToken, promiseId, sessionId.array(),
                             request.getData());
            onSessionMessage(sessionId.array(), 0/*MediaKeyMessageType::License_request = 0*/, request.getData());
            mSessionMIMETypes.put(sessionId, initDataType);
            log(" StringID : " + strSessionId + " is put into mSessionIds ");
        } catch (android.media.NotProvisionedException e) {
            log("Device not provisioned:" + e.getMessage());
            if (sessionId != null) {
                closeSession(0, strSessionId);
            }
            savePendingCreateSessionData(createSessionToken, promiseId,
                                         initData, initDataType);
            startProvisioning();
        }
    }

    @Override
    public void updateSession(int promiseId,
                              String sessionId,
                              byte[] response) {
        log("updateSession, sessionId = " + sessionId);
        if (mDrm == null) {
            onError("mDrm does NOT exist");
            return;
        }

        ByteBuffer session = getSession(sessionId);
        if (!sessionExists(session)) {
            log("Invalid session in updateSession.");
            onSessionError(promiseId, session.array());
            return;
        }

        try {
            final byte [] keySetId = mDrm.provideKeyResponse(session.array(), response);
            HashMap<String, String> infoMap = mDrm.queryKeyStatus(session.array());
            for (String strKey : infoMap.keySet()) {
                String strValue = infoMap.get(strKey);
                log("InfoMap : key("+strKey+")/value("+strValue+")");
            }
        } catch (android.media.NotProvisionedException e) {
            log("failed to provide key response:" + e.getMessage());
            onSessionError(promiseId, session.array());
            release();
            return;
        } catch (android.media.DeniedByServerException e) {
            log("failed to provide key response:" + e.getMessage());
            onSessionError(promiseId, session.array());
            release();
            return;
        } catch (java.lang.IllegalStateException e) {
            // This is not really an exception. Some error code are incorrectly
            // reported as an exception.
            log("Exception intentionally caught when calling provideKeyResponse():" + e.getMessage());
        }
        log("Key successfully added for session " + sessionId);
        onSessionUpdated(promiseId, session.array());
    }

    @Override
    public void closeSession(int promiseId, String sessionId) {
        log("closeSession");
        if (mDrm == null) {
            onError("mDrm does NOT exist");
            return;
        }

        ByteBuffer session = removeSession(sessionId);
        mDrm.closeSession(session.array());
        if (promiseId > 0) {
            onSessionClosed(promiseId, session.array());
        }
    }

    @Override
    public void release() {
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

    @Override
    public MediaCrypto getMediaCrypto() {
        log("getMediaCrypto");
        return mCrypto;
    }

    protected void onSessionCreated(int createSessionToken,
                                    int promiseId,
                                    byte[] sessionId,
                                    byte[] request) {
        mCallbacks.onSessionCreated(createSessionToken, promiseId, sessionId, request);
    }

    protected void onSessionUpdated(int promiseId, byte[] sessionId) {
        mCallbacks.onSessionUpdated(promiseId, sessionId);
    }

    protected void onSessionClosed(int promiseId, byte[] sessionId) {
        mCallbacks.onSessionClosed(promiseId, sessionId);
    }

    protected void onSessionMessage(byte[] sessionId, int sessionMessageType, byte[] request) {
        mCallbacks.onSessionMessage(sessionId, sessionMessageType, request);
    }

    protected void onSessionError(int promiseId, byte[] sessionId) {
        mCallbacks.onSessionError(promiseId, sessionId);
    }

    protected void  onSessionKeyChanged(byte[] sessionId, byte[] keyId, int statusCode) {
        mCallbacks.onSessionKeyChanged(sessionId, keyId, statusCode);
    }

    protected void onError(String message) {
        mCallbacks.onError(message);
    }

    private MediaDrm.KeyRequest getKeyRequest(ByteBuffer aSession,
                                              byte[] data,
                                              String mimeType)
        throws android.media.NotProvisionedException {
        MediaDrm.KeyRequest request = null;
        if (mProvisioning) {
            return request;
        }

        try {
            HashMap<String, String> optionalParameters = new HashMap<String, String>();
            request = mDrm.getKeyRequest(aSession.array(),
                                         data,
                                         mimeType,
                                         MediaDrm.KEY_TYPE_STREAMING,
                                         optionalParameters);

        } catch (Exception e) {
            e.printStackTrace();
        }

        String result = (request != null) ? "successed" : "failed";
        log("getKeyRequest " + result + "!");
        return request;
    }

    private class MediaDrmListener implements MediaDrm.OnEventListener {
        @Override
        public void onEvent(MediaDrm mediaDrm, byte[] sessionArray, int event,
                            int extra, byte[] data) {
            log("MediaDrmListener.onEvent()");
            if (sessionArray == null) {
                log("MediaDrmListener: Null session.");
                return;
            }
            ByteBuffer session = ByteBuffer.wrap(sessionArray);
            if (!sessionExists(session)) {
                log("MediaDrmListener: Invalid session.");
                return;
            }
            String sessionId = mSessionIds.get(session);
            if (sessionId == null || sessionId == INVALID_SESSION_ID) {
               log("MediaDrmListener: Invalid session ID.");
               return;
            }
            switch(event) {
                case MediaDrm.EVENT_PROVISION_REQUIRED:
                    // Maybe handle provisition here ?
                    log("MediaDrm.EVENT_PROVISION_REQUIRED");
                    break;
                case MediaDrm.EVENT_KEY_REQUIRED:
                    log("MediaDrm.EVENT_KEY_REQUIRED");
                    if (mProvisioning) {
                        return;
                    }
                    String initDataType = mSessionMIMETypes.get(session);
                    log("MediaDrm.EVENT_KEY_REQUIRED initDataType = " + initDataType + ", sessionId = " + sessionId);
                    MediaDrm.KeyRequest request = null;
                    try {
                        request = getKeyRequest(session, data, initDataType);
                    } catch (android.media.NotProvisionedException e) {
                        log("Device not provisioned:" + e.getMessage());
                        startProvisioning();
                        return;
                    }
                    if (request != null) {
                        onSessionMessage(session.array(), 0/*MediaKeyMessageType::License_request = 0*/, request.getData());
                    } else {
                        onError("No keyrequest generated for session: " + new String(session.array()));
                    }
                    break;
                case MediaDrm.EVENT_KEY_EXPIRED:
                    log("MediaDrm.EVENT_KEY_EXPIRED");
                    onError("Key expired for session: " + new String(session.array()));
                    break;
                case MediaDrm.EVENT_VENDOR_DEFINED:
                    log("MediaDrm.EVENT_VENDOR_DEFINED");
                    break;
                default:
                    log("Invalid DRM event " + event);
                    return;
            }
        }
    }

    private ByteBuffer openSession() throws android.media.NotProvisionedException {
        try {
            byte[] sessionId = mDrm.openSession();
            // ByteBuffer.wrap() is backed by the byte[]. Make a clone here in
            // case the underlying byte[] is modified.
            return ByteBuffer.wrap(sessionId.clone());
        } catch (android.media.NotProvisionedException e) {
            // Throw NotProvisionedException so that we can startProvisioning().
            throw e;
        } catch (java.lang.RuntimeException e) {
            log("Cannot open a new session:" + e.getMessage());
            release();
            return null;
        } catch (android.media.MediaDrmException e) {
            // Other MediaDrmExceptions (e.g. ResourceBusyException) are not
            // recoverable.
            release();
            return null;
        }
    }

    private ByteBuffer getSession(String sessionId) {
        for (ByteBuffer session : mSessionIds.keySet()) {
            if (mSessionIds.get(session).equals(sessionId)) {
                return session;
            }
        }
        return null;
    }

    private ByteBuffer removeSession(String sessionId) {
        for (ByteBuffer session : mSessionIds.keySet()) {
            if (mSessionIds.get(session).equals(sessionId)) {
                mSessionIds.remove(session);
                return session;
            }
        }
        return null;
    }

    private boolean sessionExists(ByteBuffer session) {
        if (mCryptoSessionId == null) {
            log("Session doesn't exist because media crypto session is not created.");
            return false;
        }
        if (mSingleSessionMode) {
            return mCryptoSessionId.equals(session);
        }
        if (session == null) {
            log("Session is null, not in map !");
            return false;
        }
        return !session.equals(mCryptoSessionId) && mSessionIds.containsKey(session);
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
                    log("response length=" + mResponseBody.length);
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
            return null;
        }

        private byte[] postRequest(String url, byte[] drmRequest) throws IOException {
            URL finalURL = new URL(url + "&signedRequest=" + new String(drmRequest));
            HttpURLConnection urlConnection = (HttpURLConnection) finalURL.openConnection();
            urlConnection.setRequestMethod("POST");
            log("Provisioning : postRequest=" + finalURL.toString());
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
                    log("Provisioning : postRequest - done");
                    return String.valueOf(response).getBytes();
                } else {
                    log("Server returned HTTP error code " + responseCode);
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
            log("Invalid provision response.");
            return false;
        }

        try {
            mDrm.provideProvisionResponse(response);
            return true;
        } catch (android.media.DeniedByServerException e) {
            log("failed to provide provision response:" + e.getMessage());
        } catch (java.lang.IllegalStateException e) {
            log("failed to provide provision response:" + e.getMessage());
        }
        return false;
    }

    private void savePendingCreateSessionData(int token,
                                              int promiseId,
                                              byte[] initData,
                                              String mime) {
        log("savePendingCreateSessionData() - promiseId : " + promiseId);
        mPendingCreateSessionDataQueue.offer(new PendingCreateSessionData(token, promiseId, initData, mime));
    }

    private void processPendingCreateSessionData() {
        log("processPendingCreateSessionData()");
        if (mDrm == null) {
            return;
        }

        // Check !mProvisioning because NotProvisionedException may be
        // thrown in createSession().
        try {
            while (!mProvisioning && !mPendingCreateSessionDataQueue.isEmpty()) {
                PendingCreateSessionData pendingData = mPendingCreateSessionDataQueue.poll();
                int token = pendingData.token();
                int promiseId = pendingData.promiseId();
                log("processPendingCreateSessionData() >>>> promiseId : " + promiseId + " ... ");
                byte[] initData = pendingData.initData();
                String mime = pendingData.mimeType();
                createSession(token, promiseId, mime, initData);
            }
        } catch (Exception e) {
            onError("processPendingCreateSessionData failed:" + e.getMessage());
        }
    }

    private void resumePendingOperations() {
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                processPendingCreateSessionData();
            }
        });
    }

    // Triggered only when failed on {openSession, getKeyRequest}
    private void startProvisioning() {
        log("startProvisioning");
        if (mProvisioning) {
            return;
        }
        try {
            mProvisioning = true;
            MediaDrm.ProvisionRequest request = mDrm.getProvisionRequest();
            PostRequestTask postTask = new PostRequestTask(request.getData());
            postTask.execute(request.getDefaultUrl());
        } catch (Exception e) {
            onError("Exception happened in startProvisioning !");
            e.printStackTrace();
        }
    }

    private void onProvisionResponse(byte[] response) {
        log("onProvisionResponse()");
        mProvisioning = false;

        // If |mDrm| is released, there is no need to callback native.
        if (mDrm == null) {
            return;
        }

        boolean success = provideProvisionResponse(response);

        if (success) {
            resumePendingOperations();
        } else {
            log("onProvisionResponse() Failed.");
        }
    }

    private boolean ensureMediaCryptoCreated() throws android.media.NotProvisionedException {
        if (mCrypto != null) {
            return true;
        }
        try {
            mCryptoSessionId = openSession();
            if (mCryptoSessionId == null) {
                log(" Cannot open session for MediaCrypto");
                return false;
            }

            if (MediaCrypto.isCryptoSchemeSupported(mSchemeUUID)) {
                final byte [] cryptoSessionId = mCryptoSessionId.array();
                mCrypto = new MediaCrypto(mSchemeUUID, cryptoSessionId);
                String strCrypteSessionId = new String(mCryptoSessionId.array());
                mSessionIds.put(mCryptoSessionId, INVALID_SESSION_ID);
                log("MediaCrypto successfully created! - SId " + INVALID_SESSION_ID + ", " + strCrypteSessionId);
                return true;
            } else {
                log("Cannot create MediaCrypto for unsupported scheme.");
                return false;
            }
        } catch (android.media.MediaCryptoException e) {
            log("Cannot create MediaCrypto:" + e.getMessage());
            release();
            return false;
        } catch (android.media.NotProvisionedException e) {
            log("ensureMediaCryptoCreated::Device not provisioned:" + e.getMessage());
            throw e;
        }
    }

    private UUID convertKeySystemToSchemeUUID(String keySystem) {
      if (keySystem.equals(WIDEVINE_KEY_SYSTEM)) {
          return WIDEVINE_SCHEME_UUID;
      }
      log("Cannot convert unsupported key system : " + keySystem);
      return null;
    }
}
