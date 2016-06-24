// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.mozilla.gecko.media;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

//import java.nio.ByteOrder;
import java.util.ArrayDeque;

import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.annotation.RobocopTarget;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoAppShell;

import android.os.AsyncTask;
//import android.os.Build;
import android.os.Handler;
import android.media.MediaCrypto;
import android.media.MediaCryptoException;
import android.media.MediaDrm;
import android.media.MediaDrmException;
import android.util.Log;

import java.lang.InterruptedException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.UUID;

public class MediaDrmBridge extends JNIObject {

    private static final String LOGTAG = "MediaDrmBridge";
    private static final UUID CLEARKEY_SCHEME_UUID =
      new UUID(0x1077efecc0b24d02L, 0xace33c1e52e2fb4bL);
    private static final UUID WIDEVINE_SCHEME_UUID =
      new UUID(0xedef8ba979d64aceL, 0xa3c827dcd51d21edL);
    private static final UUID PLAYREADY_SCHEME_UUID =
      new UUID(0x9a04f07998404286L, 0xab92e658e0885f95L);
    private static final String INVALID_SESSION_ID = "Invalid";

    private MediaDrm mDrm;
    private MediaCrypto mCrypto;
    private UUID mSchemeUUID;
    private Handler mHandler;
    private ByteBuffer mCryptoSessionId;
    private boolean mProvisioning;
    private HashMap<ByteBuffer, String> mSessionIds;
    private HashMap<ByteBuffer, String> mSessionMIMETypes;
    private ArrayDeque<PendingCreateSessionData> mPendingCreateSessionDataQueue;

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

    @WrapForJNI(allowMultithread = true)
    MediaDrmBridge(UUID aKeySystem) {
        Log.d(LOGTAG, "MediaDrmBridge()");
        mProvisioning = false;
        mSessionIds = new HashMap<ByteBuffer, String>();
        mSessionMIMETypes = new HashMap<ByteBuffer, String>();
        mPendingCreateSessionDataQueue = new ArrayDeque<PendingCreateSessionData>();
        mHandler = new Handler();
        mSchemeUUID = aKeySystem;
        Log.e(LOGTAG, "mSchemeUUID : " + mSchemeUUID.toString());
        try {
            mDrm = new MediaDrm(mSchemeUUID);
            try {
              String currentSecurityLevel = mDrm.getPropertyString("securityLevel");
              Log.e(LOGTAG, "Security level: current " + currentSecurityLevel);
              mDrm.setPropertyString("securityLevel", "L3");
              currentSecurityLevel = mDrm.getPropertyString("securityLevel");
              Log.e(LOGTAG, "Security level: after L3 " + currentSecurityLevel);
            }
            catch (Exception e) {
              Log.e(LOGTAG, "Failed to setPropertyString: " + e.getMessage());
            }
            mDrm.setOnEventListener(new MediaDrmListener());
        } catch (MediaDrmException e) {
            Log.e(LOGTAG, "Failed to create MediaDrm: " + e.getMessage());
        }
    }

    @WrapForJNI(allowMultithread = true)
    public boolean Init(UUID aKeySystem) {
        synchronized (getLock()) {
            Log.d(LOGTAG, "Init");
            return true;
        }
    }

    @WrapForJNI(allowMultithread = true)
    private boolean CreateSession(int aCreateSessionToken, int aPromiseId,
                                  String aInitDataType, byte[] aInitData) {
        Log.d(LOGTAG, "CreateSession");
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
                sessionId = openSession();
                strSessionId = new String(sessionId.array());
                boolean hasCrypto = ensureMediaCryptoCreated(sessionId);

                MediaDrm.KeyRequest request = null;
                request = getKeyRequest(sessionId, aInitData, aInitDataType);
                if (request == null) {
                    if (sessionId != null) {
                        CloseSession(strSessionId);
                    }
                    return false;
                }

                onSessionCreated(aCreateSessionToken, aPromiseId, sessionId.array(),
                                 request.getData());

                mSessionIds.put(sessionId, strSessionId);
                mSessionMIMETypes.put(sessionId, aInitDataType);
                Log.d(LOGTAG, " StringID : " + strSessionId + " is put into mSessionIds ");
                return true;
            } catch (android.media.NotProvisionedException e) {
                Log.e(LOGTAG, "Device not provisioned", e);
                if (sessionId != null) {
                    CloseSession(strSessionId);
                }
                savePendingCreateSessionData(aCreateSessionToken, aPromiseId,
                                             aInitData, aInitDataType);
                startProvisioning();
                return true;
            }
        }
    }

    @WrapForJNI(allowMultithread = true)
    private boolean UpdateSession(int aPromiseId, String aSessionId,
                                  byte[] aResponse) {
      Log.d(LOGTAG, "UpdateSession >>> " + aSessionId);

        if (mDrm == null) {
            Log.e(LOGTAG, "updateSession() called when MediaDrm is null.");
            return false;
        }

        ByteBuffer session = getSession(aSessionId);
        if (session == null) {
            Log.e(LOGTAG, "updateSession : found no session for Id="+aSessionId);
        }
      // TODO : Check the following error
//        if (!sessionExists(session)) {
//            Log.e(LOGTAG, "Invalid session in updateSession.");
//            onSessionError(sessionId);
//            return false;
//        }

        try {
            try {
                final byte [] keySetId = mDrm.provideKeyResponse(session.array(), aResponse);
                // Aandroid ClearKeyPlugin doesn't handle QueryKeyStatus
                if (!mSchemeUUID.equals(CLEARKEY_SCHEME_UUID)) {
                    HashMap<String, String> infoMap = mDrm.queryKeyStatus(session.array());
                    for (String strKey : infoMap.keySet()) {
                        String strValue = infoMap.get(strKey);
                        Log.e(LOGTAG, "InfoMap : key("+strKey+")/value("+strValue+")");
                    }
                }
            } catch (java.lang.IllegalStateException e) {
                // This is not really an exception. Some error code are incorrectly
                // reported as an exception.
                // TODO(qinmin): remove this exception catch when b/10495563 is fixed.
                Log.e(LOGTAG, "Exception intentionally caught when calling provideKeyResponse()", e);
            }
            onSessionUpdated(aPromiseId, session.array());
            Log.d(LOGTAG, "Key successfully added for session " + aSessionId);
            return true;
        } catch (android.media.NotProvisionedException e) {
            // TODO(xhwang): Should we handle this?
            Log.e(LOGTAG, "failed to provide key response", e);
        } catch (android.media.DeniedByServerException e) {
            Log.e(LOGTAG, "failed to provide key response", e);
        }
        //onSessionError(sessionId);
        release();
        return false;
    }

    @WrapForJNI(allowMultithread = true)
    private void CloseSession(String aSessionId) {
        Log.d(LOGTAG, "CloseSession");
        assert mDrm != null;
        ByteBuffer session = getSession(aSessionId);
        mDrm.closeSession(session.array());
    }

    @WrapForJNI(allowMultithread = true)
    private MediaCrypto GetMediaCrypto() {
        synchronized (getLock()) {
            if (mCrypto == null) {
                try {
                    getLock().wait();
                } catch (InterruptedException e){
                    Log.d(LOGTAG, " Wait for MediaCrypto !!!");
                    return null;
                }
            }
        }
        return mCrypto;
    }

    @WrapForJNI(allowMultithread = true)
    private native void onSessionCreated(int aCreateSessionToken,
                                         int aPromiseId,
                                         byte[] aSessionId,
                                         byte[] aRequest);

    @WrapForJNI(allowMultithread = true)
    private native void onSessionUpdated(int aPromiseId, byte[] aSessionId);

    @WrapForJNI(allowMultithread = true)
    private native void onSessoinClosed(int aPromiseId, int aSessionId);

    // Refer to Android Java Implementation ==================================
    private MediaDrm.KeyRequest getKeyRequest(ByteBuffer session, byte[] data,
                                              String mime)
        throws android.media.NotProvisionedException {
        assert mDrm != null;
        assert mCrypto != null;
        assert !mProvisioning;
        Log.d(LOGTAG, "getKeyRequest mime = " + mime + ", data length = " + data.length);
        HashMap<String, String> optionalParameters = new HashMap<String, String>();
        MediaDrm.KeyRequest request =
          mDrm.getKeyRequest(session.array(), data, mime,
                             MediaDrm.KEY_TYPE_STREAMING, optionalParameters);
        String result = (request != null) ? "successed" : "failed";
        Log.d(LOGTAG, "getKeyRequest " + result + "!");
        return request;
    }

    private class MediaDrmListener implements MediaDrm.OnEventListener {
        @Override
        public void onEvent(MediaDrm mediaDrm, byte[] sessionArray, int event,
                            int extra, byte[] data) {
            Log.e(LOGTAG, "MediaDrmListener:onEvent() is called !!!!!!!!!! ");
            if (sessionArray == null) {
                Log.e(LOGTAG, "MediaDrmListener: Null session.");
                return;
            }
            ByteBuffer session = ByteBuffer.wrap(sessionArray);
            // TODO : Check the following error
//            if (!sessionExists(session)) {
//                Log.e(LOGTAG, "MediaDrmListener: Invalid session.");
//                return;
//            }
            String sessionId = mSessionIds.get(session);
//            if (sessionId == null || sessionId == INVALID_SESSION_ID) {
//                Log.e(LOGTAG, "MediaDrmListener: Invalid session ID.");
//                return;
//              }
            switch(event) {
                case MediaDrm.EVENT_PROVISION_REQUIRED:
                    Log.d(LOGTAG, "MediaDrm.EVENT_PROVISION_REQUIRED");
                    break;
                case MediaDrm.EVENT_KEY_REQUIRED:
                    Log.d(LOGTAG, "MediaDrm.EVENT_KEY_REQUIRED");
                    if (mProvisioning) {
                        return;
                    }
                    String initDataType = mSessionMIMETypes.get(session);
                    MediaDrm.KeyRequest request = null;
                    try {
                        request = getKeyRequest(session, data, initDataType);
                    } catch (android.media.NotProvisionedException e) {
                        Log.e(LOGTAG, "Device not provisioned", e);
                        startProvisioning();
                        return;
                    }
                    if (request != null) {
                        //onSessionMessage(sessionId, request);
                    } else {
                        //onSessionError(sessionId);
                    }
                    break;
                case MediaDrm.EVENT_KEY_EXPIRED:
                    Log.d(LOGTAG, "MediaDrm.EVENT_KEY_EXPIRED");
                    //onSessionError(sessionId);
                    break;
                case MediaDrm.EVENT_VENDOR_DEFINED:
                    Log.d(LOGTAG, "MediaDrm.EVENT_VENDOR_DEFINED");
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

    // TODO : release the resources
    private void release() {
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
        Log.d(LOGTAG, "savePendingCreateSessionData()");
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
            byte[] initData = pendingData.initData();
            String mime = pendingData.mimeType();
            CreateSession(token, promiseId, mime, initData);
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
        }
    }

    // ===============================================================================
    private boolean ensureMediaCryptoCreated(ByteBuffer aSessionId) {
        if (mCrypto != null) {
            return true;
        }
        try {
            if (MediaCrypto.isCryptoSchemeSupported(mSchemeUUID)) {
                final byte [] cryptoSessionId = aSessionId.array();
                mCrypto = new MediaCrypto(mSchemeUUID, cryptoSessionId);
                getLock().notify();
                String strCrypteSessionId = new String(aSessionId.array());
                Log.d(LOGTAG, "MediaCrypto successfully created! - SId " + strCrypteSessionId);
                    return true;
                } else {
                    Log.e(LOGTAG, "Cannot create MediaCrypto for unsupported scheme.");
                    return false;
                }
        } catch (android.media.MediaCryptoException e) {
            Log.e(LOGTAG, "Cannot create MediaCrypto", e);
            return false;
        }
    }
}
