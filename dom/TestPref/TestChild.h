#ifndef TestChild_h
#define TestChild_h

#include "mozilla/dom/PTestChild.h"
// Header file contents
namespace mozilla {
namespace dom {
class TestChild :
    public PTestChild
{
public:
    virtual bool
    RecvParentFinish();
    virtual bool
    Recv__delete__();
    MOZ_IMPLICIT TestChild();
    virtual ~TestChild();
};
} // namespace dom
} // namespace mozilla

#endif // ifndef TestChild_h
