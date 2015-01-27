[Constructor(DOMString type, optional MyTestEventInit eventInitDict)]
interface MyTestEvent : Event
{
  readonly attribute boolean isComplete;
  readonly attribute unsigned long long percentage;
  readonly attribute DOMString msg;
};

dictionary MyTestEventInit : EventInit
{
  boolean isComplete = false;
  unsigned long long percentage = 0;
  DOMString msg = "";
};