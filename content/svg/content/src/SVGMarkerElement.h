/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SVGMarkerElement_h
#define mozilla_dom_SVGMarkerElement_h

#include "gfxMatrix.h"
#include "nsIDOMSVGFitToViewBox.h"
#include "nsIDOMSVGMarkerElement.h"
#include "nsSVGAngle.h"
#include "nsSVGEnum.h"
#include "nsSVGLength2.h"
#include "nsSVGViewBox.h"
#include "SVGAnimatedPreserveAspectRatio.h"
#include "nsSVGElement.h"
#include "mozilla/Attributes.h"

class nsSVGMarkerFrame;

nsresult NS_NewSVGMarkerElement(nsIContent **aResult,
                                already_AddRefed<nsINodeInfo> aNodeInfo);

namespace mozilla {
namespace dom {

class nsSVGOrientType
{
public:
  nsSVGOrientType()
   : mAnimVal(nsIDOMSVGMarkerElement::SVG_MARKER_ORIENT_ANGLE),
     mBaseVal(nsIDOMSVGMarkerElement::SVG_MARKER_ORIENT_ANGLE) {}

  nsresult SetBaseValue(uint16_t aValue,
                        nsSVGElement *aSVGElement);

  // XXX FIXME like https://bugzilla.mozilla.org/show_bug.cgi?id=545550 but
  // without adding an mIsAnimated member...?
  void SetBaseValue(uint16_t aValue)
    { mAnimVal = mBaseVal = uint8_t(aValue); }
  // no need to notify, since nsSVGAngle does that
  void SetAnimValue(uint16_t aValue)
    { mAnimVal = uint8_t(aValue); }

  uint16_t GetBaseValue() const
    { return mBaseVal; }
  uint16_t GetAnimValue() const
    { return mAnimVal; }

  already_AddRefed<nsIDOMSVGAnimatedEnumeration>
    ToDOMAnimatedEnum(nsSVGElement* aSVGElement);

private:
  nsSVGEnumValue mAnimVal;
  nsSVGEnumValue mBaseVal;

  struct DOMAnimatedEnum MOZ_FINAL : public nsIDOMSVGAnimatedEnumeration
  {
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS(DOMAnimatedEnum)

    DOMAnimatedEnum(nsSVGOrientType* aVal,
                    nsSVGElement *aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement) {}

    nsSVGOrientType *mVal; // kept alive because it belongs to content
    nsRefPtr<nsSVGElement> mSVGElement;

    NS_IMETHOD GetBaseVal(uint16_t* aResult)
      { *aResult = mVal->GetBaseValue(); return NS_OK; }
    NS_IMETHOD SetBaseVal(uint16_t aValue)
      { return mVal->SetBaseValue(aValue, mSVGElement); }
    NS_IMETHOD GetAnimVal(uint16_t* aResult)
      { *aResult = mVal->GetAnimValue(); return NS_OK; }
  };
};

typedef nsSVGElement SVGMarkerElementBase;

class SVGMarkerElement : public SVGMarkerElementBase,
                         public nsIDOMSVGMarkerElement,
                         public nsIDOMSVGFitToViewBox
{
  friend class ::nsSVGMarkerFrame;

protected:
  friend nsresult (::NS_NewSVGMarkerElement(nsIContent **aResult,
                                            already_AddRefed<nsINodeInfo> aNodeInfo));
  SVGMarkerElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual JSObject* WrapNode(JSContext *cx, JSObject *scope, bool *triedToWrap) MOZ_OVERRIDE;

public:
  // interfaces:

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMSVGMARKERELEMENT
  NS_DECL_NSIDOMSVGFITTOVIEWBOX

  // xxx I wish we could use virtual inheritance
  NS_FORWARD_NSIDOMNODE_TO_NSINODE
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC
  NS_FORWARD_NSIDOMSVGELEMENT(nsSVGElement::)

  // nsIContent interface
  NS_IMETHOD_(bool) IsAttributeMapped(const nsIAtom* name) const;

  virtual bool GetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                         nsAString& aResult) const;
  virtual nsresult UnsetAttr(int32_t aNameSpaceID, nsIAtom* aAttribute,
                             bool aNotify);

  // nsSVGSVGElement methods:
  virtual bool HasValidDimensions() const;

  // public helpers
  gfxMatrix GetMarkerTransform(float aStrokeWidth,
                               float aX, float aY, float aAutoAngle);
  nsSVGViewBoxRect GetViewBoxRect();
  gfxMatrix GetViewBoxTransform();

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  nsSVGOrientType* GetOrientType() { return &mOrientType; }

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }

  // WebIDL
  already_AddRefed<nsIDOMSVGAnimatedRect> ViewBox();
  already_AddRefed<DOMSVGAnimatedPreserveAspectRatio> PreserveAspectRatio();
  already_AddRefed<SVGAnimatedLength> RefX();
  already_AddRefed<SVGAnimatedLength> RefY();
  already_AddRefed<nsIDOMSVGAnimatedEnumeration> MarkerUnits();
  already_AddRefed<SVGAnimatedLength> MarkerWidth();
  already_AddRefed<SVGAnimatedLength> MarkerHeight();
  already_AddRefed<nsIDOMSVGAnimatedEnumeration> OrientType();
  already_AddRefed<SVGAnimatedAngle> OrientAngle();
  // We can use the XPIDL SetOrientToAuto
  void SetOrientToAngle(SVGAngle& angle, ErrorResult& rv);

protected:

  virtual bool ParseAttribute(int32_t aNameSpaceID, nsIAtom* aName,
                                const nsAString& aValue,
                                nsAttrValue& aResult);

  void SetParentCoordCtxProvider(SVGSVGElement *aContext);

  virtual LengthAttributesInfo GetLengthInfo();
  virtual AngleAttributesInfo GetAngleInfo();
  virtual EnumAttributesInfo GetEnumInfo();
  virtual nsSVGViewBox *GetViewBox();
  virtual SVGAnimatedPreserveAspectRatio *GetPreserveAspectRatio();

  enum { REFX, REFY, MARKERWIDTH, MARKERHEIGHT };
  nsSVGLength2 mLengthAttributes[4];
  static LengthInfo sLengthInfo[4];

  enum { MARKERUNITS };
  nsSVGEnum mEnumAttributes[1];
  static nsSVGEnumMapping sUnitsMap[];
  static EnumInfo sEnumInfo[1];

  enum { ORIENT };
  nsSVGAngle mAngleAttributes[1];
  static AngleInfo sAngleInfo[1];

  nsSVGViewBox             mViewBox;
  SVGAnimatedPreserveAspectRatio mPreserveAspectRatio;

  // derived properties (from 'orient') handled separately
  nsSVGOrientType                        mOrientType;

  SVGSVGElement                         *mCoordCtx;
  nsAutoPtr<gfxMatrix>                   mViewBoxToViewportTransform;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_SVGMarkerElement_h
