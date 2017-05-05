/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.util.Log;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;

public class GeckoHlsResourceWrapper {
    private static final String LOGTAG = "GeckoHlsResourceWrapper";
    private static final boolean DEBUG = true;
    private GeckoHlsPlayer player = null;
    private boolean destroyed = false;

    public static class HlsResourceCallbacks extends JNIObject
    implements GeckoHlsPlayer.ResourceCallbacks {
        @WrapForJNI(calledFrom = "gecko")
        HlsResourceCallbacks() {}

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onDataArrived();

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onResourceError(int errorCode);

        @Override // JNIObject
        protected void disposeNative() {
            throw new UnsupportedOperationException();
        }
    } // HlsResourceCallbacks

    private GeckoHlsResourceWrapper(String url,
                                    GeckoHlsPlayer.ResourceCallbacks callback) {
        if (DEBUG) Log.d(LOGTAG, "GeckoHlsResourceWrapper created with url = " + url);
        assertTrue(callback != null);

        player = new GeckoHlsPlayer();
        player.addResourceWrapperCallbackListener(callback);
        player.init(url);
    }

    @WrapForJNI(calledFrom = "gecko")
    public static GeckoHlsResourceWrapper create(String url,
                                                 GeckoHlsPlayer.ResourceCallbacks callback) {
        GeckoHlsResourceWrapper wrapper = new GeckoHlsResourceWrapper(url, callback);
        return wrapper;
    }

    @WrapForJNI(calledFrom = "gecko")
    public GeckoHlsPlayer GetPlayer() {
        // GeckoHlsResourceWrapper should always be created before others
        assertTrue(!destroyed);
        assertTrue(player != null);
        return player;
    }

    private static void assertTrue(boolean condition) {
        if (DEBUG && !condition) {
            throw new AssertionError("Expected condition to be true");
        }
    }

    @WrapForJNI // Called when natvie object is destroyed.
    private void destroy() {
        if (DEBUG) Log.d(LOGTAG, "destroy!! Native object is destroyed.");
        if (destroyed) {
            return;
        }
        destroyed = true;
        if (player != null) {
            player.release();
            player = null;
        }
    }
}
