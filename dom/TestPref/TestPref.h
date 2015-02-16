/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_TestPref_h
#define mozilla_dom_TestPref_h

#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/DOMEventTargetHelper.h" //For DOMEventTargetHelper
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"
#include "nsCOMPtr.h" // For already_AddRefed
#include <unistd.h>
#include <pthread.h>
#include "mozilla/dom/Hello.h"
struct JSContext;

namespace mozilla {
namespace dom {
class GlobalObject;
class MyTimerCallback;
class Promise;
//class TestPref MOZ_FINAL : public EventTarget
class TestPref MOZ_FINAL : public DOMEventTargetHelper
{
public:
NS_DECL_ISUPPORTS_INHERITED
  //Fail
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(TestPref, DOMEventTargetHelper)
  //NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(TestPref, DOMEventTargetHelper)???????????????????????????

public:
  TestPref();
    TestPref(nsISupports* aParent);
    TestPref(nsISupports* aParent, const nsAString& aStr);
    TestPref(nsPIDOMWindow* aWindow, const nsAString& aStr);

protected:
  ~TestPref();
  public:
  enum class STATES
    : char {
      PROGRESS_CHANGE,
    ERROR_OCCURR,
    COMPLETE,
  };
  // TODO: return something sensible here, and change the return type
  nsISupports*
  GetParentObject() const
  {
    return mParent;
  }

  virtual JSObject*
  WrapObject(JSContext* aCx) MOZ_OVERRIDE;

  static already_AddRefed<TestPref>
  Constructor(const GlobalObject& global, ErrorResult& aRv);
  static already_AddRefed<TestPref>
  Constructor(const GlobalObject& global, const nsAString& str,
      ErrorResult& aRv);

  void
  GetHelloStr(nsString& aRetVal) const;

  void
  SetHelloStr(const nsAString& arg);

  void
  GetReadonlyStr(nsString& aRetVal) const;

  void
  SayHello(nsString& aRetVal);

  int32_t
  RandNum();

  int32_t
  Add(int32_t a, int32_t b);

  int32_t
  Mul(int32_t a, int32_t b);

  int32_t
  Sub(int32_t a, int32_t b);

  double
  Div(int32_t a, int32_t b, ErrorResult& aRv);

  /****************************************************************************
   * Event Handlers
   ***************************************************************************/
  IMPL_EVENT_HANDLER(complete);
  // already_AddRefed<EventHandlerNonNull> GetOncomplete() const;

  // void SetOncomplete(EventHandlerNonNull* arg);

  IMPL_EVENT_HANDLER(progress);
  // already_AddRefed<EventHandlerNonNull> GetOnprogress() const;

  // void SetOnprogress(EventHandlerNonNull* arg);

  IMPL_EVENT_HANDLER(error);
  // already_AddRefed<EventHandlerNonNull> GetOnerror() const;

  // void SetOnerror(EventHandlerNonNull* arg);
  void
  RunnableProc(TestPref* aThis) {
    printf(" ==================== RunnableProc Running PID = %d, TID = %u====================\n",getpid(), (unsigned int)pthread_self());
    nsRefPtr<TestPref> thisObject = static_cast<TestPref*>(aThis);
    thisObject->DispatchMyTestEvent(NS_LITERAL_STRING("complete"), true, 100);
  }
  // Return a raw pointer here to avoid refcounting, but make sure it's safe (the object should be kept alive by the callee).
  already_AddRefed<Promise> Sort(const Sequence<int32_t>& data, ErrorResult& aRv);

  already_AddRefed<mozilla::dom::Hello> Hello() const
  {
    nsRefPtr<mozilla::dom::Hello> hello = new mozilla::dom::Hello(GetParentObject());
    return hello.forget();
  }
  void SetHello(mozilla::dom::Hello& arg)
  {
    return;
  }
private:
  //Sequence<int32_t> mResult;
  nsRefPtr<Promise> mPromise;
  nsCOMPtr<nsISupports> mParent;
  nsString hStr;
  // The actual timer object
  nsCOMPtr<nsITimer> mTimer;
  nsCOMPtr<nsITimer> mTimer2;
  nsRefPtr<MyTimerCallback> cb;
  STATES mStates;

  static void
  TimerCallback(nsITimer *aTimer, void *aClosure)
  {
    static int progress = 0;
    nsRefPtr<TestPref> thisObject = static_cast<TestPref*>(aClosure);
    printf(" ==================== TimerCallback Running PID = %d, TID = %u====================\n",getpid(), (unsigned int)pthread_self());

    if(progress == 100) {
      thisObject->mStates = STATES::COMPLETE;
      printf(" ======== Complete ======== \n");
      thisObject->DispatchMyTestEvent(NS_LITERAL_STRING("complete"), true, 100);
      aTimer->Cancel();
    }
    else if(progress == 50) {
      thisObject->mStates = STATES::ERROR_OCCURR;
      printf(" ======== progress = %d ======== \n", progress);
      thisObject->DispatchMyTestEvent(NS_LITERAL_STRING("error"), false, progress);
    }
    else {
      thisObject->mStates = STATES::PROGRESS_CHANGE;
      printf(" ======== progress = %d ======== \n", progress);
      thisObject->DispatchMyTestEvent(NS_LITERAL_STRING("progress"), false, progress);
    }
    progress += 5;

  }
  nsresult InitTimer(uint32_t aDelay);
  nsresult InitTimerCallbackFunc(uint32_t aDelay);
  nsresult DispatchMyTestEvent(const nsAString& aType, bool aIsComplete, uint64_t aPercentage);
};

class MyTimerCallback MOZ_FINAL : public nsITimerCallback
{
  ~MyTimerCallback()
  {
  }
public:
  MyTimerCallback(TestPref* aObj);

NS_DECL_ISUPPORTS

  NS_IMETHODIMP
  Notify(nsITimer* aTimer) MOZ_FINAL
  {
    printf(
        " ==================== Thread Running PID = %d, TID = %u====================\n",
        getpid(), (unsigned int) pthread_self());
    return NS_OK;
  }

  //nsresult DispatchMyTestEvent(const nsAString& aType);
private:
  TestPref* mContent;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_TestPref_h
