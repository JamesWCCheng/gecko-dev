/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.annotation.WrapForJNI;

import java.util.ArrayList;
import java.util.UUID;

import android.media.MediaDrm;
import android.media.MediaCrypto;
import android.util.Log;

public final class MediaDrmProxy extends JNIObject {
    private static final String LOGTAG = "GeckoMediaDrmProxy";
    private static final boolean DEBUG = true;
    private static final UUID WIDEVINE_SCHEME_UUID =
        new UUID(0xedef8ba979d64aceL, 0xa3c827dcd51d21edL);

    private static final String WIDEVINE_KEY_SYSTEM = "com.widevine.alpha";
    // A flag to avoid using the native object that has been destroyed.
    private boolean mDestroyed;

    private GeckoMediaDrm mImpl;
    private String mDrmStubUUID;

    public static ArrayList<MediaDrmProxy> mProxyList = new ArrayList<MediaDrmProxy>();

    @WrapForJNI
    static public boolean isSchemeSupported(String keySystem) {
        if (keySystem.equals(WIDEVINE_KEY_SYSTEM)) {
            return MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID)
                    && MediaCrypto.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID);
        }
        Log.d("MediaDrmProxy", "isSchemeSupported key sytem = " + keySystem);
        return false;
    }

    @WrapForJNI
    static public boolean isSchemeMIMESupported(String keySystem, String mimeType) {
        if (keySystem.equals(WIDEVINE_KEY_SYSTEM)) {
            return MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID, mimeType);
        }
        Log.d("MediaDrmProxy", "isSchemeSupported key sytem = " + keySystem + ", mimeType = " + mimeType);
        return false;
    }

    // Interface for callback to native.
    public interface Callbacks {
        void onSessionCreated(int createSessionToken,
                              int promiseId,
                              byte[] sessionId,
                              byte[] request);

        void onSessionUpdated(int promiseId, byte[] sessionId);

        void onSessionClosed(int promiseId, byte[] sessionId);

        void onSessionMessage(byte[] sessionId,
                              int sessionMessageType,
                              byte[] request);

        void onSessionError(int promiseId, byte[] sessionId, String message);

        // MediaDrm.KeyStatus is available in API level 23(M)
        // https://developer.android.com/reference/android/media/MediaDrm.KeyStatus.html
        // For compatibility between L and M above, we'll unwrap the structure
        // and pass the keyid and status code to native(MediaDrmCDMProxy).
        void onSessionBatchedKeyChanged(byte[] sessionId,
                                        SessionKeyInfo[] keyInfos);

        void onRejectPromise(int promiseId, String message);
    } // Callbacks

    @WrapForJNI
    public static class NativeMediaDrmProxyCallbacks extends JNIObject implements Callbacks {
        @Override
        public native void onSessionCreated(int createSessionToken,
                                            int promiseId,
                                            byte[] sessionId,
                                            byte[] request);

        @Override
        public native void onSessionUpdated(int promiseId, byte[] sessionId);

        @Override
        public native void onSessionClosed(int promiseId, byte[] sessionId);

        @Override
        public native void onSessionMessage(byte[] sessionId,
                                            int sessionMessageType,
                                            byte[] request);

        @Override
        public native void onSessionError(int promiseId,
                                          byte[] sessionId,
                                          String message);

        @Override
        public native void onSessionBatchedKeyChanged(byte[] sessionId,
                                                      SessionKeyInfo[] keyInfos);

        @Override
        public native void onRejectPromise(int promiseId, String message);

        @Override // JNIObject
        protected native void disposeNative();
    } // NativeMediaDrmProxyCallbacks

    // A proxy to callback from LocalMediaDrmBridge to native instance.
    public static class MediaDrmProxyCallbacks implements GeckoMediaDrm.Callbacks {
        private final Callbacks mNativeCallbacks;
        private final MediaDrmProxy mProxy;

        public MediaDrmProxyCallbacks(MediaDrmProxy proxy, Callbacks callbacks) {
            mNativeCallbacks = callbacks;
            mProxy = proxy;
        }

        @Override
        public void onSessionCreated(int createSessionToken,
                                     int promiseId,
                                     byte[] sessionId,
                                     byte[] request) {
            if (!mProxy.isDestroyed()) {
                mNativeCallbacks.onSessionCreated(createSessionToken,
                                            promiseId,
                                            sessionId,
                                            request);
            }
        }

        @Override
        public void onSessionUpdated(int promiseId, byte[] sessionId) {
            if (!mProxy.isDestroyed()) {
                mNativeCallbacks.onSessionUpdated(promiseId, sessionId);
            }
        }

        @Override
        public void onSessionClosed(int promiseId, byte[] sessionId) {
            if (!mProxy.isDestroyed()) {
                mNativeCallbacks.onSessionClosed(promiseId, sessionId);
            }
        }

        @Override
        public void onSessionMessage(byte[] sessionId,
                                     int sessionMessageType,
                                     byte[] request) {
            if (!mProxy.isDestroyed()) {
                mNativeCallbacks.onSessionMessage(sessionId, sessionMessageType, request);
            }
        }

        @Override
        public void onSessionError(byte[] sessionId, String message) {
            if (!mProxy.isDestroyed()) {
                mNativeCallbacks.onSessionError(0, sessionId, message);
            }
        }

        @Override
        public void onSessionBatchedKeyChanged(byte[] sessionId,
                                               SessionKeyInfo[] keyInfos) {
            if (!mProxy.isDestroyed()) {
                mNativeCallbacks.onSessionBatchedKeyChanged(sessionId, keyInfos);
            }
        }

        @Override
        public void onRejectPromise(int promiseId, String message) {
            if (!mProxy.isDestroyed()) {
                mNativeCallbacks.onRejectPromise(promiseId, message);
            }
        }
    } // MediaDrmProxyCallbacks

    public boolean isDestroyed() {
        return mDestroyed;
    }

    // Get corresponding MediaCrypto object by a generated UUID for MediaCodec.
    @WrapForJNI
    public static MediaCrypto getMediaCrypto(String uuid) {
        for (int i = 0; i < mProxyList.size(); i++) {
            if (mProxyList.get(i) != null &&
                mProxyList.get(i).getUUID().equals(uuid)) {
                return mProxyList.get(i).getMediaCryptoFromBridge();
            }
        }
        Log.d(LOGTAG, " NULL crytpo ........................  ");
        return null;
    }

    @WrapForJNI
    public static MediaDrmProxy create(String keySystem,
                                       Callbacks nativeCallbacks) {
        String drmStubId = UUID.randomUUID().toString();
        MediaDrmProxy proxy = new MediaDrmProxy(keySystem, nativeCallbacks, drmStubId);
        return proxy;
    }

    MediaDrmProxy(String keySystem, Callbacks nativeCallbacks, String uuid) {
        log("Constructing MediaDrmProxy ... ");
        try {
            mImpl = new LocalMediaDrmBridge(keySystem);
            mImpl.setCallbacks(new MediaDrmProxyCallbacks(this, nativeCallbacks));
            mDrmStubUUID = uuid;
            mProxyList.add(this);
        } catch (Exception e) {
            log("Constructing MediaDrmProxy ... error");
        }
    }

    @Override
    protected native void disposeNative();

    @WrapForJNI
    private void createSession(int createSessionToken,
                               int promiseId,
                               String initDataType,
                               byte[] initData) {
        log("createSession");
        mImpl.createSession(createSessionToken,
                            promiseId,
                            initDataType,
                            initData);
    }

    @WrapForJNI
    private void updateSession(int promiseId, String sessionId, byte[] response) {
        log("updateSession, sessionId(" + sessionId + ")");
        mImpl.updateSession(promiseId, sessionId, response);
    }

    @WrapForJNI
    private void closeSession(int promiseId, String sessionId) {
        log("closeSession, sessionId(" + sessionId + ")");
        mImpl.closeSession(promiseId, sessionId);
    }

    @WrapForJNI // Called when natvie object is destroyed.
    private void destroy() {
        log("destroy!! Natvie object is destroyed.");
        if (mDestroyed) {
            return;
        }
        mDestroyed = true;
        release();
    }

    @WrapForJNI
    private String getUUID() {
        return mDrmStubUUID;
    }

    private MediaCrypto getMediaCryptoFromBridge() {
        return mImpl != null ? mImpl.getMediaCrypto() : null;
    }

    private void release() {
        log("release()");
        mProxyList.remove(this);
        mImpl.release();
    }

    private void log(String msg) {
        if (DEBUG) {
          Log.d(LOGTAG, msg);
        }
    }
}
