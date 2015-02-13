/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/DOMRequestHelper.jsm");

let debug = (aMsg) => {
  //console.log(aMsg);
  dump(aMsg);
};

const JSWEBIDLTESTER_CID = Components.ID("{1a6338b4-b19e-11e4-849d-382c4ac721d7}");
const JSWEBIDLTESTER_CONTRACTID = "@mozilla.org/js-webidl-test;1";
function Bar(){}
function Foo(){}
Foo.prototype =
{
  __proto__ : Bar.prototype,
};
function JSWebidlTest() {
  dump("==================JSWebidlTest==================\n");
  //console.log("ORZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
  this.ff = new Foo();
  dump(this.ff instanceof Bar);
   dump( "==========================\n");
  dump(this instanceof DOMRequestIpcHelper);
   dump("==========================\n");
};

JSWebidlTest.prototype = {
  //dump("==================JSWebidlTest.prototype==================\n");
  __proto__: DOMRequestIpcHelper.prototype,

  classID: JSWEBIDLTESTER_CID,
  contractID: JSWEBIDLTESTER_CONTRACTID,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupportsWeakReference,
                                         Ci.nsIObserver,
                                         Ci.nsIDOMGlobalPropertyInitializer]),
  get info() {

    return {
      latitude: "123",
      longitude: "456",
      fake: "12345",
     };
  },
  // get ongetlocation() {
  //    dump("==================get ongetlocation==================\n");
  //   return this.__DOM_IMPL__.getEventHandler("ongetlocation");
  // },
  // set ongetlocation(aHandler)
  // {
  //   dump("==================set ongetlocation==================\n");
  //   let self = this;
  //   this.__DOM_IMPL__.setEventHandler("ongetlocation", aHandler);
  //   this._window.setTimeout(
  //       function()
  //       {
  //         let event = new self._window.Event("getlocation");
  //         self.__DOM_IMPL__.dispatchEvent(event);
  //       }, Math.random() * 2000 + 5000);
  // },

   init: function(aWindow) {
    dump("==================init==================\n");
     this.initDOMRequestHelper(aWindow, []);
   },

   uninit: function() {
     dump("==================uninit==================\n");
     let self = this;

     this.forEachPromiseResolver(function(aKey) {
       self.takePromiseResolver(aKey).reject("got destroyed");
     });
   },
   sort: function(unsorted_array) {
       let self = this;
       dump("============unsorted_array========  \n");
        this._window.setTimeout(
        function() 
        {
          //let event = new self._window.Event("getlocation");
          //self.__DOM_IMPL__.dispatchEvent(event);
          let event = new self._window.MyTestEvent("getlocation", {isComplete: true,
                                                                  percentage: 100, msg: "javascript is weird"});
          self.__DOM_IMPL__.dispatchEvent(event);
        }, Math.random() * 2000 + 5000);
     return this._sendPromise(function(aResolverId) {
        dump("============sort========  " + aResolverId +  "\n");
        //self.takePromiseResolver(aResolverId).resolve([1,3,5]);
        self._window.setTimeout(
        function() {
          // We fulfill the promise !
          debug("============sort resolved========  " + aResolverId + "\n");
          self.takePromiseResolver(aResolverId).resolve(Cu.cloneInto([1,3,5], self._window));
          //self.takePromiseResolver(aResolverId).reject("GG");
        }, Math.random() * 2000 + 1000);
     });
   },

   _sendPromise: function(aCallback) {
     let self = this;
     return this.createPromise(function(aResolve, aReject) {
       let resolverId = self.getPromiseResolverId({ resolve: aResolve, reject: aReject });
       //self.takePromiseResolver(resolverId).resolve([1,3,5]);
       aCallback(resolverId);
     });
   }
 };

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([JSWebidlTest]);