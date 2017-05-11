/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import java.nio.ByteBuffer;
import org.mozilla.gecko.annotation.WrapForJNI;

//A subset of the class VideoInfo in dom/media/MediaInfo.h
@WrapForJNI
public final class GeckoVideoInfo {
    final public ByteBuffer codecSpecificData;
    final public ByteBuffer extraData;
    final public int displayX;
    final public int displayY;
    final public int pictureX;
    final public int pictureY;
    final public int rotation;
    final public int stereoMode;
    final public long duration;
    final public String mimeType;
    public GeckoVideoInfo(int displayX, int displayY, int pictureX, int pictureY,
                          int rotation, int stereoMode, long duration, String mimeType,
                          ByteBuffer extraData, ByteBuffer codecSpecificData) {
        this.displayX = displayX;
        this.displayY = displayY;
        this.pictureX = pictureX;
        this.pictureY = pictureY;
        this.rotation = rotation;
        this.stereoMode = stereoMode;
        this.duration = duration;
        this.mimeType = mimeType;
        this.extraData = extraData;
        this.codecSpecificData = codecSpecificData;
    }
}
