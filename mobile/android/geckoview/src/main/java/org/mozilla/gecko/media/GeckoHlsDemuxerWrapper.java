/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


package org.mozilla.gecko.media;

import android.util.Log;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.util.MimeTypes;
import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;

public final class GeckoHlsDemuxerWrapper {
    private static final String LOGTAG = "GeckoHlsDemuxerWrapper";
    private static final boolean DEBUG = true;

    // NOTE : These TRACK definition should be synced with Gecko.
    private static final int TRACK_UNDEFINED = 0;
    private static final int TRACK_AUDIO = 1;
    private static final int TRACK_VIDEO = 2;
    private static final int TRACK_TEXT = 3;

    private GeckoHlsPlayer player = null;
    // A flag to avoid using the native object that has been destroyed.
    private boolean destroyed;

    public static class HlsDemuxerCallbacks extends JNIObject
        implements GeckoHlsPlayer.DemuxerCallbacks {

        @WrapForJNI(calledFrom = "gecko")
        HlsDemuxerCallbacks() {}

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onInitialized(boolean hasAudio, boolean hasVideo);;

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onError(int errorCode);

        @Override // JNIObject
        protected void disposeNative() {
            throw new UnsupportedOperationException();
        }
    } // HlsDemuxerCallbacks

    private static void assertTrue(boolean condition) {
        if (DEBUG && !condition) {
            throw new AssertionError("Expected condition to be true");
        }
    }

    private GeckoHlsPlayer.TrackType getPlayerTrackType(int trackType) {
        if (trackType == 1) {
            return GeckoHlsPlayer.TrackType.AUDIO;
        } else if (trackType == 2) {
            return GeckoHlsPlayer.TrackType.VIDEO;
        } else if (trackType == 3) {
            return GeckoHlsPlayer.TrackType.TEXT;
        }
        return GeckoHlsPlayer.TrackType.UNDEFINED;
    }

    @WrapForJNI
    public long getBuffered() {
        assertTrue(player != null);
        return player.getBufferedPosition();
    }

    @WrapForJNI(calledFrom = "gecko")
    public static GeckoHlsDemuxerWrapper create(GeckoHlsPlayer player,
                                                GeckoHlsPlayer.DemuxerCallbacks callback) {
        GeckoHlsDemuxerWrapper wrapper = new GeckoHlsDemuxerWrapper(player, callback);
        return wrapper;
    }

    @WrapForJNI
    public int getNumberOfTracks(int trackType) {
        int tracks = player != null ? player.getNumberOfTracks(getPlayerTrackType(trackType)) : 0;
        if (DEBUG) Log.d(LOGTAG, "[GetNumberOfTracks] type : " + trackType + ", num = " + tracks);
        return tracks;
    }
    @WrapForJNI
    public boolean isLiveStream() {
        return player.isLiveStream();
    }
    @WrapForJNI
    public GeckoAudioInfo getAudioInfo(int index) {
        assertTrue(player != null);

        if (DEBUG) Log.d(LOGTAG, "[getAudioInfo] formatIndex : " + index);
        Format fmt = player.getAudioTrackFormat(index);
        GeckoAudioInfo aInfo = null;
        if (fmt != null) {
            aInfo = new GeckoAudioInfo(fmt.sampleRate, fmt.channelCount, 16, 0,
                    player.getDuration(), fmt.sampleMimeType,
                    fmt.initializationData.isEmpty() ? null : fmt.initializationData.get(0));
            /* According to https://github.com/google/ExoPlayer/blob
             * /d979469659861f7fe1d39d153b90bdff1ab479cc/library/core/src/main
             * /java/com/google/android/exoplayer2/audio/MediaCodecAudioRenderer.java#L221-L224,
             * if the input audio format is not raw, exoplayer would assure that
             * the sample's pcm encoding bitdepth is 16.
             * For HLS content, it should always be 16.
             */
        }
        return aInfo;
    }

    @WrapForJNI
    public GeckoVideoInfo getVideoInfo(int index) {
        assertTrue(player != null);

        if (DEBUG) Log.d(LOGTAG, "[getVideoInfo] formatIndex : " + index);
        Format fmt = player.getVideoTrackFormat(index);
        GeckoVideoInfo vInfo = null;
        if (fmt != null) {
            vInfo = new GeckoVideoInfo(fmt.width, fmt.height, fmt.width, fmt.height, fmt.stereoMode, fmt.rotationDegrees, player.getDuration(), fmt.sampleMimeType);
        }
        return vInfo;
    }

    @WrapForJNI
    public boolean seek(long seekTime) {
        // seekTime : milliseconds.
        assertTrue(player != null);
        if (DEBUG) Log.d(LOGTAG, "seek  : " + seekTime + " (Ms)");
        return player.seek(seekTime);
    }

    GeckoHlsDemuxerWrapper(GeckoHlsPlayer player,
                           GeckoHlsPlayer.DemuxerCallbacks callback) {
        if (DEBUG) Log.d(LOGTAG, "Constructing GeckoHlsDemuxerWrapper ...");
        assertTrue(callback != null);
        assertTrue(player != null);
        try {
            this.player = player;
            this.player.addDemuxerWrapperCallbackListener(callback);
        } catch (Exception e) {
            Log.e(LOGTAG, "Constructing GeckoHlsDemuxerWrapper ... error", e);
            callback.onError(GeckoHlsPlayer.DemuxerError.UNKNOWN.code());
        }
    }

    @WrapForJNI
    private GeckoHlsSample[] getSamples(int mediaType, int number) {
        ConcurrentLinkedQueue<GeckoHlsSample> samples = null;
        // getA/VSamples will always return a non-null instance.
        if (mediaType == TRACK_VIDEO) {
            samples = player.getVideoSamples(number);
        } else if (mediaType == TRACK_AUDIO) {
            samples = player.getAudioSamples(number);
        }

        assertTrue(samples.size() <= number);
        return samples.toArray(new GeckoHlsSample[samples.size()]);
    }

    @WrapForJNI
    private long getNextKeyFrameTime() {
        assertTrue(player != null);
        return player.getNextKeyFrameTime();
    }

    @WrapForJNI // Called when natvie object is destroyed.
    private void destroy() {
        if (DEBUG) Log.d(LOGTAG, "destroy!! Native object is destroyed.");
        if (destroyed) {
            return;
        }
        destroyed = true;
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
