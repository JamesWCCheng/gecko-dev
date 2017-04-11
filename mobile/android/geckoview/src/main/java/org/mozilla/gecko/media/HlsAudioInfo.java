/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import org.mozilla.gecko.annotation.WrapForJNI;

@WrapForJNI
public final class HlsAudioInfo {
    public byte[] codecSpecificData = null;
    public byte[] extraData = null;
    public int rate = 0;
    public int channels = 0;
    public int bitDepth = 0;
    public int profile = 0;
    public String mimeType = null;
    public long duration = 0;
    public HlsAudioInfo() {}
}
