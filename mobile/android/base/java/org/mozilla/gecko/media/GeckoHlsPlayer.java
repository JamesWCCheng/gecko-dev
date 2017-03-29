/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.text.TextUtils;
import android.util.Log;
import android.view.Surface;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.ExoPlaybackException;
import com.google.android.exoplayer2.ExoPlayer;
import com.google.android.exoplayer2.ExoPlayerFactory;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.Timeline;
import com.google.android.exoplayer2.audio.AudioCapabilities;
import com.google.android.exoplayer2.audio.AudioRendererEventListener;
import com.google.android.exoplayer2.decoder.DecoderCounters;
import com.google.android.exoplayer2.decoder.DecoderInputBuffer;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.metadata.Metadata;
import com.google.android.exoplayer2.metadata.MetadataRenderer;
import com.google.android.exoplayer2.source.MediaSource;
import com.google.android.exoplayer2.source.TrackGroup;
import com.google.android.exoplayer2.source.TrackGroupArray;
import com.google.android.exoplayer2.source.hls.HlsMediaSource;
import com.google.android.exoplayer2.trackselection.AdaptiveVideoTrackSelection;
import com.google.android.exoplayer2.trackselection.DefaultTrackSelector;
import com.google.android.exoplayer2.trackselection.TrackSelection;
import com.google.android.exoplayer2.trackselection.TrackSelectionArray;
import com.google.android.exoplayer2.upstream.DataSource;
import com.google.android.exoplayer2.upstream.DefaultBandwidthMeter;
import com.google.android.exoplayer2.upstream.DefaultDataSourceFactory;
import com.google.android.exoplayer2.upstream.DefaultHttpDataSourceFactory;
import com.google.android.exoplayer2.upstream.HttpDataSource;
import com.google.android.exoplayer2.util.Util;
import com.google.android.exoplayer2.video.VideoRendererEventListener;

import org.mozilla.gecko.GeckoAppShell;

import java.util.ArrayList;
import java.util.concurrent.ConcurrentLinkedQueue;

public class GeckoHlsPlayer implements ExoPlayer.EventListener {
    private static boolean DEBUG = true;
    private static final String TAG = "GeckoHlsPlayer";
    private static final DefaultBandwidthMeter BANDWIDTH_METER = new DefaultBandwidthMeter();
    private static final int MAX_TIMELINE_ITEM_LINES = 3;
    private DataSource.Factory mediaDataSourceFactory;

    private long durationUs;
    private Timeline.Period period;
    private Timeline.Window window;
    protected String userAgent;
    private Handler mainHandler;
    public final String extension = null;
    public static final String EXTENSION_EXTRA = "extension";
    private EventLogger eventLogger;

    private ExoPlayer player;

    private DefaultTrackSelector trackSelector;
    private boolean isTimelineStatic = false;
    private GeckoHlsRendererBase[] renderers;
    private MediaSource mediaSource;

    private ComponentListener componentListener;

    private boolean trackGroupUpdated = false;
    private long bufferedPosition;
    private Format audioFormat;
    private Format videoFormat;
    private int numVideoTracks = 0;
    private int numAudioTracks = 0;
    private GeckoHlsVideoRenderer vRenderer = null;
    private GeckoHlsAudioRenderer aRenderer = null;
    private boolean enableV = true;
    private boolean enableA = true;

    private boolean isInitDone = false;
    private GeckoHlsDemuxerWrapper.Callbacks nativeCallbacks;

    public enum Track_Type {
        TRACK_UNDEFINED,
        TRACK_AUDIO,
        TRACK_VIDEO,
        TRACK_TEXT,
    }

    private static void assertTrue(boolean condition) {
      if (DEBUG && !condition) {
        throw new AssertionError("Expected condition to be true");
      }
    }

    public final class ComponentListener implements VideoRendererEventListener,
            AudioRendererEventListener, MetadataRenderer.Output {

        // General purpose implementation
        public void onDataArrived() {
            assertTrue(nativeCallbacks != null);
            nativeCallbacks.onDataArrived();
        }

        // VideoRendererEventListener implementation

        @Override
        public void onVideoEnabled(DecoderCounters counters) {
//            videoDecoderCounters = counters;
        }

        @Override
        public void onVideoDecoderInitialized(String decoderName, long initializedTimestampMs,
                                              long initializationDurationMs) {
        }

        @Override
        public void onVideoInputFormatChanged(Format format) {
            assertTrue(nativeCallbacks != null);

            videoFormat = format;
            if (DEBUG) Log.d( TAG, "onVideoInputFormatChanged [" + videoFormat + "]");
            if (DEBUG) Log.d( TAG, "sMimetype [" + videoFormat.sampleMimeType + "], ContainerMIMETYPE" + videoFormat.containerMimeType);
            nativeCallbacks.onVideoFormatChanged();
        }

        @Override
        public void onDroppedFrames(int count, long elapsed) {
        }

        @Override
        public void onVideoSizeChanged(int width, int height, int unappliedRotationDegrees,
                                       float pixelWidthHeightRatio) {
//            if (videoListener != null) {
//                videoListener.onVideoSizeChanged(width, height, unappliedRotationDegrees,
//                        pixelWidthHeightRatio);
//            }
        }

        @Override
        public void onRenderedFirstFrame(Surface surface) {
//            if (videoListener != null && SimpleExoPlayer.this.surface == surface) {
//                videoListener.onRenderedFirstFrame();
//            }
        }

        @Override
        public void onVideoDisabled(DecoderCounters counters) {
            videoFormat = null;
//            videoDecoderCounters = null;
        }

        // AudioRendererEventListener implementation

        @Override
        public void onAudioEnabled(DecoderCounters counters) {
//            audioDecoderCounters = counters;
        }

        @Override
        public void onAudioSessionId(int sessionId) {
//            audioSessionId = sessionId;
        }

        @Override
        public void onAudioDecoderInitialized(String decoderName, long initializedTimestampMs,
                                              long initializationDurationMs) {
        }

        @Override
        public void onAudioInputFormatChanged(Format format) {
            assertTrue(nativeCallbacks != null);

            audioFormat = format;
            if (DEBUG) Log.d(TAG, "onAudioInputFormatChanged [" + audioFormat + "]");
            nativeCallbacks.onAudioFormatChanged();
        }

        @Override
        public void onAudioTrackUnderrun(int bufferSize, long bufferSizeMs,
                                         long elapsedSinceLastFeedMs) {
        }

        @Override
        public void onAudioDisabled(DecoderCounters counters) {
            audioFormat = null;
//            audioDecoderCounters = null;
//            audioSessionId = AudioTrack.SESSION_ID_NOT_SET;
        }

        // MetadataRenderer.Output implementation

        @Override
        public void onMetadata(Metadata metadata) {
            if (DEBUG) Log.d(TAG, "onMetadata [" + metadata + "]");
//            if (metadataOutput != null) {
//                metadataOutput.onMetadata(metadata);
//            }
        }
    }

    public DataSource.Factory buildDataSourceFactory(Context va, DefaultBandwidthMeter bandwidthMeter) {
        return new DefaultDataSourceFactory(va, bandwidthMeter,
                buildHttpDataSourceFactory(bandwidthMeter));
    }

    public HttpDataSource.Factory buildHttpDataSourceFactory(DefaultBandwidthMeter bandwidthMeter) {
        return new DefaultHttpDataSourceFactory(userAgent, bandwidthMeter);
    }

    private MediaSource buildMediaSource(Uri uri, String overrideExtension) {
        if (DEBUG) Log.d(TAG, "buildMediaSource uri[" + uri + "]" + ", overridedExt[" + overrideExtension + "]");
        int type = Util.inferContentType(!TextUtils.isEmpty(overrideExtension) ? "." + overrideExtension
                : uri.getLastPathSegment());
        switch (type) {
            case C.TYPE_HLS:
                return new HlsMediaSource(uri, mediaDataSourceFactory, mainHandler, null);
            default: {
                throw new IllegalStateException("Unsupported type: " + type);
            }
        }
    }

    GeckoHlsPlayer() {
        if (DEBUG) Log.d(TAG, "GeckoHlsPlayer construct");
    }

    void addDemuxerWrapperCallbackListener(GeckoHlsDemuxerWrapper.Callbacks callback) {
        if (DEBUG) Log.d(TAG, "GeckoHlsPlayer addDemuxerWrapperCallbackListener");
        nativeCallbacks = callback;
    }

    synchronized void init(String url) {
        if (DEBUG) Log.d(TAG, "GeckoHlsPlayer init");
        if (isInitDone == true) {
            return;
        }
        Context ctx = GeckoAppShell.getApplicationContext();
        componentListener = new ComponentListener();
        mainHandler = new Handler();

        durationUs = 0;
        window = new Timeline.Window();
        period = new Timeline.Period();
        TrackSelection.Factory videoTrackSelectionFactory =
                new AdaptiveVideoTrackSelection.Factory(BANDWIDTH_METER);
        trackSelector = new DefaultTrackSelector(videoTrackSelectionFactory);

        ArrayList<GeckoHlsRendererBase> renderersList = new ArrayList<>();
        vRenderer = new GeckoHlsVideoRenderer(MediaCodecSelector.DEFAULT,
                                              mainHandler,
                                              componentListener);
        renderersList.add(vRenderer);
        aRenderer = new GeckoHlsAudioRenderer(MediaCodecSelector.DEFAULT,
                mainHandler,
                componentListener,
                (AudioCapabilities) null);
        renderersList.add(aRenderer);
        renderers = renderersList.toArray(new GeckoHlsRendererBase[renderersList.size()]);

        player = ExoPlayerFactory.newInstance(renderers, trackSelector);
        player.addListener(this);

        eventLogger = new EventLogger(trackSelector);
        player.addListener(eventLogger);

        Uri[] uris = new Uri[]{Uri.parse(url)};
        String[] extensions = new String[]{extension};
        userAgent = Util.getUserAgent(ctx, "RemoteDecoder");
        mediaDataSourceFactory = buildDataSourceFactory(ctx, null);

        MediaSource[] mediaSources = new MediaSource[1];
        mediaSources[0] = buildMediaSource(uris[0], extensions[0]);
        mediaSource = mediaSources[0];

        player.prepare(mediaSource);
        isInitDone = true;
    }

    synchronized void deinit() {
        if (DEBUG) Log.d(TAG, "GeckoHlsPlayer deinit");
        nativeCallbacks = null;
        isInitDone = false;
    }

    @Override
    public void onLoadingChanged(boolean isLoading) {
        if (DEBUG) Log.d(TAG, "loading [" + isLoading + "]");
    }

    @Override
    public void onPlayerStateChanged(boolean playWhenReady, int state) {
        if (DEBUG) Log.d(TAG, "state [" + playWhenReady + ", "+ getStateString(state) + "]");
        if (state == ExoPlayer.STATE_READY) {
            player.setPlayWhenReady(true);
        }
    }

    @Override
    public void onPositionDiscontinuity() {
        if (DEBUG) Log.d(TAG, "positionDiscontinuity");
    }

    @Override
    public void onPlayerError(ExoPlaybackException e) {
        if (DEBUG) Log.e(TAG, "playerFailed" , e);
    }

    @Override
    public void onTracksChanged(TrackGroupArray ignored, TrackSelectionArray trackSelections) {
        if (DEBUG) Log.e(TAG, "onTracksChanged : TGA[" + ignored + "], TSA[" + trackSelections + "]");
        // TODO : need to synchronized with getNumberTracks()
        if (trackGroupUpdated) {
            numVideoTracks = 0;
            numAudioTracks = 0;
        }
        for (int j = 0; j < ignored.length; j++) {
            TrackGroup tg = ignored.get(j);
            for (int i = 0; i < tg.length; i++) {
                Format fmt = tg.getFormat(i);
                if (fmt.sampleMimeType != null) {
                    if (enableV && fmt.sampleMimeType.startsWith(new String("video"))) {
                        numVideoTracks++;
                    } else if (enableA && fmt.sampleMimeType.startsWith(new String("audio"))) {
                        numAudioTracks++;
                    }
                }
            }
        }
        trackGroupUpdated = true;
        nativeCallbacks.onTrackInfoChanged(numAudioTracks > 0? true : false,
                                           numVideoTracks > 0? true : false );
    }

    @Override
    public void onTimelineChanged(Timeline timeline, Object manifest) {
        isTimelineStatic = !timeline.isEmpty()
                && !timeline.getWindow(timeline.getWindowCount() - 1, window).isDynamic;

        int periodCount = timeline.getPeriodCount();
        int windowCount = timeline.getWindowCount();
        if (DEBUG) Log.d(TAG, "sourceInfo [periodCount=" + periodCount + ", windowCount=" + windowCount);
        for (int i = 0; i < Math.min(periodCount, MAX_TIMELINE_ITEM_LINES); i++) {
          timeline.getPeriod(i, period);
          if (durationUs < period.getDurationUs()) {
              durationUs = period.getDurationUs();
          }
        }
        for (int i = 0; i < Math.min(windowCount, MAX_TIMELINE_ITEM_LINES); i++) {
          timeline.getWindow(i, window);
          if (durationUs < window.getDurationUs()) {
              durationUs = window.getDurationUs();
          }
        }
        if (DEBUG) Log.d(TAG, "Media duration = " + durationUs + "(us)");
    }

    private static String getStateString(int state) {
        switch (state) {
            case ExoPlayer.STATE_BUFFERING:
                return "B";
            case ExoPlayer.STATE_ENDED:
                return "E";
            case ExoPlayer.STATE_IDLE:
                return "I";
            case ExoPlayer.STATE_READY:
                return "R";
            default:
                return "?";
        }
    }

    // =======================================================================
    // API for GeckoHlsDemuxerWrapper
    // =======================================================================
    public ConcurrentLinkedQueue<GeckoHlsSample> getVideoSamples(int number) {
        if (vRenderer != null) {
            return vRenderer.getQueuedSamples(number);
        }
        return new ConcurrentLinkedQueue<GeckoHlsSample>();
    }

    public ConcurrentLinkedQueue<GeckoHlsSample> getAudioSamples(int number) {
        if (aRenderer != null) {
            return aRenderer.getQueuedSamples(number);
        }
        return new ConcurrentLinkedQueue<GeckoHlsSample>();
    }

    public long getDuration() {
        assertTrue(player != null);
        // TODO : Find a way to get A/V duration separately.
        long duratoin = player.getDuration() * 1000;
        if (DEBUG) Log.d(TAG, "getDuration : " + duratoin  + "(Us)");
        return duratoin;
    }

    public long getBufferedPosition() {
        assertTrue(player != null);
        long bufferedPos = player.getBufferedPosition() * 1000;
        if (DEBUG) Log.d(TAG, "getBufferedPosition : " + bufferedPos + "(Us)");
        return player.getBufferedPosition();
    }

    public int getNumberTracks(Track_Type trackType) {
        if (DEBUG) Log.d(TAG, "getNumberTracks");
        assertTrue(trackGroupUpdated != false);

        if (trackType == Track_Type.TRACK_VIDEO) {
            return numVideoTracks;
        } else if (trackType == Track_Type.TRACK_AUDIO) {
            return numAudioTracks;
        }
        return 0;
    }

    public Format getVideoTrackFormat() {
        if (DEBUG) Log.d(TAG, "getVideoTrackFormat");
        return videoFormat;
    }

    public Format getAudioTrackFormat() {
        if (DEBUG) Log.d(TAG, "getAudioTrackFormat");
        return audioFormat;
    }

    public boolean seek(long positionMs) {
        // positionMs : milliseconds.
        // NOTE : It's not possible to seek media by tracktype via ExoPlayer Interface.
        if (DEBUG) Log.d(TAG, "seeking  : " + positionMs + " (Ms)");
        try {
            // TODO : Gather Timeline Period / Window information to develop
            //        complete timeilne, and seekTime should be inside the duration.
            Long startTime = Long.MAX_VALUE;
            for (GeckoHlsRendererBase r : renderers) {
                // Find the min value of the start time
                if (r.firstSampleStartTime != null && r.firstSampleStartTime < startTime) {
                    startTime = r.firstSampleStartTime;
                }
                r.clearInputSamplesQueue();
            }

            player.seekTo(positionMs - startTime / 1000);
        } catch (Exception e) {
            return false;
        }
        return true;
    }

    public void release() {
        if (DEBUG) Log.d(TAG, "releasing  ...");
        if (player != null) {
            player.removeListener(eventLogger);
            player.removeListener(this);
            player.stop();
            player.release();
            player = null;
        }
        deinit();
    }
}