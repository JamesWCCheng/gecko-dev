/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.os.Handler;
import android.util.Log;

import com.google.android.exoplayer2.BaseRenderer;
import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.FormatHolder;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;

import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

// TODO: Extract the identical member function or data from Video/Audio Renderer
public abstract class GeckoHlsRendererBase extends BaseRenderer {
    protected static final int QUEUED_INPUT_SAMPLE_SIZE = 100;
    protected final MediaCodecSelector mediaCodecSelector;
    protected final FormatHolder formatHolder;
    protected boolean DEBUG;
    protected String LOGTAG;
    protected GeckoHlsPlayer.ComponentListener playerListener;
    protected ConcurrentLinkedQueue<GeckoHlsSample> dexmuedInputSamples;

    protected ByteBuffer inputBuffer = null;
    protected Format format = null;
    protected boolean initialized = false;
    protected boolean waitingForData = false;
    protected boolean inputStreamEnded = false;
    protected long firstSampleStartTime = 0;

    protected abstract void maybeInitRenderer();
    protected abstract void resetRenderer();
    protected abstract boolean feedInputBuffersQueue();
    protected abstract boolean clearInputSamplesQueue();
    protected abstract void onInputFormatChanged(Format newFormat);

    protected void assertTrue(boolean condition) {
        if (DEBUG && !condition) {
            throw new AssertionError("Expected condition to be true");
        }
    }

    public GeckoHlsRendererBase(int trackType, MediaCodecSelector selector,
                                GeckoHlsPlayer.ComponentListener eventListener) {
        super(trackType);
        assertTrue(selector != null);
        playerListener = eventListener;
        mediaCodecSelector = selector;
        formatHolder = new FormatHolder();
        initialized = false;
        waitingForData = false;
        dexmuedInputSamples = new ConcurrentLinkedQueue<>();
    }

    public long getFirstSamplePTS() { return firstSampleStartTime; }

    public ConcurrentLinkedQueue<GeckoHlsSample> getQueuedSamples(int number) {
        ConcurrentLinkedQueue<GeckoHlsSample> samples =
            new ConcurrentLinkedQueue<GeckoHlsSample>();

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

    private void readFormat() {
        int result = readSource(formatHolder, null);
        if (result == C.RESULT_FORMAT_READ) {
            onInputFormatChanged(formatHolder.format);
        }
    }

    @Override
    protected void onEnabled(boolean joining) {
        // Do nothing.
    }

    @Override
    protected void onDisabled() {
        format = null;
        resetRenderer();
    }

    @Override
    public boolean isReady() {
        return format != null;
    }

    @Override
    public boolean isEnded() {
        return inputStreamEnded;
    }

    @Override
    protected void onPositionReset(long positionUs, boolean joining) {
        if (DEBUG) Log.d(LOGTAG, "onPositionReset : positionUs = " + positionUs);
        inputStreamEnded = false;
        if (initialized) {
            clearInputSamplesQueue();
        }
    }

    /*
     * This is called by ExoPlayerImplInternal.java.
     * ExoPlayer checks the status of renderer, i.e. isReady() / isEnded(), and
     * calls renderer.render by passing its wall clock time.
     */
    @Override
    public void render(long positionUs, long elapsedRealtimeUs) {
        if (DEBUG) Log.d(LOGTAG, "positionUs = " + positionUs +
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
}
