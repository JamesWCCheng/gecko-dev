/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;

import org.mozilla.gecko.annotation.WrapForJNI;

import java.io.IOException;
import java.nio.ByteBuffer;

public final class GeckoHlsSample {
    public byte[] byteArray;

    @WrapForJNI
    public long duration;

    @WrapForJNI
    public BufferInfo info;

    @WrapForJNI
    public CryptoInfo cryptoInfo;

    @WrapForJNI
    public void writeToByteBuffer(ByteBuffer dest) throws IOException {
        if (byteArray != null && dest != null && info.size > 0) {
            dest.put(byteArray, info.offset, info.size);
        }
    }

    @WrapForJNI
    public boolean isEOS() {
        return (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
    }

    @WrapForJNI
    public boolean isKeyFrame() {
        return (info.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0;
    }

    @WrapForJNI
    public void dispose() {
      byteArray = null;
      info = null;
      cryptoInfo = null;
    }

    public static GeckoHlsSample create(ByteBuffer src, BufferInfo info, CryptoInfo cryptoInfo) {
        BufferInfo bufferInfo = new BufferInfo();
        bufferInfo.set(0, info.size, info.presentationTimeUs, info.flags);
        return new GeckoHlsSample(byteArrayFromBuffer(src, info.offset, info.size), bufferInfo, cryptoInfo);
    }

    private GeckoHlsSample(byte[] bytes, BufferInfo info, CryptoInfo cryptoInfo) {
        duration = Long.MAX_VALUE;
        byteArray = bytes;
        this.info = info;
        this.cryptoInfo = cryptoInfo;
    }

    public static byte[] byteArrayFromBuffer(ByteBuffer buffer, int offset, int size) {
        if (buffer == null || buffer.capacity() == 0 || size == 0) {
            return null;
        }
        if (buffer.hasArray() && offset == 0 && buffer.array().length == size) {
            return buffer.array();
        }
        int length = Math.min(offset + size, buffer.capacity()) - offset;
        byte[] bytes = new byte[length];
        buffer.position(offset);
        buffer.get(bytes);
        return bytes;
    }

    @Override
    public String toString() {
        if (isEOS()) {
            return "EOS GeckoHlsSample";
        }

        StringBuilder str = new StringBuilder();
        str.append("{ info=").
                append("{ offset=").append(info.offset).
                append(", size=").append(info.size).
                append(", pts=").append(info.presentationTimeUs).
                append(", duration=").append(duration).
                append(", flags=").append(Integer.toHexString(info.flags)).append(" }").
                append(" }");
            return str.toString();
    }
}
