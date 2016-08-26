/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

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

        void onSessionError(byte[] sessionId);

        void onSessionKeyChanged(byte[] sessionId,
                                 byte[] keyId,
                                 int statusCode);
    } // Callbacks

    @WrapForJNI
    public static class NativeCallbacksToMediaDrmProxySupport extends JNIObject implements Callbacks {
        void onSessionCreated(int createSessionToken,
                              int promiseId,
                              byte[] sessionId,
                              byte[] request) {

        }

        void onSessionUpdated(int promiseId, byte[] sessionId) {

        }

        void onSessionClosed(int promiseId, byte[] sessionId) {}

        void onSessionMessage(byte[] sessionId,
                              int sessionMessageType,
                              byte[] request) {

        }

        void onSessionError(byte[] sessionId) {

        }

        void onSessionKeyChanged(byte[] sessionId,
                                 byte[] keyId,
                                 int statusCode) {

        }

        @Override // JNIObject
        protected native void disposeNative();
    } // NativeCallbacksToMediaDrmProxySupport

    @WrapForJNI
    public static MediaDrmProxy create(String keySystem,
                                       Callbacks callbacks,
                                       boolean isRemote) {
        Log.d(LOGTAG, "Create MediaDrmProxy isRemote = " + isRemote);
        if (isRemote) {
            if (!RemoteManager.getRemoteManager().init()) {
                return null;
            }
            return RemoteManager.getRemoteManager().createMediaDrmBridge(keySystem, callbacks);
        } else {
            return createMediaDrmProxy(keySystem, callbacks, false);
        }
    }

    // Called by remote manager.
    public static MediaDrmProxy createMediaDrmProxy(String keySystem,
                                                    Callbacks callbacks,
                                                    boolean isRemote) {
        return MediaDrmProxy(keySystem, callbacks, isRemote);
    }

    MediaDrmProxy(String keySystem, Callbacks callbacks, boolean isRemote) {
        Log.d(LOGTAG, "MediaDrmProxy");
        if (isRemote) {
            mImpl = RemoteMediaDrmBridge(keySystem, callbacks);
        } else {
            // [TODO] Wait for Kilik 4-2 finished.
            // mImpl = MediaDrmBridge(keySystem, callbacks);
        }
    }

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
    static public boolean isSchemeSupported(String keySystem) {
        if (!passMiniumSupportVersionCheck()) {
            return false;
        }
        if (keySystem.equals(WIDEVINE_KEY_SYSTEM)) {
            return MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID)
                    && MediaCrypto.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID);
        }
        return false;
    }

    @WrapForJNI
    static public boolean isSchemeMIMESupported(String keySystem, String mimeType) {
        if (!passMiniumSupportVersionCheck()) {
            return false;
        }
        Log.d(LOGTAG, "isSchemeMIMESupported " + mimeType);
        if (keySystem.equals(WIDEVINE_KEY_SYSTEM)) {
            return MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID, mimeType);
        }
        return false;
    }

    @WrapForJNI
    private boolean createSession(int createSessionToken,
                                  int promiseId,
                                  String initDataType,
                                  byte[] initData) {
        Log.d(LOGTAG, "createSession");
        return mImpl..createSession(createSessionToken,
                                    promiseId,
                                    initDataType,
                                    initData);
    }

    @WrapForJNI
    private boolean updateSession(int promiseId, String sessionId, byte[] response) {
        Log.d(LOGTAG, "updateSession, sessionId = " + sessionId);
        return mImpl.createSession(promiseId, sessionId, response);
    }

    @WrapForJNI
    private void closeSession(int promiseId, String sessionId) {
        Log.d(LOGTAG, "closeSession");
        mImpl.closeSession(promiseId, sessionId);
    }

    @WrapForJNI
    private native void onSessionCreated(int aCreateSessionToken, int aPromiseId, byte[] aSessionId,
            byte[] aRequest);

    @WrapForJNI
    private native void onSessionUpdated(int aPromiseId, byte[] aSessionId);

    @WrapForJNI
    private native void onSessionClosed(int aPromiseId, byte[] aSessionId);

    @WrapForJNI
    private native void onSessionMessage(byte[] aSessionId, int aSessionMessageType,
            byte[] aRequest);

    @WrapForJNI
    private native void onSessionError(byte[] aSessionId);

    // Key status change callback.
    // MediaDrm.KeyStatus is available in API level 23(M)
    // https://developer.android.com/reference/android/media/MediaDrm.KeyStatus.html
    // Just unwrap the structure and pass the keyid and status code to native for
    // compatibility between L and M.
    // Reference:
    // https://cs.chromium.org/chromium/src/media/base/android/java/src/org/chromium/media/MediaDrmBridge.java?q=mediadrmbridge.java&sq=package:chromium&dr&l=96
    @WrapForJNI
    private native void onSessionKeyChanged(byte[] aSessionId, byte[] aKeyId, int aStatusCode);

    @WrapForJNI // Called when natvie object is destroyed.
    public void destroy() {
        Log.d(LOGTAG, "destroy!! Natvie object is destroyed.");
        if (mDestroyed) {
            return;
        }
        mDestroyed = true;
        release();
    }

    // [TODO] : release the resources
    private void release() {}
}
