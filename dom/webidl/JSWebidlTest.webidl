/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */
dictionary LocationInfo {
    DOMString latitude;
    DOMString longitude;
  };
[NavigatorProperty="jswebidltest",
JSImplementation="@mozilla.org/js-webidl-test;1"]
interface JSWebidlTest : EventTarget {
    //LocationInfo
    readonly attribute any info;
    attribute EventHandler ongetlocation;
    Promise<sequence<long>> sort(sequence<long> data);
};