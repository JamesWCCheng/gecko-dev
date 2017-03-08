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

import com.google.android.exoplayer2.Format;

import android.content.Context;
import android.os.Build;
import android.util.Log;

public final class GeckoHlsDemuxerWrapper {
    private static final String LOGTAG = "GeckoHlsDemuxerWrapper";
    private static final boolean DEBUG = false;

    // NOTE : These TRACK definition should be synced with Gecko.
    private static final int TRACK_UNDEFINED = 0;
    private static final int TRACK_AUDIO = 1;
    private static final int TRACK_VIDEO = 2;
    private static final int TRACK_TEXT = 3;

    private GeckoHlsPlayer.Track_Type getPlayerTrackType(int trackType) {
        if (trackType == 1) {
            return GeckoHlsPlayer.Track_Type.TRACK_AUDIO;
        } else if (trackType == 2) {
            return GeckoHlsPlayer.Track_Type.TRACK_VIDEO;
        } else if (trackType == 3) {
            return GeckoHlsPlayer.Track_Type.TRACK_TEXT;
        }
        return GeckoHlsPlayer.Track_Type.TRACK_UNDEFINED;
    }

    @WrapForJNI
    private static final String AAC = "audio/mp4a-latm";
    @WrapForJNI
    private static final String AVC = "video/avc";

    private Queue<Sample> samples = new ConcurrentLinkedQueue<>();
    private GeckoHlsPlayer player = null;
    // A flag to avoid using the native object that has been destroyed.
    private boolean mDestroyed;

    @WrapForJNI(calledFrom = "gecko")
    public static GeckoHlsDemuxerWrapper create() {
        GeckoHlsDemuxerWrapper wrapper = new GeckoHlsDemuxerWrapper();
        return wrapper;
    }

    @WrapForJNI
    public int GetNumberOfTracks(int trackType) {
        if (DEBUG) Log.d(LOGTAG, "[GetNumberOfTracks]");
        if (player != null) {
            return player.getNumberTracks(getPlayerTrackType(trackType));
        }
        return 0;
    }

    @WrapForJNI
    public HlsAudioInfo GetAudioInfo(int trackNumber) {
        assert player != null;
        if (DEBUG) Log.d(LOGTAG, "[GetAudioInfo]");
        Format fmt = player.getAudioTrackFormat();
        HlsAudioInfo aInfo = new HlsAudioInfo();
        if (player != null) {
            aInfo.rate = fmt.sampleRate;
            aInfo.channels = fmt.channelCount;
        }
        return aInfo;
    }

    @WrapForJNI
    public HlsVideoInfo GetVideoInfo(int trackNumber) {
        assert player != null;
        if (DEBUG) Log.d(LOGTAG, "[GetVideoInfo]");
        Format fmt = player.getVideoTrackFormat();
        HlsVideoInfo vInfo = new HlsVideoInfo();
        if (fmt != null) {
            vInfo.displayX = fmt.width;
            vInfo.displayY = fmt.height;
            vInfo.pictureX = fmt.width;
            vInfo.pictureY = fmt.height;
            vInfo.stereoMode = fmt.stereoMode;
            vInfo.rotation = fmt.rotationDegrees;
        }
        return vInfo;
    }

    GeckoHlsDemuxerWrapper() {
        if (DEBUG) Log.d(LOGTAG, "Constructing GeckoHlsDemuxerWrapper");
        try {
            Context ctx = GeckoAppShell.getApplicationContext();
            player = new GeckoHlsPlayer(ctx);
        } catch (Exception e) {
            Log.e(LOGTAG, "Constructing GeckoHlsDemuxerWrapper ... error", e);
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
        if (player != null) {
            player.release();
            player = null;
        }
    }
}
