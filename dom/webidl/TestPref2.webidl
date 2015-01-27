[Constructor,Constructor(DOMString str), Pref="dom.Test.enabled"]
interface TestPref2 : EventTarget {
    attribute DOMString helloStr;
    readonly attribute DOMString readonlyStr;
    DOMString sayHello();
    long randNum();
    long add(long a, long b);
    long mul(long a, long b);
    long sub(long a, long b);
    [Throws, Pref="dom.Test.div.enabled"]
    double div(long a, long b);
    attribute EventHandler oncomplete;
    attribute EventHandler onprogress;
    attribute EventHandler onerror;
    [Throws]
    Promise<sequence<long>> sort(sequence<long> data);
};