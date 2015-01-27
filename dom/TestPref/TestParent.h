#ifndef TestParent_h
#define TestParent_h

#include "mozilla/dom/PTestParent.h"
// Header file contents
namespace mozilla {
namespace dom {
class TestParent :
    public PTestParent
{
public:
    virtual bool
    RecvChildRequest();

    virtual void
    ActorDestroy(ActorDestroyReason aWhy);

    MOZ_IMPLICIT TestParent();
    virtual ~TestParent();
};
} // namespace dom
} // namespace mozilla



#endif // ifndef TestParent_h
