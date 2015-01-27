#ifndef nsTest_h
#define nsTest_h
#include "nsITest.h"
#include "mozilla/Attributes.h"

#define NS_TEST_CID \
{ 0xd8a0f960, 0xa381, 0x42a0, { 0xa0, 0x6c, 0x81, 0xd6, 0xdb, 0x16, 0x1c, 0xce } }

#define NS_TEST_CONTRACTID "@mozilla.org/test;1"

class nsTest : public nsITest
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSITEST

  nsTest();

private:
  ~nsTest();
  char* mValue;
protected:
  /* additional members */
};
#endif