// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.mozilla.gecko.media;
import org.mozilla.gecko.AppConstants;

import java.util.ArrayList;

import android.media.MediaCrypto;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

final class RemoteMediaDrmBridgeStub extends IMediaDrmBridge.Stub implements IBinder.DeathRecipient {
    private static final String LOGTAG = "GeckoRemoteMediaDrmBridgeStub";
    private static final boolean DEBUG = false;
    private volatile IMediaDrmBridgeCallbacks mCallbacks = null;
    private GeckoMediaDrm mBridge = null;
    private String mStubUUID = "";

    public static ArrayList<RemoteMediaDrmBridgeStub> mBridgeStubs =
        new ArrayList<RemoteMediaDrmBridgeStub>();

    private String getUUID() {
        return mStubUUID;
    }

    private MediaCrypto getMediaCryptoFromBridge() {
        return mBridge != null ? mBridge.getMediaCrypto() : null;
    }

    public static MediaCrypto getMediaCrypto(String uuid) {
        if (DEBUG) Log.d(LOGTAG, "getMediaCrypto()");

        for (int i = 0; i < mBridgeStubs.size(); i++) {
            if (mBridgeStubs.get(i) != null &&
                mBridgeStubs.get(i).getUUID().equals(uuid)) {
                return mBridgeStubs.get(i).getMediaCryptoFromBridge();
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
            if (DEBUG) Log.d(LOGTAG, "onSessionCreated()");
            try {
                mRemote.onSessionCreated(createSessionToken,
                                         promiseId,
                                         sessionId,
                                         request);
            } catch (RemoteException e) {
                // Dead recipient.
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionUpdated(int promiseId, byte[] sessionId) {
            if (DEBUG) Log.d(LOGTAG, "onSessionUpdated()");
            try {
                mRemote.onSessionUpdated(promiseId, sessionId);
            } catch (RemoteException e) {
                // Dead recipient.
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionClosed(int promiseId, byte[] sessionId) {
            if (DEBUG) Log.d(LOGTAG, "onSessionClosed()");
            try {
                mRemote.onSessionClosed(promiseId, sessionId);
            } catch (RemoteException e) {
                // Dead recipient.
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionMessage(byte[] sessionId,
                                     int sessionMessageType,
                                     byte[] request) {
            if (DEBUG) Log.d(LOGTAG, "onSessionMessage()");
            try {
                mRemote.onSessionMessage(sessionId, sessionMessageType, request);
            } catch (RemoteException e) {
                // Dead recipient.
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionError(byte[] sessionId, String message) {
            if (DEBUG) Log.d(LOGTAG, "onSessionError()");
            try {
                mRemote.onSessionError(sessionId, message);
            } catch (RemoteException e) {
                // Dead recipient.
                e.printStackTrace();
            }
        }

        @Override
        public void onSessionBatchedKeyChanged(byte[] sessionId,
                                               SessionKeyInfo[] keyInfos) {
            if (DEBUG) Log.d(LOGTAG, "onSessionBatchedKeyChanged()");
            try {
                mRemote.onSessionBatchedKeyChanged(sessionId, keyInfos);
            } catch (RemoteException e) {
                // Dead recipient.
                e.printStackTrace();
            }
        }

        @Override
        public void onRejectPromise(int promiseId, String message) {
            if (DEBUG) Log.d(LOGTAG, "onRejectPromise()");
            try {
                mRemote.onRejectPromise(promiseId, message);
            } catch (RemoteException e) {
                // Dead recipient.
                e.printStackTrace();
            }
        }
    }

    private static void assertTrue(boolean condition) {
        if (DEBUG && !condition) {
            throw new AssertionError("Expected condition to be true");
        }
    }

    RemoteMediaDrmBridgeStub(String keySystem, String stubUUID) throws RemoteException {
        if (AppConstants.Versions.preLollipop) {
            Log.e(LOGTAG, "Pre-Lollipop should never enter here!!");
            throw new RemoteException("Error, unsupported version!");
        }
        try {
            if (AppConstants.Versions.feature21Plus &&
                AppConstants.Versions.preMarshmallow) {
                mBridge = new GeckoMediaDrmBridgeV21(keySystem);
            } else {
                mBridge = new GeckoMediaDrmBridgeV23(keySystem);
            }
            mStubUUID = stubUUID;
            mBridgeStubs.add(this);
        } catch (Exception e) {
            throw new RemoteException("RemoteMediaDrmBridgeStub cannot create bridge implementation.");
        }
    }

    @Override
    public synchronized void setCallbacks(IMediaDrmBridgeCallbacks callbacks) throws RemoteException {
        if (DEBUG) Log.d(LOGTAG, "setCallbacks()");
        assertTrue(mBridge != null);
        assertTrue(callbacks != null);
        mCallbacks = callbacks;
        callbacks.asBinder().linkToDeath(this, 0);
        mBridge.setCallbacks(new Callbacks(mCallbacks));
    }

    @Override
    public synchronized void createSession(int createSessionToken,
                                           int promiseId,
                                           String initDataType,
                                           byte[] initData) throws RemoteException {
        if (DEBUG) Log.d(LOGTAG, "createSession()");
        try {
            assertTrue(mCallbacks != null);
            assertTrue(mBridge != null);
            mBridge.createSession(createSessionToken,
                                  promiseId,
                                  initDataType,
                                  initData);
        } catch (Exception e) {
            Log.e(LOGTAG, "Failed to createSession.", e);
            mCallbacks.onRejectPromise(promiseId, "Failed to createSession.");
        }
    }

    @Override
    public synchronized void updateSession(int promiseId,
                                           String sessionId,
                                           byte[] response) throws RemoteException {
        if (DEBUG) Log.d(LOGTAG, "updateSession()");
        try {
            assertTrue(mCallbacks != null);
            assertTrue(mBridge != null);
            mBridge.updateSession(promiseId, sessionId, response);
        } catch (Exception e) {
            Log.e(LOGTAG, "Failed to updateSession.", e);
            mCallbacks.onRejectPromise(promiseId, "Failed to updateSession.");
        }
    }

    @Override
    public synchronized void closeSession(int promiseId, String sessionId) throws RemoteException {
        if (DEBUG) Log.d(LOGTAG, "closeSession()");
        try {
            assertTrue(mCallbacks != null);
            assertTrue(mBridge != null);
            mBridge.closeSession(promiseId, sessionId);
        } catch (Exception e) {
            Log.e(LOGTAG, "Failed to closeSession.", e);
            mCallbacks.onRejectPromise(promiseId, "Failed to closeSession.");
        }
    }

    // IBinder.DeathRecipient
    @Override
    public synchronized void binderDied() {
        Log.e(LOGTAG, "Binder died !!");
        try {
            release();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    @Override
    public synchronized void release() {
        if (DEBUG) Log.d(LOGTAG, "release()");
        mBridgeStubs.remove(this);
        if (mBridge != null) {
            mBridge.release();
            mBridge = null;
        }
        mCallbacks.asBinder().unlinkToDeath(this, 0);
        mCallbacks = null;
        mStubUUID = "";
    }
}
