/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


package org.mozilla.gecko.media;

import java.nio.ByteBuffer;

import java.util.concurrent.ConcurrentLinkedQueue;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.AppConstants;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.mozglue.JNIObject;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.Format;

import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;
import android.os.Build;
import android.util.Log;

import static org.mozilla.gecko.R.id.url;

public final class GeckoHlsDemuxerWrapper {
    private static final String LOGTAG = "GeckoHlsDemuxerWrapper";
    private static final boolean DEBUG = true;

    // NOTE : These TRACK definition should be synced with Gecko.
    private static final int TRACK_UNDEFINED = 0;
    private static final int TRACK_AUDIO = 1;
    private static final int TRACK_VIDEO = 2;
    private static final int TRACK_TEXT = 3;

    @WrapForJNI
    private static final String AAC = "audio/mp4a-latm";
    @WrapForJNI
    private static final String AVC = "video/avc";

    private GeckoHlsPlayer player = null;
    // A flag to avoid using the native object that has been destroyed.
    private boolean mDestroyed;

    public interface Callbacks {
        void onAudioFormatChanged();
        void onVideoFormatChanged();
        void onTrackInfoChanged(boolean hasAudio, boolean hasVideo);
        void onDataArrived();
    }

    public static class HlsDemuxerCallbacks extends JNIObject implements Callbacks {
        @WrapForJNI(calledFrom = "gecko")
        HlsDemuxerCallbacks() {
        }

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onAudioFormatChanged();

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onVideoFormatChanged();

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onTrackInfoChanged(boolean hasAudio, boolean hasVideo);

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onDataArrived();

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
    public long getBuffered() {
        assertTrue(player != null);
        return player.getBufferedPosition();
    }

    @WrapForJNI(calledFrom = "gecko")
    public static GeckoHlsDemuxerWrapper create(GeckoHlsResourceWrapper resWrapper, Callbacks callback) {
        GeckoHlsDemuxerWrapper wrapper = new GeckoHlsDemuxerWrapper(resWrapper, callback);
        return wrapper;
    }

    @WrapForJNI
    public int getNumberOfTracks(int trackType) {
        int tracks = player != null ? player.getNumberTracks(getPlayerTrackType(trackType)) : 0;
        if (DEBUG) Log.d(LOGTAG, "[GetNumberOfTracks] type : " + trackType + ", num = " + tracks);
        return tracks;
    }

    @WrapForJNI
    public HlsAudioInfo getAudioInfo(int trackNumber) {
        assertTrue(player != null);

        if (DEBUG) Log.d(LOGTAG, "[getAudioInfo]");
        Format fmt = player.getAudioTrackFormat();
        long aDuration = player.getDuration();
        HlsAudioInfo aInfo = new HlsAudioInfo();
        if (player != null) {
            aInfo.rate = fmt.sampleRate;
            aInfo.channels = fmt.channelCount;
            // TODO: due to http://searchfox.org/mozilla-central/rev/ca7015fa45b30b29176fbaa70ba0a36fe9263c38/dom/media/platforms/android/AndroidDecoderModule.cpp#197
            // I have no idea how to get this value, hard code 16 even pcmEncoding is not indicating 16.
            aInfo.bitDepth = fmt.pcmEncoding == C.ENCODING_PCM_16BIT? 16 : 16;
            aInfo.mimeType = fmt.sampleMimeType;
            aInfo.duration = aDuration;
            // TODO: currently I extract csd0 only
            // Reference: http://searchfox.org/mozilla-central/rev/7419b368156a6efa24777b21b0e5706be89a9c2f/dom/media/platforms/android/RemoteDataDecoder.cpp#324-329
            if (!fmt.initializationData.isEmpty()) {
                aInfo.codecSpecificData = fmt.initializationData.get(0);
            }
        }
        return aInfo;
    }

    @WrapForJNI
    public HlsVideoInfo getVideoInfo(int trackNumber) {
        assertTrue(player != null);

        if (DEBUG) Log.d(LOGTAG, "[getVideoInfo]");
        Format fmt = player.getVideoTrackFormat();
        long vDuration = player.getDuration();
        HlsVideoInfo vInfo = new HlsVideoInfo();
        if (fmt != null) {
            vInfo.displayX = fmt.width;
            vInfo.displayY = fmt.height;
            vInfo.pictureX = fmt.width;
            vInfo.pictureY = fmt.height;
            vInfo.stereoMode = fmt.stereoMode;
            vInfo.rotation = fmt.rotationDegrees;
            vInfo.mimeType = fmt.sampleMimeType;
            vInfo.duration = vDuration;
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

    GeckoHlsDemuxerWrapper(GeckoHlsResourceWrapper resWrapper, Callbacks callback) {
        if (DEBUG) Log.d(LOGTAG, "Constructing GeckoHlsDemuxerWrapper : " + url + ", callback : " + callback);
        try {
            player = resWrapper.GetPlayer();
            player.addDemuxerWrapperCallbackListener(callback);
        } catch (Exception e) {
            Log.e(LOGTAG, "Constructing GeckoHlsDemuxerWrapper ... error", e);
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
