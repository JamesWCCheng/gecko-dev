/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.annotation.TargetApi;
import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;
import android.media.MediaCrypto;
import android.media.MediaFormat;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.ExoPlaybackException;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.FormatHolder;
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.mediacodec.MediaCodecInfo;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.mediacodec.MediaCodecUtil;
import com.google.android.exoplayer2.util.Assertions;
import com.google.android.exoplayer2.util.MimeTypes;
import com.google.android.exoplayer2.util.NalUnitUtil;
import com.google.android.exoplayer2.util.TraceUtil;
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

    @SuppressWarnings("serial")
    public static class DecoderInitializationException extends Exception {
        private static final int CUSTOM_ERROR_CODE_BASE = -50000;
        private static final int NO_SUITABLE_DECODER_ERROR = CUSTOM_ERROR_CODE_BASE + 1;
        private static final int DECODER_QUERY_ERROR = CUSTOM_ERROR_CODE_BASE + 2;

        public final String mimeType;
        public final boolean secureDecoderRequired;
        public final String decoderName;
        public final String diagnosticInfo;

        public DecoderInitializationException(Format format, Throwable cause,
                                              boolean secureDecoderRequired, int errorCode) {
            super("Decoder init failed: [" + errorCode + "], " + format, cause);
            this.mimeType = format.sampleMimeType;
            this.secureDecoderRequired = secureDecoderRequired;
            this.decoderName = null;
            this.diagnosticInfo = buildCustomDiagnosticInfo(errorCode);
        }

        public DecoderInitializationException(Format format, Throwable cause,
                                              boolean secureDecoderRequired, String decoderName) {
            super("Decoder init failed: " + decoderName + ", " + format, cause);
            this.mimeType = format.sampleMimeType;
            this.secureDecoderRequired = secureDecoderRequired;
            this.decoderName = decoderName;
            this.diagnosticInfo = Util.SDK_INT >= 21 ? getDiagnosticInfoV21(cause) : null;
        }

        @TargetApi(21)
        private static String getDiagnosticInfoV21(Throwable cause) {
            if (cause instanceof MediaCodec.CodecException) {
                return ((MediaCodec.CodecException) cause).getDiagnosticInfo();
            }
            return null;
        }

        private static String buildCustomDiagnosticInfo(int errorCode) {
            String sign = errorCode < 0 ? "neg_" : "";
            return "com.google.android.exoplayer.MediaCodecTrackRenderer_" + sign + Math.abs(errorCode);
        }

    }

    private static final int RECONFIGURATION_STATE_NONE = 0;
    private static final int RECONFIGURATION_STATE_WRITE_PENDING = 1;
    private static final int RECONFIGURATION_STATE_QUEUE_PENDING = 2;

    /**
     * H.264/AVC buffer to queue when using the adaptation workaround (see
     * {@link #codecNeedsAdaptationWorkaround(String)}. Consists of three NAL units with start codes:
     * Baseline sequence/picture parameter sets and a 32 * 32 pixel IDR slice. This stream can be
     * queued to force a resolution change when adapting to a new format.
     */
    private static final byte[] ADAPTATION_WORKAROUND_BUFFER = Util.getBytesFromHexString(
            "0000016742C00BDA259000000168CE0F13200000016588840DCE7118A0002FBF1C31C3275D78");

    private final MediaCodecSelector mediaCodecSelector;
    private final FormatHolder formatHolder;

    private Format format;
    private boolean codecIsAdaptive;
    private boolean codecNeedsDiscardToSpsWorkaround;
    private boolean codecNeedsAdaptationWorkaround;
    private boolean codecNeedsAdaptationWorkaroundBuffer;
    private boolean shouldSkipAdaptationWorkaroundOutputBuffer;
    private ConcurrentLinkedQueue<GeckoHlsSample> dexmuedInputSamples;
    private ConcurrentLinkedQueue<GeckoHlsSample> dexmuedNoDurationSamples;

    private boolean codecReconfigured;
    private int codecReconfigurationState;

    private boolean inputStreamEnded;
    private boolean outputStreamEnded;

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
        codecReconfigurationState = RECONFIGURATION_STATE_NONE;

        playerListener = (GeckoHlsPlayer.ComponentListener)eventListener;
        this.allowedJoiningTimeMs = 5000;
        eventDispatcher = new VideoRendererEventListener.EventDispatcher(eventHandler, eventListener);
        joiningDeadlineMs = C.TIME_UNSET;
        waitingForData = false;

        dexmuedInputSamples = new ConcurrentLinkedQueue<>();
        dexmuedNoDurationSamples = new ConcurrentLinkedQueue<>();
    }

    public void handleMessage(int messageType, Object message) throws ExoPlaybackException {
        super.handleMessage(messageType, message);
    }

    @Override
    public final int supportsMixedMimeTypeAdaptation() throws ExoPlaybackException {
        return ADAPTIVE_NOT_SEAMLESS;
    }

    @Override
    public final int supportsFormat(Format format) throws ExoPlaybackException {
        try {
            return supportsFormat(mediaCodecSelector, format);
        } catch (MediaCodecUtil.DecoderQueryException e) {
            throw ExoPlaybackException.createForRenderer(e, getIndex());
        }
    }

    @SuppressWarnings("deprecation")
    protected final void maybeInitRenderer() throws ExoPlaybackException {
        if (!shouldInitRenderer()) {
            return;
        }

        MediaCodecInfo decoderInfo = null;
        try {
            decoderInfo = mediaCodecSelector.getDecoderInfo(format.sampleMimeType, false);
        } catch (MediaCodecUtil.DecoderQueryException e) {
            throwDecoderInitError(new DecoderInitializationException(format, e,
                    false, DecoderInitializationException.DECODER_QUERY_ERROR));
        }

        if (decoderInfo == null) {
            throwDecoderInitError(new DecoderInitializationException(format, null, false,
                    DecoderInitializationException.NO_SUITABLE_DECODER_ERROR));
        }

        String codecName = decoderInfo.name;
        codecIsAdaptive = decoderInfo.adaptive;
        codecNeedsDiscardToSpsWorkaround = codecNeedsDiscardToSpsWorkaround(codecName, format);
        codecNeedsAdaptationWorkaround = codecNeedsAdaptationWorkaround(codecName);
        try {
            long codecInitializingTimestamp = SystemClock.elapsedRealtime();
            codecMaxValues = getCodecMaxValues(format, streamFormats);
            long codecInitializedTimestamp = SystemClock.elapsedRealtime();
            onCodecInitialized(codecName, codecInitializedTimestamp,
                    codecInitializedTimestamp - codecInitializingTimestamp);
        } catch (Exception e) {
            throwDecoderInitError(new DecoderInitializationException(format, e,
                    false, codecName));
        }

        clearInputSamplesQueue();
        inputBuffer = ByteBuffer.wrap(new byte[codecMaxValues.inputSize]);
        initialized = true;
    }

    private void throwDecoderInitError(DecoderInitializationException e)
            throws ExoPlaybackException {
        throw ExoPlaybackException.createForRenderer(e, getIndex());
    }

    protected void releaseRenderer() {
        if (initialized) {
            codecReconfigured = false;
            codecIsAdaptive = false;
            codecNeedsDiscardToSpsWorkaround = false;
            codecNeedsAdaptationWorkaround = false;
            codecNeedsAdaptationWorkaroundBuffer = false;
            shouldSkipAdaptationWorkaroundOutputBuffer = false;
            codecReconfigurationState = RECONFIGURATION_STATE_NONE;
            inputBuffer = null;
            initialized = false;
        }
    }

    @Override
    public void render(long positionUs, long elapsedRealtimeUs) throws ExoPlaybackException {
        if (DEBUG) Log.d(TAG, "positionUs = " + positionUs +
                              ", elapsedRealtimeUs = "+ elapsedRealtimeUs +
                              ", inputStreamEnded = " + inputStreamEnded +
                              ", outputStreamEnded = " + outputStreamEnded);
        if (inputStreamEnded || outputStreamEnded) {
            return;
        }
        if (format == null) {
            readFormat();
        }
        maybeInitRenderer();
        if (initialized) {
            TraceUtil.beginSection("drainAndFeed");
            while (feedInputBuffersQueue()) {}
            TraceUtil.endSection();
        } else if (format != null) {
            skipToKeyframeBefore(positionUs);
        }
    }

    private void readFormat() throws ExoPlaybackException {
        int result = readSource(formatHolder, null);
        if (result == C.RESULT_FORMAT_READ) {
            onInputFormatChanged(formatHolder.format);
        }
    }

    protected void flushRenderer() throws ExoPlaybackException {
        codecNeedsAdaptationWorkaroundBuffer = false;
        shouldSkipAdaptationWorkaroundOutputBuffer = false;

        clearInputSamplesQueue();
        if (codecReconfigured && format != null) {
            // Any reconfiguration data that we send shortly before the flush may be discarded. We
            // avoid this issue by sending reconfiguration data following every flush.
            codecReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
        }
    }

    @Override
    public synchronized boolean clearInputSamplesQueue() {
        dexmuedInputSamples.clear();
        dexmuedNoDurationSamples.clear();
        return true;
    }

    private synchronized boolean feedInputBuffersQueue() throws ExoPlaybackException {
        if (!initialized || inputStreamEnded ||
            dexmuedInputSamples.size() >= QUEUED_INPUT_SAMPLE_SIZE) {
            // We need to reinitialize the codec or the input stream has ended.
            return false;
        }

        DecoderInputBuffer bufferForRead = new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DISABLED);
        bufferForRead.data = inputBuffer;
        bufferForRead.clear();

        if (codecNeedsAdaptationWorkaroundBuffer) {
            if (DEBUG) Log.d(TAG, "feedInputBuffersQueue : codecNeedsAdaptationWorkaroundBuffer !!!!!!!!!!!!!!!!!!!!!!! ");
            codecNeedsAdaptationWorkaroundBuffer = false;
            bufferForRead.data.put(ADAPTATION_WORKAROUND_BUFFER);
            return true;
        }

        // For adaptive reconfiguration OMX decoders expect all reconfiguration data to be supplied
        // at the start of the buffer that also contains the first frame in the new format.
        if (codecReconfigurationState == RECONFIGURATION_STATE_WRITE_PENDING) {
            for (int i = 0; i < format.initializationData.size(); i++) {
                byte[] data = format.initializationData.get(i);
                bufferForRead.data.put(data);
            }
            codecReconfigurationState = RECONFIGURATION_STATE_QUEUE_PENDING;
        }

        int result = readSource(formatHolder, bufferForRead);
        if (result == C.RESULT_NOTHING_READ) {
            if (DEBUG) Log.d(TAG, "feedInputBuffersQueue : RESULT_NOTHING_READ >>>>>>>>>>");
            return false;
        }
        if (result == C.RESULT_FORMAT_READ) {
            if (codecReconfigurationState == RECONFIGURATION_STATE_QUEUE_PENDING) {
                // We received two formats in a row. Clear the current buffer of any reconfiguration data
                // associated with the first format.
                bufferForRead.clear();
                codecReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
            }
            onInputFormatChanged(formatHolder.format);
            return true;
        }
        if (DEBUG) Log.d(TAG, "feedInputBuffersQueue: is EndOfStream : " + bufferForRead.isEndOfStream());
        // We've read a buffer.
        if (bufferForRead.isEndOfStream()) {
            if (codecReconfigurationState == RECONFIGURATION_STATE_QUEUE_PENDING) {
                // We received a new format immediately before the end of the stream. We need to clear
                // the corresponding reconfiguration data from the current buffer, but re-write it into
                // a subsequent buffer if there are any (e.g. if the user seeks backwards).
                bufferForRead.clear();
                codecReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
            }
            inputStreamEnded = true;
            return false;
        }
        boolean bufferEncrypted = bufferForRead.isEncrypted();

        if (codecNeedsDiscardToSpsWorkaround && !bufferEncrypted) {
            NalUnitUtil.discardToSps(bufferForRead.data);
            if (bufferForRead.data.position() == 0) {
                return true;
            }
            codecNeedsDiscardToSpsWorkaround = false;
        }
        try {
            bufferForRead.flip();
            if (DEBUG) Log.d(TAG, "feedInputBuffersQueue: bufferTimeUs : " + bufferForRead.timeUs + ", queueSize = " + dexmuedInputSamples.size());

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
        } catch (MediaCodec.CryptoException e) {
            throw ExoPlaybackException.createForRenderer(e, getIndex());
        }

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
            for (int i = 0; i < sizeOfNoDura; i++) {
                // Comparing window size.
                for (int j = -2; j < 4; j++) {
                    if (inputArray[i+j].info.presentationTimeUs > inputArray[i].info.presentationTimeUs &&
                        i+j >= 0 &&
                        i+j < range) {
                        inputArray[i].duration =
                            Math.min(inputArray[i].duration,
                                     inputArray[i+j].info.presentationTimeUs - inputArray[i].info.presentationTimeUs);
                    }
                }
            }
            GeckoHlsSample sample = null;
            for (sample = dexmuedNoDurationSamples.poll(); sample != null; sample = dexmuedNoDurationSamples.poll()) {
                if (DEBUG) Log.d(TAG, "[calculatDuration]:  bufferTimeUs : " + sample.info.presentationTimeUs + ", duration : " + sample.duration);
                dexmuedInputSamples.offer(sample);
            }
        }
    }

    protected void onOutputStreamEnded() {
        // Do nothing.
    }

    @Override
    public boolean isEnded() {
        return inputStreamEnded || outputStreamEnded;
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

    private static boolean codecNeedsAdaptationWorkaround(String name) {
        return Util.SDK_INT < 24
                && ("OMX.Nvidia.h264.decode".equals(name) || "OMX.Nvidia.h264.decode.secure".equals(name))
                && ("flounder".equals(Util.DEVICE) || "flounder_lte".equals(Util.DEVICE)
                || "grouper".equals(Util.DEVICE) || "tilapia".equals(Util.DEVICE));
    }

    private static boolean codecNeedsDiscardToSpsWorkaround(String name, Format format) {
        return Util.SDK_INT < 21 && format.initializationData.isEmpty()
                && "OMX.MTK.VIDEO.DECODER.AVC".equals(name);
    }

//    ===============================================================================================

    protected int supportsFormat(MediaCodecSelector mediaCodecSelector, Format format)
            throws MediaCodecUtil.DecoderQueryException {
        String mimeType = format.sampleMimeType;
        if (!MimeTypes.isVideo(mimeType)) {
            return FORMAT_UNSUPPORTED_TYPE;
        }

        MediaCodecInfo decoderInfo = mediaCodecSelector.getDecoderInfo(mimeType, false);
        if (decoderInfo == null) {
            return FORMAT_UNSUPPORTED_SUBTYPE;
        }

        boolean decoderCapable = decoderInfo.isCodecSupported(format.codecs);
        if (decoderCapable && format.width > 0 && format.height > 0) {
            if (Util.SDK_INT >= 21) {
                decoderCapable = decoderInfo.isVideoSizeAndRateSupportedV21(format.width, format.height,
                        format.frameRate);
            } else {
                decoderCapable = format.width * format.height <= MediaCodecUtil.maxH264DecodableFrameSize();
                if (!decoderCapable) {
                    if (DEBUG) Log.d(TAG, "FalseCheck [legacyFrameSize, " + format.width + "x" + format.height + "] ["
                            + Util.DEVICE_DEBUG_INFO + "]");
                }
            }
        }

        int adaptiveSupport = decoderInfo.adaptive ? ADAPTIVE_SEAMLESS : ADAPTIVE_NOT_SEAMLESS;
        int formatSupport = decoderCapable ? FORMAT_HANDLED : FORMAT_EXCEEDS_CAPABILITIES;
        return adaptiveSupport | formatSupport;
    }

    @Override
    protected void onEnabled(boolean joining) throws ExoPlaybackException {
    }

    @Override
    protected void onStreamChanged(Format[] formats) throws ExoPlaybackException {
        streamFormats = formats;
        super.onStreamChanged(formats);
    }

    @Override
    protected void onPositionReset(long positionUs, boolean joining) throws ExoPlaybackException {
        inputStreamEnded = false;
        outputStreamEnded = false;
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
    protected void onStarted() {
    }

    @Override
    protected void onStopped() {
        joiningDeadlineMs = C.TIME_UNSET;
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

    protected void onCodecInitialized(String name, long initializedTimestampMs,
                                      long initializationDurationMs) {
        eventDispatcher.decoderInitialized(name, initializedTimestampMs, initializationDurationMs);
    }

    protected void onInputFormatChanged(Format newFormat) throws ExoPlaybackException {
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
        if (DEBUG) Log.d(TAG, "[onInputFormatChanged] >>>>>>>>>>>>>>>> onInputFormatChanged !!!! initialized : " + initialized);
        if (initialized && canReconfigureCodec(codecIsAdaptive, oldFormat, format)) {
            codecReconfigured = true;
            codecReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
            codecNeedsAdaptationWorkaroundBuffer = codecNeedsAdaptationWorkaround
                    && format.width == oldFormat.width && format.height == oldFormat.height;
        } else {
            releaseRenderer();
            maybeInitRenderer();
        }

        eventDispatcher.inputFormatChanged(newFormat);
    }

    protected boolean canReconfigureCodec(boolean codecIsAdaptive,
                                          Format oldFormat, Format newFormat) {
        boolean canReconfig = areAdaptationCompatible(oldFormat, newFormat)
                                && newFormat.width <= codecMaxValues.width && newFormat.height <= codecMaxValues.height
                                && newFormat.maxInputSize <= codecMaxValues.inputSize
                                && (codecIsAdaptive
                                || (oldFormat.width == newFormat.width && oldFormat.height == newFormat.height));
        if (DEBUG) Log.d(TAG, "[canReconfigureCodec] >>>>>>>>>>>>>>>> canConfig :" + canReconfig);
        return canReconfig;
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
