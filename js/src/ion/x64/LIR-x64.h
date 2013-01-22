/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_lir_x64_h__
#define jsion_lir_x64_h__

#include "ion/TypeOracle.h"

namespace js {
namespace ion {

// Given a typed input, returns an untyped box.
class LBox : public LInstructionHelper<1, 1, 0>
{
    MIRType type_;

  public:
    LIR_HEADER(Box)

    LBox(MIRType type, const LAllocation &payload)
      : type_(type)
    {
        setOperand(0, payload);
    }

    MIRType type() const {
        return type_;
    }
};

// Given an untyped input, guards on whether it's a specific type and returns
// the unboxed payload.
class LUnboxBase : public LInstructionHelper<1, 1, 0>
{
  public:
    LUnboxBase(const LAllocation &input) {
        setOperand(0, input);
    }

    static const size_t Input = 0;

    MUnbox *mir() const {
        return mir_->toUnbox();
    }
};

class LUnbox : public LUnboxBase {
  public:
    LIR_HEADER(Unbox)

    LUnbox(const LAllocation &input)
      : LUnboxBase(input)
    { }
};

class LUnboxDouble : public LUnboxBase {
  public:
    LIR_HEADER(UnboxDouble)

    LUnboxDouble(const LAllocation &input)
      : LUnboxBase(input)
    { }
};

// Constant double.
class LDouble : public LInstructionHelper<1, 0, 0>
{
    double d_;

  public:
    LIR_HEADER(Double)

    LDouble(double d)
      : d_(d)
    { }

    double getDouble() const {
        return d_;
    }
};

// Duplicated constant of packed double.
class LPackedD : public LInstructionHelper<1, 0, 0>
{
    double d_;

  public:
    LIR_HEADER(PackedD)

    LPackedD(double d)
      : d_(d)
    { }

    double getDouble() const {
        return d_;
    }
};

// Performs an add, sub, mul, or div on two double values.
class LMathPD : public LBinaryMath<0>
{
    JSOp jsop_;

  public:
    LIR_HEADER(MathPD)

    LMathPD(JSOp jsop)
      : jsop_(jsop)
    { }

    JSOp jsop() const {
        return jsop_;
    }
};

class LLoadElementPD : public LInstructionHelper<1, 2, 1>
{
  public:
    LIR_HEADER(LoadElementPD)

    LLoadElementPD(const LAllocation &elements, const LAllocation &index,
                   const LDefinition &intTagMask)
    {
        setOperand(0, elements);
        setOperand(1, index);
        setTemp(0, intTagMask);
    }
    const MLoadElement *mir() const {
        return mir_->toLoadElement();
    }
    const LAllocation *elements() {
        return getOperand(0);
    }
    const LAllocation *index() {
        return getOperand(1);
    }
    const LDefinition *intTagMask() {
        return getTemp(0);
    }
};

class LStoreElementPD : public LInstructionHelper<0, 3, 0>
{
  public:
    LIR_HEADER(StoreElementPD)

    LStoreElementPD(const LAllocation &elements, const LAllocation &index, const LAllocation &value) {
        setOperand(0, elements);
        setOperand(1, index);
        setOperand(2, value);
    }

    const MStoreElement *mir() const {
        return mir_->toStoreElement();
    }
    const LAllocation *elements() {
        return getOperand(0);
    }
    const LAllocation *index() {
        return getOperand(1);
    }
    const LAllocation *value() {
        return getOperand(2);
    }
};

} // namespace ion
} // namespace js

#endif // jsion_lir_x64_h__

