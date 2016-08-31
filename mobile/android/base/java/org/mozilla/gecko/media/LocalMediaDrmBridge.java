/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import static android.os.Build.VERSION.SDK_INT;
import static android.os.Build.VERSION_CODES.LOLLIPOP;
import static android.os.Build.VERSION_CODES.M;
import android.util.Log;

final class LocalMediaDrmBridge implements GeckoMediaDrm {
    private static final String LOGTAG = "\033[1;41mLocalMediaDrmBridge\033[m";
    private static final boolean DEBUG = true;
    private GeckoMediaDrm mBridge = null;
    private CallbacksForwarder mCallbacks;

    // Forward the callback calls to the callback class of MediaDrmProxy.
    private static class CallbacksForwarder implements GeckoMediaDrm.Callbacks {
        private final GeckoMediaDrm.Callbacks mCallbacks;
        CallbacksForwarder(GeckoMediaDrm.Callbacks callbacks) {
            mCallbacks = callbacks;
        }

        @Override
        public void onSessionCreated(int createSessionToken,
                                     int promiseId,
                                     byte[] sessionId,
                                     byte[] request) {
            mCallbacks.onSessionCreated(createSessionToken,
                                        promiseId,
                                        sessionId,
                                        request);
        }

        @Override
        public void onSessionUpdated(int promiseId, byte[] sessionId) {
            mCallbacks.onSessionUpdated(promiseId, sessionId);
        }

        @Override
        public void onSessionClosed(int promiseId, byte[] sessionId) {
            mCallbacks.onSessionClosed(promiseId, sessionId);
        }

        @Override
        public void onSessionMessage(byte[] sessionId,
                                     int sessionMessageType,
                                     byte[] request) {
            mCallbacks.onSessionMessage(sessionId, sessionMessageType, request);
        }

        @Override
        public void onSessionError(int promiseId,
                                   byte[] sessionId) {
            mCallbacks.onSessionError(promiseId, sessionId);
        }

        @Override
        public void onSessionKeyChanged(byte[] sessionId,
                                        byte[] keyId,
                                        int statusCode) {
            mCallbacks.onSessionKeyChanged(sessionId, keyId, statusCode);
        }

        @Override
        public void onError(String message) {
            mCallbacks.onError(message);
        }
    } // CallbacksForwarder

    LocalMediaDrmBridge(String keySystem) {
        if (M > SDK_INT && SDK_INT >= LOLLIPOP) {
            mBridge = new LollipopGeckoMediaDrmBridge(keySystem);
        } else if (SDK_INT >= M) {
            mBridge = new MashmallowGeckoMediaDrmBridge(keySystem);
        } else {
            mBridge = null;
        }
    }
    @Override
    public synchronized void setCallbacks(Callbacks callbacks) {
        log("setCallbacks");
        mCallbacks = new CallbacksForwarder(callbacks);
        mBridge.setCallbacks(mCallbacks);
    }

    @Override
    public synchronized void createSession(int createSessionToken,
                                           int promiseId,
                                           String initDataType,
                                           byte[] initData) {
        log("createSession");
        if (mBridge == null) {
            Log.e(LOGTAG, "cannot send createSession to an ended drm bridge");
            mCallbacks.onError("cannot send createSession to an ended drm bridge");
            return;
        }
        try {
            mBridge.createSession(createSessionToken, promiseId, initDataType, initData);
        } catch (Exception e) {
            log("fail to createSession");
            reportError(e);
        }
    }

    @Override
    public synchronized void updateSession(int promiseId, String sessionId, byte[] response) {
        log("updateSession");
        if (mBridge == null) {
            Log.e(LOGTAG, "cannot send updateSession to an ended drm bridge");
            mCallbacks.onError("cannot send updateSession to an ended drm bridge");
            return;
        }
        try {
            mBridge.updateSession(promiseId, sessionId, response);
        } catch (Exception e) {
            log("fail to updateSession");
            reportError(e);
        }
    }

    @Override
    public synchronized void closeSession(int promiseId, String sessionId) {
        log("closeSession");
        if (mBridge == null) {
            Log.e(LOGTAG, "cannot send closeSession to an ended drm bridge");
            mCallbacks.onError("cannot send closeSession to an ended drm bridge");
            return;
        }
        try {
            mBridge.closeSession(promiseId, sessionId);
        } catch (Exception e) {
            log("fail to closeSession");
            reportError(e);
        }
    }

    @Override
    public synchronized void release() {
        log("release");
        if (mBridge == null) {
            Log.e(LOGTAG, "cannot send release to an ended drm bridge");
            mCallbacks.onError("cannot send closeSession to an ended drm bridge");
            return;
        }
        try {
            mBridge.release();
            // [TODO] Check the lifecycle if it is ok to set null.
            mBridge = null;
        } catch (Exception e) {
            log("fail to release");
            reportError(e);
        }
    }

    private void reportError(Exception e) {
        if (e != null) {
            e.printStackTrace();
        }
        mCallbacks.onError(e.getMessage());
    }

    private void log(String msg) {
        if (DEBUG) {
          Log.d(LOGTAG, "[" + this + "][" + msg + "]");
        }
    }
} // LocalMediaDrmBridge