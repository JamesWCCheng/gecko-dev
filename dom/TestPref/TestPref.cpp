/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "mozilla/dom/Promise.h"
#include "mozilla/dom/TestPref.h"
#include "mozilla/dom/TestPrefBinding.h"
#include "mozilla/dom/BindingDeclarations.h" // For GlobalObject
#include "mozilla/dom/MyTestEvent.h"
//#include "mozilla/dom/PTestChild.h"
//#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/TestChild.h"
#include <cstdlib> // For rand() and srand()
#include <ctime> // For time()
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsTest.h"
#include "nsITest.h"
#include <vector>
#include <algorithm>

namespace mozilla {
namespace dom {

/*
NS_IMPL_CYCLE_COLLECTION_INHERITED_0(TestPref, EventTarget)
NS_IMPL_ADDREF_INHERITED(TestPref, EventTarget)
NS_IMPL_RELEASE_INHERITED(TestPref, EventTarget)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TestPref)
NS_INTERFACE_MAP_END_INHERITING(EventTarget)
*/

NS_IMPL_ADDREF_INHERITED(TestPref, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(TestPref, DOMEventTargetHelper)

//NS_IMPL_CYCLE_COLLECTION_INHERITED_0(TestPref, DOMEventTargetHelper)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(TestPref)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)
//Fail
NS_IMPL_CYCLE_COLLECTION_INHERITED(TestPref, DOMEventTargetHelper, mParent,
                                   mTimer, mTimer2, cb)

NS_IMPL_ISUPPORTS(MyTimerCallback, nsITimerCallback)
StaticAutoPtr<TestChild> testChild;
MyTimerCallback::MyTimerCallback(TestPref* aObj) : mContent(aObj)
{

}

nsresult TestPref::DispatchMyTestEvent(const nsAString& aType, bool aIsComplete, uint64_t aPercentage)
{
    MyTestEventInit init;
    init.mIsComplete = aIsComplete;
    init.mPercentage = aPercentage;
    if(mStates == STATES::COMPLETE) {
      init.mMsg = nsString(NS_LITERAL_STRING("COMPLETE"));
    }
    else if(mStates == STATES::ERROR_OCCURR) {
      init.mMsg = nsString(NS_LITERAL_STRING("ERROR_OCCURR"));
    }
    else if(mStates == STATES::PROGRESS_CHANGE) {
      init.mMsg = nsString(NS_LITERAL_STRING("PROGRESS_CHANGE"));
    }


    nsRefPtr<MyTestEvent> event =
      MyTestEvent::Constructor(this, aType, init);
    event->SetTrusted(true);
    return DispatchTrustedEvent(event);
  }


TestPref::TestPref(nsISupports* aParent)
  ://DOMEventTargetHelper(aParent)
  mParent(aParent)
{
  printf("TestPref::TestPref(nsISupports* aParent):mParent(aParent) \n");
}

TestPref::TestPref(nsISupports* aParent, const nsAString& aStr)
  ://DOMEventTargetHelper(aParent)
  mParent(aParent)
  , hStr(aStr)
{
  cb = new MyTimerCallback(this);
  //mTimer = do_CreateInstance("@mozilla.org/timer;1");
  mTimer2 = do_CreateInstance("@mozilla.org/timer;1");
  printf(" ==================== TestPref::TestPref Main Thread Running PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
}

TestPref::TestPref(nsPIDOMWindow* aWindow, const nsAString& aStr)
  :DOMEventTargetHelper(aWindow)
  , hStr(aStr)
{
  cb = new MyTimerCallback(this);
  //mTimer = do_CreateInstance("@mozilla.org/timer;1");
  mTimer2 = do_CreateInstance("@mozilla.org/timer;1");
  printf(" ==================== TestPref::TestPref nsPIDOMWindow Main Thread Running PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
}

TestPref::TestPref()
{
  printf("TestPref::TestPref()");
}

TestPref::~TestPref()
{
}

void
TestPref::GetHelloStr(nsString& aRetVal) const
{
  aRetVal = hStr;
}

void
TestPref::SetHelloStr(const nsAString& arg)
{
  hStr.Assign(arg);
}

void
TestPref::GetReadonlyStr(nsString& aRetVal) const
{
  aRetVal = nsString(NS_LITERAL_STRING("This string can be read only!"));
}

void
TestPref::SayHello(nsString& aRetVal)
{
  aRetVal = nsString(NS_LITERAL_STRING("TestPref FireFox!"));
//  printf("=======================%S \n", nsString(NS_LITERAL_STRING("TestPref FireFox!")).get());
//  printf("=======================%ls \n", nsString(NS_LITERAL_STRING("TestPref FireFox!")).get());
//  wprintf(L"=======================%ls \n", nsString(NS_LITERAL_STRING("TestPref FireFox!")).get());
//  printf("%s\n", NS_ConvertUTF16toUTF8(
//      nsString(NS_LITERAL_STRING("TestPref FireFox!"))
//      ).get());


  testChild = new TestChild();

  testChild->SendChildRequest();
  printf(" ==================== SendChildRequest PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
}
// typedef void* (*ThreadPrototype)(void*);
// void ThreadProc(void * aThsiObj) {

// printf(" ==================== PThread Running In +++++===================\n");
//   nsRefPtr<TestPref> thisObject = static_cast<TestPref*>(aThsiObj);
//   printf(" ==================== PThread Running In ----===================\n");
//   while(true) 
//   {

//   //printf(" ==================== PThread Running ====================\n");
//     sleep(5);

//      nsCOMPtr<nsIRunnable> runnable =
//     NS_NewRunnableMethodWithArg<TestPref*>((TestPref*)(aThsiObj),
//                                                        &TestPref::RunnableProc,
//                                                        thisObject);
//     NS_DispatchToMainThread(runnable);

//   }
// }
int32_t
TestPref::RandNum()
{
  //auto result = InitTimer(3000);
  InitTimerCallbackFunc(2000);
   // nsRefPtr<TestPref> thisObject = static_cast<TestPref*>(this);


  // printf(" ==================== result = %d ====================\n", result);
  // if(NS_SUCCEEDED(result)) {
  //   printf(" ==================== result success ====================\n");
  // }
  // if(NS_SUCCEEDED(result2)) {
  //   printf(" ==================== result2 success ====================\n");
  // }
  // pthread_t id;
  // int ret;
  // printf(" ==================== PThread Running ++++===================\n");
  //  ret=pthread_create(&id, NULL, (ThreadPrototype)ThreadProc, (void*)this);
   printf(" ==================== PThread Running ----====================\n");
  //std::srand(std::time(0));//Use current time as seed for random generator
  static bool init = false;
  if(!init){
    std::srand(std::time(0));//Use current time as seed for random generator
    init = true;
  }
      int32_t result = 0;
      nsresult rv;
  //return std::rand()%100;

 nsCOMPtr<nsITest> tester =  do_CreateInstance(NS_TEST_CONTRACTID, &rv);
 if(NS_SUCCEEDED(rv)) {

    tester->Add(5, 10, &result);
 }
 else {
  printf("========== GG do_CreateInstance ==========");
 }
nsCOMPtr<nsITest> tester2 =  do_CreateInstance(NS_TEST_CONTRACTID, &rv);
if(NS_SUCCEEDED(rv)) {
  printf("@@@@@@@@@@@@@@@@@@@ tester = %p, tester2 = %p \n", tester.get(), tester2.get());
 }
 nsCOMPtr<nsITest> tester3 =  do_GetService(NS_TEST_CONTRACTID, &rv);
 nsCOMPtr<nsITest> tester4 =  do_GetService(NS_TEST_CONTRACTID, &rv);
if(NS_SUCCEEDED(rv)) {
    printf("@@@@@@@@@@@@@@@@@@@ tester3 = %p, tester4 = %p \n", tester3.get(), tester4.get());
    tester3->Add(10, 10, &result);
 }

nsCOMPtr<nsITest> jsTester =  do_CreateInstance("@mozilla.org/jstest;1", &rv);

if(NS_SUCCEEDED(rv)) {
    printf("@@@@@@@@@@@@@@@@@@@ jsTester = %p, tester4  \n", jsTester.get());
    jsTester->Add(10, 600, &result);
 }
nsCOMPtr<nsITest> jsTester2 =  do_GetService("@mozilla.org/jstest;1", &rv);
nsCOMPtr<nsITest> jsTester3 =  do_GetService("@mozilla.org/jstest;1", &rv);
if(NS_SUCCEEDED(rv)) {
    printf("@@@@@@@@@@@@@@@@@@@ jsTester = %p, jsTester2 = %p \n", jsTester2.get(), jsTester3.get());
    jsTester2->Add(10, 500, &result);
 }

 return result;
}

int32_t
TestPref::Add(int32_t a, int32_t b)
{
  return a + b;
}

int32_t
TestPref::Sub(int32_t a, int32_t b)
{
  return a - b;
}

int32_t
TestPref::Mul(int32_t a, int32_t b)
{
  return a * b;
}

double
TestPref::Div(int32_t a, int32_t b, ErrorResult& aRv)
{
  if(!b){
    //if b is zero, then throws an error
    aRv.Throw(NS_ERROR_ILLEGAL_VALUE);
    // See what kinds of error type you can throw here:
    // https://developer.mozilla.org/en-US/docs/Mozilla/Errors
  }
  return (double)a / b;
}

JSObject*
TestPref::WrapObject(JSContext* aCx)
{
  printf("TestPref::WrapObject \n");
  return TestPrefBinding::Wrap(aCx, this);
}

already_AddRefed<TestPref>
TestPref::Constructor(const GlobalObject& global, ErrorResult& aRv)
{
  printf("TestPref::Constructor(const GlobalObject& global, ErrorResult& aRv) \n");
  nsRefPtr<TestPref> obj = new TestPref(global.GetAsSupports());
  return obj.forget();
}

already_AddRefed<TestPref>
TestPref::Constructor(const GlobalObject& global, const nsAString& str, ErrorResult& aRv)
{
  printf("TestPref::Constructor(const GlobalObject& global, const nsAString& str, ErrorResult& aRv) \n");
  nsRefPtr<TestPref> obj = new TestPref(global.GetAsSupports(), str);
  obj->mStates = STATES::PROGRESS_CHANGE;
  return obj.forget();
}
nsresult
TestPref::InitTimer(uint32_t aDelay)
{
  printf(" ==================== InitTimer ====================\n");
  return mTimer->InitWithCallback(cb, aDelay,
                                      nsITimer::TYPE_REPEATING_PRECISE);
}
nsresult
TestPref::InitTimerCallbackFunc(uint32_t aDelay)
{
  printf(" ==================== InitTimerCallbackFunc ====================\n");
  return mTimer2->InitWithFuncCallback(TimerCallback, this, aDelay,
                                      nsITimer::TYPE_REPEATING_PRECISE);
}
already_AddRefed<Promise>
TestPref::Sort(const Sequence<int32_t>& data, ErrorResult& aRv)
{
   Sequence<int32_t> result;
  std::vector<int32_t> data_clone;
  for(size_t i = 0; i < data.Length(); i++) {
    printf("******************   %d\n", data[i]);
      data_clone.push_back(data[i]);
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
    if(global == nullptr) {
    printf("global nullptr *************************GG\n");
  }

  MOZ_ASSERT(global);
  mPromise = Promise::Create(global, aRv);

  std::sort(data_clone.begin(), data_clone.end());

  for(auto i : data_clone) {
    printf("sort******************   %d\n", i);
    result.AppendElement(i);
  }

  for(size_t i = 0; i < result.Length(); i++) {
    printf("RRRR******************   %d\n", result[i]);
  }

  //mResult
  mPromise->MaybeResolve(result);
  return mPromise.forget();
  //mPromise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
}
} // namespace dom
} // namespace mozilla