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
import com.google.android.exoplayer2.FormatHolder;
import com.google.android.exoplayer2.RendererCapabilities;
import com.google.android.exoplayer2.audio.AudioRendererEventListener;
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.mediacodec.MediaCodecInfo;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.mediacodec.MediaCodecUtil;
import com.google.android.exoplayer2.util.Assertions;
import com.google.android.exoplayer2.util.MimeTypes;
import com.google.android.exoplayer2.util.Util;

import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

public class GeckoHlsAudioRenderer extends GeckoHlsRendererBase {
    private final String LOGTAG;
    private static boolean DEBUG = false;
    private ConcurrentLinkedQueue<GeckoHlsSample> dexmuedInputSamples = new ConcurrentLinkedQueue<>();
    private static final int QUEUED_INPUT_SAMPLE_SIZE = 100;
    private boolean initialized = false;
    private ByteBuffer inputBuffer = null;
    private boolean waitingForData;
    private GeckoHlsPlayer.ComponentListener playerListener;

    private final MediaCodecSelector mediaCodecSelector;
    private final FormatHolder formatHolder;

    private final AudioRendererEventListener.EventDispatcher eventDispatcher;

    private Format format;
    private boolean inputStreamEnded;

    public GeckoHlsAudioRenderer(MediaCodecSelector mediaCodecSelector,
                                 Handler eventHandler,
                                 AudioRendererEventListener eventListener) {
        super(C.TRACK_TYPE_AUDIO);
        LOGTAG = getClass().getSimpleName();
        Assertions.checkState(Util.SDK_INT >= 16);
        this.mediaCodecSelector = Assertions.checkNotNull(mediaCodecSelector);
        formatHolder = new FormatHolder();
        eventDispatcher = new AudioRendererEventListener.EventDispatcher(eventHandler, eventListener);

        waitingForData = false;
        playerListener = (GeckoHlsPlayer.ComponentListener)eventListener;
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
        boolean decoderCapable = Util.SDK_INT < 21 ||
                                 (format.sampleRate == -1 ||
                                  decoderInfo.isAudioSampleRateSupportedV21(format.sampleRate)) &&
                                 (format.channelCount == -1 ||
                                  decoderInfo.isAudioChannelCountSupportedV21(format.channelCount));
        int formatSupport = decoderCapable ?
            RendererCapabilities.FORMAT_HANDLED :
            RendererCapabilities.FORMAT_EXCEEDS_CAPABILITIES;
        return RendererCapabilities.ADAPTIVE_NOT_SEAMLESS | formatSupport;
    }

    protected void onInputFormatChanged(Format newFormat) {
        Format oldFormat = format;
        format = newFormat;
        boolean drmInitDataChanged = !Util.areEqual(format.drmInitData, oldFormat == null ? null : oldFormat.drmInitData);
        if (drmInitDataChanged) {
            if (format.drmInitData != null) {
                // TODO : Notify encrypted
            }
        }

        resetRenderer();
        maybeInitRenderer();
        eventDispatcher.inputFormatChanged(newFormat);
    }

    protected void onEnabled(boolean joining) {
    }

    protected void onPositionReset(long positionUs, boolean joining) {
        inputStreamEnded = false;
        if (initialized) {
            clearInputSamplesQueue();
        }
    }

    @Override
    protected void onDisabled() {
        try {
            format = null;
            resetRenderer();
        } finally {
        }
    }

    @Override
    public boolean isEnded() {
        return inputStreamEnded;
    }

    @Override
    public boolean isReady() {
        return format != null;
    }

    protected boolean shouldInitRenderer() {
        return !initialized && format != null;
    }

    protected final void maybeInitRenderer() {
        if(!shouldInitRenderer()) {
            return;
        }

        clearInputSamplesQueue();
        inputBuffer = ByteBuffer.wrap(new byte[22048]);
        initialized = true;
    }

    private void readFormat() {
        int result = readSource(formatHolder, (DecoderInputBuffer)null);
        if(result == -5) {
            onInputFormatChanged(formatHolder.format);
        }

    }

    public synchronized ConcurrentLinkedQueue<GeckoHlsSample> getQueuedSamples(int number) {
        ConcurrentLinkedQueue<GeckoHlsSample> samples = new ConcurrentLinkedQueue<GeckoHlsSample>();
        int queuedSize = dexmuedInputSamples.size();
        for (int i = 0; i < queuedSize; i++) {
            if (i >= number) {
                break;
            }
            GeckoHlsSample sample = dexmuedInputSamples.poll();
            samples.offer(sample);
        }
        if (samples.isEmpty()) {
            waitingForData = true;
        } else if (firstSampleStartTime == 0) {
            firstSampleStartTime = samples.peek().info.presentationTimeUs;
        }
        return samples;
    }

    @Override
    public synchronized boolean clearInputSamplesQueue() {
        dexmuedInputSamples.clear();
        return true;
    }

    private synchronized boolean feedInputBuffersQueue() {
        if (!initialized || inputStreamEnded ||
            dexmuedInputSamples.size() >= QUEUED_INPUT_SAMPLE_SIZE) {
            // We need to reinitialize the codec or the input stream has ended.
            return false;
        }
        DecoderInputBuffer bufferForRead = new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DISABLED);
        bufferForRead.data = inputBuffer;
        bufferForRead.clear();

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

        boolean bufferEncrypted = bufferForRead.isEncrypted();

        bufferForRead.flip();
        if (DEBUG) Log.d(LOGTAG, "feedInputBuffersQueue: bufferTimeUs : " + bufferForRead.timeUs + ", queueSize = " + dexmuedInputSamples.size());

        int size = bufferForRead.data.limit();
        byte[] realData = new byte[size];
        bufferForRead.data.get(realData, 0, size);
        ByteBuffer buffer = ByteBuffer.wrap(realData);
        inputBuffer.clear();

        // add the demuxed sample buffer into linked list
        CryptoInfo cryptoInfo = bufferForRead.isEncrypted() ? bufferForRead.cryptoInfo.getFrameworkCryptoInfoV16() : null;
        BufferInfo bufferInfo = new BufferInfo();
        // The flags in DecoderInputBuffer is syned with MediaCodec Buffer flags.
        int flags = 0;
        flags |= bufferForRead.isKeyFrame() ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0;
        flags |= bufferForRead.isEndOfStream() ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0;
        bufferInfo.set(0, size, bufferForRead.timeUs, flags);
        GeckoHlsSample sample = GeckoHlsSample.create(buffer, bufferInfo, cryptoInfo);
        dexmuedInputSamples.offer(sample);

        if (waitingForData) {
            playerListener.onDataArrived();
            waitingForData = false;
        }

        return true;
    }

    public void render(long positionUs, long elapsedRealtimeUs) {
        if (DEBUG) Log.d(LOGTAG, "positionUs = " + positionUs +
                         ", elapsedRealtimeUs = "+ elapsedRealtimeUs +
                         ", inputStreamEnded = " + inputStreamEnded);
        if (inputStreamEnded) {
            return;
        }
        if(format == null) {
            readFormat();
        }

        maybeInitRenderer();
        if (initialized) {
            while (feedInputBuffersQueue()) {}
        }
    }

    protected void resetRenderer() {
        initialized = false;
    }
}


