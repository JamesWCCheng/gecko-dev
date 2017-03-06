package org.mozilla.gecko.media;

import android.content.Context;
import android.graphics.SurfaceTexture;
import android.net.Uri;
import android.os.Handler;
import android.text.TextUtils;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.TextureView;

import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.ExoPlaybackException;
import com.google.android.exoplayer2.ExoPlayer;
import com.google.android.exoplayer2.ExoPlayerFactory;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.Renderer;
import com.google.android.exoplayer2.Timeline;
import com.google.android.exoplayer2.audio.AudioCapabilities;
import com.google.android.exoplayer2.audio.AudioRendererEventListener;
import com.google.android.exoplayer2.decoder.DecoderCounters;
import com.google.android.exoplayer2.mediacodec.MediaCodecSelector;
import com.google.android.exoplayer2.metadata.Metadata;
import com.google.android.exoplayer2.metadata.MetadataRenderer;
import com.google.android.exoplayer2.source.MediaSource;
import com.google.android.exoplayer2.source.TrackGroupArray;
import com.google.android.exoplayer2.source.hls.HlsMediaSource;
import com.google.android.exoplayer2.text.Cue;
import com.google.android.exoplayer2.text.TextRenderer;
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

import java.util.ArrayList;
import java.util.List;

public class GeckoHlsPlayer implements ExoPlayer.EventListener {

    private static final String TAG = "GeckoHlsPlayer";
    private static final String HLS_URL = "https://devimages.apple.com.edgekey.net/streaming/examples/bipbop_4x3/gear1/prog_index.m3u8";
    private static final DefaultBandwidthMeter BANDWIDTH_METER = new DefaultBandwidthMeter();
    private DataSource.Factory mediaDataSourceFactory;
    private Timeline.Window window;
    protected String userAgent;
    private Handler mainHandler;
    public final String extension = null;
    public static final String EXTENSION_EXTRA = "extension";
    private EventLogger eventLogger;

    private ExoPlayer player;

    private DefaultTrackSelector trackSelector;
    private boolean isTimelineStatic = false;
    private final Renderer[] renderers;
    private MediaSource mediaSource;

    private final ComponentListener componentListener;

    public final class ComponentListener implements VideoRendererEventListener,
            AudioRendererEventListener, TextRenderer.Output, MetadataRenderer.Output,
            SurfaceHolder.Callback, TextureView.SurfaceTextureListener {

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
//            videoFormat = format;
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
//            videoFormat = null;
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
//            audioFormat = format;
        }

        @Override
        public void onAudioTrackUnderrun(int bufferSize, long bufferSizeMs,
                                         long elapsedSinceLastFeedMs) {
        }

        @Override
        public void onAudioDisabled(DecoderCounters counters) {
//            audioFormat = null;
//            audioDecoderCounters = null;
//            audioSessionId = AudioTrack.SESSION_ID_NOT_SET;
        }

        // TextRenderer.Output implementation

        @Override
        public void onCues(List<Cue> cues) {
//            if (textOutput != null) {
//                textOutput.onCues(cues);
//            }
        }

        // MetadataRenderer.Output implementation

        @Override
        public void onMetadata(Metadata metadata) {
//            if (metadataOutput != null) {
//                metadataOutput.onMetadata(metadata);
//            }
        }

        // SurfaceHolder.Callback implementation

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
//            setVideoSurfaceInternal(holder.getSurface(), false);
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            // Do nothing.
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
//            setVideoSurfaceInternal(null, false);
        }

        // TextureView.SurfaceTextureListener implementation

        @Override
        public void onSurfaceTextureAvailable(SurfaceTexture surfaceTexture, int width, int height) {
//            setVideoSurfaceInternal(new Surface(surfaceTexture), true);
        }

        @Override
        public void onSurfaceTextureSizeChanged(SurfaceTexture surfaceTexture, int width, int height) {
            // Do nothing.
        }

        @Override
        public boolean onSurfaceTextureDestroyed(SurfaceTexture surfaceTexture) {
//            setVideoSurfaceInternal(null, true);
            return true;
        }

        @Override
        public void onSurfaceTextureUpdated(SurfaceTexture surfaceTexture) {
            // Do nothing.
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

    public void setSurface(Surface surface) {
        player.prepare(mediaSource);

        ExoPlayer.ExoPlayerMessage[] messages = new ExoPlayer.ExoPlayerMessage[1];
        int count = 0;
        for (Renderer renderer : renderers) {
            if (renderer.getTrackType() == C.TRACK_TYPE_VIDEO) {
                messages[count++] = new ExoPlayer.ExoPlayerMessage(renderer, C.MSG_SET_SURFACE, surface);
            }
        }

        if (surface != null) {
            player.blockingSendMessages(messages);
        }
    }

    public GeckoHlsPlayer(Context va) {
        componentListener = new ComponentListener();
        mainHandler = new Handler();

        window = new Timeline.Window();
        TrackSelection.Factory videoTrackSelectionFactory =
                new AdaptiveVideoTrackSelection.Factory(BANDWIDTH_METER);
        trackSelector = new DefaultTrackSelector(videoTrackSelectionFactory);

        ArrayList<Renderer> renderersList = new ArrayList<>();
        renderersList.add(new GeckoHlsVideoRender(va, MediaCodecSelector.DEFAULT, mainHandler, componentListener));
        renderersList.add(new GeckoHlsAudioRender(MediaCodecSelector.DEFAULT, mainHandler, componentListener, (AudioCapabilities)null));
        renderers = renderersList.toArray(new Renderer[renderersList.size()]);

        player = ExoPlayerFactory.newInstance(renderers, trackSelector);
        player.addListener(this);

        eventLogger = new EventLogger(trackSelector);
        player.addListener(eventLogger);

        Uri[] uris = new Uri[]{Uri.parse(HLS_URL)};
        String[] extensions = new String[]{extension};
        userAgent = Util.getUserAgent(va, "RemoteDecoder");
        mediaDataSourceFactory = buildDataSourceFactory(va, null);

        MediaSource[] mediaSources = new MediaSource[1];
        mediaSources[0] = buildMediaSource(uris[0], extensions[0]);
        mediaSource = mediaSources[0];
    }

    @Override
    public void onLoadingChanged(boolean isLoading) {
        Log.d(TAG, "loading [" + isLoading + "]");
    }

    @Override
    public void onPlayerStateChanged(boolean playWhenReady, int state) {
//        Log.d(TAG, "state [" + getSessionTimeString() + ", " + playWhenReady + ", "
//                + getStateString(state) + "]");
        if (state == ExoPlayer.STATE_READY) {
            player.setPlayWhenReady(true);
        }
    }

    @Override
    public void onPositionDiscontinuity() {
        Log.d(TAG, "positionDiscontinuity");
    }

    @Override
    public void onPlayerError(ExoPlaybackException e) {
//        Log.e(TAG, "playerFailed [" + getSessionTimeString() + "]", e);
    }

    @Override
    public void onTracksChanged(TrackGroupArray ignored, TrackSelectionArray trackSelections) {
    }

    @Override
    public void onTimelineChanged(Timeline timeline, Object manifest) {
        isTimelineStatic = !timeline.isEmpty()
                && !timeline.getWindow(timeline.getWindowCount() - 1, window).isDynamic;
    }
}