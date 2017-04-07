/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.FormatHolder;
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.mediacodec.MediaCodecInfo;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.mediacodec.MediaCodecUtil;
import com.google.android.exoplayer2.RendererCapabilities;
import com.google.android.exoplayer2.util.Assertions;
import com.google.android.exoplayer2.util.MimeTypes;
import com.google.android.exoplayer2.util.Util;
import com.google.android.exoplayer2.video.VideoRendererEventListener;

import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

public class GeckoHlsVideoRenderer extends GeckoHlsRendererBase {
    private static final String TAG = "GeckoHlsVideoRenderer";
    private static boolean DEBUG = true;
    private boolean initialized;
    private ByteBuffer inputBuffer;
    private int QUEUED_INPUT_SAMPLE_SIZE = 100;

    private final MediaCodecSelector mediaCodecSelector;
    private final FormatHolder formatHolder;

    private Format format;
    private ConcurrentLinkedQueue<GeckoHlsSample> dexmuedInputSamples;
    private ConcurrentLinkedQueue<GeckoHlsSample> dexmuedNoDurationSamples;

    private boolean inputStreamEnded;

    private final VideoRendererEventListener.EventDispatcher eventDispatcher;
    private final long allowedJoiningTimeMs;

    private Format[] streamFormats;
    private CodecMaxValues codecMaxValues;

    private long joiningDeadlineMs;

    private boolean waitingForData;
    private GeckoHlsPlayer.ComponentListener playerListener;

    public GeckoHlsVideoRenderer(MediaCodecSelector mediaCodecSelector,
                                 Handler eventHandler, VideoRendererEventListener eventListener) {
        super(C.TRACK_TYPE_VIDEO);
        Assertions.checkState(Util.SDK_INT >= 16);

        initialized = false;
        this.mediaCodecSelector = Assertions.checkNotNull(mediaCodecSelector);
        formatHolder = new FormatHolder();

        playerListener = (GeckoHlsPlayer.ComponentListener)eventListener;
        allowedJoiningTimeMs = 5000;
        eventDispatcher = new VideoRendererEventListener.EventDispatcher(eventHandler, eventListener);
        joiningDeadlineMs = C.TIME_UNSET;
        waitingForData = false;

        dexmuedInputSamples = new ConcurrentLinkedQueue<>();
        dexmuedNoDurationSamples = new ConcurrentLinkedQueue<>();
    }

    @Override
    public final int supportsMixedMimeTypeAdaptation() {
        return ADAPTIVE_NOT_SEAMLESS;
    }

    @Override
    public final int supportsFormat(Format format) {
        String mimeType = format.sampleMimeType;
        if (!MimeTypes.isVideo(mimeType)) {
            return RendererCapabilities.FORMAT_UNSUPPORTED_TYPE;
        }
        MediaCodecInfo decoderInfo = null;
        try {
            decoderInfo = mediaCodecSelector.getDecoderInfo(mimeType, false);
        } catch (MediaCodecUtil.DecoderQueryException e) {
            Log.e(TAG, e.getMessage());
        }
        if (decoderInfo == null) {
            return RendererCapabilities.FORMAT_UNSUPPORTED_SUBTYPE;
        }

        boolean decoderCapable = decoderInfo.isCodecSupported(format.codecs);
        if (decoderCapable && format.width > 0 && format.height > 0) {
            if (Util.SDK_INT >= 21) {
                decoderCapable = decoderInfo.isVideoSizeAndRateSupportedV21(format.width, format.height,
                        format.frameRate);
            } else {
                try {
                    decoderCapable = format.width * format.height <= MediaCodecUtil.maxH264DecodableFrameSize();
                } catch (MediaCodecUtil.DecoderQueryException e) {
                    Log.e(TAG, e.getMessage());
                }
                if (!decoderCapable) {
                    if (DEBUG) Log.d(TAG, "FalseCheck [legacyFrameSize, " + format.width + "x" + format.height + "] ["
                            + Util.DEVICE_DEBUG_INFO + "]");
                }
            }
        }

        int adaptiveSupport = decoderInfo.adaptive ?
            RendererCapabilities.ADAPTIVE_SEAMLESS :
            RendererCapabilities.ADAPTIVE_NOT_SEAMLESS;
        int formatSupport = decoderCapable ?
            RendererCapabilities.FORMAT_HANDLED :
            RendererCapabilities.FORMAT_EXCEEDS_CAPABILITIES;
        return adaptiveSupport | formatSupport;
    }

    @SuppressWarnings("deprecation")
    protected final void maybeInitRenderer() {
        if (!shouldInitRenderer()) {
            return;
        }
        codecMaxValues = getCodecMaxValues(format, streamFormats);

        clearInputSamplesQueue();
        inputBuffer = ByteBuffer.wrap(new byte[codecMaxValues.inputSize]);
        initialized = true;
    }

    protected void releaseRenderer() {
        if (initialized) {
            inputBuffer = null;
            initialized = false;
        }
    }

    @Override
    public void render(long positionUs, long elapsedRealtimeUs) {
        if (DEBUG) Log.d(TAG, "positionUs = " + positionUs +
                              ", elapsedRealtimeUs = "+ elapsedRealtimeUs +
                              ", inputStreamEnded = " + inputStreamEnded);
        if (inputStreamEnded) {
            return;
        }
        if (format == null) {
            readFormat();
        }

        maybeInitRenderer();
        if (initialized) {
            while (feedInputBuffersQueue()) {}
        }
    }

    private void readFormat() {
        int result = readSource(formatHolder, null);
        if (result == C.RESULT_FORMAT_READ) {
            onInputFormatChanged(formatHolder.format);
        }
    }

    protected void flushRenderer() {
        clearInputSamplesQueue();
    }

    @Override
    public synchronized boolean clearInputSamplesQueue() {
        dexmuedInputSamples.clear();
        dexmuedNoDurationSamples.clear();
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
            Log.d(TAG, "if (bufferForRead.isEndOfStream()) {");
        }

        bufferForRead.flip();

        int size = bufferForRead.data.limit();
        byte[] realData = new byte[size];
        bufferForRead.data.get(realData, 0, size);
        ByteBuffer buffer = ByteBuffer.wrap(realData);
        inputBuffer.clear();

        CryptoInfo cryptoInfo = bufferForRead.isEncrypted() ? bufferForRead.cryptoInfo.getFrameworkCryptoInfoV16() : null;
        BufferInfo bufferInfo = new BufferInfo();
        // The flags in DecoderInputBuffer is syned with MediaCodec Buffer flags.
        int flags = 0;
        flags |= bufferForRead.isKeyFrame() ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0;
        flags |= bufferForRead.isEndOfStream() ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0;
        bufferInfo.set(0, size, bufferForRead.timeUs, flags);

        GeckoHlsSample sample = GeckoHlsSample.create(buffer, bufferInfo, cryptoInfo);
        calculatDuration(sample);

        if (waitingForData) {
            playerListener.onDataArrived();
            waitingForData = false;
        }
        return true;
    }

    private void calculatDuration(GeckoHlsSample inputSample) {
        // TODO : Temparary code here, need to refacotr !!
        if (inputSample != null) {
            dexmuedNoDurationSamples.offer(inputSample);
        }
        int sizeOfNoDura = dexmuedNoDurationSamples.size();
        int range = sizeOfNoDura >= 7 ? 7 : sizeOfNoDura;
        GeckoHlsSample[] inputArray =
                dexmuedNoDurationSamples.toArray(new GeckoHlsSample[sizeOfNoDura]);
        if (range >= 7 && !inputStreamEnded) {
            // Calculate the first 'range' elements
            for (int i = 0; i < range; i++) {
                // Comparing window size.
                for (int j = -2; j < 4; j++) {
                    if (i+j >= 0 &&
                        i+j < range &&
                        inputArray[i+j].info.presentationTimeUs > inputArray[i].info.presentationTimeUs) {
                        inputArray[i].duration =
                            Math.min(inputArray[i].duration,
                                     inputArray[i+j].info.presentationTimeUs - inputArray[i].info.presentationTimeUs);
                    }
                }
            }
            GeckoHlsSample goingToQueue = dexmuedNoDurationSamples.poll();
            if (DEBUG) Log.d(TAG, "[calculatDuration]:  bufferTimeUs : " + goingToQueue.info.presentationTimeUs + ", duration : " + goingToQueue.duration);
            dexmuedInputSamples.offer(goingToQueue);
        } else if (inputStreamEnded) {
            if (DEBUG) Log.d(TAG, "inputStreamEnded, meet EOS");
            for (int i = 0; i < sizeOfNoDura; i++) {
                // Comparing window size.
                for (int j = -2; j < 4; j++) {
                    if (i+j >= 0 && i+j < sizeOfNoDura && inputArray[i+j].info.presentationTimeUs > inputArray[i].info.presentationTimeUs) {
                        inputArray[i].duration =
                            Math.min(inputArray[i].duration,
                                     inputArray[i+j].info.presentationTimeUs - inputArray[i].info.presentationTimeUs);
                    }
                }
            }
            GeckoHlsSample sample = null;
            for (sample = dexmuedNoDurationSamples.poll(); sample != null; sample = dexmuedNoDurationSamples.poll()) {
                if (sample.duration == Long.MAX_VALUE) {
                    sample.duration = totalDurationUs - sample.duration;
                    if (DEBUG) Log.d(TAG, "[calculatDuration] Adjust the PTS of the last sample to " + sample.duration + " (us)");
                }
                dexmuedInputSamples.offer(sample);
            }
        }
    }

    @Override
    public boolean isEnded() {
        return inputStreamEnded;
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
        } else if (firstSampleStartTime == null) {
            firstSampleStartTime = samples.peek().info.presentationTimeUs;
        }
        return samples;
    }

    @Override
    protected void onEnabled(boolean joining) {
    }

    @Override
    protected void onStreamChanged(Format[] formats) {
        streamFormats = formats;
    }

    @Override
    protected void onPositionReset(long positionUs, boolean joining) {
        if (DEBUG) Log.d(TAG, "onPositionReset positionUs = " + positionUs + "joining = " + joining);
        inputStreamEnded = false;
        if (initialized) {
            flushRenderer();
        }
        joiningDeadlineMs = joining && allowedJoiningTimeMs > 0
                ? (SystemClock.elapsedRealtime() + allowedJoiningTimeMs) : C.TIME_UNSET;
    }

    @Override
    public boolean isReady() {
        if (format != null) {
            // Ready. If we were joining then we've now joined, so clear the joining deadline.
            joiningDeadlineMs = C.TIME_UNSET;
            return true;
        } else if (joiningDeadlineMs == C.TIME_UNSET) {
            // Not joining.
            return false;
        } else if (SystemClock.elapsedRealtime() < joiningDeadlineMs) {
            // Joining and still within the joining deadline.
            return true;
        } else {
            // The joining deadline has been exceeded. Give up and clear the deadline.
            joiningDeadlineMs = C.TIME_UNSET;
            return false;
        }
    }

    @Override
    protected void onDisabled() {
        try {
            format = null;
            releaseRenderer();
        } finally {
        }
    }

    protected boolean shouldInitRenderer() {
        return !initialized && format != null;
    }

    protected void onInputFormatChanged(Format newFormat) {
        Format oldFormat = format;
        format = newFormat;

        boolean drmInitDataChanged = !Util.areEqual(format.drmInitData, oldFormat == null ? null
                : oldFormat.drmInitData);
        if (drmInitDataChanged) {
            if (format.drmInitData != null) {
//                TODO: May need to notify reader
            } else {
            }
        }
        releaseRenderer();
        maybeInitRenderer();
        eventDispatcher.inputFormatChanged(newFormat);
    }

    private static CodecMaxValues getCodecMaxValues(Format format, Format[] streamFormats) {
        int maxWidth = format.width;
        int maxHeight = format.height;
        int maxInputSize = getMaxInputSize(format);
        for (Format streamFormat : streamFormats) {
            if (areAdaptationCompatible(format, streamFormat)) {
                maxWidth = Math.max(maxWidth, streamFormat.width);
                maxHeight = Math.max(maxHeight, streamFormat.height);
                maxInputSize = Math.max(maxInputSize, getMaxInputSize(streamFormat));
            }
        }
        return new CodecMaxValues(maxWidth, maxHeight, maxInputSize);
    }

    private static int getMaxInputSize(Format format) {
        if (format.maxInputSize != Format.NO_VALUE) {
            // The format defines an explicit maximum input size.
            return format.maxInputSize;
        }

        if (format.width == Format.NO_VALUE || format.height == Format.NO_VALUE) {
            // We can't infer a maximum input size without video dimensions.
            return Format.NO_VALUE;
        }

        // Attempt to infer a maximum input size from the format.
        int maxPixels;
        int minCompressionRatio;
        switch (format.sampleMimeType) {
            case MimeTypes.VIDEO_H263:
            case MimeTypes.VIDEO_MP4V:
                maxPixels = format.width * format.height;
                minCompressionRatio = 2;
                break;
            case MimeTypes.VIDEO_H264:
                if ("BRAVIA 4K 2015".equals(Util.MODEL)) {
                    // The Sony BRAVIA 4k TV has input buffers that are too small for the calculated 4k video
                    // maximum input size, so use the default value.
                    return Format.NO_VALUE;
                }
                // Round up width/height to an integer number of macroblocks.
                maxPixels = ((format.width + 15) / 16) * ((format.height + 15) / 16) * 16 * 16;
                minCompressionRatio = 2;
                break;
            case MimeTypes.VIDEO_VP8:
                // VPX does not specify a ratio so use the values from the platform's SoftVPX.cpp.
                maxPixels = format.width * format.height;
                minCompressionRatio = 2;
                break;
            case MimeTypes.VIDEO_H265:
            case MimeTypes.VIDEO_VP9:
                maxPixels = format.width * format.height;
                minCompressionRatio = 4;
                break;
            default:
                // Leave the default max input size.
                return Format.NO_VALUE;
        }
        // Estimate the maximum input size assuming three channel 4:2:0 subsampled input frames.
        return (maxPixels * 3) / (2 * minCompressionRatio);
    }

    private static boolean areAdaptationCompatible(Format first, Format second) {
        return first.sampleMimeType.equals(second.sampleMimeType)
                && getRotationDegrees(first) == getRotationDegrees(second);
    }

    private static int getRotationDegrees(Format format) {
        return format.rotationDegrees == Format.NO_VALUE ? 0 : format.rotationDegrees;
    }

    private static final class CodecMaxValues {

        public final int width;
        public final int height;
        public final int inputSize;

        public CodecMaxValues(int width, int height, int inputSize) {
            this.width = width;
            this.height = height;
            this.inputSize = inputSize;
        }

    }
}
