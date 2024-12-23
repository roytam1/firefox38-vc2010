/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMathMLSelectedFrame_h___
#define nsMathMLSelectedFrame_h___

#include "nsMathMLContainerFrame.h"

class nsMathMLSelectedFrame : public nsMathMLContainerFrame {
public:
  virtual void
  Init(nsIContent*       aContent,
       nsContainerFrame* aParent,
       nsIFrame*         aPrevInFlow) override;

  NS_IMETHOD
  TransmitAutomaticData() override;

  virtual void
  SetInitialChildList(ChildListID     aListID,
                      nsFrameList&    aChildList) override;

  virtual nsresult
  ChildListChanged(int32_t aModType) override;

  virtual void BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                const nsRect&           aDirtyRect,
                                const nsDisplayListSet& aLists) override;

  virtual nsresult
  Place(nsRenderingContext& aRenderingContext,
        bool                 aPlaceOrigin,
        nsHTMLReflowMetrics& aDesiredSize) override;

  virtual void
  Reflow(nsPresContext*          aPresContext,
         nsHTMLReflowMetrics&     aDesiredSize,
         const nsHTMLReflowState& aReflowState,
         nsReflowStatus&          aStatus) override;

  virtual nsQueryFrame::FrameIID GetFrameId() override = 0;

protected:
  explicit nsMathMLSelectedFrame(nsStyleContext* aContext) :
    nsMathMLContainerFrame(aContext) {}
  virtual ~nsMathMLSelectedFrame();
  
  virtual nsIFrame* GetSelectedFrame() = 0;
  nsIFrame*       mSelectedFrame;

  bool            mInvalidMarkup;
  
private:
  void* operator new(size_t, nsIPresShell*) MOZ_MUST_OVERRIDE MOZ_DELETE;
};

#endif /* nsMathMLSelectedFrame_h___ */
