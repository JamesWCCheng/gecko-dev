/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;
import android.media.MediaCrypto;
import android.media.MediaFormat;
import android.media.PlaybackParams;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.Surface;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.ExoPlaybackException;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.FormatHolder;
import com.google.android.exoplayer2.audio.AudioCapabilities;
import com.google.android.exoplayer2.audio.AudioRendererEventListener;
import com.google.android.exoplayer2.audio.AudioTrack;
import com.google.android.exoplayer2.decoder.DecoderCounters;
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.drm.DrmSession;
import com.google.android.exoplayer2.drm.DrmSessionManager;
import com.google.android.exoplayer2.drm.FrameworkMediaCrypto;
import com.google.android.exoplayer2.mediacodec.MediaCodecInfo;
import com.google.android.exoplayer2.mediacodec.MediaCodecRenderer;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.mediacodec.MediaCodecUtil;
import com.google.android.exoplayer2.util.Assertions;
import com.google.android.exoplayer2.util.MediaClock;
import com.google.android.exoplayer2.util.MimeTypes;
import com.google.android.exoplayer2.util.NalUnitUtil;
import com.google.android.exoplayer2.util.TraceUtil;
import com.google.android.exoplayer2.util.Util;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentLinkedQueue;

public class GeckoHlsAudioRenderer extends GeckoHlsRendererBase implements MediaClock {
    private static boolean DEBUG = false;
    private static final String TAG = "GeckoHlsAudioRenderer";
    private ConcurrentLinkedQueue<GeckoHlsSample> dexmuedInputSamples = new ConcurrentLinkedQueue<>();
    private static final int QUEUED_DEMUXED_INPUT_BUFFER_SIZE = 100;
    private boolean initialized = false;
    private ByteBuffer inputBuffer = null;
    private boolean waitingForData;
    private GeckoHlsPlayer.ComponentListener playerListener;

    // Copy from GrandFather
    private static final byte[] ADAPTATION_WORKAROUND_BUFFER = Util.getBytesFromHexString("0000016742C00BDA259000000168CE0F13200000016588840DCE7118A0002FBF1C31C3275D78");

    private final MediaCodecSelector mediaCodecSelector;
    private final DrmSessionManager<FrameworkMediaCrypto> drmSessionManager = null;
    private final boolean playClearSamplesWithoutKeys = true;
    private final DecoderInputBuffer buffer;
    private final FormatHolder formatHolder;
    private final List<Long> decodeOnlyPresentationTimestamps;
    private final MediaCodec.BufferInfo outputBufferInfo;

    // Copy from Father
    private final AudioRendererEventListener.EventDispatcher eventDispatcher;
    private final AudioTrack audioTrack;

    private boolean passthroughEnabled;
    private MediaFormat passthroughMediaFormat;
    private int pcmEncoding;
    private int audioSessionId;
    private long currentPositionUs;
    private boolean allowPositionDiscontinuity;


    // Maybe common usage copy from grandfather
    private Format format;
    private MediaCodec codec;
    private DrmSession<FrameworkMediaCrypto> drmSession;
    private DrmSession<FrameworkMediaCrypto> pendingDrmSession;
    private boolean codecIsAdaptive;
    private boolean codecNeedsDiscardToSpsWorkaround;
    private boolean codecNeedsAdaptationWorkaround;
    private boolean codecNeedsAdaptationWorkaroundBuffer;
    private boolean shouldSkipAdaptationWorkaroundOutputBuffer;
    private long codecHotswapDeadlineMs;
    private int outputIndex;
    private boolean shouldSkipOutputBuffer;
    private boolean codecReconfigured;
    private int codecReconfigurationState;
    private int codecReinitializationState;
    private boolean codecReceivedBuffers;
    private boolean codecReceivedEos;
    private boolean inputStreamEnded;
    private boolean outputStreamEnded;
    private boolean waitingForKeys;
    protected DecoderCounters decoderCounters;

    private static final int RECONFIGURATION_STATE_NONE = 0;
    private static final int RECONFIGURATION_STATE_WRITE_PENDING = 1;
    private static final int RECONFIGURATION_STATE_QUEUE_PENDING = 2;
    private static final int REINITIALIZATION_STATE_NONE = 0;
    private static final int REINITIALIZATION_STATE_SIGNAL_END_OF_STREAM = 1;
    private static final int REINITIALIZATION_STATE_WAIT_END_OF_STREAM = 2;

    public GeckoHlsAudioRenderer(MediaCodecSelector mediaCodecSelector,
                                 Handler eventHandler,
                                 AudioRendererEventListener eventListener,
                                 AudioCapabilities audioCapabilities) {
        super(C.TRACK_TYPE_AUDIO);
        Assertions.checkState(Util.SDK_INT >= 16);
        this.mediaCodecSelector = Assertions.checkNotNull(mediaCodecSelector);
        this.buffer = new DecoderInputBuffer(0);
        this.formatHolder = new FormatHolder();
        this.decodeOnlyPresentationTimestamps = new ArrayList<Long>();
        this.outputBufferInfo = new MediaCodec.BufferInfo();
        this.codecReconfigurationState = 0;
        this.codecReinitializationState = 0;
        this.audioTrack = new AudioTrack(audioCapabilities, new AudioTrackListener());
        this.eventDispatcher = new AudioRendererEventListener.EventDispatcher(eventHandler, eventListener);

        waitingForData = false;
        playerListener = (GeckoHlsPlayer.ComponentListener)eventListener;
    }

    @Override
    public final int supportsFormat(Format format) throws ExoPlaybackException {
        try {
            return this.supportsFormat(this.mediaCodecSelector, format);
        } catch (MediaCodecUtil.DecoderQueryException var3) {
            throw ExoPlaybackException.createForRenderer(var3, this.getIndex());
        }
    }

    protected int supportsFormat(MediaCodecSelector mediaCodecSelector, Format format) throws MediaCodecUtil.DecoderQueryException {
        String mimeType = format.sampleMimeType;
        if (!MimeTypes.isAudio(mimeType)) {
            return 0;
        } else if (this.allowPassthrough(mimeType) && mediaCodecSelector.getPassthroughDecoderInfo() != null) {
            return 7;
        } else {
            MediaCodecInfo decoderInfo = mediaCodecSelector.getDecoderInfo(mimeType, false);
            if (decoderInfo == null) {
                return 1;
            } else {
                boolean decoderCapable = Util.SDK_INT < 21 || (format.sampleRate == -1 || decoderInfo.isAudioSampleRateSupportedV21(format.sampleRate)) && (format.channelCount == -1 || decoderInfo.isAudioChannelCountSupportedV21(format.channelCount));
                int formatSupport = decoderCapable ? 3 : 2;
                return 4 | formatSupport;
            }
        }
    }

    protected MediaCodecInfo getDecoderInfo(MediaCodecSelector mediaCodecSelector, Format format, boolean requiresSecureDecoder) throws MediaCodecUtil.DecoderQueryException {
        if (this.allowPassthrough(format.sampleMimeType)) {
            MediaCodecInfo passthroughDecoderInfo = mediaCodecSelector.getPassthroughDecoderInfo();
            if (passthroughDecoderInfo != null) {
                this.passthroughEnabled = true;
                return passthroughDecoderInfo;
            }
        }

        this.passthroughEnabled = false;
        return mediaCodecSelector.getDecoderInfo(format.sampleMimeType, requiresSecureDecoder);
    }

    protected boolean allowPassthrough(String mimeType) {
        return false;
    }

    public MediaClock getMediaClock() {
        return null;
    }

    protected void onCodecInitialized(String name, long initializedTimestampMs, long initializationDurationMs) {
        this.eventDispatcher.decoderInitialized(name, initializedTimestampMs, initializationDurationMs);
    }

    protected void onInputFormatChanged(Format newFormat) throws ExoPlaybackException {
        // From GrandFather
        Format oldFormat = this.format;
        this.format = newFormat;
        boolean drmInitDataChanged = !Util.areEqual(this.format.drmInitData, oldFormat == null ? null : oldFormat.drmInitData);
        if (drmInitDataChanged) {
            if (this.format.drmInitData != null) {
                if (this.drmSessionManager == null) {
                    throw ExoPlaybackException.createForRenderer(new IllegalStateException("Media requires a DrmSessionManager"), this.getIndex());
                }

                this.pendingDrmSession = this.drmSessionManager.acquireSession(Looper.myLooper(), this.format.drmInitData);
                if (this.pendingDrmSession == this.drmSession) {
                    this.drmSessionManager.releaseSession(this.pendingDrmSession);
                }
            } else {
                this.pendingDrmSession = null;
            }
        }

        if (this.initialized && this.pendingDrmSession == this.drmSession && this.canReconfigureCodec(this.codec, this.codecIsAdaptive, oldFormat, this.format)) {
            this.codecReconfigured = true;
            this.codecReconfigurationState = 1;
            this.codecNeedsAdaptationWorkaroundBuffer = this.codecNeedsAdaptationWorkaround && this.format.width == oldFormat.width && this.format.height == oldFormat.height;
        } else if (this.codecReceivedBuffers) {
            this.codecReinitializationState = 1;
        } else {
            this.releaseRenderer();
            this.maybeInitRenderer();
        }
        //
        this.eventDispatcher.inputFormatChanged(newFormat);
        this.pcmEncoding = "audio/raw".equals(newFormat.sampleMimeType) ? newFormat.pcmEncoding : 2;
    }

    protected void onOutputFormatChanged(MediaCodec codec, MediaFormat outputFormat) {
        boolean passthrough = this.passthroughMediaFormat != null;
        String mimeType = passthrough ? this.passthroughMediaFormat.getString("mime") : "audio/raw";
        MediaFormat format = passthrough ? this.passthroughMediaFormat : outputFormat;
        int channelCount = format.getInteger("channel-count");
        int sampleRate = format.getInteger("sample-rate");
        this.audioTrack.configure(mimeType, channelCount, sampleRate, this.pcmEncoding, 0);
    }

    protected void onAudioSessionId(int audioSessionId) {
        // Do nothing.
    }

    protected void onAudioTrackPositionDiscontinuity() {
        // Do nothing.
    }

    protected void onAudioTrackUnderrun(int bufferSize, long bufferSizeMs,
                                        long elapsedSinceLastFeedMs) {
        // Do nothing.
    }

    protected void onEnabled(boolean joining) throws ExoPlaybackException {
        this.decoderCounters = new DecoderCounters();
        this.eventDispatcher.enabled(this.decoderCounters);
    }

    protected void onPositionReset(long positionUs, boolean joining) throws ExoPlaybackException {
        inputStreamEnded = false;
        outputStreamEnded = false;
        if (initialized) {
            flushRenderer();
        }
        this.currentPositionUs = positionUs;
        this.allowPositionDiscontinuity = true;
    }

    protected void onStarted() {
    }

    protected void onStopped() {
    }

    protected void onDisabled() {
        try {
            super.onDisabled();
        } finally {
            this.decoderCounters.ensureUpdated();
            this.eventDispatcher.disabled(this.decoderCounters);
        }

    }

    public boolean isEnded() {
        if (this.outputStreamEnded) {
            return true;
        }
        return false;

    }

    public boolean isReady() {
        boolean hasOutput = true;
        return this.format != null && !this.waitingForKeys && hasOutput;
    }

    public long getPositionUs() {
        long newCurrentPositionUs = this.audioTrack.getCurrentPositionUs(this.isEnded());
        if(newCurrentPositionUs != AudioTrack.CURRENT_POSITION_NOT_SET) {
            this.currentPositionUs = this.allowPositionDiscontinuity?newCurrentPositionUs:Math.max(this.currentPositionUs, newCurrentPositionUs);
            this.allowPositionDiscontinuity = false;
        }

        return this.currentPositionUs;
    }

    protected void onOutputStreamEnded() {
    }

    public void handleMessage(int messageType, Object message) throws ExoPlaybackException {
        switch(messageType) {
            case 2:
                this.audioTrack.setVolume(((Float)message).floatValue());
                break;
            case 3:
                this.audioTrack.setPlaybackParams((PlaybackParams)message);
                break;
            case 4:
                int streamType = ((Integer)message).intValue();
                this.audioTrack.setStreamType(streamType);
                break;
            default:
                super.handleMessage(messageType, message);
        }

    }


    // Copy from GrandFather
    private static boolean codecNeedsAdaptationWorkaround(String name) {
        return Util.SDK_INT < 24 && ("OMX.Nvidia.h264.decode".equals(name) || "OMX.Nvidia.h264.decode.secure".equals(name)) && ("flounder".equals(Util.DEVICE) || "flounder_lte".equals(Util.DEVICE) || "grouper".equals(Util.DEVICE) || "tilapia".equals(Util.DEVICE));
    }

    private static boolean codecNeedsDiscardToSpsWorkaround(String name, Format format) {
        return Util.SDK_INT < 21 && format.initializationData.isEmpty() && "OMX.MTK.VIDEO.DECODER.AVC".equals(name);
    }

    private void throwDecoderInitError(MediaCodecRenderer.DecoderInitializationException e) throws ExoPlaybackException {
        throw ExoPlaybackException.createForRenderer(e, this.getIndex());
    }

    protected boolean shouldInitRenderer() {
        return !this.initialized && this.format != null;
    }

    protected final void maybeInitRenderer() throws ExoPlaybackException {
        if(this.shouldInitRenderer()) {
            this.drmSession = this.pendingDrmSession;
            String mimeType = this.format.sampleMimeType;
            MediaCrypto mediaCrypto = null;
            boolean drmSessionRequiresSecureDecoder = false;
            if(this.drmSession != null) {
                int decoderInfo = this.drmSession.getState();
                if(decoderInfo == 0) {
                    throw ExoPlaybackException.createForRenderer(this.drmSession.getError(), this.getIndex());
                }

                if(decoderInfo != 3 && decoderInfo != 4) {
                    return;
                }

                mediaCrypto = this.drmSession.getMediaCrypto().getWrappedMediaCrypto();
                drmSessionRequiresSecureDecoder = this.drmSession.requiresSecureDecoderComponent(mimeType);
            }

            MediaCodecInfo decoderInfo1 = null;

            try {
                decoderInfo1 = this.getDecoderInfo(this.mediaCodecSelector, this.format, drmSessionRequiresSecureDecoder);
                if(decoderInfo1 == null && drmSessionRequiresSecureDecoder) {
                    decoderInfo1 = this.getDecoderInfo(this.mediaCodecSelector, this.format, false);
                    if(decoderInfo1 != null) {
                        if (DEBUG) Log.w("MediaCodecRenderer", "Drm session requires secure decoder for " + mimeType + ", but " + "no secure decoder available. Trying to proceed with " + decoderInfo1.name + ".");
                    }
                }
            } catch (MediaCodecUtil.DecoderQueryException var11) {
                this.throwDecoderInitError(new MediaCodecRenderer.DecoderInitializationException(this.format, var11, drmSessionRequiresSecureDecoder, -49998));
            }

            if(decoderInfo1 == null) {
                this.throwDecoderInitError(new MediaCodecRenderer.DecoderInitializationException(this.format, (Throwable)null, drmSessionRequiresSecureDecoder, -49999));
            }

            String codecName = decoderInfo1.name;
            this.codecIsAdaptive = decoderInfo1.adaptive;
            this.codecNeedsDiscardToSpsWorkaround = codecNeedsDiscardToSpsWorkaround(codecName, this.format);
            this.codecNeedsAdaptationWorkaround = codecNeedsAdaptationWorkaround(codecName);

            this.codecHotswapDeadlineMs = this.getState() == 2?SystemClock.elapsedRealtime() + 1000L:-9223372036854775807L;
            this.outputIndex = -1;
            ++this.decoderCounters.decoderInitCount;

            clearInputSamplesQueue();
            inputBuffer = ByteBuffer.wrap(new byte[22048]);
            this.initialized = true;
        }
    }

    private void readFormat() throws ExoPlaybackException {
        int result = this.readSource(this.formatHolder, (DecoderInputBuffer)null);
        if(result == -5) {
            this.onInputFormatChanged(this.formatHolder.format);
        }

    }

    protected void flushRenderer() throws ExoPlaybackException {
        codecHotswapDeadlineMs = C.TIME_UNSET;
        outputIndex = C.INDEX_UNSET;
        waitingForKeys = false;
        shouldSkipOutputBuffer = false;
        decodeOnlyPresentationTimestamps.clear();
        codecNeedsAdaptationWorkaroundBuffer = false;
        shouldSkipAdaptationWorkaroundOutputBuffer = false;
        clearInputSamplesQueue();
        if (codecReinitializationState != REINITIALIZATION_STATE_NONE) {
            // We're already waiting to release and re-initialize the codec. Since we're now flushing,
            // there's no need to wait any longer.
            releaseRenderer();
            maybeInitRenderer();
        } else {
            codecReceivedBuffers = false;
        }
        if (codecReconfigured && format != null) {
            // Any reconfiguration data that we send shortly before the flush may be discarded. We
            // avoid this issue by sending reconfiguration data following every flush.
            codecReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
        }
    }

    private void processOutputFormat() {
    }

    private void processEndOfStream() throws ExoPlaybackException {
        if(this.codecReinitializationState == REINITIALIZATION_STATE_WAIT_END_OF_STREAM) {
            this.releaseRenderer();
            this.maybeInitRenderer();
        } else {
            this.outputStreamEnded = true;
            this.onOutputStreamEnded();
        }

    }

    protected void onProcessedOutputBuffer(long presentationTimeUs) {
    }

    private static MediaCodec.CryptoInfo getFrameworkCryptoInfo(DecoderInputBuffer buffer, int adaptiveReconfigurationBytes) {
        MediaCodec.CryptoInfo cryptoInfo = buffer.cryptoInfo.getFrameworkCryptoInfoV16();
        if(adaptiveReconfigurationBytes == 0) {
            return cryptoInfo;
        } else {
            if(cryptoInfo.numBytesOfClearData == null) {
                cryptoInfo.numBytesOfClearData = new int[1];
            }

            cryptoInfo.numBytesOfClearData[0] += adaptiveReconfigurationBytes;
            return cryptoInfo;
        }
    }

    private boolean shouldWaitForKeys(boolean bufferEncrypted) throws ExoPlaybackException {
        if(this.drmSession == null) {
            return false;
        } else {
            int drmSessionState = this.drmSession.getState();
            if(drmSessionState == 0) {
                throw ExoPlaybackException.createForRenderer(this.drmSession.getError(), this.getIndex());
            } else {
                return drmSessionState != 4 && (bufferEncrypted || !this.playClearSamplesWithoutKeys);
            }
        }
    }

    private boolean shouldSkipOutputBuffer(long presentationTimeUs) {
        int size = this.decodeOnlyPresentationTimestamps.size();

        for(int i = 0; i < size; ++i) {
            if(this.decodeOnlyPresentationTimestamps.get(i).longValue() == presentationTimeUs) {
                this.decodeOnlyPresentationTimestamps.remove(i);
                return true;
            }
        }

        return false;
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
    public synchronized boolean clearInputSamplesQueue() {
        dexmuedInputSamples.clear();
        return true;
    }

    private synchronized boolean feedInputBuffersQueue() throws ExoPlaybackException {
        if (!initialized || codecReinitializationState == REINITIALIZATION_STATE_WAIT_END_OF_STREAM
                || inputStreamEnded || dexmuedInputSamples.size() >= QUEUED_DEMUXED_INPUT_BUFFER_SIZE) {
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
            if (DEBUG) Log.d(TAG, "feedInputBuffersQueue: bufferTimeUs : " + bufferForRead.timeUs + ", queueSize = " + dexmuedInputSamples.size());

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
        } catch (MediaCodec.CryptoException e) {
            throw ExoPlaybackException.createForRenderer(e, getIndex());
        }

        if (waitingForData) {
            playerListener.onDataArrived();
            waitingForData = false;
        }

        return true;
    }

    public void render(long positionUs, long elapsedRealtimeUs) throws ExoPlaybackException {
        if (DEBUG) Log.d(TAG, this + ": positionUs = " + positionUs + ", elapsedRealtimeUs = "+ elapsedRealtimeUs);
        if(!this.outputStreamEnded) {
            if(this.format == null) {
                this.readFormat();
            }

            this.maybeInitRenderer();
            if (initialized) {
                TraceUtil.beginSection("drainAndFeed");
                while (feedInputBuffersQueue()) {}
                TraceUtil.endSection();
            } else if(this.format != null) {
                this.skipToKeyframeBefore(positionUs);
            }

            this.decoderCounters.ensureUpdated();
        }
    }

    protected void releaseRenderer() {
        if(this.codec != null) {
            this.codecHotswapDeadlineMs = -9223372036854775807L;
            this.outputIndex = -1;
            this.waitingForKeys = false;
            this.shouldSkipOutputBuffer = false;
            this.decodeOnlyPresentationTimestamps.clear();
            this.codecReconfigured = false;
            this.codecReceivedBuffers = false;
            this.codecIsAdaptive = false;
            this.codecNeedsDiscardToSpsWorkaround = false;
            this.codecNeedsAdaptationWorkaround = false;
            this.codecNeedsAdaptationWorkaroundBuffer = false;
            this.shouldSkipAdaptationWorkaroundOutputBuffer = false;
            this.codecReceivedEos = false;
            this.codecReconfigurationState = 0;
            this.codecReinitializationState = 0;
            ++this.decoderCounters.decoderReleaseCount;

        }
        initialized = false;
    }
    protected boolean canReconfigureCodec(MediaCodec codec, boolean codecIsAdaptive, Format oldFormat, Format newFormat) {
        return false;
    }

    protected long getDequeueOutputBufferTimeoutUs() {
        return 0L;
    }

    private final class AudioTrackListener implements AudioTrack.Listener {

        @Override
        public void onAudioSessionId(int audioSessionId) {
            eventDispatcher.audioSessionId(audioSessionId);
            GeckoHlsAudioRenderer.this.onAudioSessionId(audioSessionId);
        }

        @Override
        public void onPositionDiscontinuity() {
            onAudioTrackPositionDiscontinuity();
            // We are out of sync so allow currentPositionUs to jump backwards.
            GeckoHlsAudioRenderer.this.allowPositionDiscontinuity = true;
        }

        @Override
        public void onUnderrun(int bufferSize, long bufferSizeMs, long elapsedSinceLastFeedMs) {
            eventDispatcher.audioTrackUnderrun(bufferSize, bufferSizeMs, elapsedSinceLastFeedMs);
            onAudioTrackUnderrun(bufferSize, bufferSizeMs, elapsedSinceLastFeedMs);
        }

    }
}


