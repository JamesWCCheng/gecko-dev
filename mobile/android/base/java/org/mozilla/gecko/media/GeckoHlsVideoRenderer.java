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
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.mediacodec.MediaCodecInfo;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.mediacodec.MediaCodecUtil;
import com.google.android.exoplayer2.RendererCapabilities;
import com.google.android.exoplayer2.util.MimeTypes;
import com.google.android.exoplayer2.video.VideoRendererEventListener;

import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.mozilla.gecko.AppConstants.Versions;

public class GeckoHlsVideoRenderer extends GeckoHlsRendererBase {
    private final VideoRendererEventListener.EventDispatcher eventDispatcher;

    private Format[] streamFormats;
    private CodecMaxValues codecMaxValues;
    private ConcurrentLinkedQueue<GeckoHlsSample> dexmuedNoDurationSamples;
    private long nextKeyFrameTime;

    public GeckoHlsVideoRenderer(MediaCodecSelector selector,
                                 Handler eventHandler,
                                 VideoRendererEventListener eventListener) {
        super(C.TRACK_TYPE_VIDEO, selector, (GeckoHlsPlayer.ComponentListener) eventListener);
        assertTrue(Versions.feature16Plus);
        LOGTAG = getClass().getSimpleName();
        DEBUG = false;

        eventDispatcher = new VideoRendererEventListener.EventDispatcher(eventHandler, eventListener);
        nextKeyFrameTime = 0;
        dexmuedNoDurationSamples = new ConcurrentLinkedQueue<>();
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
                    if (DEBUG) Log.d(LOGTAG, "FalseCheck [legacyFrameSize, " +
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
        // Calculate maximum size which might be used for target format.
        codecMaxValues = getCodecMaxValues(format, streamFormats);
        // Create a buffer with maximal size for reading source.
        inputBuffer = ByteBuffer.wrap(new byte[codecMaxValues.inputSize]);
        initialized = true;
    }

    @Override
    protected void resetRenderer() {
        clearInputSamplesQueue();
        nextKeyFrameTime = 0;
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
            Log.d(LOGTAG, "Now we're at the End Of Stream.");
        }

        bufferForRead.flip();

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
        calculatDuration(sample);

        if (waitingForData && dexmuedInputSamples.size() > 0) {
            playerListener.onDataArrived();
            waitingForData = false;
        }
        return true;
    }

    @Override
    protected boolean clearInputSamplesQueue() {
        dexmuedInputSamples.clear();
        dexmuedNoDurationSamples.clear();
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

    private void calculatDuration(GeckoHlsSample inputSample) {
        /*
         * NOTE :
         * Since we customized renderer as a demuxer. Here we're not able to
         * obtain duration from the DecoderInputBuffer as there's no duration inside.
         * So we calcualte it by referring to nearby samples' timestamp.
         * A temporary queue |dexmuedNoDurationSamples| is used to queue demuxed
         * samples which have no duration information at first.
         * Considering there're 6 demuxed samples in the queue already,
         * e.g. |-2|-1|0|1|2|3|...
         * Once a new demuxed sample A (7th) is put into the temporary queue,
         * e.g. |-2|-1|0|1|2|3|A|...
         * we are able to calculate the correct duration for sample 0 by finding
         * the most closest but greater pts than sample 0 among these 6 samples,
         * here, let's say sample -2 ~ 3.
         */
        if (inputSample != null) {
            dexmuedNoDurationSamples.offer(inputSample);
        }
        int sizeOfNoDura = dexmuedNoDurationSamples.size();
        int range = sizeOfNoDura >= 7 ? 7 : sizeOfNoDura;
        GeckoHlsSample[] inputArray =
            dexmuedNoDurationSamples.toArray(new GeckoHlsSample[sizeOfNoDura]);
        if (range >= 7 && !inputStreamEnded) {
            // Calculate the first 'range' elements.
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
            GeckoHlsSample toQueue = dexmuedNoDurationSamples.poll();
            if (DEBUG) Log.d(LOGTAG, "Demuxed sample PTS : " +
                             toQueue.info.presentationTimeUs +
                             ", duration : " + toQueue.duration);
            dexmuedInputSamples.offer(toQueue);
        } else if (inputStreamEnded) {
            for (int i = 0; i < sizeOfNoDura; i++) {
                for (int j = -2; j < 4; j++) {
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
            for (sample = dexmuedNoDurationSamples.poll(); sample != null; sample = dexmuedNoDurationSamples.poll()) {
                if (sample.duration == Long.MAX_VALUE) {
                    sample.duration = tmpDuration;
                    if (DEBUG) Log.d(LOGTAG, "Adjust the PTS of the last sample to " +
                                     sample.duration + " (us)");
                }
                tmpDuration = sample.duration;
                if (DEBUG) Log.d(LOGTAG, "last loop to offer samples - PTS : " +
                                 sample.info.presentationTimeUs + ", Duration : " +
                                 sample.duration);
                dexmuedInputSamples.offer(sample);
            }
        }
    }

    public long getNextKeyFrameTime() {
        for (GeckoHlsSample sample : dexmuedInputSamples) {
            if ((sample.info.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0) {
                return sample.info.presentationTimeUs;
            }
        }
        return 0;
    }

    @Override
    protected void onStreamChanged(Format[] formats) {
        streamFormats = formats;
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
}
