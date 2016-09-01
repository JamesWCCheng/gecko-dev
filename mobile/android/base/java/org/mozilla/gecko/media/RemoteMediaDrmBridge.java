/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.media.MediaCrypto;
import android.os.IBinder;
import android.os.DeadObjectException;
import android.os.RemoteException;
import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.annotation.RobocopTarget;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoAppShell;
import android.util.Log;

final class RemoteMediaDrmBridge implements GeckoMediaDrm {
    private static final String LOGTAG = "\033[1;41mRemoteMediaDrmBridge\033[m";
    private static final boolean DEBUG = false;
    private CallbacksForwarder mCallbacks;
    private IMediaDrmBridge mRemote;

    private static class CallbacksForwarder extends IMediaDrmBridgeCallbacks.Stub {
        // MediaDrmProxy callback handle.
        private final GeckoMediaDrm.Callbacks mCallbacks;
        CallbacksForwarder(Callbacks callbacks) {
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
        public void onSessionError(int promiseId, byte[] sessionId) {
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

    public RemoteMediaDrmBridge(IMediaDrmBridge remoteBridge){
        mRemote = remoteBridge;
    }
    @Override
    public synchronized void setCallbacks(Callbacks callbacks) {
        log("setCallbacks");
        mCallbacks = new CallbacksForwarder(callbacks);
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot send setCallbacks to an ended drm bridge");
            mCallbacks.onError("cannot send setCallbacks to an ended drm bridge");
            return;
        }
        try {
            mRemote.setCallbacks(mCallbacks);
        } catch (DeadObjectException e) {;
            log("fail to setCallbacks");
            reportError(e);
        } catch (RemoteException e) {
            log("fail to setCallbacks");
            reportError(e);
        }
    }

    @Override
    public synchronized void createSession(int createSessionToken,
                                           int promiseId,
                                           String initDataType,
                                           byte[] initData) {
        log("createSession");
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot send createSession to an ended drm bridge");
            mCallbacks.onError("cannot send createSession to an ended drm bridge");
            return;
        }
        try {
            mRemote.createSession(createSessionToken, promiseId, initDataType, initData);
        } catch (DeadObjectException e) {;
            log("fail to createSession");
            reportError(e);
        } catch (RemoteException e) {
            log("fail to createSession");
            reportError(e);
        }
    }

    @Override
    public synchronized void updateSession(int promiseId, String sessionId, byte[] response) {
        log("updateSession");
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot send updateSession to an ended drm bridge");
            mCallbacks.onError("cannot send updateSession to an ended drm bridge");
            return;
        }
        try {
            mRemote.updateSession(promiseId, sessionId, response);
        } catch (DeadObjectException e) {
            log("fail to updateSession");
            reportError(e);
        } catch (RemoteException e) {
            log("fail to updateSession");
            reportError(e);
        }
    }

    @Override
    public synchronized void closeSession(int promiseId, String sessionId) {
        log("closeSession");
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot send closeSession to an ended drm bridge");
            mCallbacks.onError("cannot send closeSession to an ended drm bridge");
            return;
        }
        try {
            mRemote.closeSession(promiseId, sessionId);
        } catch (DeadObjectException e) {
            log("fail to closeSession");
            reportError(e);
        } catch (RemoteException e) {
            log("fail to closeSession");
            reportError(e);
        }
    }

    @Override
    public synchronized void release() {
        log("release");
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot send release to an ended drm bridge");
            mCallbacks.onError("cannot send release to an ended drm bridge");
            return;
        }
        try {
            mRemote.release();
            // [TODO] Check the lifecycle if it is ok to set null.
            mRemote = null;
        } catch (DeadObjectException e) {
            log("fail to release");
            reportError(e);
        } catch (RemoteException e) {
            log("fail to release");
            reportError(e);
        }
    }

    @Override
    public MediaCrypto getMediaCrypto() {
        log("getMediaCrypto");
        return null;
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
} // class RemoteMediaDrmBridge