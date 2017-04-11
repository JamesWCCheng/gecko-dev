/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;
import android.os.Handler;
import android.util.Log;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.RendererCapabilities;
import com.google.android.exoplayer2.audio.AudioRendererEventListener;
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.mediacodec.MediaCodecInfo;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.mediacodec.MediaCodecUtil;
import com.google.android.exoplayer2.util.MimeTypes;

import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.mozilla.gecko.AppConstants.Versions;

public class GeckoHlsAudioRenderer extends GeckoHlsRendererBase {
    private final AudioRendererEventListener.EventDispatcher eventDispatcher;

    public GeckoHlsAudioRenderer(MediaCodecSelector selector,
                                 Handler eventHandler,
                                 AudioRendererEventListener eventListener) {
        super(C.TRACK_TYPE_AUDIO, selector, (GeckoHlsPlayer.ComponentListener) eventListener);
        assertTrue(Versions.feature16Plus);
        LOGTAG = getClass().getSimpleName();
        DEBUG = false;

        eventDispatcher = new AudioRendererEventListener.EventDispatcher(eventHandler, eventListener);
    }

    @Override
    public final int supportsFormat(Format format) {
        String mimeType = format.sampleMimeType;
        if (!MimeTypes.isAudio(mimeType)) {
            return RendererCapabilities.FORMAT_UNSUPPORTED_TYPE;
        }
        MediaCodecInfo decoderInfo = null;
        try {
            decoderInfo = mediaCodecSelector.getDecoderInfo(mimeType, false);
        } catch (MediaCodecUtil.DecoderQueryException e) {
            Log.e(LOGTAG, e.getMessage());
        }
        if (decoderInfo == null) {
            return RendererCapabilities.FORMAT_UNSUPPORTED_SUBTYPE;
        }
        boolean decoderCapable = Versions.preLollipop ||
                                 (format.sampleRate == -1 ||
                                  decoderInfo.isAudioSampleRateSupportedV21(format.sampleRate)) &&
                                 (format.channelCount == -1 ||
                                  decoderInfo.isAudioChannelCountSupportedV21(format.channelCount));
        int formatSupport = decoderCapable ?
            RendererCapabilities.FORMAT_HANDLED :
            RendererCapabilities.FORMAT_EXCEEDS_CAPABILITIES;
        return RendererCapabilities.ADAPTIVE_NOT_SEAMLESS | formatSupport;
    }

    protected void handleDrmInitChanged(Format oldFormat, Format newFormat) {
        Object oldDrmInit = oldFormat == null ? null : oldFormat.drmInitData;
        Object newDrnInit = newFormat.drmInitData;

//      TODO: Notify MFR it's encrypted or not.
        if (newDrnInit != oldDrmInit) {
            if (newDrnInit != null) {
            } else {
            }
        }
    }

    @Override
    protected final void maybeInitRenderer() {
        if(initialized || format == null) {
            return;
        }

        inputBuffer = ByteBuffer.wrap(new byte[22048]);
        initialized = true;
    }

    @Override
    protected void resetRenderer() {
        clearInputSamplesQueue();
        inputBuffer = null;
        initialized = false;
    }

    @Override
    protected boolean feedInputBuffersQueue() {
        if (!initialized || inputStreamEnded ||
            dexmuedInputSamples.size() >= QUEUED_INPUT_SAMPLE_SIZE) {
            // Need to reinitialize the renderer or the input stream has ended
            // or we just reached the maximum queue size.
            return false;
        }
        DecoderInputBuffer bufferForRead =
            new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DISABLED);
        bufferForRead.data = inputBuffer;
        bufferForRead.clear();

        // Read data from HlsMediaSource
        int result = readSource(formatHolder, bufferForRead);
        if (result == C.RESULT_NOTHING_READ) {
            return false;
        }
        if (result == C.RESULT_FORMAT_READ) {
            onInputFormatChanged(formatHolder.format);
            return true;
        }

        // We've read a buffer.
        if (bufferForRead.isEndOfStream()) {
            inputStreamEnded = true;
        }

        bufferForRead.flip();
        if (DEBUG) Log.d(LOGTAG, "feedInputBuffersQueue: bufferTimeUs : " +
                         bufferForRead.timeUs + ", queueSize = " +
                         dexmuedInputSamples.size());

        int size = bufferForRead.data.limit();
        byte[] realData = new byte[size];
        bufferForRead.data.get(realData, 0, size);
        ByteBuffer buffer = ByteBuffer.wrap(realData);
        inputBuffer.clear();

        CryptoInfo cryptoInfo = bufferForRead.isEncrypted() ? bufferForRead.cryptoInfo.getFrameworkCryptoInfoV16() : null;
        BufferInfo bufferInfo = new BufferInfo();
        // Flags in DecoderInputBuffer are syned with MediaCodec Buffer flags.
        int flags = 0;
        flags |= bufferForRead.isKeyFrame() ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0;
        flags |= bufferForRead.isEndOfStream() ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0;
        bufferInfo.set(0, size, bufferForRead.timeUs, flags);
        GeckoHlsSample sample = GeckoHlsSample.create(buffer, bufferInfo, cryptoInfo);
        dexmuedInputSamples.offer(sample);

        if (waitingForData && dexmuedInputSamples.size() > 0) {
            playerListener.onDataArrived();
            waitingForData = false;
        }
        return true;
    }

    @Override
    protected boolean clearInputSamplesQueue() {
        dexmuedInputSamples.clear();
        return true;
    }

    @Override
    protected void onInputFormatChanged(Format newFormat) {
        Format oldFormat = format;
        format = newFormat;

        handleDrmInitChanged(oldFormat, newFormat);

        resetRenderer();
        maybeInitRenderer();
        eventDispatcher.inputFormatChanged(newFormat);
    }
}
