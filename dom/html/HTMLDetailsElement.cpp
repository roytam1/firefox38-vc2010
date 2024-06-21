/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLDetailsElement.h"

#include "mozilla/dom/HTMLDetailsElementBinding.h"
#include "mozilla/dom/HTMLUnknownElement.h"
#include "mozilla/Preferences.h"

// Expand NS_IMPL_NS_NEW_HTML_ELEMENT(Details) to add pref check.
nsGenericHTMLElement*
NS_NewHTMLDetailsElement(already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
                         mozilla::dom::FromParser aFromParser)
{
  if (!mozilla::dom::HTMLDetailsElement::IsDetailsEnabled()) {
    return new mozilla::dom::HTMLUnknownElement(aNodeInfo);
  }

  return new mozilla::dom::HTMLDetailsElement(aNodeInfo);
}

namespace mozilla {
namespace dom {

bool
HTMLDetailsElement::IsDetailsEnabled()
{
  static bool isDetailsEnabled = false;
  static bool added = false;

  if (!added) {
    Preferences::AddBoolVarCache(&isDetailsEnabled,
                                 "dom.details_element.enabled");
    added = true;
  }

  return isDetailsEnabled;
}

HTMLDetailsElement::HTMLDetailsElement(already_AddRefed<mozilla::dom::NodeInfo>& aNodeInfo)
  : nsGenericHTMLElement(aNodeInfo)
{
}

HTMLDetailsElement::~HTMLDetailsElement()
{
}

NS_IMPL_ELEMENT_CLONE(HTMLDetailsElement)

nsIContent*
HTMLDetailsElement::GetFirstSummary() const
{
  // XXX: Bug 1245032: Might want to cache the first summary element.
  for (nsIContent* child = nsINode::GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    if (child->IsHTML(nsGkAtoms::summary)) {
      return child;
    }
  }
  return nullptr;
}

nsChangeHint
HTMLDetailsElement::GetAttributeChangeHint(const nsIAtom* aAttribute,
                                           int32_t aModType) const
{
  nsChangeHint hint =
    nsGenericHTMLElement::GetAttributeChangeHint(aAttribute, aModType);
  if (aAttribute == nsGkAtoms::open) {
    NS_UpdateHint(hint, nsChangeHint_ReconstructFrame);
  }
  return hint;
}

JSObject*
HTMLDetailsElement::WrapNode(JSContext* aCx)
{
  return HTMLDetailsElementBinding::Wrap(aCx, this);
}

} // namespace dom
} // namespace mozilla
