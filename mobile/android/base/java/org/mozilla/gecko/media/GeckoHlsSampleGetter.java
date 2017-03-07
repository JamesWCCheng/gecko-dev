/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


package org.mozilla.gecko.media;

import java.util.ArrayList;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.AppConstants;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.mozglue.JNIObject;

import android.content.Context;
import android.os.Build;
import android.util.Log;

public final class GeckoHlsSampleGetter {
    private static final String LOGTAG = "GeckoHlsSampleGetter";
    private static final boolean DEBUG = false;

    private static final int TRACK_UNDEFINED = 0;
    private static final int TRACK_AUDIO = 1;
    private static final int TRACK_VIDEO = 2;
    private static final int TRACK_TEXT = 3;

    @WrapForJNI
    private static final String AAC = "audio/mp4a-latm";
    @WrapForJNI
    private static final String AVC = "video/avc";

    private Queue<Sample> samples = new ConcurrentLinkedQueue<>();
    private GeckoHlsPlayer player = null;
    // A flag to avoid using the native object that has been destroyed.
    private boolean mDestroyed;

    @WrapForJNI(calledFrom = "gecko")
    public static GeckoHlsSampleGetter create() {
        GeckoHlsSampleGetter getter = new GeckoHlsSampleGetter();
        return getter;
    }

    @WrapForJNI
    public static int GetNumberOfTracks(int trackType) {
        if (DEBUG) Log.d(LOGTAG, "[GetNumberOfTracks]");
        return 0;
    }

    @WrapForJNI
    public HlsAudioInfo GetAudioInfo(int trackNumber) {
        if (DEBUG) Log.d(LOGTAG, "[HasTrackType]");
        HlsAudioInfo aInfo = new HlsAudioInfo();
        return aInfo;
    }

    @WrapForJNI
    public HlsVideoInfo GetVideoInfo(int trackNumber) {
        if (DEBUG) Log.d(LOGTAG, "[HasTrackType]");
        HlsVideoInfo vInfo = new HlsVideoInfo();
        return vInfo;
    }

    GeckoHlsSampleGetter() {
        if (DEBUG) Log.d(LOGTAG, "Constructing GeckoHlsSampleGetter");
        try {
            Context ctx = GeckoAppShell.getApplicationContext();
            player = new GeckoHlsPlayer(ctx);
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
