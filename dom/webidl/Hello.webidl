[Constructor,Constructor(DOMString str)]
interface Hello {
    attribute DOMString helloStr;
    readonly attribute DOMString readonlyStr;
    DOMString sayHello();
    long randNum();
    long add(double a, double b);
    long sub(double a, double b);
    long mul(double a, double b);
    [Throws]
    double div(double a, double b);
};