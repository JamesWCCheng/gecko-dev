/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.annotation.TargetApi;
import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaCrypto;
import android.media.MediaFormat;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.view.Surface;

import com.google.android.exoplayer2.BaseRenderer;
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
import com.google.android.exoplayer2.video.VideoFrameReleaseTimeHelper;
import com.google.android.exoplayer2.video.VideoRendererEventListener;

import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

public class GeckoHlsVideoRenderer extends BaseRenderer {
    private static final String TAG = "GeckoHlsVideoRenderer";
    private static boolean DEBUG = true;
    private boolean passToCodec;
    private boolean initialized;
    private ByteBuffer inputBuffer;
    private int QUEUED_INPUT_SAMPLE_SIZE = 10;
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

    private static MediaFormat getMediaFormat(Format format, CodecMaxValues codecMaxValues,
                                              boolean deviceNeedsAutoFrcWorkaround) {
        MediaFormat frameworkMediaFormat = format.getFrameworkMediaFormatV16();
        // Set the maximum adaptive video dimensions.
        frameworkMediaFormat.setInteger(MediaFormat.KEY_MAX_WIDTH, codecMaxValues.width);
        frameworkMediaFormat.setInteger(MediaFormat.KEY_MAX_HEIGHT, codecMaxValues.height);
        // Set the maximum input size.
        if (codecMaxValues.inputSize != Format.NO_VALUE) {
            frameworkMediaFormat.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, codecMaxValues.inputSize);
        }
        // Set FRC workaround.
        if (deviceNeedsAutoFrcWorkaround) {
            frameworkMediaFormat.setInteger("auto-frc", 0);
        }
        return frameworkMediaFormat;
    }

    private static final long MAX_CODEC_HOTSWAP_TIME_MS = 1000;
    private static final int RECONFIGURATION_STATE_NONE = 0;
    private static final int RECONFIGURATION_STATE_WRITE_PENDING = 1;
    private static final int RECONFIGURATION_STATE_QUEUE_PENDING = 2;
    private static final int REINITIALIZATION_STATE_NONE = 0;
    private static final int REINITIALIZATION_STATE_SIGNAL_END_OF_STREAM = 1;
    private static final int REINITIALIZATION_STATE_WAIT_END_OF_STREAM = 2;

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
    private final MediaCodec.BufferInfo outputBufferInfo;

    private int outputIndex;

    private MediaCodec codec;
    private Surface surface;
    private int scalingMode;
    private Format format;
    private boolean codecIsAdaptive;
    private boolean codecNeedsDiscardToSpsWorkaround;
    private boolean codecNeedsFlushWorkaround;
    private boolean codecNeedsAdaptationWorkaround;
    private boolean codecNeedsAdaptationWorkaroundBuffer;
    private boolean shouldSkipAdaptationWorkaroundOutputBuffer;
    private ConcurrentLinkedQueue<DecoderInputBuffer> queuedInputSamples;

    private long codecHotswapDeadlineMs;
    private boolean codecReconfigured;
    private int codecReconfigurationState;
    private int codecReinitializationState;
    private boolean codecReceivedBuffers;
    private boolean codecReceivedEos;

    private boolean inputStreamEnded;
    private boolean outputStreamEnded;

    private final VideoFrameReleaseTimeHelper frameReleaseTimeHelper;
    private final VideoRendererEventListener.EventDispatcher eventDispatcher;
    private final long allowedJoiningTimeMs;
    private final boolean deviceNeedsAutoFrcWorkaround;

    private Format[] streamFormats;
    private CodecMaxValues codecMaxValues;

    private boolean renderedFirstFrame;
    private long joiningDeadlineMs;

    private boolean waitingForData;
    private GeckoHlsPlayer.ComponentListener playerListener;

    public GeckoHlsVideoRenderer(Context context, MediaCodecSelector mediaCodecSelector,
                                 Handler eventHandler, VideoRendererEventListener eventListener,
                                 boolean passToCodec) {
        super(C.TRACK_TYPE_VIDEO);
        initialized = false;

        Assertions.checkState(Util.SDK_INT >= 16);
        this.mediaCodecSelector = Assertions.checkNotNull(mediaCodecSelector);
        formatHolder = new FormatHolder();
        outputBufferInfo = new MediaCodec.BufferInfo();
        codecReconfigurationState = RECONFIGURATION_STATE_NONE;
        codecReinitializationState = REINITIALIZATION_STATE_NONE;

        playerListener = (GeckoHlsPlayer.ComponentListener)eventListener;
        this.allowedJoiningTimeMs = 5000;
        frameReleaseTimeHelper = new VideoFrameReleaseTimeHelper(context);
        eventDispatcher = new VideoRendererEventListener.EventDispatcher(eventHandler, eventListener);
        deviceNeedsAutoFrcWorkaround = deviceNeedsAutoFrcWorkaround();
        joiningDeadlineMs = C.TIME_UNSET;
        waitingForData = false;

        // NOTE : Modify this to change behavior.
        this.passToCodec = passToCodec;
        if (!passToCodec) {
            renderedFirstFrame = true;
        }
    }

    public void handleMessage(int messageType, Object message) throws ExoPlaybackException {
        if(messageType == 1) {
            this.setSurface((Surface)message);
        } else if(messageType == 5) {
            this.scalingMode = ((Integer)message).intValue();
            if(this.codec != null) {
                this.codec.setVideoScalingMode(this.scalingMode);
            }
        } else {
            super.handleMessage(messageType, message);
        }
    }

    private void setSurface(Surface surface) throws ExoPlaybackException {
        this.renderedFirstFrame = false;
        if(this.surface != surface) {
            this.surface = surface;
            int state = this.getState();
            if (state == STATE_ENABLED || state == STATE_STARTED) {
                this.releaseRenderer();
                this.maybeInitRenderer();
            }
        }
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
        codecNeedsFlushWorkaround = codecNeedsFlushWorkaround(codecName);
        codecNeedsAdaptationWorkaround = codecNeedsAdaptationWorkaround(codecName);
        try {
            long codecInitializingTimestamp = SystemClock.elapsedRealtime();
            codecMaxValues = getCodecMaxValues(format, streamFormats);
            if (passToCodec) {
                codec = MediaCodec.createByCodecName(codecName);
                MediaFormat mediaFormat = getMediaFormat(format, codecMaxValues, false);
                MediaCrypto mediaCrypto = null;
                codec.configure(mediaFormat, surface, mediaCrypto, 0);
                codec.start();
                this.outputIndex = -1;
            }

            long codecInitializedTimestamp = SystemClock.elapsedRealtime();
            onCodecInitialized(codecName, codecInitializedTimestamp,
                    codecInitializedTimestamp - codecInitializingTimestamp);
        } catch (Exception e) {
            throwDecoderInitError(new DecoderInitializationException(format, e,
                    false, codecName));
        }
        codecHotswapDeadlineMs = getState() == STATE_STARTED
                ? (SystemClock.elapsedRealtime() + MAX_CODEC_HOTSWAP_TIME_MS) : C.TIME_UNSET;

        queuedInputSamples = new ConcurrentLinkedQueue<>();

        inputBuffer = ByteBuffer.wrap(new byte[codecMaxValues.inputSize]);
        initialized = true;
    }

    private void throwDecoderInitError(DecoderInitializationException e)
            throws ExoPlaybackException {
        throw ExoPlaybackException.createForRenderer(e, getIndex());
    }

    protected void releaseRenderer() {
        if (initialized) {
            this.outputIndex = -1;

            codecHotswapDeadlineMs = C.TIME_UNSET;
            codecReconfigured = false;
            codecReceivedBuffers = false;
            codecIsAdaptive = false;
            codecNeedsDiscardToSpsWorkaround = false;
            codecNeedsFlushWorkaround = false;
            codecNeedsAdaptationWorkaround = false;
            codecNeedsAdaptationWorkaroundBuffer = false;
            shouldSkipAdaptationWorkaroundOutputBuffer = false;
            codecReceivedEos = false;
            codecReconfigurationState = RECONFIGURATION_STATE_NONE;
            codecReinitializationState = REINITIALIZATION_STATE_NONE;
            inputBuffer = null;
            initialized = false;
            if (passToCodec) {
                try {
                    this.codec.stop();
                } finally {
                    try {
                        this.codec.release();
                    } finally {
                        this.codec = null;
                    }
                }
            }
        }
    }

    @Override
    public void render(long positionUs, long elapsedRealtimeUs) throws ExoPlaybackException {
        if (DEBUG) Log.d(TAG, this + ": positionUs = " + positionUs + ", elapsedRealtimeUs = "+ elapsedRealtimeUs);
        if (outputStreamEnded) {
            return;
        }
        if (format == null) {
            readFormat();
        }
        maybeInitRenderer();
        if (initialized) {
            TraceUtil.beginSection("drainAndFeed");
            if (passToCodec) {
                while (drainOutputBuffer(positionUs, elapsedRealtimeUs)) {}
            } else {
//                while (drainQueuedSamples(positionUs, elapsedRealtimeUs)) {}
//                getQueuedSamples(1);
            }
            while (feedSampleQueue()) {}
            if (passToCodec) {
                while (feedInputBuffer()) {}
            }
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
        this.outputIndex = -1;
        codecHotswapDeadlineMs = C.TIME_UNSET;
        codecNeedsAdaptationWorkaroundBuffer = false;
        shouldSkipAdaptationWorkaroundOutputBuffer = false;
        if (codecNeedsFlushWorkaround) {
            // Workaround framework bugs. See [Internal: b/8347958, b/8578467, b/8543366, b/23361053].
            releaseRenderer();
            maybeInitRenderer();
        } else if (codecReinitializationState != REINITIALIZATION_STATE_NONE) {
            // We're already waiting to release and re-initialize the codec. Since we're now flushing,
            // there's no need to wait any longer.
            releaseRenderer();
            maybeInitRenderer();
        } else {
            // We can flush and re-use the existing decoder.
            codecReceivedBuffers = false;
            if (passToCodec) {
                codec.flush();
            }
        }
        if (codecReconfigured && format != null) {
            // Any reconfiguration data that we send shortly before the flush may be discarded. We
            // avoid this issue by sending reconfiguration data following every flush.
            codecReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
        }
    }

    private synchronized boolean feedSampleQueue() throws ExoPlaybackException {
        if (!initialized || codecReinitializationState == REINITIALIZATION_STATE_WAIT_END_OF_STREAM
                || inputStreamEnded || queuedInputSamples.size() >= QUEUED_INPUT_SAMPLE_SIZE) {
            // We need to reinitialize the codec or the input stream has ended.
            return false;
        }

        DecoderInputBuffer bufferForRead = new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DISABLED);
        bufferForRead.data = inputBuffer;
        bufferForRead.clear();

        if (codecReinitializationState == REINITIALIZATION_STATE_SIGNAL_END_OF_STREAM) {
            // We need to re-initialize the codec. Send an end of stream signal to the existing codec so
            // that it outputs any remaining buffers before we release it.
            codecReceivedEos = true;
            codecReinitializationState = REINITIALIZATION_STATE_WAIT_END_OF_STREAM;
            return false;
        }

        if (codecNeedsAdaptationWorkaroundBuffer) {
            codecNeedsAdaptationWorkaroundBuffer = false;
            bufferForRead.data.put(ADAPTATION_WORKAROUND_BUFFER);
            codecReceivedBuffers = true;
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
            if (!codecReceivedBuffers) {
                processEndOfStream();
                return false;
            }
            try {
                codecReceivedEos = true;
            } catch (MediaCodec.CryptoException e) {
                throw ExoPlaybackException.createForRenderer(e, getIndex());
            }
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

            if (this.getTrackType() == C.TRACK_TYPE_VIDEO) {
                if (DEBUG) Log.d(TAG, "feedSampleQueue: bufferTimeUs : " + bufferForRead.timeUs + ", queueSize = " + queuedInputSamples.size());
            }
            byte[] realData = new byte[bufferForRead.data.limit()];
            bufferForRead.data.get(realData, 0, bufferForRead.data.limit());
            bufferForRead.data = ByteBuffer.wrap(realData);
            inputBuffer.clear();
            queuedInputSamples.offer(bufferForRead);
        } catch (MediaCodec.CryptoException e) {
            throw ExoPlaybackException.createForRenderer(e, getIndex());
        }

        if (waitingForData) {
            playerListener.onDataArrived();
            waitingForData = false;
        }
        return true;
    }

    public synchronized boolean feedInputBuffer() {
        if(queuedInputSamples.isEmpty()) {
            return false;
        }
        int index = this.codec.dequeueInputBuffer(0L);
        if (index < 0) {
            return false;
        }
        ByteBuffer forInput = this.codec.getInputBuffer(index);
        DecoderInputBuffer goingToFeed = queuedInputSamples.poll();
        long presentationTimeUs = goingToFeed.timeUs;
        for (int i = goingToFeed.data.position(); i < goingToFeed.data.limit(); i++) {
            forInput.put(goingToFeed.data.get(i));
        }

        this.codec.queueInputBuffer(index, 0, goingToFeed.data.limit(), presentationTimeUs, 0);
        this.codecReceivedBuffers = true;
        this.codecReconfigurationState = 0;
        return true;
    }

    protected void onOutputStreamEnded() {
        // Do nothing.
    }

    @Override
    public boolean isEnded() {
        return outputStreamEnded;
    }

    public boolean isReady1() {
        boolean hasOutput = passToCodec ? this.outputIndex >= 0 : true;
        return format != null &&
                (isSourceReady() || hasOutput ||
                (codecHotswapDeadlineMs != C.TIME_UNSET && SystemClock.elapsedRealtime() < codecHotswapDeadlineMs));
    }

    public synchronized ConcurrentLinkedQueue<DecoderInputBuffer> getQueuedSamples(int number) {
        ConcurrentLinkedQueue<DecoderInputBuffer> samples = new ConcurrentLinkedQueue<DecoderInputBuffer>();
        int queuedSize = queuedInputSamples.size();
        for (int i = 0; i < queuedSize; i++) {
            if (i >= number) {
                break;
            }
            DecoderInputBuffer outputBuffer = queuedInputSamples.poll();
            samples.offer(outputBuffer);
        }
        if (samples.isEmpty()) {
            waitingForData = true;
        }
        return samples;
    }

    private boolean drainQueuedSamples(long positionUs, long elapsedRealtimeUs) throws ExoPlaybackException {
        if (DEBUG) Log.d(TAG, "                       drainOutputBuffer ===> positionUs : " + positionUs + ", elapsedRT : " + elapsedRealtimeUs);
        int queueSize = queuedInputSamples.size();
        if (queueSize > 0) {
            // We've dequeued a buffer.
            if (shouldSkipAdaptationWorkaroundOutputBuffer) {
                shouldSkipAdaptationWorkaroundOutputBuffer = false;
                return true;
            }
            if ((outputBufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                // The dequeued buffer indicates the end of the stream. Process it immediately.
                processEndOfStream();
                return false;
            } else {
                // The dequeued buffer is a media buffer. Do some initial setup. The buffer will be
                // processed by calling processOutputBuffer (possibly multiple times) below.

                DecoderInputBuffer outputBuffer = queuedInputSamples.peek();
                outputBufferInfo.presentationTimeUs = outputBuffer.timeUs;
            }
        }

        if (processOutputBuffer(positionUs, elapsedRealtimeUs, -1, outputBufferInfo.presentationTimeUs)) {
            if (DEBUG) Log.d(TAG, "                       remove outputbuffer ===> timeUs : " + outputBufferInfo.presentationTimeUs);
            return true;
        }
        return false;
    }

    @SuppressWarnings("deprecation")
    private boolean drainOutputBuffer(long positionUs, long elapsedRealtimeUs)
            throws ExoPlaybackException {

        if (DEBUG) Log.d(TAG, "                       drainOutputBuffer ===> positionUs : " + positionUs + ", elapsedRT : " + elapsedRealtimeUs);
        if(this.outputIndex < 0) {
            this.outputIndex = this.codec.dequeueOutputBuffer(this.outputBufferInfo, 0);
            if(this.outputIndex < 0) {
                if(this.outputIndex == -2) {
                    this.processOutputFormat();
                    return true;
                }
                if(this.outputIndex == -3) {
                    this.processOutputBuffersChanged();
                    return true;
                }
                return false;
            }

            // We've dequeued a buffer.
            if (shouldSkipAdaptationWorkaroundOutputBuffer) {
                shouldSkipAdaptationWorkaroundOutputBuffer = false;
                this.codec.releaseOutputBuffer(this.outputIndex, false);
                this.outputIndex = -1;
                return true;
            }
            if ((outputBufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                // The dequeued buffer indicates the end of the stream. Process it immediately.
                processEndOfStream();
                this.outputIndex = -1;
                return false;
            } else {
                // The dequeued buffer is a media buffer. Do some initial setup. The buffer will be
                // processed by calling processOutputBuffer (possibly multiple times) below.

                ByteBuffer outputBuffer = this.codec.getOutputBuffer(this.outputIndex);
                if (outputBuffer != null) {
                    outputBuffer.position(outputBufferInfo.offset);
                    outputBuffer.limit(outputBufferInfo.offset + outputBufferInfo.size);
                }
            }
        }

        if (processOutputBuffer(positionUs, elapsedRealtimeUs, this.outputIndex, outputBufferInfo.presentationTimeUs)) {
            if (DEBUG) Log.d(TAG, "                       remove outputbuffer ===> timeUs : " + outputBufferInfo.presentationTimeUs);
            this.outputIndex = -1;
            return true;
        } else {
            return false;
        }
    }

    private void dropOutputBuffer(int bufferIndex) {
        TraceUtil.beginSection("dropVideoBuffer");
        if (codec != null && passToCodec && bufferIndex >= 0) {
            codec.releaseOutputBuffer(bufferIndex, false);
        } else {
            queuedInputSamples.poll();
        }
        TraceUtil.endSection();
    }

    private void processOutputFormat() {
        MediaFormat format = this.codec.getOutputFormat();
        if(this.codecNeedsAdaptationWorkaround && format.getInteger("width") == 32 && format.getInteger("height") == 32) {
            this.shouldSkipAdaptationWorkaroundOutputBuffer = true;
        }
    }

    private void processOutputBuffersChanged() {
    }


    private void processEndOfStream() throws ExoPlaybackException {
        if (codecReinitializationState == REINITIALIZATION_STATE_WAIT_END_OF_STREAM) {
            // We're waiting to re-initialize the codec, and have now processed all final buffers.
            releaseRenderer();
            maybeInitRenderer();
        } else {
            outputStreamEnded = true;
            onOutputStreamEnded();
        }
    }

    private static boolean codecNeedsFlushWorkaround(String name) {
        return Util.SDK_INT < 18
                || (Util.SDK_INT == 18
                && ("OMX.SEC.avc.dec".equals(name) || "OMX.SEC.avc.dec.secure".equals(name)))
                || (Util.SDK_INT == 19 && Util.MODEL.startsWith("SM-G800")
                && ("OMX.Exynos.avc.dec".equals(name) || "OMX.Exynos.avc.dec.secure".equals(name)));
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
        frameReleaseTimeHelper.enable();
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
        if (this.passToCodec) {
            renderedFirstFrame = false;
        }
        joiningDeadlineMs = joining && allowedJoiningTimeMs > 0
                ? (SystemClock.elapsedRealtime() + allowedJoiningTimeMs) : C.TIME_UNSET;
    }

    @Override
    public boolean isReady() {
        if ((renderedFirstFrame || shouldInitRenderer()) && isReady1()) {
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
        frameReleaseTimeHelper.disable();
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

        if (initialized && canReconfigureCodec(codecIsAdaptive, oldFormat, format)) {
            codecReconfigured = true;
            codecReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
            codecNeedsAdaptationWorkaroundBuffer = codecNeedsAdaptationWorkaround
                    && format.width == oldFormat.width && format.height == oldFormat.height;
        } else {
            if (codecReceivedBuffers) {
                // Signal end of stream and wait for any final output buffers before re-initialization.
                codecReinitializationState = REINITIALIZATION_STATE_SIGNAL_END_OF_STREAM;
            } else {
                // There aren't any final output buffers, so perform re-initialization immediately.
                releaseRenderer();
                maybeInitRenderer();
            }
        }

        eventDispatcher.inputFormatChanged(newFormat);
    }

    protected boolean canReconfigureCodec(boolean codecIsAdaptive,
                                          Format oldFormat, Format newFormat) {
        return areAdaptationCompatible(oldFormat, newFormat)
                && newFormat.width <= codecMaxValues.width && newFormat.height <= codecMaxValues.height
                && newFormat.maxInputSize <= codecMaxValues.inputSize
                && (codecIsAdaptive
                || (oldFormat.width == newFormat.width && oldFormat.height == newFormat.height));
    }

    protected boolean processOutputBuffer(long positionUs, long elapsedRealtimeUs,
                                          int bufferIndex, long bufferPresentationTimeUs) {
        if (!renderedFirstFrame) {
            if (Util.SDK_INT >= 21) {
                renderOutputBufferV21(bufferIndex, System.nanoTime());
            } else {
                renderOutputBuffer(bufferIndex);
            }
            return true;
        }

        if (getState() != STATE_STARTED) {
            return false;
        }

        // Compute how many microseconds it is until the buffer's presentation time.
        long elapsedSinceStartOfLoopUs = (SystemClock.elapsedRealtime() * 1000) - elapsedRealtimeUs;
        long earlyUs = bufferPresentationTimeUs - positionUs - elapsedSinceStartOfLoopUs;

        // Compute the buffer's desired release time in nanoseconds.
        long systemTimeNs = System.nanoTime();
        long unadjustedFrameReleaseTimeNs = systemTimeNs + (earlyUs * 1000);

        // Apply a timestamp adjustment, if there is one.
        long adjustedReleaseTimeNs = frameReleaseTimeHelper.adjustReleaseTime(
                bufferPresentationTimeUs, unadjustedFrameReleaseTimeNs);
        earlyUs = (adjustedReleaseTimeNs - systemTimeNs) / 1000;

        if (earlyUs < -30000) {
            // We're more than 30ms late rendering the frame. Drop it
            this.dropOutputBuffer(bufferIndex);
            return true;
        }

        if (Util.SDK_INT >= 21) {
            // Let the underlying framework time the release.
            if (earlyUs < 50000) {
                renderOutputBufferV21(bufferIndex, adjustedReleaseTimeNs);
                return true;
            }
        } else {
            // We need to time the release ourselves.
            if (earlyUs < 30000) {
                if (earlyUs > 11000) {
                    // We're a little too early to render the frame. Sleep until the frame can be rendered.
                    // Note: The 11ms threshold was chosen fairly arbitrarily.
                    try {
                        // Subtracting 10000 rather than 11000 ensures the sleep time will be at least 1ms.
                        Thread.sleep((earlyUs - 10000) / 1000);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                }
                renderOutputBuffer(bufferIndex);
                return true;
            }
        }

        // We're either not playing, or it's not time to render the frame yet.
        return false;
    }

    private void renderOutputBuffer(int bufferIndex) {
        if (passToCodec && bufferIndex >= 0 && codec != null) {
            codec.releaseOutputBuffer(bufferIndex, true);
        }
        if (!renderedFirstFrame) {
            renderedFirstFrame = true;
        }
        if (!passToCodec && !queuedInputSamples.isEmpty()) {
            queuedInputSamples.poll();
        }
    }

    @TargetApi(21)
    private void renderOutputBufferV21(int bufferIndex, long releaseTimeNs) {
        if (passToCodec && codec != null && bufferIndex >= 0) {
            codec.releaseOutputBuffer(bufferIndex, releaseTimeNs);
        }
        if (!renderedFirstFrame) {
            renderedFirstFrame = true;
        }
        if (!passToCodec && !queuedInputSamples.isEmpty()) {
            queuedInputSamples.poll();
        }
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

    private static boolean deviceNeedsAutoFrcWorkaround() {
        // nVidia Shield prior to M tries to adjust the playback rate to better map the frame-rate of
        // content to the refresh rate of the display. For example playback of 23.976fps content is
        // adjusted to play at 1.001x speed when the output display is 60Hz. Unfortunately the
        // implementation causes ExoPlayer's reported playback position to drift out of sync. Captions
        // also lose sync [Internal: b/26453592].
        return Util.SDK_INT <= 22 && "foster".equals(Util.DEVICE) && "NVIDIA".equals(Util.MANUFACTURER);
    }

    private static boolean areAdaptationCompatible(Format first, Format second) {
        return first.sampleMimeType.equals(second.sampleMimeType)
                && getRotationDegrees(first) == getRotationDegrees(second);
    }

    private static float getPixelWidthHeightRatio(Format format) {
        return format.pixelWidthHeightRatio == Format.NO_VALUE ? 1 : format.pixelWidthHeightRatio;
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
