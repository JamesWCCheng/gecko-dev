package org.mozilla.gecko.media;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;

@WrapForJNI
public final class SessionKeyInfo {
    public byte[] keyId;
    public int status;

    public SessionKeyInfo(byte[] keyId, int status) {
        this.keyId = keyId;
        this.status = status;
    }
}
