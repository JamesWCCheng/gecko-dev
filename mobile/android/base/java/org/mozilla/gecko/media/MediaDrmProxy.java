/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


package org.mozilla.gecko.media;

import java.io.IOException;
import java.lang.InterruptedException;
import java.util.HashMap;
import java.util.UUID;

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
import android.media.MediaCrypto;
import android.media.MediaCryptoException;
import android.media.MediaDrm;
import android.media.MediaDrmException;
import android.util.Log;

public final class MediaDrmProxy extends JNIObject {

    private static final String LOGTAG = "\033[1;41mMediaDrmProxy\033[m";
    private static final boolean DEBUG = true;
    private static final UUID WIDEVINE_SCHEME_UUID =
            new UUID(0xedef8ba979d64aceL, 0xa3c827dcd51d21edL);

    private static final String WIDEVINE_KEY_SYSTEM = "com.widevine.alpha";


    // This flag is to protect using the native objected that has been destroyed.
    private boolean mDestroyed;

    private GeckoMediaDrm mImpl;

    @Override
    protected native void disposeNative();

    // Callback to native part
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

        void onSessionError(int promiseId, byte[] sessionId);

        // Key status change callback.
        // MediaDrm.KeyStatus is available in API level 23(M)
        // https://developer.android.com/reference/android/media/MediaDrm.KeyStatus.html
        // Just unwrap the structure and pass the keyid and status code to native for
        // compatibility between L and M.
        // Reference:
        // https://cs.chromium.org/chromium/src/media/base/android/java/src/org/chromium/media/MediaDrmBridge.java?q=mediadrmbridge.java&sq=package:chromium&dr&l=96
        void onSessionKeyChanged(byte[] sessionId,
                                 byte[] keyId,
                                 int statusCode);
        void onError(String message);
    } // Callbacks

    @WrapForJNI
    public static class NativeCallbacksToMediaDrmProxySupport extends JNIObject implements Callbacks {
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
        public native void onSessionError(int promiseId, byte[] sessionId);
        @Override
        public native void onSessionKeyChanged(byte[] sessionId,
                                               byte[] keyId,
                                               int statusCode);
        @Override
        public native void onError(String message);

        @Override // JNIObject
        protected native void disposeNative();
    } // NativeCallbacksToMediaDrmProxySupport

    // Callback class to call back to Native implementation Cpp class.
    public static class GeckoMediaDrmCallbacks implements GeckoMediaDrm.Callbacks {
        // native callback handle;
        private final Callbacks mCallbacks;
        private final MediaDrmProxy mProxy;
        public GeckoMediaDrmCallbacks(MediaDrmProxy proxy, Callbacks callbacks) {
            mCallbacks = callbacks;
            mProxy = proxy;
        }
        @Override
        public void onSessionCreated(int createSessionToken,
                                     int promiseId,
                                     byte[] sessionId,
                                     byte[] request) {
            if (!mProxy.Destroyed()) {
                mCallbacks.onSessionCreated(createSessionToken,
                                            promiseId,
                                            sessionId,
                                            request);
            }
        }

        @Override
        public void onSessionUpdated(int promiseId, byte[] sessionId) {
            if (!mProxy.Destroyed()) {
                mCallbacks.onSessionUpdated(promiseId, sessionId);
            }
        }

        @Override
        public void onSessionClosed(int promiseId, byte[] sessionId) {
            if (!mProxy.Destroyed()) {
                mCallbacks.onSessionClosed(promiseId, sessionId);
            }
        }

        @Override
        public void onSessionMessage(byte[] sessionId,
                                     int sessionMessageType,
                                     byte[] request) {
            if (!mProxy.Destroyed()) {
                mCallbacks.onSessionMessage(sessionId, sessionMessageType, request);
            }
        }

        @Override
        public void onSessionError(int promiseId, byte[] sessionId) {
            if (!mProxy.Destroyed()) {
                mCallbacks.onSessionError(promiseId, sessionId);
            }
        }

        @Override
        public void onSessionKeyChanged(byte[] sessionId,
                                        byte[] keyId,
                                        int statusCode) {
            if (!mProxy.Destroyed()) {
                mCallbacks.onSessionKeyChanged(sessionId, keyId, statusCode);
            }
        }

        @Override
        public void onError(String message) {
            if (!mProxy.Destroyed()) {
                mCallbacks.onError(message);
            }
        }
    } // GeckoMediaDrmCallbacks

    // Called by natvie MediaDrmProxySupport.
    @WrapForJNI
    public static MediaDrmProxy create(String keySystem,
                                       Callbacks callbacks,
                                       boolean isRemote) {
        Log.d("MediaDrmProxy", "Create MediaDrmProxy isRemote = " + isRemote);

        MediaDrmProxy proxy = null;
        if (isRemote) {
            // [TODO] : Pass the following uuid to corresponding AsyncCodec
            String uuid = UUID.randomUUID().toString();
            proxy = RemoteManager.getInstance().createMediaDrmBridge(keySystem, uuid);
            if (proxy == null) {
                return null;
            }
        } else {
            proxy = createMediaDrmProxy(keySystem, null);
        }

        proxy.setCallbacks(new GeckoMediaDrmCallbacks(proxy, callbacks));
        return proxy;
    }

    // Called by remote manager.
    public static MediaDrmProxy createMediaDrmProxy(String keySystem,
                                                    IMediaDrmBridge remoteBridge) {
        return new MediaDrmProxy(keySystem, remoteBridge);
    }

    MediaDrmProxy(String keySystem, IMediaDrmBridge remoteBridge) {
        if (remoteBridge != null) {
            log("MediaDrmProxy Create RemoteMediaDrmBridge");
            mImpl = new RemoteMediaDrmBridge(remoteBridge);
        } else {
            mImpl = new LocalMediaDrmBridge(keySystem);
        }
    }
    private void setCallbacks(GeckoMediaDrmCallbacks callbacks) {
        mImpl.setCallbacks(callbacks);
    }

    public Object getLock() {
        return this;
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
    static public boolean isSchemeSupported(String keySystem) {
        if (!passMiniumSupportVersionCheck()) {
            return false;
        }
        if (keySystem.equals(WIDEVINE_KEY_SYSTEM)) {
            return MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID)
                    && MediaCrypto.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID);
        }
        Log.d("MediaDrmProxy", "isSchemeSupported key sytem = " + keySystem);
        return false;
    }

    @WrapForJNI
    static public boolean isSchemeMIMESupported(String keySystem, String mimeType) {
        if (!passMiniumSupportVersionCheck()) {
            return false;
        }
        if (keySystem.equals(WIDEVINE_KEY_SYSTEM)) {
            return MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID, mimeType);
        }
        Log.d("MediaDrmProxy", "isSchemeSupported key sytem = " + keySystem + ", mimeType = " + mimeType);
        return false;
    }

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
        log("updateSession, sessionId = " + sessionId);
        mImpl.updateSession(promiseId, sessionId, response);
    }

    @WrapForJNI
    private void closeSession(int promiseId, String sessionId) {
        log("closeSession");
        mImpl.closeSession(promiseId, sessionId);
    }

    @WrapForJNI // Called when natvie object is destroyed.
    public void destroy() {
        log("destroy!! Natvie object is destroyed.");
        if (mDestroyed) {
            return;
        }
        mDestroyed = true;
        release();
    }

    private void release() {
        log("release");
        mImpl.release();
    }

    public boolean Destroyed() {
        return mDestroyed;
    }

    private void log(String msg) {
        if (DEBUG) {
          Log.d(LOGTAG, "[" + this + "][" + msg + "]");
        }
    }
}
