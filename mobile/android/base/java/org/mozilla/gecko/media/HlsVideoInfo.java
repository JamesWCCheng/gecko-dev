/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import org.mozilla.gecko.annotation.WrapForJNI;

public final class HlsVideoInfo {
    @WrapForJNI
    public byte[] codecSpecificData = null;

    @WrapForJNI
    public byte[] extraData = null;

    @WrapForJNI
    public int displayX = 0;
    @WrapForJNI
    public int displayY = 0;

    @WrapForJNI
    public int pictureX = 0;
    @WrapForJNI
    public int pictureY = 0;
    
    @WrapForJNI
    public int rotation = 0;

    @WrapForJNI
    public int stereoMode = 0;

    public HlsVideoInfo() {
    }
}
