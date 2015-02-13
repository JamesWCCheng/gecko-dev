/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/DOMRequestHelper.jsm");

let debug = (aMsg) => { console.log(aMsg);
  dump(aMsg);
};

const JSWEBIDLTESTER_CID = Components.ID("{1a6338b4-b19e-11e4-849d-382c4ac721d7}");
const JSWEBIDLTESTER_CONTRACTID = "@mozilla.org/js-webidl-test;1";


function JSWebidlTest() {
}

JSWebidlTest.prototype = {
  __proto__: DOMRequestIpcHelper.prototype,

  classID: JSWEBIDLTESTER_CID,
  contractID: JSWEBIDLTESTER_CONTRACTID,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupportsWeakReference,
                                         Ci.nsIObserver,
                                         Ci.nsIDOMGlobalPropertyInitializer]),
  get info() {
    return {
      latitude: "123",
      longitude: "456"
     };
  },
  get ongetlocation() {
    return this.__DOM_IMPL__.getEventHandler("ongetlocation");
  },
  set ongetlocation(aHandler) {
    this.__DOM_IMPL__.setEventHandler("ongetlocation", aHandler);
  },
 };

   init: function(aWindow) {
     this.initDOMRequestHelper(aWindow, []);
   },
  
   uninit: function() {
     let self = this;

     this.forEachPromiseResolver(function(aKey) {
       self.takePromiseResolver(aKey).reject("got destroyed");
     });
   },
   sort: function(unsorted_array) {

     return this._sendPromise(function(aResolverId) {
        debug(aResolverId);
        let self = this;
        window.setTimeout(
        function() {
          // We fulfill the promise !
          self.takePromiseResolver(aResolverId).resolve([1,3,5]]);
        }, Math.random() * 2000 + 1000);
       });
     });
   },

   _sendPromise: function(aCallback) {
     let self = this;
     return this.createPromise(function(aResolve, aReject) {
       let resolverId = self.getPromiseResolverId({ resolve: aResolve, reject: aReject });
       aCallback(resolverId);
     });
   }
 };

 this.NSGetFactory = XPCOMUtils.generateNSGetFactory([JSWebidlTest]);
