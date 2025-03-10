/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ServoStyleSet.h"

#include "mozilla/DocumentStyleRootIterator.h"
#include "mozilla/ServoRestyleManager.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "nsAnimationManager.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSPseudoElements.h"
#include "nsHTMLStyleSheet.h"
#include "nsIDocumentInlines.h"
#include "nsPrintfCString.h"
#include "nsStyleContext.h"
#include "nsStyleSet.h"

using namespace mozilla;
using namespace mozilla::dom;

ServoStyleSet::ServoStyleSet()
  : mPresContext(nullptr)
  , mBatching(0)
{
}

void
ServoStyleSet::Init(nsPresContext* aPresContext)
{
  mPresContext = aPresContext;
  mRawSet.reset(Servo_StyleSet_Init(aPresContext));

  // Now that we have an mRawSet, go ahead and notify about whatever stylesheets
  // we have so far.
  for (auto& sheetArray : mSheets) {
    for (auto& sheet : sheetArray) {
      // There's no guarantee this will create a list on the servo side whose
      // ordering matches the list that would have been created had all those
      // sheets been appended/prepended/etc after we had mRawSet.  But hopefully
      // that's OK (e.g. because servo doesn't care about the relative ordering
      // of sheets from different cascade levels in the list?).
      MOZ_ASSERT(sheet->RawSheet(), "We should only append non-null raw sheets.");
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(), sheet->RawSheet(), false);
    }
  }

  // No need to Servo_StyleSet_FlushStyleSheets because we just created the
  // mRawSet, so there was nothing to flush.
}

void
ServoStyleSet::BeginShutdown()
{
  // It's important to do this before mRawSet is released, since that will cause
  // a RuleTree GC, which needs to happen after we have dropped all of the
  // document's strong references to RuleNodes.  We also need to do it here,
  // in BeginShutdown, and not in Shutdown, since Shutdown happens after the
  // frame tree has been destroyed, but before the script runners that delete
  // native anonymous content (which also could be holding on the RuleNodes)
  // have run.  By clearing style here, before the frame tree is destroyed,
  // the AllChildrenIterator will find the anonymous content.
  //
  // Note that this is pretty bad for performance; we should find a way to
  // get by with the ServoNodeDatas being dropped as part of the document
  // going away.
  DocumentStyleRootIterator iter(mPresContext->Document());
  while (Element* root = iter.GetNextStyleRoot()) {
    ServoRestyleManager::ClearServoDataFromSubtree(root);
  }
}

void
ServoStyleSet::Shutdown()
{
  mRawSet = nullptr;
}

bool
ServoStyleSet::GetAuthorStyleDisabled() const
{
  return false;
}

nsresult
ServoStyleSet::SetAuthorStyleDisabled(bool aStyleDisabled)
{
  MOZ_CRASH("stylo: not implemented");
}

void
ServoStyleSet::BeginUpdate()
{
  ++mBatching;
}

nsresult
ServoStyleSet::EndUpdate()
{
  MOZ_ASSERT(mBatching > 0);
  if (--mBatching > 0) {
    return NS_OK;
  }

  Servo_StyleSet_FlushStyleSheets(mRawSet.get());
  return NS_OK;
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleFor(Element* aElement,
                               nsStyleContext* aParentContext,
                               LazyComputeBehavior aMayCompute)
{
  return GetContext(aElement, aParentContext, nullptr,
                    CSSPseudoElementType::NotPseudo, aMayCompute);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::GetContext(nsIContent* aContent,
                          nsStyleContext* aParentContext,
                          nsIAtom* aPseudoTag,
                          CSSPseudoElementType aPseudoType,
                          LazyComputeBehavior aMayCompute)
{
  MOZ_ASSERT(aContent->IsElement());
  Element* element = aContent->AsElement();


  ResolveMappedAttrDeclarationBlocks();
  RefPtr<ServoComputedValues> computedValues;
  if (aMayCompute == LazyComputeBehavior::Allow) {
    computedValues =
      Servo_ResolveStyleLazily(element, nullptr, mRawSet.get()).Consume();
  } else {
    computedValues = ResolveServoStyle(element);
  }

  MOZ_ASSERT(computedValues);
  return GetContext(computedValues.forget(), aParentContext, aPseudoTag, aPseudoType,
                    element);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::GetContext(already_AddRefed<ServoComputedValues> aComputedValues,
                          nsStyleContext* aParentContext,
                          nsIAtom* aPseudoTag,
                          CSSPseudoElementType aPseudoType,
                          Element* aElementForAnimation)
{
  // XXXbholley: nsStyleSet does visited handling here.

  // XXXbholley: Figure out the correct thing to pass here. Does this fixup
  // duplicate something that servo already does?
  bool skipFixup = false;

  RefPtr<nsStyleContext> result =
    NS_NewStyleContext(aParentContext, mPresContext, aPseudoTag,
                       aPseudoType, Move(aComputedValues), skipFixup);

  // Ignore animations for print or print preview, and for elements
  // that are not attached to the document tree.
  if (mPresContext->IsDynamic() &&
      aElementForAnimation &&
      aElementForAnimation->IsInComposedDoc()) {
    // Update/build CSS animations in the case where animation properties are
    // changed.
    // FIXME: This isn't right place to update CSS animations. We should do it
    // , presumably, in cascade_node() in servo side and process the initial
    // restyle there too.
    // To do that we need to make updating CSS animations process independent
    // from nsStyleContext. Also we need to make the process thread safe.
    mPresContext->AnimationManager()->UpdateAnimations(result,
                                                       aElementForAnimation);
  }

  return result.forget();
}

void
ServoStyleSet::ResolveMappedAttrDeclarationBlocks()
{
  if (nsHTMLStyleSheet* sheet = mPresContext->Document()->GetAttributeStyleSheet()) {
    sheet->CalculateMappedServoDeclarations();
  }
}

bool
ServoStyleSet::PrepareAndTraverseSubtree(RawGeckoElementBorrowed aRoot,
                                         mozilla::TraversalRootBehavior aRootBehavior) {
  ResolveMappedAttrDeclarationBlocks();

  // Get the Document's root element to ensure that the cache is valid before
  // calling into the (potentially-parallel) Servo traversal, where a cache hit
  // is necessary to avoid a data race when updating the cache.
  mozilla::Unused << aRoot->OwnerDoc()->GetRootElement();

  MOZ_ASSERT(!sInServoTraversal);
  sInServoTraversal = true;
  bool postTraversalRequired =
    Servo_TraverseSubtree(aRoot, mRawSet.get(), aRootBehavior);
  sInServoTraversal = false;
  return postTraversalRequired;
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleFor(Element* aElement,
                               nsStyleContext* aParentContext,
                               LazyComputeBehavior aMayCompute,
                               TreeMatchContext& aTreeMatchContext)
{
  // aTreeMatchContext is used to speed up selector matching,
  // but if the element already has a ServoComputedValues computed in
  // advance, then we shouldn't need to use it.
  return ResolveStyleFor(aElement, aParentContext, aMayCompute);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleForText(nsIContent* aTextNode,
                                   nsStyleContext* aParentContext)
{
  MOZ_ASSERT(aTextNode && aTextNode->IsNodeOfType(nsINode::eTEXT));
  MOZ_ASSERT(aTextNode->GetParent());
  MOZ_ASSERT(aParentContext);

  // Gecko expects text node style contexts to be like elements that match no
  // rules: inherit the inherit structs, reset the reset structs. This is cheap
  // enough to do on the main thread, which means that the parallel style system
  // can avoid worrying about text nodes.
  const ServoComputedValues* parentComputedValues =
    aParentContext->StyleSource().AsServoComputedValues();
  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_Inherit(mRawSet.get(), parentComputedValues).Consume();

  return GetContext(computedValues.forget(), aParentContext,
                    nsCSSAnonBoxes::mozText, CSSPseudoElementType::AnonBox,
                    nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleForOtherNonElement(nsStyleContext* aParentContext)
{
  // The parent context can be null if the non-element share a style context
  // with the root of an anonymous subtree.
  const ServoComputedValues* parent =
    aParentContext ? aParentContext->StyleSource().AsServoComputedValues() : nullptr;
  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_Inherit(mRawSet.get(), parent).Consume();
  MOZ_ASSERT(computedValues);

  return GetContext(computedValues.forget(), aParentContext,
                    nsCSSAnonBoxes::mozOtherNonElement,
                    CSSPseudoElementType::AnonBox,
                    nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolvePseudoElementStyle(Element* aOriginatingElement,
                                         CSSPseudoElementType aType,
                                         nsStyleContext* aParentContext,
                                         Element* aPseudoElement)
{
  if (aPseudoElement) {
    NS_WARNING("stylo: We don't support CSS_PSEUDO_ELEMENT_SUPPORTS_USER_ACTION_STATE yet");
  }

  // NB: We ignore aParentContext, on the assumption that pseudo element styles
  // should just inherit from aOriginatingElement's primary style, which Servo
  // already knows.
  MOZ_ASSERT(aType < CSSPseudoElementType::Count);
  nsIAtom* pseudoTag = nsCSSPseudoElements::GetPseudoAtom(aType);

  RefPtr<ServoComputedValues> computedValues =
    Servo_ResolvePseudoStyle(aOriginatingElement, pseudoTag,
                             /* is_probe = */ false, mRawSet.get()).Consume();
  MOZ_ASSERT(computedValues);

  bool isBeforeOrAfter = aType == CSSPseudoElementType::before ||
                         aType == CSSPseudoElementType::after;
  return GetContext(computedValues.forget(), aParentContext, pseudoTag, aType,
                    isBeforeOrAfter ? aOriginatingElement : nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveTransientStyle(Element* aElement, CSSPseudoElementType aType)
{
  nsIAtom* pseudoTag = nullptr;
  if (aType != CSSPseudoElementType::NotPseudo) {
    pseudoTag = nsCSSPseudoElements::GetPseudoAtom(aType);
  }

  RefPtr<ServoComputedValues> computedValues =
    Servo_ResolveStyleLazily(aElement, pseudoTag, mRawSet.get()).Consume();

  return GetContext(computedValues.forget(), nullptr, pseudoTag, aType,
                    nullptr);
}

// aFlags is an nsStyleSet flags bitfield
already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveAnonymousBoxStyle(nsIAtom* aPseudoTag,
                                        nsStyleContext* aParentContext,
                                        uint32_t aFlags)
{
  MOZ_ASSERT(nsCSSAnonBoxes::IsAnonBox(aPseudoTag));

  MOZ_ASSERT(aFlags == 0 ||
             aFlags == nsStyleSet::eSkipParentDisplayBasedStyleFixup);
  bool skipFixup = aFlags & nsStyleSet::eSkipParentDisplayBasedStyleFixup;

  const ServoComputedValues* parentStyle =
    aParentContext ? aParentContext->StyleSource().AsServoComputedValues()
                   : nullptr;
  RefPtr<ServoComputedValues> computedValues =
    Servo_ComputedValues_GetForAnonymousBox(parentStyle, aPseudoTag,
                                            mRawSet.get()).Consume();
#ifdef DEBUG
  if (!computedValues) {
    nsString pseudo;
    aPseudoTag->ToString(pseudo);
    NS_ERROR(nsPrintfCString("stylo: could not get anon-box: %s",
             NS_ConvertUTF16toUTF8(pseudo).get()).get());
    MOZ_CRASH();
  }
#endif

  return NS_NewStyleContext(aParentContext, mPresContext, aPseudoTag,
                            CSSPseudoElementType::AnonBox,
                            computedValues.forget(), skipFixup);
}

// manage the set of style sheets in the style set
nsresult
ServoStyleSet::AppendStyleSheet(SheetType aType,
                                ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(aSheet->IsApplicable());
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  MOZ_ASSERT(aSheet->RawSheet(), "Raw sheet should be in place before insertion.");
  mSheets[aType].RemoveElement(aSheet);
  mSheets[aType].AppendElement(aSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_AppendStyleSheet(mRawSet.get(), aSheet->RawSheet(), !mBatching);
  }

  return NS_OK;
}

nsresult
ServoStyleSet::PrependStyleSheet(SheetType aType,
                                 ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(aSheet->IsApplicable());
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  MOZ_ASSERT(aSheet->RawSheet(), "Raw sheet should be in place before insertion.");
  mSheets[aType].RemoveElement(aSheet);
  mSheets[aType].InsertElementAt(0, aSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_PrependStyleSheet(mRawSet.get(), aSheet->RawSheet(), !mBatching);
  }

  return NS_OK;
}

nsresult
ServoStyleSet::RemoveStyleSheet(SheetType aType,
                                ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  mSheets[aType].RemoveElement(aSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_RemoveStyleSheet(mRawSet.get(), aSheet->RawSheet(), !mBatching);
  }

  return NS_OK;
}

nsresult
ServoStyleSet::ReplaceSheets(SheetType aType,
                             const nsTArray<RefPtr<ServoStyleSheet>>& aNewSheets)
{
  // Gecko uses a two-dimensional array keyed by sheet type, whereas Servo
  // stores a flattened list. This makes ReplaceSheets a pretty clunky thing
  // to express. If the need ever arises, we can easily make this more efficent,
  // probably by aligning the representations better between engines.

  if (mRawSet) {
    for (ServoStyleSheet* sheet : mSheets[aType]) {
      Servo_StyleSet_RemoveStyleSheet(mRawSet.get(), sheet->RawSheet(), false);
    }
  }

  mSheets[aType].Clear();
  mSheets[aType].AppendElements(aNewSheets);

  if (mRawSet) {
    for (ServoStyleSheet* sheet : mSheets[aType]) {
      MOZ_ASSERT(sheet->RawSheet(), "Raw sheet should be in place before replacement.");
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(), sheet->RawSheet(), false);
    }
  }

  if (!mBatching) {
    Servo_StyleSet_FlushStyleSheets(mRawSet.get());
  }

  return NS_OK;
}

nsresult
ServoStyleSet::InsertStyleSheetBefore(SheetType aType,
                                      ServoStyleSheet* aNewSheet,
                                      ServoStyleSheet* aReferenceSheet)
{
  MOZ_ASSERT(aNewSheet);
  MOZ_ASSERT(aReferenceSheet);
  MOZ_ASSERT(aNewSheet->IsApplicable());

  mSheets[aType].RemoveElement(aNewSheet);
  size_t idx = mSheets[aType].IndexOf(aReferenceSheet);
  if (idx == mSheets[aType].NoIndex) {
    return NS_ERROR_INVALID_ARG;
  }
  MOZ_ASSERT(aReferenceSheet->RawSheet(), "Reference sheet should have a raw sheet.");

  MOZ_ASSERT(aNewSheet->RawSheet(), "Raw sheet should be in place before insertion.");
  mSheets[aType].InsertElementAt(idx, aNewSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    Servo_StyleSet_InsertStyleSheetBefore(mRawSet.get(), aNewSheet->RawSheet(),
                                          aReferenceSheet->RawSheet(), !mBatching);
  }

  return NS_OK;
}

int32_t
ServoStyleSet::SheetCount(SheetType aType) const
{
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));
  return mSheets[aType].Length();
}

ServoStyleSheet*
ServoStyleSet::StyleSheetAt(SheetType aType,
                            int32_t aIndex) const
{
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));
  return mSheets[aType][aIndex];
}

nsresult
ServoStyleSet::RemoveDocStyleSheet(ServoStyleSheet* aSheet)
{
  return RemoveStyleSheet(SheetType::Doc, aSheet);
}

nsresult
ServoStyleSet::AddDocStyleSheet(ServoStyleSheet* aSheet,
                                nsIDocument* aDocument)
{
  MOZ_ASSERT(aSheet->IsApplicable());
  MOZ_ASSERT(aSheet->RawSheet(), "Raw sheet should be in place by this point.");

  RefPtr<StyleSheet> strong(aSheet);

  nsTArray<RefPtr<ServoStyleSheet>>& sheetsArray = mSheets[SheetType::Doc];

  sheetsArray.RemoveElement(aSheet);

  size_t index =
    aDocument->FindDocStyleSheetInsertionPoint(sheetsArray, aSheet);
  sheetsArray.InsertElementAt(index, aSheet);

  if (mRawSet) {
    // Maintain a mirrored list of sheets on the servo side.
    ServoStyleSheet* followingSheet = sheetsArray.SafeElementAt(index + 1);
    if (followingSheet) {
      MOZ_ASSERT(followingSheet->RawSheet(), "Every mSheets element should have a raw sheet");
      Servo_StyleSet_InsertStyleSheetBefore(mRawSet.get(), aSheet->RawSheet(),
                                            followingSheet->RawSheet(), !mBatching);
    } else {
      Servo_StyleSet_AppendStyleSheet(mRawSet.get(), aSheet->RawSheet(), !mBatching);
    }
  }

  return NS_OK;
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ProbePseudoElementStyle(Element* aOriginatingElement,
                                       CSSPseudoElementType aType,
                                       nsStyleContext* aParentContext)
{
  // NB: We ignore aParentContext, on the assumption that pseudo element styles
  // should just inherit from aOriginatingElement's primary style, which Servo
  // already knows.
  MOZ_ASSERT(aType < CSSPseudoElementType::Count);
  nsIAtom* pseudoTag = nsCSSPseudoElements::GetPseudoAtom(aType);

  RefPtr<ServoComputedValues> computedValues =
    Servo_ResolvePseudoStyle(aOriginatingElement, pseudoTag,
                             /* is_probe = */ true, mRawSet.get()).Consume();
  if (!computedValues) {
    return nullptr;
  }

  // For :before and :after pseudo-elements, having display: none or no
  // 'content' property is equivalent to not having the pseudo-element
  // at all.
  bool isBeforeOrAfter = pseudoTag == nsCSSPseudoElements::before ||
                         pseudoTag == nsCSSPseudoElements::after;
  if (isBeforeOrAfter) {
    const nsStyleDisplay *display = Servo_GetStyleDisplay(computedValues);
    const nsStyleContent *content = Servo_GetStyleContent(computedValues);
    // XXXldb What is contentCount for |content: ""|?
    if (display->mDisplay == StyleDisplay::None ||
        content->ContentCount() == 0) {
      return nullptr;
    }
  }

  return GetContext(computedValues.forget(), aParentContext, pseudoTag, aType,
                    isBeforeOrAfter ? aOriginatingElement : nullptr);
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ProbePseudoElementStyle(Element* aOriginatingElement,
                                       CSSPseudoElementType aType,
                                       nsStyleContext* aParentContext,
                                       TreeMatchContext& aTreeMatchContext,
                                       Element* aPseudoElement)
{
  if (aPseudoElement) {
    NS_ERROR("stylo: We don't support CSS_PSEUDO_ELEMENT_SUPPORTS_USER_ACTION_STATE yet");
  }
  return ProbePseudoElementStyle(aOriginatingElement, aType, aParentContext);
}

nsRestyleHint
ServoStyleSet::HasStateDependentStyle(dom::Element* aElement,
                                      EventStates aStateMask)
{
  NS_WARNING("stylo: HasStateDependentStyle always returns zero!");
  return nsRestyleHint(0);
}

nsRestyleHint
ServoStyleSet::HasStateDependentStyle(dom::Element* aElement,
                                      CSSPseudoElementType aPseudoType,
                                     dom::Element* aPseudoElement,
                                     EventStates aStateMask)
{
  NS_WARNING("stylo: HasStateDependentStyle always returns zero!");
  return nsRestyleHint(0);
}

bool
ServoStyleSet::StyleDocument()
{
  // Restyle the document from the root element and each of the document level
  // NAC subtree roots.
  bool postTraversalRequired = false;
  DocumentStyleRootIterator iter(mPresContext->Document());
  while (Element* root = iter.GetNextStyleRoot()) {
    if (PrepareAndTraverseSubtree(root, TraversalRootBehavior::Normal)) {
      postTraversalRequired = true;
    }
  }
  return postTraversalRequired;
}

void
ServoStyleSet::StyleNewSubtree(Element* aRoot)
{
  MOZ_ASSERT(!aRoot->HasServoData());
  DebugOnly<bool> postTraversalRequired =
    PrepareAndTraverseSubtree(aRoot, TraversalRootBehavior::Normal);
  MOZ_ASSERT(!postTraversalRequired);
}

void
ServoStyleSet::StyleNewChildren(Element* aParent)
{
  PrepareAndTraverseSubtree(aParent, TraversalRootBehavior::UnstyledChildrenOnly);
  // We can't assert that Servo_TraverseSubtree returns false, since aParent
  // or some of its other children might have pending restyles.
}

void
ServoStyleSet::NoteStyleSheetsChanged()
{
  Servo_StyleSet_NoteStyleSheetsChanged(mRawSet.get());
}

#ifdef DEBUG
void
ServoStyleSet::AssertTreeIsClean()
{
  DocumentStyleRootIterator iter(mPresContext->Document());
  while (Element* root = iter.GetNextStyleRoot()) {
    Servo_AssertTreeIsClean(root);
  }
}
#endif

bool
ServoStyleSet::FillKeyframesForName(const nsString& aName,
                                    const nsTimingFunction& aTimingFunction,
                                    const ServoComputedValues* aComputedValues,
                                    nsTArray<Keyframe>& aKeyframes)
{
  NS_ConvertUTF16toUTF8 name(aName);
  return Servo_StyleSet_FillKeyframesForName(mRawSet.get(),
                                             &name,
                                             &aTimingFunction,
                                             aComputedValues,
                                             &aKeyframes);
}

void
ServoStyleSet::RebuildData()
{
  Servo_StyleSet_RebuildData(mRawSet.get());
}

already_AddRefed<ServoComputedValues>
ServoStyleSet::ResolveServoStyle(Element* aElement)
{
  return Servo_ResolveStyle(aElement, mRawSet.get()).Consume();
}

bool ServoStyleSet::sInServoTraversal = false;
