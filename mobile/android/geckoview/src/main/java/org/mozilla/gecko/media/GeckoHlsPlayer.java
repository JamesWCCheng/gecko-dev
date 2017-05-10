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
import com.google.android.exoplayer2.PlaybackParameters;
import com.google.android.exoplayer2.Timeline;
import com.google.android.exoplayer2.audio.AudioRendererEventListener;
import com.google.android.exoplayer2.decoder.DecoderCounters;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.metadata.Metadata;
import com.google.android.exoplayer2.metadata.MetadataRenderer;
import com.google.android.exoplayer2.source.MediaSource;
import com.google.android.exoplayer2.source.TrackGroup;
import com.google.android.exoplayer2.source.TrackGroupArray;
import com.google.android.exoplayer2.source.hls.HlsMediaSource;
import com.google.android.exoplayer2.trackselection.AdaptiveTrackSelection;
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
    private static final String LOGTAG = "GeckoHlsPlayer";
    private static final DefaultBandwidthMeter BANDWIDTH_METER = new DefaultBandwidthMeter();
    private static final int MAX_TIMELINE_ITEM_LINES = 3;
    private static boolean DEBUG = true;

    private DataSource.Factory mediaDataSourceFactory;
    private Timeline.Period period;
    private Timeline.Window window;
    private Handler mainHandler;
    private EventLogger eventLogger;
    private ExoPlayer player;
    private GeckoHlsRendererBase[] renderers;
    private DefaultTrackSelector trackSelector;
    private MediaSource mediaSource;
    private ComponentListener componentListener;

    private boolean isTimelineStatic = false;
    private long durationUs;

    private boolean trackGroupUpdated = false;
    private long bufferedPosition;

    private int numVideoTracks = 0;
    private int numAudioTracks = 0;
    private GeckoHlsVideoRenderer vRenderer = null;
    private GeckoHlsAudioRenderer aRenderer = null;
    private boolean enableV = true;
    private boolean enableA = true;

    private boolean hasVideo = false;
    private boolean hasAudio = false;
    private boolean videoInfoUpdated = false;
    private boolean audioInfoUpdated = false;
    private boolean isPlayerInitDone = false;
    private boolean isDemuxerInitDone = false;
    private DemuxerCallbacks demuxerCallbacks;
    private ResourceCallbacks resourceCallbacks;

    protected String userAgent;

    public enum Track_Type {
        TRACK_UNDEFINED,
        TRACK_AUDIO,
        TRACK_VIDEO,
        TRACK_TEXT,
    }

    public static final int E_RESOURCE_BASE         = -100;
    public static final int E_RESOURCE_UNKNOWN      = E_RESOURCE_BASE + 1;
    public static final int E_RESOURCE_PLAYER       = E_RESOURCE_BASE + 2;
    public static final int E_RESOURCE_UNSUPPORTED  = E_RESOURCE_BASE + 3;

    public static final int E_DEMUXER_BASE         = -200;
    public static final int E_DEMUXER_UNKNOWN      = E_DEMUXER_BASE + 1;
    public static final int E_DEMUXER_PLAYER       = E_DEMUXER_BASE + 2;
    public static final int E_DEMUXER_UNSUPPORTED  = E_DEMUXER_BASE + 3;

    public interface DemuxerCallbacks {
        void onInitialized(boolean hasAudio, boolean hasVideo);
        void onDemuxerError(int errorCode);
    }

    public interface ResourceCallbacks {
        void onDataArrived();
        void onResourceError(int errorCode);
    }

    private static void assertTrue(boolean condition) {
      if (DEBUG && !condition) {
        throw new AssertionError("Expected condition to be true");
      }
    }

    public void checkInitDone() {
        assertTrue(demuxerCallbacks != null);
        if (isDemuxerInitDone) {
            return;
        }
        boolean vDone = hasVideo ? videoInfoUpdated : true;
        boolean aDone = hasAudio ? audioInfoUpdated : true;

        if (vDone && aDone) {
            demuxerCallbacks.onInitialized(hasAudio, hasVideo);
            isDemuxerInitDone = true;
        }
    }

    public final class ComponentListener implements VideoRendererEventListener,
            AudioRendererEventListener, MetadataRenderer.Output {

        // General purpose implementation
        public void onDataArrived() {
            assertTrue(resourceCallbacks != null);
            resourceCallbacks.onDataArrived();
        }

        // VideoRendererEventListener implementation
        @Override
        public void onVideoEnabled(DecoderCounters counters) {
            // do nothing
        }

        @Override
        public void onVideoDecoderInitialized(String decoderName, long initializedTimestampMs,
                                              long initializationDurationMs) {
            // do nothing
        }

        @Override
        public void onVideoInputFormatChanged(Format format) {
            assertTrue(demuxerCallbacks != null);
            if (DEBUG) Log.d(LOGTAG, "[CB] onVideoInputFormatChanged [" + format + "]");
            if (DEBUG) Log.d(LOGTAG, "[CB] SampleMIMEType [" +
                             format.sampleMimeType + "], ContainerMIMEType [" +
                             format.containerMimeType + "]");
            videoInfoUpdated = true;
            checkInitDone();
        }

        @Override
        public void onDroppedFrames(int count, long elapsed) {
            // do nothing
        }

        @Override
        public void onVideoSizeChanged(int width, int height, int unappliedRotationDegrees,
                                       float pixelWidthHeightRatio) {
            // do nothing
        }

        @Override
        public void onRenderedFirstFrame(Surface surface) {
            // do nothing
        }

        @Override
        public void onVideoDisabled(DecoderCounters counters) {}

        // AudioRendererEventListener implementation
        @Override
        public void onAudioEnabled(DecoderCounters counters) {
            // do nothing
        }

        @Override
        public void onAudioSessionId(int sessionId) {
            // do nothing
        }

        @Override
        public void onAudioDecoderInitialized(String decoderName, long initializedTimestampMs,
                                              long initializationDurationMs) {
            // do nothing
        }

        @Override
        public void onAudioInputFormatChanged(Format format) {
            assertTrue(demuxerCallbacks != null);
            if (DEBUG) Log.d(LOGTAG, "[CB] onAudioInputFormatChanged [" + format + "]");
            audioInfoUpdated = true;
            checkInitDone();
        }

        @Override
        public void onAudioTrackUnderrun(int bufferSize, long bufferSizeMs,
                                         long elapsedSinceLastFeedMs) {
            // do nothing
        }

        @Override
        public void onAudioDisabled(DecoderCounters counters) {}

        // MetadataRenderer.Output implementation
        @Override
        public void onMetadata(Metadata metadata) {
            // do nothing
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
        if (DEBUG) Log.d(LOGTAG, "buildMediaSource uri[" + uri + "]" + ", overridedExt[" + overrideExtension + "]");
        int type = Util.inferContentType(!TextUtils.isEmpty(overrideExtension) ? "." + overrideExtension
                : uri.getLastPathSegment());
        switch (type) {
            case C.TYPE_HLS:
                return new HlsMediaSource(uri, mediaDataSourceFactory, mainHandler, null);
            default: {
                resourceCallbacks.onResourceError(E_RESOURCE_UNSUPPORTED);
                throw new IllegalStateException("Unsupported type: " + type);
            }
        }
    }

    GeckoHlsPlayer() {
        if (DEBUG) Log.d(LOGTAG, " construct");
    }

    void addResourceWrapperCallbackListener(ResourceCallbacks callback) {
        if (DEBUG) Log.d(LOGTAG, " addResourceWrapperCallbackListener ...");
        resourceCallbacks = callback;
    }

    void addDemuxerWrapperCallbackListener(DemuxerCallbacks callback) {
        if (DEBUG) Log.d(LOGTAG, " addDemuxerWrapperCallbackListener ...");
        demuxerCallbacks = callback;
    }

    synchronized void init(String url) {
        if (DEBUG) Log.d(LOGTAG, " init");
        assertTrue(resourceCallbacks != null);
        if (isPlayerInitDone == true) {
            return;
        }
        Context ctx = GeckoAppShell.getApplicationContext();
        componentListener = new ComponentListener();
        mainHandler = new Handler();

        durationUs = 0;
        window = new Timeline.Window();
        period = new Timeline.Period();

        // Prepare trackSelector
        TrackSelection.Factory videoTrackSelectionFactory =
                new AdaptiveTrackSelection.Factory(BANDWIDTH_METER);
        trackSelector = new DefaultTrackSelector(videoTrackSelectionFactory);

        // Prepare customized renderer
        ArrayList<GeckoHlsRendererBase> renderersList = new ArrayList<>();
        vRenderer = new GeckoHlsVideoRenderer(MediaCodecSelector.DEFAULT,
                                              mainHandler,
                                              componentListener);
        renderersList.add(vRenderer);
        aRenderer = new GeckoHlsAudioRenderer(MediaCodecSelector.DEFAULT,
                                              mainHandler,
                                              componentListener);
        renderersList.add(aRenderer);
        renderers = renderersList.toArray(new GeckoHlsRendererBase[renderersList.size()]);

        // Create ExoPlayer instance with specific components.
        player = ExoPlayerFactory.newInstance(renderers, trackSelector);
        player.addListener(this);

        if (DEBUG) {
            eventLogger = new EventLogger(trackSelector);
            player.addListener(eventLogger);
        }

        Uri[] uris = new Uri[]{Uri.parse(url)};
        userAgent = Util.getUserAgent(ctx, "RemoteDecoder");
        mediaDataSourceFactory = buildDataSourceFactory(ctx, BANDWIDTH_METER);

        MediaSource[] mediaSources = new MediaSource[1];
        mediaSources[0] = buildMediaSource(uris[0], null);
        mediaSource = mediaSources[0];

        player.prepare(mediaSource);
        isPlayerInitDone = true;
    }

    @Override
    public void onLoadingChanged(boolean isLoading) {
        if (DEBUG) Log.d(LOGTAG, "loading [" + isLoading + "]");
    }

    @Override
    public void onPlayerStateChanged(boolean playWhenReady, int state) {
        if (DEBUG) Log.d(LOGTAG, "state [" + playWhenReady + ", " + getStateString(state) + "]");
        if (state == ExoPlayer.STATE_READY) {
            player.setPlayWhenReady(true);
        }
    }

    @Override
    public void onPositionDiscontinuity() {
        if (DEBUG) Log.d(LOGTAG, "positionDiscontinuity");
    }

    @Override
    public void onPlaybackParametersChanged(PlaybackParameters playbackParameters) {
        if (DEBUG) Log.d(LOGTAG, "playbackParameters " + String.format(
                         "[speed=%.2f, pitch=%.2f]", playbackParameters.speed, playbackParameters.pitch));
    }

    @Override
    public void onPlayerError(ExoPlaybackException e) {
        if (DEBUG) Log.e(LOGTAG, "playerFailed" , e);
        if (resourceCallbacks != null) {
            resourceCallbacks.onResourceError(E_RESOURCE_PLAYER);
        }
        if (demuxerCallbacks != null) {
            demuxerCallbacks.onDemuxerError(E_DEMUXER_PLAYER);
        }
    }

    @Override
    public synchronized void onTracksChanged(TrackGroupArray ignored, TrackSelectionArray trackSelections) {
        if (DEBUG) Log.e(LOGTAG, "onTracksChanged : TGA[" + ignored +
                         "], TSA[" + trackSelections + "]");
        if (trackGroupUpdated) {
            trackGroupUpdated = false;
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
        hasVideo = numVideoTracks > 0 ? true : false;
        hasAudio = numAudioTracks > 0 ? true : false;
        trackGroupUpdated = true;
    }

    @Override
    public void onTimelineChanged(Timeline timeline, Object manifest) {
        isTimelineStatic = !timeline.isEmpty()
                && !timeline.getWindow(timeline.getWindowCount() - 1, window).isDynamic;

        int periodCount = timeline.getPeriodCount();
        int windowCount = timeline.getWindowCount();
        if (DEBUG) Log.d(LOGTAG, "sourceInfo [periodCount=" + periodCount + ", windowCount=" + windowCount);
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
        // TODO : Need to check if the duration from play.getDuration is different
        // with the one calculated from multi-timelines/windows.
        if (DEBUG) Log.d(LOGTAG, "Media duration (from Timeline) = " + durationUs +
                         "(us)" + " player.getDuration() = " + player.getDuration() +
                         "(ms)");
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
        return vRenderer != null ? vRenderer.getQueuedSamples(number) :
                                   new ConcurrentLinkedQueue<GeckoHlsSample>();
    }

    public ConcurrentLinkedQueue<GeckoHlsSample> getAudioSamples(int number) {
        return aRenderer != null ? aRenderer.getQueuedSamples(number) :
                                   new ConcurrentLinkedQueue<GeckoHlsSample>();
    }

    public long getDuration() {
        assertTrue(player != null);
        // Value returned by getDuration() is in milliseconds.
        long duratoin = player.getDuration() * 1000;
        if (DEBUG) Log.d(LOGTAG, "getDuration : " + duratoin  + "(Us)");
        return duratoin;
    }

    public long getBufferedPosition() {
        assertTrue(player != null);
        // Value returned by getBufferedPosition() is in milliseconds.
        long bufferedPos = player.getBufferedPosition() * 1000;
        if (DEBUG) Log.d(LOGTAG, "getBufferedPosition : " + bufferedPos + "(Us)");
        return player.getBufferedPosition();
    }

    public synchronized int getNumberTracks(Track_Type trackType) {
        if (DEBUG) Log.d(LOGTAG, "getNumberTracks");
        assertTrue(trackGroupUpdated != false);

        if (trackType == Track_Type.TRACK_VIDEO) {
            return numVideoTracks;
        } else if (trackType == Track_Type.TRACK_AUDIO) {
            return numAudioTracks;
        }
        return 0;
    }

    public Format getVideoTrackFormat(int index) {
        if (DEBUG) Log.d(LOGTAG, "getVideoTrackFormat");
        assertTrue(vRenderer != null);
        return hasVideo ? vRenderer.getFormat(index) : null;
    }

    public Format getAudioTrackFormat(int index) {
        if (DEBUG) Log.d(LOGTAG, "getAudioTrackFormat");
        assertTrue(aRenderer != null);
        return hasAudio ? aRenderer.getFormat(index) : null;
    }

    public boolean seek(long positionMs) {
        // positionMs : milliseconds.
        // NOTE : 1) It's not possible to seek media by tracktype via ExoPlayer Interface.
        //        2) positionMs is samples PTS from MFR, we need to re-adjust it
        //           for ExoPlayer by subtracting sample start time.
        try {
            // TODO : Gather Timeline Period / Window information to develop
            //        complete timeilne, and seekTime should be inside the duration.
            Long startTime = Long.MAX_VALUE;
            for (GeckoHlsRendererBase r : renderers) {
                if (r == vRenderer  && enableV || r == aRenderer  && enableA) {
                // Find the min value of the start time
                    startTime = Math.min(startTime, r.getFirstSamplePTS());
                }
            }
            if (DEBUG) Log.d(LOGTAG, "seeking  : " + positionMs / 1000 +
                             " (s); startTime : " + startTime);
            player.seekTo(positionMs - startTime / 1000);
        } catch (Exception e) {
            demuxerCallbacks.onDemuxerError(E_DEMUXER_UNKNOWN);
            return false;
        }
        return true;
    }

    public long getNextKeyFrameTime() {
        long nextKeyFrameTime = vRenderer != null ? vRenderer.getNextKeyFrameTime() : 0;
        return nextKeyFrameTime;
    }

    public void release() {
        if (DEBUG) Log.d(LOGTAG, "releasing  ...");
        if (player != null) {
            if (eventLogger != null) {
                player.removeListener(eventLogger);
            }
            player.removeListener(this);
            player.stop();
            player.release();
            vRenderer = null;
            aRenderer = null;
            player = null;
        }
        demuxerCallbacks = null;
        resourceCallbacks = null;
        isPlayerInitDone = false;
        isDemuxerInitDone = false;
    }
}