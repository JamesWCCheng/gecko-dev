/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import org.mozilla.gecko.annotation.WrapForJNI;

public final class HlsAudioInfo {
    @WrapForJNI
    public byte[] codecSpecificData = null;

    @WrapForJNI
    public byte[] extraData = null;

    @WrapForJNI
    public int rate = 0;

    @WrapForJNI
    public int channels = 0;

    @WrapForJNI
    public int bitDepth = 0;

    @WrapForJNI
    public int profile = 0;

    @WrapForJNI
    public String mimeType = null;

    @WrapForJNI
    public HlsAudioInfo() {
    }
}
