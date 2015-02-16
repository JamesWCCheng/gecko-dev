#include "TestParent.h"
#include <unistd.h>
#include <pthread.h>
// C++ file contents
namespace mozilla {
namespace dom {
bool
TestParent::RecvChildRequest()
{
  printf(" ==================== RecvChildRequest PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
  return SendParentFinish();
}

void
TestParent::ActorDestroy(ActorDestroyReason aWhy)
{
  printf(" ==================== TestParent::ActorDestroy() PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx, why = %d\n",getpid(),  (unsigned int)pthread_self(), aWhy);
}

MOZ_IMPLICIT TestParent::TestParent()
{
    printf(" ==================== TestParent::TestParent() PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
    MOZ_COUNT_CTOR(TestParent);
}

MOZ_IMPLICIT TestParent::~TestParent()
{
    printf(" ==================== TestParent::~TestParent() PID = %d, TID = %uuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",getpid(),  (unsigned int)pthread_self());
    MOZ_COUNT_DTOR(TestParent);
}

} // namespace dom
} // namespace mozilla