/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Hello.h"
#include "mozilla/dom/HelloBinding.h"
#include <cstdlib> // For rand() and srand()
#include <ctime> // For time()

#include "mozilla/dom/BindingDeclarations.h" // For GlobalObject

namespace mozilla {
namespace dom {


NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(Hello)
NS_IMPL_CYCLE_COLLECTING_ADDREF(Hello)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Hello)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Hello)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
Hello::Hello(nsISupports* aParent):mParent(aParent)
{
}

Hello::Hello(nsISupports* aParent, const nsAString& aStr):mParent(aParent), mStr(aStr)
{
}

Hello::Hello()
{
}

Hello::~Hello()
{
}
void
Hello::GetHelloStr(nsString& aRetVal) const
{
  //aRetVal = (hStr)? hStr : new nsString(NS_LITERAL_STRING("empty"));
  aRetVal = mStr;
}

void
Hello::SetHelloStr(const nsAString& arg)
{
  /*
  if(arg){
      hStr.AssignWithConversion(arg);
  }
  */
  mStr.Assign(arg);
}

void
Hello::GetReadonlyStr(nsString& aRetVal) const
{
  aRetVal = nsString(NS_LITERAL_STRING("This string can be read only!"));
}

void
Hello::SayHello(nsString& aRetVal)
{
  aRetVal = nsString(NS_LITERAL_STRING("Hello FireFox!"));
}

int32_t
Hello::RandNum()
{
  //std::srand(std::time(0));//Use current time as seed for random generator
  static bool init = false;
  if(!init){
    std::srand(std::time(0));//Use current time as seed for random generator
    init = true;
  }
  return rand()%100;
}

int32_t
Hello::Add(double a, double b)
{
  return a + b;
}

int32_t
Hello::Sub(double a, double b)
{
  return a - b;
}

int32_t
Hello::Mul(double a, double b)
{
  return a * b;
}

double
Hello::Div(double a, double b, ErrorResult& aRv)
{
  printf("%f James Test", a);
  if(!b){
    //if b is zero, then throws an error
    aRv.Throw(NS_ERROR_ILLEGAL_VALUE);
    // See what kinds of error type you can throw here:
    // https://developer.mozilla.org/en-US/docs/Mozilla/Errors
  }
  return (double)a / b;
}

JSObject*
Hello::WrapObject(JSContext* aCx)
{
  return HelloBinding::Wrap(aCx, this);
}
/* static */ already_AddRefed<Hello>
Hello::Constructor(const GlobalObject& global, ErrorResult& aRv)
{
  nsRefPtr<Hello> obj = new Hello(global.GetAsSupports());
  return obj.forget();
}

/* static */ already_AddRefed<Hello>
Hello::Constructor(const GlobalObject& global, const nsAString& str, ErrorResult& aRv)
{
  nsRefPtr<Hello> obj = new Hello(global.GetAsSupports(), str);
  return obj.forget();
}


} // namespace dom
} // namespace mozilla
