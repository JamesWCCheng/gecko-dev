#include "TestChild.h"
#include "mozilla/dom/ContentChild.h"
#include <unistd.h>
#include <pthread.h>

// C++ file contents
namespace mozilla {
namespace dom {
bool
TestChild::RecvParentFinish()
{
  printf(" ==================== RecvParentFinish PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
  return true;
}

MOZ_IMPLICIT TestChild::TestChild()
{
    ContentChild::GetSingleton()->SendPTestConstructor(this);
    printf(" ==================== TestChild::TestChild() PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
    MOZ_COUNT_CTOR(TestChild);
}

auto TestChild::Recv__delete__() -> bool
{
    printf(" ==================== TestChild::Recv__delete__() PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
    return true;
}

MOZ_IMPLICIT TestChild::~TestChild()
{
    printf(" ==================== TestChild::~TestChild() PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
    MOZ_COUNT_DTOR(TestChild);
}

} // namespace dom
} // namespace mozilla