/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

// Non-default types used in interface.

interface IMediaDrmBridgeCallbacks {

    oneway void onSessionCreated(int createSessionToken,
                                 int promiseId,
                                 in byte[] sessionId,
                                 in byte[] request);

    oneway void onSessionUpdated(int promiseId, in byte[] sessionId);

    oneway void onSessionClosed(int promiseId, in byte[] sessionId);

    oneway void onSessionMessage(in byte[] sessionId,
                                 int sessionMessageType,
                                 in byte[] request);

    oneway void onSessionError(in byte[] sessionId);

    oneway void onSessionKeyChanged(in byte[] sessionId,
                                    in byte[] keyId,
                                    int statusCode);
}
