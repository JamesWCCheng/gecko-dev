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
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.mediacodec.MediaCodecInfo;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.mediacodec.MediaCodecUtil;
import com.google.android.exoplayer2.RendererCapabilities;
import com.google.android.exoplayer2.util.MimeTypes;
import com.google.android.exoplayer2.video.VideoRendererEventListener;

import java.util.Arrays;
import java.util.ArrayList;
import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.mozilla.gecko.AppConstants.Versions;

public class GeckoHlsVideoRenderer extends GeckoHlsRendererBase {
    private final VideoRendererEventListener.EventDispatcher eventDispatcher;

    /*
     * By configuring these states, initialization data is provided for
     * ExoPlayer's HlsMediaSouck to parse HLS bitstream and then provide samples
     * starting with an Access Unit Delimiter including SPS/PPS for TS,
     * and provide samples starting with an AUD without SPS/PPS for FMP4.
     */
    private static final int RECONFIGURATION_STATE_NONE = 0;
    private static final int RECONFIGURATION_STATE_WRITE_PENDING = 1;
    private static final int RECONFIGURATION_STATE_QUEUE_PENDING = 2;
    private boolean rendererReconfigured;
    private int rendererReconfigurationState = RECONFIGURATION_STATE_NONE;

    // A list of the formats which may be included in the bitstream.
    private Format[] streamFormats;
    // The max width/height/inputBufferSize for specific codec format.
    private CodecMaxValues codecMaxValues;
    // A temporary queue for samples whose duration is not calculated yet.
    private ConcurrentLinkedQueue<GeckoHlsSample> demuxedNoDurationSamples =
        new ConcurrentLinkedQueue<>();
    private long nextKeyFrameTime = 0;

    // Contain CSD-0(SPS)/CSD-1(PPS) information (in AnnexB format) for
    // prepending each keyframe. When video format changes, this information
    // changes accordingly.
    private byte[] csdInfo = null;

    public GeckoHlsVideoRenderer(MediaCodecSelector selector,
                                 Handler eventHandler,
                                 VideoRendererEventListener eventListener) {
        super(C.TRACK_TYPE_VIDEO, selector, (GeckoHlsPlayer.ComponentListener) eventListener);
        assertTrue(Versions.feature16Plus);
        LOGTAG = getClass().getSimpleName();
        DEBUG = true;
        eventDispatcher = new VideoRendererEventListener.EventDispatcher(eventHandler, eventListener);
    }

    @Override
    public final int supportsMixedMimeTypeAdaptation() {
        return ADAPTIVE_NOT_SEAMLESS;
    }

    @Override
    public final int supportsFormat(Format format) {
        final String mimeType = format.sampleMimeType;
        if (!MimeTypes.isVideo(mimeType)) {
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

        boolean decoderCapable = decoderInfo.isCodecSupported(format.codecs);
        if (decoderCapable && format.width > 0 && format.height > 0) {
            if (Versions.preLollipop) {
                try {
                    decoderCapable = format.width * format.height <= MediaCodecUtil.maxH264DecodableFrameSize();
                } catch (MediaCodecUtil.DecoderQueryException e) {
                    Log.e(LOGTAG, e.getMessage());
                }
                if (!decoderCapable) {
                    if (DEBUG) Log.d(LOGTAG, "Check [legacyFrameSize, " +
                                     format.width + "x" + format.height + "]");
                }
            } else {
                decoderCapable =
                    decoderInfo.isVideoSizeAndRateSupportedV21(format.width,
                                                               format.height,
                                                               format.frameRate);
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

    @Override
    protected final void maybeInitRenderer() {
        if (initialized || format == null) {
            return;
        }
        if (DEBUG) Log.d(LOGTAG, "Initializing ... ");
        // Calculate maximum size which might be used for target format.
        codecMaxValues = getCodecMaxValues(format, streamFormats);
        // Create a buffer with maximal size for reading source.
        inputBuffer = ByteBuffer.wrap(new byte[codecMaxValues.inputSize]);
        initialized = true;
    }

    @Override
    protected void resetRenderer() {
        if (DEBUG) Log.d(LOGTAG, "[resetRenderer] initialized = " + initialized);
        if (initialized) {
            rendererReconfigured = false;
            rendererReconfigurationState = RECONFIGURATION_STATE_NONE;
            nextKeyFrameTime = 0;
            inputBuffer = null;
            csdInfo = null;
            initialized = false;
        }
    }

    /*
     * The place we get demuxed data from HlsMediaSource(ExoPlayer).
     * The data will then be converted to GeckoHlsSample and deliver to
     * GeckoHlsDemuxerWrapper for further use.
     */
    @Override
    protected synchronized boolean feedInputBuffersQueue() {
        if (!initialized || inputStreamEnded || isQueuedEnoughData()) {
            // Need to reinitialize the renderer or the input stream has ended
            // or we just reached the maximum queue size.
            return false;
        }

        DecoderInputBuffer bufferForRead =
            new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DISABLED);
        bufferForRead.data = inputBuffer;
        bufferForRead.clear();

        // For adaptive reconfiguration OMX decoders expect all reconfiguration
        // data to be supplied at the start of the buffer that also contains
        // the first frame in the new format.
        if (rendererReconfigurationState == RECONFIGURATION_STATE_WRITE_PENDING) {
            if (DEBUG) Log.d(LOGTAG, "[feedInput][RECONFIGURATION_STATE_WRITE_PENDING] put initialization data");
            for (int i = 0; i < format.initializationData.size(); i++) {
                byte[] data = format.initializationData.get(i);
                bufferForRead.data.put(data);
            }
            rendererReconfigurationState = RECONFIGURATION_STATE_QUEUE_PENDING;
        }

        // Read data from HlsMediaSource
        int result = readSource(formatHolder, bufferForRead);
        if (result == C.RESULT_NOTHING_READ) {
            return false;
        }
        if (result == C.RESULT_FORMAT_READ) {
            if (rendererReconfigurationState == RECONFIGURATION_STATE_QUEUE_PENDING) {
                if (DEBUG) Log.d(LOGTAG, "[feedInput][RECONFIGURATION_STATE_QUEUE_PENDING] 2 formats in a row.");
                // We received two formats in a row. Clear the current buffer of any reconfiguration data
                // associated with the first format.
                bufferForRead.clear();
                rendererReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
            }
            onInputFormatChanged(formatHolder.format);
            return true;
        }

        // We've read a buffer.
        if (bufferForRead.isEndOfStream()) {
            if (DEBUG) Log.d(LOGTAG, "Now we're at the End Of Stream.");
            if (rendererReconfigurationState == RECONFIGURATION_STATE_QUEUE_PENDING) {
                if (DEBUG) Log.d(LOGTAG, "[feedInput][RECONFIGURATION_STATE_QUEUE_PENDING] isEndOfStream.");
                // We received a new format immediately before the end of the stream. We need to clear
                // the corresponding reconfiguration data from the current buffer, but re-write it into
                // a subsequent buffer if there are any (e.g. if the user seeks backwards).
                bufferForRead.clear();
                rendererReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
            }
            inputStreamEnded = true;
            GeckoHlsSample sample = GeckoHlsSample.EOS;
            calculatDuration(sample);
            return false;
        }

        bufferForRead.flip();

        int csdInfoSize = csdInfo != null ? csdInfo.length : 0;
        int dataSize = bufferForRead.data.limit();
        int size = bufferForRead.isKeyFrame() ? csdInfoSize + dataSize : dataSize;
        byte[] realData = new byte[size];
        if (bufferForRead.isKeyFrame()) {
            // Prepend the CSD information to the sample if it's a key frame.
            System.arraycopy(csdInfo, 0, realData, 0, csdInfoSize);
            bufferForRead.data.get(realData, csdInfoSize, dataSize);
        } else {
            bufferForRead.data.get(realData, 0, dataSize);
        }
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

        assertTrue(formats.size() >= 0);
        // We add a new format in the list once format changes, so the formatIndex
        // should indicate to the last(latest) format.
        sample.formatIndex = formats.size()-1;
        // There's no duration information from the ExoPlayer's sample, we need
        // to calculate it.
        calculatDuration(sample);
        rendererReconfigurationState = RECONFIGURATION_STATE_NONE;

        if (waitingForData && isQueuedEnoughData()) {
            if (DEBUG) Log.d(LOGTAG, "onDataArrived");
            playerListener.onDataArrived();
            waitingForData = false;
        }
        return true;
    }

    @Override
    protected void onPositionReset(long positionUs, boolean joining) {
        super.onPositionReset(positionUs, joining);
        if (initialized && rendererReconfigured && format != null) {
            if (DEBUG) Log.d(LOGTAG, "[onPositionReset] RECONFIGURATION_STATE_WRITE_PENDING");
            // Any reconfiguration data that we put shortly before the reset
            // may be invalid. We avoid this issue by sending reconfiguration
            // data following every position reset.
            rendererReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
        }
    }

    @Override
    protected boolean clearInputSamplesQueue() {
        if (DEBUG) Log.d(LOGTAG, "clearInputSamplesQueue");
        demuxedInputSamples.clear();
        demuxedNoDurationSamples.clear();
        return true;
    }

    @Override
    protected void onInputFormatChanged(Format newFormat) {
        Format oldFormat = format;
        format = newFormat;
        if (DEBUG) Log.d(LOGTAG, "[onInputFormatChanged] old : " + oldFormat +
                         " => new : " + format);

        handleDrmInitChanged(oldFormat, newFormat);

        if (initialized && canReconfigure(oldFormat, format)) {
            if (DEBUG) Log.d(LOGTAG, "[onInputFormatChanged] starting reconfiguration !");
            rendererReconfigured = true;
            rendererReconfigurationState = RECONFIGURATION_STATE_WRITE_PENDING;
        } else {
            // The begining of demuxing, 1st time format change.
            resetRenderer();
            maybeInitRenderer();
        }
        formats.add(format);
        updateCSDInfo(format);
        eventDispatcher.inputFormatChanged(newFormat);
    }

    protected boolean canReconfigure(Format oldFormat, Format newFormat) {
        boolean canReconfig = areAdaptationCompatible(oldFormat, newFormat)
          && newFormat.width <= codecMaxValues.width && newFormat.height <= codecMaxValues.height
          && newFormat.maxInputSize <= codecMaxValues.inputSize;
        if (DEBUG) Log.d(LOGTAG, "[canReconfigure] : " + canReconfig);
        return canReconfig;
    }

    private void calculatDuration(GeckoHlsSample inputSample) {
        /*
         * NOTE :
         * Since we customized renderer as a demuxer. Here we're not able to
         * obtain duration from the DecoderInputBuffer as there's no duration inside.
         * So we calcualte it by referring to nearby samples' timestamp.
         * A temporary queue |demuxedNoDurationSamples| is used to queue demuxed
         * samples from HlsMediaSource which have no duration information at first.
         * Considering there're 9 demuxed samples in the _no duration_ queue already,
         * e.g. |-2|-1|0|1|2|3|4|5|6|...
         * Once a new demuxed(No duration) sample A (10th) is put into the
         * temporary queue,
         * e.g. |-2|-1|0|1|2|3|4|5|6|A|...
         * we are able to calculate the correct duration for sample 0 by finding
         * the most closest but greater pts than sample 0 among these 9 samples,
         * here, let's say sample -2 to 6.
         */
        if (inputSample != null) {
            demuxedNoDurationSamples.offer(inputSample);
        }
        int sizeOfNoDura = demuxedNoDurationSamples.size();
        // A calculation window we've ever found suitable for both HLS TS & FMP4.
        int range = sizeOfNoDura >= 10 ? 10 : sizeOfNoDura;
        GeckoHlsSample[] inputArray =
            demuxedNoDurationSamples.toArray(new GeckoHlsSample[sizeOfNoDura]);
        if (range >= 10 && !inputStreamEnded) {
            // Calculate the first 'range' elements.
            for (int i = 0; i < range; i++) {
                // Comparing among samples in the window.
                for (int j = -2; j < 7; j++) {
                    if (i+j >= 0 &&
                        i+j < range &&
                        inputArray[i+j].info.presentationTimeUs > inputArray[i].info.presentationTimeUs) {
                        inputArray[i].duration =
                            Math.min(inputArray[i].duration,
                                     inputArray[i+j].info.presentationTimeUs - inputArray[i].info.presentationTimeUs);
                    }
                }
            }
            GeckoHlsSample toQueue = demuxedNoDurationSamples.poll();
            demuxedInputSamples.offer(toQueue);
            if (DEBUG) Log.d(LOGTAG, "Demuxed sample PTS : " +
                             toQueue.info.presentationTimeUs + ", duration :" +
                             toQueue.duration + ", isKeyFrame(" +
                             toQueue.isKeyFrame() + ", formatIndex(" +
                             toQueue.formatIndex + "), queue size : " +
                             demuxedInputSamples.size() + ", NoDuQueue size : " +
                             demuxedNoDurationSamples.size());
        } else if (inputStreamEnded) {
            for (int i = 0; i < sizeOfNoDura; i++) {
                for (int j = -2; j < 7; j++) {
                    if (i+j >= 0 && i+j < sizeOfNoDura &&
                        inputArray[i+j].info.presentationTimeUs > inputArray[i].info.presentationTimeUs) {
                        inputArray[i].duration =
                            Math.min(inputArray[i].duration,
                                     inputArray[i+j].info.presentationTimeUs - inputArray[i].info.presentationTimeUs);
                    }
                }
            }
            // TODO : We're not able to calculate the duration for the last sample.
            //        A workaround here is to assign a close duration to it.
            long tmpDuration = 33333;
            GeckoHlsSample sample = null;
            for (sample = demuxedNoDurationSamples.poll(); sample != null; sample = demuxedNoDurationSamples.poll()) {
                if (sample.duration == Long.MAX_VALUE) {
                    sample.duration = tmpDuration;
                    if (DEBUG) Log.d(LOGTAG, "Adjust the PTS of the last sample to " +
                                     sample.duration + " (us)");
                }
                tmpDuration = sample.duration;
                if (DEBUG) Log.d(LOGTAG, "last loop to offer samples - PTS : " +
                                 sample.info.presentationTimeUs + ", Duration : " +
                                 sample.duration + ", isEOS : " + sample.isEOS());
                demuxedInputSamples.offer(sample);
            }
        }
    }

    // Return the time of first keyframe sample in the queue.
    // If there's no sample in the queue, return the last one we've found.
    public long getNextKeyFrameTime() {
        for (GeckoHlsSample sample : demuxedInputSamples) {
            if ((sample.info.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0) {
                nextKeyFrameTime = sample.info.presentationTimeUs;
                break;
            }
        }
        return nextKeyFrameTime;
    }

    @Override
    protected void onStreamChanged(Format[] formats) {
        streamFormats = formats;
    }

    protected void handleDrmInitChanged(Format oldFormat, Format newFormat) {
        Object oldDrmInit = oldFormat == null ? null : oldFormat.drmInitData;
        Object newDrnInit = newFormat.drmInitData;

//      TODO: Notify MFR if the content is encrypted or not.
        if (newDrnInit != oldDrmInit) {
            if (newDrnInit != null) {
            } else {
            }
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

    private void updateCSDInfo(Format format) {
        int size= 0;
        for (int i = 0; i < format.initializationData.size(); i++) {
            size += format.initializationData.get(i).length;
        }
        int startPos = 0;
        csdInfo = new byte[size];
        for (int i = 0; i < format.initializationData.size(); i++) {
            byte[] data = format.initializationData.get(i);
            System.arraycopy(data, 0, csdInfo, startPos, data.length);
            startPos = data.length;
        }
        if (DEBUG) Log.d(LOGTAG, "CSDInfo [" +
                         Utils.bytesToHex(csdInfo) + "]");
    }
}
