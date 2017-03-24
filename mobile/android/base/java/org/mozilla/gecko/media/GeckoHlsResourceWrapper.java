/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import org.mozilla.gecko.annotation.WrapForJNI;

public class GeckoHlsResourceWrapper {
    private static final String LOGTAG = "GeckoHlsResourceWrapper";
    private static final boolean DEBUG = true;
    private GeckoHlsPlayer player = null;

    private GeckoHlsResourceWrapper(String url) {
        player = GeckoHlsPlayer.INSTANCE;
        player.init(url);
    }
    @WrapForJNI(calledFrom = "gecko")
    public static GeckoHlsResourceWrapper create(String url) {
        GeckoHlsResourceWrapper wrapper = new GeckoHlsResourceWrapper(url);
        return wrapper;
    }
}
