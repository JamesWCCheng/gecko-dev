/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


package org.mozilla.gecko.media;

import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.AppConstants;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.mozglue.JNIObject;

import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.Format;

import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;
import android.os.Build;
import android.util.Log;

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
    }

    public static class HlsDemuxerCallbacks extends JNIObject implements Callbacks {
        @WrapForJNI(calledFrom = "gecko")
        HlsDemuxerCallbacks() {}

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onAudioFormatChanged();

        @Override
        @WrapForJNI(dispatchTo = "gecko")
        public native void onVideoFormatChanged();

        @Override // JNIObject
        protected void disposeNative() {
            throw new UnsupportedOperationException();
        }
    } // HlsDemuxerCallbacks

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

    @WrapForJNI(calledFrom = "gecko")
    public static GeckoHlsDemuxerWrapper create(String url, Callbacks callback) {
        GeckoHlsDemuxerWrapper wrapper = new GeckoHlsDemuxerWrapper(url, callback);
        return wrapper;
    }

    @WrapForJNI
    public int GetNumberOfTracks(int trackType) {
        int tracks = player != null ? player.getNumberTracks(getPlayerTrackType(trackType)) : 0;
        if (DEBUG) Log.d(LOGTAG, "[GetNumberOfTracks] type : " + trackType + ", num = " + tracks);
        return tracks;
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
            aInfo.mimeType = fmt.sampleMimeType;
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
            vInfo.mimeType = fmt.sampleMimeType;
        }
        return vInfo;
    }

    GeckoHlsDemuxerWrapper(String url, Callbacks callback) {
        if (DEBUG) Log.d(LOGTAG, "Constructing GeckoHlsDemuxerWrapper : " + url + ", callback : " + callback);
        try {
            Context ctx = GeckoAppShell.getApplicationContext();
            player = new GeckoHlsPlayer(ctx, url, callback);
        } catch (Exception e) {
            Log.e(LOGTAG, "Constructing GeckoHlsDemuxerWrapper ... error", e);
        }
    }

    @WrapForJNI
    private Sample[] getSamples(int mediaType, int number) {
        if (DEBUG) Log.d(LOGTAG, "getSample, mediaType = " + mediaType);
        LinkedList<DecoderInputBuffer> inputBuffers = null;
        Sample[] samples = new Sample[number];
        // TODO : Convert inputBuffers to Samples.
        if (mediaType == TRACK_VIDEO) {
            inputBuffers = player.getVideoSamples(number);
        } else if (mediaType == TRACK_AUDIO) {
            inputBuffers = player.getAudioSamples(number);
        }

        int index = 0;
        DecoderInputBuffer inputBuffer = null;
        for (inputBuffer = inputBuffers.pollFirst(); inputBuffer != null; inputBuffer = inputBuffers.pollFirst()) {
            CryptoInfo cryptoInfo = inputBuffer.cryptoInfo.getFrameworkCryptoInfoV16();
            BufferInfo bufferInfo = new BufferInfo();
            long pts = inputBuffer.timeUs;
            // The flags in DecoderInputBuffer is syned with MediaCodec Buffer flags.
            int flags = 0;
            flags |= inputBuffer.isKeyFrame() ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0;
            flags |= inputBuffer.isEndOfStream() ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0;
            int size = inputBuffer.data.limit();

            byte[] realData = new byte[size];
            inputBuffer.data.get(realData, 0, size);
            ByteBuffer buffer = ByteBuffer.wrap(realData);
            bufferInfo.set(0, size, pts, flags);
            if (DEBUG) Log.d(LOGTAG, "Type(" + mediaType + "), PTS(" + pts + "), size(" + size + ")");
            samples[index] = Sample.create(buffer, bufferInfo, cryptoInfo);
            index++;
        }
        return samples;
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
