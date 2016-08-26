/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.os.IBinder;
import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.annotation.RobocopTarget;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoAppShell;


final class RemoteMediaDrmBridge implements GeckoMediaDrm {
    private static final String LOGTAG = "\033[1;41mRemoteMediaDrmBridge\033[m";
    
    
    private static class CallbacksForwarder extends IMediaDrmBridgeCallbacks.Stub {
        private final CallbackRemoteManager.getRemoteManager().s mCallbacks;
        CallbacksForwarder(Callbacks callbacks) {
            mCallbacks = callbacks;
        }

        @Override
        void onSessionCreated(int createSessionToken,
                              int promiseId,
                              byte[] sessionId,
                              byte[] request) {
            mCallbacks.onSessionCreated(createSessionToken,
                                        promiseId,
                                        sessionId,
                                        request);
        }

        @Override
        void onSessionUpdated(int promiseId, byte[] sessionId) {
            mCallbacks.onSessionUpdated(promiseId, sessionId);
        }

        @Override
        void onSessionClosed(int promiseId, byte[] sessionId) {
            mCallbacks.onSessionClosed(promiseId, sessionId);
        }

        @Override
        void onSessionMessage(byte[] sessionId,
                              int sessionMessageType,
                              byte[] request) {
            mCallbacks.onSessionMessage(sessionId, sessionMessageType, request);
        }

        @Override
        void onSessionError(byte[] sessionId) {
            mCallbacks.onSessionError(sessionId);
        }

        @Override
        void onSessionKeyChanged(byte[] sessionId,
                                 byte[] keyId,
                                 int statusCode) {
            mCallbacks.onSessionKeyChanged(sessionId, keyId, statusCode);
        }
    } // CallbacksForwarder
    
        
    public RemoteMediaDrmBridge(String keySystem){
        
    }
    @Override
    public void setCallbacks(Callbacks callbacks) {
        
    }
    
    @Override
    public void createSession(int createSessionToken,
                              int promiseId,
                              String initDataType,
                              byte[] initData) {
        
    }
    
    @Override
    public void updateSession(int promiseId, String sessionId, byte[] response) {
        
    }
    
    @Override
    public void closeSession(int promiseId, String sessionId)
    {
        
    }
} // class RemoteMediaDrmBridge