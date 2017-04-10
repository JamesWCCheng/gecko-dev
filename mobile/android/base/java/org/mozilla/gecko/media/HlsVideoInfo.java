/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import org.mozilla.gecko.annotation.WrapForJNI;

@WrapForJNI
public final class HlsVideoInfo {
    public byte[] codecSpecificData = null;
    public byte[] extraData = null;
    public int displayX = 0;
    public int displayY = 0;
    public int pictureX = 0;
    public int pictureY = 0;
    public int rotation = 0;
    public int stereoMode = 0;
    public long duration = 0;
    public String mimeType = null;
    public HlsVideoInfo() {}
}
