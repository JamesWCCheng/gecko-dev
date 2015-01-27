Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

/**
 * We set up a sample component. The constructor is empty, all the interesting
 * stuff goes in the prototype.
 */
function myTest() { }

myTest.prototype = {
    /**
     * .classID is required for generateNSGetFactory to work correctly.
     * Make sure this CID matches the "component" in your .manifest file.
     */
    classID: Components.ID("{c69f5acd-0f5c-4278-b7c2-bb8b6ec475f6}"),

    /**
     * .classDescription and .contractID are only used for
     * backwards compatibility with Gecko 1.9.2 and
     * XPCOMUtils.generateNSGetModule.
     */
    classDescription: "nsSample: JS version", // any human-readable string
    contractID: "@mozilla.org/jstest;1",

    /**
     * List all the interfaces your component supports.
     * @note nsISupports is generated automatically; you don't need to list it.
     */
    QueryInterface: XPCOMUtils.generateQI([Components.interfaces.nsITest]),

    /*
     * get and set are new Magic in JS1.5, borrowing the intent -- if not
     * the exact syntax -- from the JS2 design.  They define accessors for
     * properties on the JS object, follow the expected rules for prototype
     * delegation, and make a mean cup of coffee.
     */
    get value()       { return this.val; },
    set value(newval) { return this.val = newval; },

    writeValue: function (aPrefix) {
        debug("myTest::writeValue => " + aPrefix + this.val + "\n");
    },
    add: function (a, b) { return a + b; },
    sub: function (a, b) { return a - b; },
    mul: function (a, b) { return a * b; },

    val: "<default value>"
};

/**
 * XPCOMUtils.generateNSGetFactory was introduced in Mozilla 2 (Firefox 4).
 * XPCOMUtils.generateNSGetModule is for Mozilla 1.9.2 (Firefox 3.6).
 */
if (XPCOMUtils.generateNSGetFactory)
    this.NSGetFactory = XPCOMUtils.generateNSGetFactory([myTest]);
else
    var NSGetModule = XPCOMUtils.generateNSGetModule([myTest]);