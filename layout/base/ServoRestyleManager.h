/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ServoRestyleManager_h
#define mozilla_ServoRestyleManager_h

#include "mozilla/DocumentStyleRootIterator.h"
#include "mozilla/EventStates.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoElementSnapshot.h"
#include "nsChangeHint.h"
#include "nsHashKeys.h"
#include "nsINode.h"
#include "nsISupportsImpl.h"
#include "nsPresContext.h"

namespace mozilla {
namespace dom {
class Element;
} // namespace dom
} // namespace mozilla
class nsAttrValue;
class nsIAtom;
class nsIContent;
class nsIFrame;
class nsStyleChangeList;

namespace mozilla {

/**
 * Restyle manager for a Servo-backed style system.
 */
class ServoRestyleManager : public RestyleManager
{
  friend class ServoStyleSet;
public:
  typedef RestyleManager base_type;

  explicit ServoRestyleManager(nsPresContext* aPresContext);

  void PostRestyleEvent(dom::Element* aElement,
                        nsRestyleHint aRestyleHint,
                        nsChangeHint aMinChangeHint);
  void PostRestyleEventForLazyConstruction();
  void RebuildAllStyleData(nsChangeHint aExtraHint,
                           nsRestyleHint aRestyleHint);
  void PostRebuildAllStyleDataEvent(nsChangeHint aExtraHint,
                                    nsRestyleHint aRestyleHint);
  void ProcessPendingRestyles();

  void ContentInserted(nsINode* aContainer, nsIContent* aChild);
  void ContentAppended(nsIContent* aContainer,
                       nsIContent* aFirstNewContent);
  void ContentRemoved(nsINode* aContainer,
                      nsIContent* aOldChild,
                      nsIContent* aFollowingSibling);

  void RestyleForInsertOrChange(nsINode* aContainer,
                                nsIContent* aChild);
  void RestyleForAppend(nsIContent* aContainer,
                        nsIContent* aFirstNewContent);
  nsresult ContentStateChanged(nsIContent* aContent,
                               EventStates aStateMask);
  void AttributeWillChange(dom::Element* aElement,
                           int32_t aNameSpaceID,
                           nsIAtom* aAttribute,
                           int32_t aModType,
                           const nsAttrValue* aNewValue);

  void AttributeChanged(dom::Element* aElement, int32_t aNameSpaceID,
                        nsIAtom* aAttribute, int32_t aModType,
                        const nsAttrValue* aOldValue);

  nsresult ReparentStyleContext(nsIFrame* aFrame);

  /**
   * Gets the appropriate frame given a content and a pseudo-element tag.
   *
   * Right now only supports a null tag, before or after. If the pseudo-element
   * is not null, the content needs to be an element.
   */
  static nsIFrame* FrameForPseudoElement(const nsIContent* aContent,
                                         nsIAtom* aPseudoTagOrNull);

  /**
   * Clears the ServoElementData and HasDirtyDescendants from all elements
   * in the subtree rooted at aElement.
   */
  static void ClearServoDataFromSubtree(Element* aElement);

protected:
  ~ServoRestyleManager() override
  {
    MOZ_ASSERT(!mReentrantChanges);
  }

private:
  /**
   * Traverses a tree of content that Servo has just restyled, recreating style
   * contexts for their frames with the new style data.
   */
  void RecreateStyleContexts(Element* aElement,
                             nsStyleContext* aParentContext,
                             ServoStyleSet* aStyleSet,
                             nsStyleChangeList& aChangeList);

  void RecreateStyleContextsForText(nsIContent* aTextNode,
                                    nsStyleContext* aParentContext,
                                    ServoStyleSet* aStyleSet);

  inline ServoStyleSet* StyleSet() const
  {
    MOZ_ASSERT(PresContext()->StyleSet()->IsServo(),
               "ServoRestyleManager should only be used with a Servo-flavored "
               "style backend");
    return PresContext()->StyleSet()->AsServo();
  }

  // We use a separate data structure from nsStyleChangeList because we need a
  // frame to create nsStyleChangeList entries, and the primary frame may not be
  // attached yet.
  struct ReentrantChange {
    nsCOMPtr<nsIContent> mContent;
    nsChangeHint mHint;
  };
  typedef AutoTArray<ReentrantChange, 10> ReentrantChangeList;

  // Only non-null while processing change hints. See the comment in
  // ProcessPendingRestyles.
  ReentrantChangeList* mReentrantChanges;
};

} // namespace mozilla

#endif // mozilla_ServoRestyleManager_h
