package org.mozilla.gecko.media;

//import org.apache.http.HttpResponse;
//import org.apache.http.client.ClientProtocolException;
//import org.apache.http.client.HttpClient;
//import org.apache.http.client.methods.HttpPost;
//import org.apache.http.impl.client.DefaultHttpClient;
//import org.apache.http.util.EntityUtils;
//import org.apache.commons.lang3.CharEncoding.UTF_8;
//import java.io.IOException;
//import java.nio.ByteOrder;
//import java.util.ArrayDeque;

import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.annotation.RobocopTarget;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoAppShell;

//import android.os.AsyncTask;
//import android.os.Build;
//import android.os.Handler;
import android.media.MediaCrypto;
import android.media.MediaCryptoException;
import android.media.MediaDrm;
import android.media.MediaDrmException;
import android.util.Log;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.UUID;

public class MediaDrmBridge extends JNIObject {
	
    private static final String LOGTAG = "MediaDrmBridge";
    private static final UUID CLEARKEY_SCHEME_UUID =
      new UUID(0x1077efecc0b24d02L, 0xace33c1e52e2fb4bL);
    private static final UUID WIDEVINE_SCHEME_UUID =
      new UUID(0xedef8ba979d64aceL, 0xa3c827dcd51d21edL);
    private static final String INVALID_SESSION_ID = "Invalid";
    
    private MediaDrm mDrm;
    private MediaCrypto mCrypto;
    private UUID mSchemeUUID;
    private ByteBuffer mCryptoSessionId;
    private boolean mProvisioning;
    private HashMap<ByteBuffer, String> mSessionIds;
    
    @Override protected native void disposeNative();
    
    public Object getLock() {
        return this;
    }
    
    @WrapForJNI(allowMultithread = true)
    MediaDrmBridge() {
    	Log.d(LOGTAG, "MediaDrmBridge()");
    }
    
    @WrapForJNI(allowMultithread = true)
    public boolean Init(UUID aKeySystem) {
    	synchronized (getLock()) {
    		Log.d(LOGTAG, "Init");
    		mProvisioning = false;
    		mSessionIds = new HashMap<ByteBuffer, String>();
        	try {
        		mDrm = new MediaDrm(aKeySystem);
        		mDrm.setOnEventListener(new MediaDrmListener());
        		mSchemeUUID = aKeySystem;
        		// TODO: android L not implemented. figure it out
                //mDrm.setPropertyString("privacyMode", "enable");
        		return true;
        	} catch (MediaDrmException e) {
        		Log.e(LOGTAG, "Failed to create MediaDrm: " + e.getMessage());
        		return false;
        	}	
    	}
    }
    
    @WrapForJNI(allowMultithread = true)
    private boolean CreateSession(int aCreateSessionToken,
	    						  int aPromiseId,
	    						  String aInitDataType,
	    						  byte[] aInitData) {
    	Log.d(LOGTAG, "CreateSession");
    	if (mDrm == null) {
    		Log.d(LOGTAG, "mDrm not exists !");
    		return false;
    	}
    	if (mProvisioning) {
    		Log.d(LOGTAG, "Pending createsession because of provisioning !");
    		return true;
    	}
    	ByteBuffer sessionId = null;
    	String strSessionId = null;
    	try {
    		sessionId = openSession();
    		strSessionId = new String(sessionId.array());
        	boolean hasCrypto = ensureMediaCryptoCreated(sessionId);

        	MediaDrm.KeyRequest request = null;
        	request = getKeyRequest(sessionId, aInitData, aInitDataType);
        	if (request == null) {
        		if (sessionId != null) {
        			CloseSession(strSessionId);
        		}
        		return false;
        	}

        	onSessionCreated(aCreateSessionToken, aPromiseId, sessionId.array(),
        					 request.getData());
        	
        	mSessionIds.put(sessionId, strSessionId);
        	Log.d(LOGTAG, " StringID : " + strSessionId + " is put into mSessionIds ");
        	return true;
    	} catch (android.media.NotProvisionedException e) {
    		Log.e(LOGTAG, "Device not provisioned", e);
    		if (sessionId != null) {
    			CloseSession(strSessionId);
    		}
        	// TODO : Start provisioning
    		return true;
    	}
    }

    @WrapForJNI(allowMultithread = true)
    private boolean UpdateSession(int aPromiseId,
	    						  String aSessionId,
	    						  byte[] aResponse) {
    	Log.d(LOGTAG, "UpdateSession >>> " + aSessionId);

        if (mDrm == null) {
            Log.e(LOGTAG, "updateSession() called when MediaDrm is null.");
            return false;
        }

        ByteBuffer session = getSession(aSessionId);
        if (session == null) {
        	Log.e(LOGTAG, "updateSession : found no session for Id="+aSessionId);
        }
    	// TODO : Check the following error
//        if (!sessionExists(session)) {
//            Log.e(LOGTAG, "Invalid session in updateSession.");
//            onSessionError(sessionId);
//            return false;
//        }

        try {
            try {
            	final byte [] keySetId = mDrm.provideKeyResponse(session.array(), aResponse);

            	// Aandroid ClearKeyPlugin doesn't handle QueryKeyStatus
            	HashMap<String, String> infoMap = mDrm.queryKeyStatus(session.array());
            	
            } catch (java.lang.IllegalStateException e) {
                // This is not really an exception. Some error code are incorrectly
                // reported as an exception.
                // TODO(qinmin): remove this exception catch when b/10495563 is fixed.
                Log.e(LOGTAG, "Exception intentionally caught when calling provideKeyResponse()", e);
            }
            //onSessionReady(sessionId);
            Log.d(LOGTAG, "Key successfully added for session " + aSessionId);
            return true;
        } catch (android.media.NotProvisionedException e) {
            // TODO(xhwang): Should we handle this?
            Log.e(LOGTAG, "failed to provide key response", e);
        } catch (android.media.DeniedByServerException e) {
            Log.e(LOGTAG, "failed to provide key response", e);
        }
        //onSessionError(sessionId);
        release();
        return false;
    }

    @WrapForJNI(allowMultithread = true)
    private void CloseSession(String aSessionId) {
    	Log.d(LOGTAG, "CloseSession");
    	assert mDrm != null;
    	ByteBuffer session = getSession(aSessionId);
    	mDrm.closeSession(session.array());
    }
    
    @WrapForJNI(allowMultithread = true)
    private MediaCrypto GetMediaCrypto() {
    	return mCrypto;
    }

    @WrapForJNI(allowMultithread = true)
    private native void onSessionCreated(int aCreateSessionToken,
    									 int aPromiseId,
    									 byte[] aSessionId,
    									 byte[] aRequest);
    
    @WrapForJNI(allowMultithread = true)
    private native void onSessionUpdated(int aPromiseId,
    									 int aSessionId);

    @WrapForJNI(allowMultithread = true)
    private native void onSessoinClosed(int aPromiseId,
    									int aSessionId);

    // Refer to Android Java Implementation ==================================
    private MediaDrm.KeyRequest getKeyRequest(ByteBuffer session, byte[] data,
    										  String mime)
            throws android.media.NotProvisionedException {
        assert mDrm != null;
        assert mCrypto != null;
        assert !mProvisioning;

        HashMap<String, String> optionalParameters = new HashMap<String, String>();
        MediaDrm.KeyRequest request =
        	mDrm.getKeyRequest(session.array(), data, mime,
        					   MediaDrm.KEY_TYPE_STREAMING,
        					   optionalParameters);
        String result = (request != null) ? "successed" : "failed";
        Log.d(LOGTAG, "getKeyRequest " + result + "!");
        return request;
    }    
    
    private class MediaDrmListener implements MediaDrm.OnEventListener {
        @Override
        public void onEvent(MediaDrm mediaDrm, byte[] sessionArray, int event,
        					int extra, byte[] data) {
        	Log.e(LOGTAG, "MediaDrmListener:onEvent() is called !!!!! ");
            if (sessionArray == null) {
                Log.e(LOGTAG, "MediaDrmListener: Null session.");
                return;
            }
            ByteBuffer session = ByteBuffer.wrap(sessionArray);
        	// TODO : Check the following error
//            if (!sessionExists(session)) {
//                Log.e(LOGTAG, "MediaDrmListener: Invalid session.");
//                return;
//            }
            String sessionId = mSessionIds.get(session);
//            if (sessionId == null || sessionId == INVALID_SESSION_ID) {
//                Log.e(LOGTAG, "MediaDrmListener: Invalid session ID.");
//                return;
//            }
            switch(event) {
	            case MediaDrm.EVENT_PROVISION_REQUIRED:
	                Log.d(LOGTAG, "MediaDrm.EVENT_PROVISION_REQUIRED");
	                break;
	            case MediaDrm.EVENT_KEY_REQUIRED:
	                Log.d(LOGTAG, "MediaDrm.EVENT_KEY_REQUIRED");
	                if (mProvisioning) {
	                    return;
	                }
	                //String mime = mSessionMimeTypes.get(session);
	                MediaDrm.KeyRequest request = null;
//	                try {
//	                    request = getKeyRequest(session, data, mime);
//	                } catch (android.media.NotProvisionedException e) {
//	                    Log.e(LOGTAG, "Device not provisioned", e);
//	                    startProvisioning();
//	                    return;
//	                }
	                if (request != null) {
	                    //onSessionMessage(sessionId, request);
	                } else {
	                    //onSessionError(sessionId);
	                }
	                break;
	            case MediaDrm.EVENT_KEY_EXPIRED:
	                Log.d(LOGTAG, "MediaDrm.EVENT_KEY_EXPIRED");
	                //onSessionError(sessionId);
	                break;
	            case MediaDrm.EVENT_VENDOR_DEFINED:
	                Log.d(LOGTAG, "MediaDrm.EVENT_VENDOR_DEFINED");
	                assert false;  // Should never happen.
	                break;
	            default:
	                Log.e(LOGTAG, "Invalid DRM event " + event);
	                return;
            }
        }
    }
    
    private ByteBuffer openSession() throws android.media.NotProvisionedException {
        assert mDrm != null;
        try {
            byte[] sessionId = mDrm.openSession();
            // ByteBuffer.wrap() is backed by the byte[]. Make a clone here in
            // case the underlying byte[] is modified.
            return ByteBuffer.wrap(sessionId.clone());
        } catch (java.lang.RuntimeException e) {  // TODO(xhwang): Drop this?
            Log.e(LOGTAG, "Cannot open a new session", e);
            release();
            return null;
        } catch (android.media.NotProvisionedException e) {
            // Throw NotProvisionedException so that we can startProvisioning().
            throw e;
        } catch (android.media.MediaDrmException e) {
            // Other MediaDrmExceptions (e.g. ResourceBusyException) are not
            // recoverable.
            
            release();
            return null;
        }
    }
    
    private ByteBuffer getSession(String aSessionId) {
        for (ByteBuffer session : mSessionIds.keySet()) {
            if (mSessionIds.get(session).equals(aSessionId)) {
                return session;
            }
        }
        return null;
    }
    
	// TODO : release the resources
    private void release() {
    }
    
    private boolean ensureMediaCryptoCreated(ByteBuffer aSessionId) {
    	if (mCrypto != null) {
    		return true;
    	}
    	try {
        	if (MediaCrypto.isCryptoSchemeSupported(mSchemeUUID)) {
        		final byte [] cryptoSessionId = aSessionId.array();
        		mCrypto = new MediaCrypto(mSchemeUUID, cryptoSessionId);
        		String strCrypteSessionId = new String(aSessionId.array());
        		Log.d(LOGTAG, "MediaCrypto successfully created! - SId " + strCrypteSessionId);
        		return true;
        	} else {
        		Log.e(LOGTAG, "Cannot create MediaCrypto for unsupported scheme.");
        		return false;
        	}
    	} catch (android.media.MediaCryptoException e) {
    		Log.e(LOGTAG, "Cannot create MediaCrypto", e);
    		return false;
    	}
    }
}
