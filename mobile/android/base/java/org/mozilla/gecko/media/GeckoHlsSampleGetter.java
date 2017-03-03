/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


package org.mozilla.gecko.media;

import java.util.ArrayList;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.AppConstants;

import android.util.Log;
import android.os.Build;

public final class GeckoHlsSampleGetter {
    private static final String LOGTAG = "GeckoHlsSampleGetter";
    private static final boolean DEBUG = false;

    private static final int TRACK_VIDEO = 0;
    private static final int TRACK_AUDIO = 1;

    @WrapForJNI
    private static final String AAC = "audio/mp4a-latm";
    @WrapForJNI
    private static final String AVC = "video/avc";

    private Queue<Sample> samples = new ConcurrentLinkedQueue<>();

    // A flag to avoid using the native object that has been destroyed.
    private boolean mDestroyed;

    @WrapForJNI
    public static boolean isSystemSupported() {
        // Support versions >= Marshmallow
        if (AppConstants.Versions.preMarshmallow) {
            if (DEBUG) Log.d(LOGTAG, "System Not supported !!, current SDK version is " + Build.VERSION.SDK_INT);
            return false;
        }
        return true;
    }

    GeckoHlsSampleGetter() {
        if (DEBUG) Log.d(LOGTAG, "Constructing GeckoHlsSampleGetter");
        try {

        } catch (Exception e) {
            Log.e(LOGTAG, "Constructing GeckoHlsSampleGetter ... error", e);
        }
    }

    @WrapForJNI
    private Sample getSample(int mediaType) {
        if (DEBUG) Log.d(LOGTAG, "getSample, mediaType = " + mediaType);
        if (mediaType == TRACK_VIDEO) {
            return Sample.create();
        } else {
        }
        return null;
    }

    @WrapForJNI // Called when natvie object is destroyed.
    private void destroy() {
        if (DEBUG) Log.d(LOGTAG, "destroy!! Native object is destroyed.");
        if (mDestroyed) {
            return;
        }
        mDestroyed = true;
        release();
    }

    private void release() {
        if (DEBUG) Log.d(LOGTAG, "release");
    }
}
