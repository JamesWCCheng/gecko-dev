/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.annotation.TargetApi;
import static android.os.Build.VERSION_CODES.M;
import android.media.MediaDrm;
import android.util.Log;
import java.util.List;

public class MashmallowGeckoMediaDrmBridge extends LollipopGeckoMediaDrmBridge {

    private static final String LOGTAG = "MashmallowGeckoMediaDrmBridge";

    private void log(String msg) {
        Log.d(LOGTAG, msg);
    }

    MashmallowGeckoMediaDrmBridge(String keySystem) {
        super(keySystem);
        log("MashmallowGeckoMediaDrmBridge");
        if (mDrm != null) {
            // [TODO] Implement ExpirationUpdateListener for M.
            mDrm.setOnKeyStatusChangeListener(new KeyStatusChangeListener(), null);
        }
    }

    @TargetApi(M)
    private class KeyStatusChangeListener implements MediaDrm.OnKeyStatusChangeListener {
        @Override
        public void onKeyStatusChange(MediaDrm mediaDrm,
                                      byte[] sessionId,
                                      List<MediaDrm.KeyStatus> keyInformation,
                                      boolean hasNewUsableKey) {
            // [TODO] Check all the parameters ared handled  properly.
            // hasNewUsableKey seems not to be used.
            // https://cs.chromium.org/chromium/src/third_party/WebKit/Source/modules/encryptedmedia/MediaKeySession.cpp?cl=GROK&l=853
            log("KeyStatusChangeListener.onKeyStatusChange(), hasNewUsableKey = " + hasNewUsableKey);
            // Iterate to callback the status change event for all key in this session.
            // Update the key status map in
            // https://dxr.mozilla.org/mozilla-central/rev/4c05938a64a7fde3ac2d7f4493aee1c5f2ad8a0a/dom/media/gmp/GMPCDMCallbackProxy.cpp#293
            for (MediaDrm.KeyStatus keyStatus : keyInformation) {
                onSessionKeyChanged(sessionId, keyStatus.getKeyId(), keyStatus.getStatusCode());
            }
        }
    }
}
