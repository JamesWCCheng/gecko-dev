/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

public interface GeckoMediaDrm {
    public interface Callbacks {
        void onSessionCreated(int createSessionToken,
                              int promiseId,
                              byte[] sessionId,
                              byte[] request);
        void onSessionUpdated(int promiseId, byte[] sessionId);
        void onSessionClosed(int promiseId, byte[] sessionId);
        void onSessionMessage(byte[] sessionId,
                              int sessionMessageType,
                              byte[] request);
        void onSessionError(byte[] sessionId);
        void onSessionKeyChanged(byte[] sessionId, byte[] keyId, int statusCode);
    }
    public void setCallbacks(Callbacks callbacks);
    public void createSession(int createSessionToken,
                              int promiseId,
                              String initDataType,
                              byte[] initData);
    public void updateSession(int promiseId, String sessionId, byte[] response);
    public void closeSession(int promiseId, String sessionId);
}
