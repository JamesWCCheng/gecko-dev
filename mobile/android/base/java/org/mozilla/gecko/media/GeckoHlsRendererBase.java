/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import com.google.android.exoplayer2.BaseRenderer;

// TODO: Extract the identical member function or data from Video/Audio Renderer
public abstract class GeckoHlsRendererBase extends BaseRenderer {
    public GeckoHlsRendererBase(int trackType) { super(trackType); }
    public abstract boolean clearInputSamplesQueue();
    public Long firstSampleStartTime = null;
}
