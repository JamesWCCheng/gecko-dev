// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.mozilla.gecko.media;

import java.util.ArrayList;

import android.media.MediaCrypto;
import static android.os.Build.VERSION.SDK_INT;
import static android.os.Build.VERSION_CODES.LOLLIPOP;
import static android.os.Build.VERSION_CODES.M;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

/* package */
final class RemoteMediaDrmBridgeStub extends IMediaDrmBridge.Stub implements IBinder.DeathRecipient {
    private static final String LOGTAG = "RemoteMediaDrmBridgeStub";
    private static final boolean DEBUG = false;
    private volatile IMediaDrmBridgeCallbacks mCallbacks = null;
    private GeckoMediaDrm mBridge = null;
    private String mStubUUID = "";

    public static ArrayList<RemoteMediaDrmBridgeStub> mStubList =
        new ArrayList<RemoteMediaDrmBridgeStub>();

    private String getUUID() {
        return mStubUUID;
    }

    private MediaCrypto getMediaCryptoFromBridge() {
        return mBridge != null ? mBridge.getMediaCrypto() : null;
    }

    public static MediaCrypto getMediaCrypto(String uuid) {
        for (int i = 0; i < mStubList.size(); i++) {
            if (mStubList.get(i) != null &&
                mStubList.get(i).getUUID().equals(uuid)) {
                return mStubList.get(i).getMediaCryptoFromBridge();
            }
        }
        return null;
    }

    private final class Callbacks implements GeckoMediaDrm.Callbacks {
        private IMediaDrmBridgeCallbacks mRemote;

        public Callbacks(IMediaDrmBridgeCallbacks remote) {
            mRemote = remote;
        }

        @Override
        public void onSessionCreated(int createSessionToken,
                                     int promiseId,
                                     byte[] sessionId,
                                     byte[] request) {
            log("onSessionCreated");
            try {
                mRemote.onSessionCreated(createSessionToken,
                                         promiseId,
                                         sessionId,
                                         request);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionUpdated(int promiseId, byte[] sessionId) {
            log("onSessionUpdated");
            try {
                mRemote.onSessionUpdated(promiseId, sessionId);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionClosed(int promiseId, byte[] sessionId) {
            log("onSessionClosed");
            try {
                mRemote.onSessionClosed(promiseId, sessionId);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionMessage(byte[] sessionId,
                                     int sessionMessageType,
                                     byte[] request) {
            log("onSessionMessage");
            try {
                mRemote.onSessionMessage(sessionId, sessionMessageType, request);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionError(int promiseId, byte[] sessionId) {
            log("onSessionError");
            try {
                mRemote.onSessionError(promiseId, sessionId);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionKeyChanged(byte[] sessionId, byte[] keyId, int statusCode) {
            log("onSessionKeyChanged");
            try {
                mRemote.onSessionKeyChanged(sessionId, keyId, statusCode);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionBatchedKeyChanged(byte[] sessionId,
                                               SessionKeyInfo[] keyInfos) {
        }

        @Override
        public void onError(String message) {
            log("onError");
            try {
                mRemote.onError(message);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }
    }

    private void log(String funcName) {
        if (DEBUG) {
          Log.d(LOGTAG, "[" + this + "][" + funcName + "]");
        }
    }

    private void reportError(Exception e) {
        if (e != null) {
            e.printStackTrace();
        }
        try {
            mCallbacks.onError(e.getMessage());
        } catch (RemoteException re) {
            re.printStackTrace();
        }
    }

    private void assertCallbacks() {
        if (mCallbacks == null) {
            throw new IllegalStateException(LOGTAG + ": callback must be supplied with setCallbacks().");
        }
    }

    private void assertBridge() {
        if (mBridge == null) {
            throw new IllegalStateException(LOGTAG + ": bridge doesn't exist !.");
        }
    }

    @Override
    public void release() {
        log("release");
        mStubList.remove(this);
        if (mBridge != null) {
            mBridge.release();
            mBridge = null;
        }
        mCallbacks.asBinder().unlinkToDeath(this, 0);
        mCallbacks = null;
    }

    RemoteMediaDrmBridgeStub(String keySystem, String uuid) {
        if (M > SDK_INT && SDK_INT >= LOLLIPOP) {
            mBridge = new GeckoMediaDrmBridgeV21(keySystem);
        } else if (SDK_INT >= M) {
            mBridge = new GeckoMediaDrmBridgeV23(keySystem);
        } else {
            throw new IllegalStateException(LOGTAG + "Bridge cannot be created correctly !!");
        }
        mStubUUID = uuid;
        mStubList.add(this);
    }

    @Override
    public synchronized void setCallbacks(IMediaDrmBridgeCallbacks callbacks) throws RemoteException {
        assertBridge();
        mCallbacks = callbacks;
        callbacks.asBinder().linkToDeath(this, 0);
        mBridge.setCallbacks(new Callbacks(mCallbacks));
    }

    // IBinder.DeathRecipient
    @Override
    public synchronized void binderDied() {
        Log.e(LOGTAG, "Callbacks is dead");
        try {
            release();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    @Override
    public synchronized void createSession(int createSessionToken,
                                           int promiseId,
                                           String initDataType,
                                           byte[] initData) throws RemoteException {
        log("createSession");
        try {
            assertCallbacks();
            assertBridge();
            mBridge.createSession(createSessionToken,
                                  promiseId,
                                  initDataType,
                                  initData);
        } catch (Exception e) {
            reportError(e);
        }
    }

    @Override
    public synchronized void updateSession(int promiseId,
                                           String sessionId,
                                           byte[] response) throws RemoteException {
        log("updateSession");
        try {
            assertCallbacks();
            assertBridge();
            mBridge.updateSession(promiseId, sessionId, response);
        } catch (Exception e) {
            reportError(e);
        }
    }

    @Override
    public synchronized void closeSession(int promiseId, String sessionId) throws RemoteException {
        log("closeSession");
        try {
            assertCallbacks();
            assertBridge();
            mBridge.closeSession(promiseId, sessionId);
        } catch (Exception e) {
            reportError(e);
        }
    }
}
