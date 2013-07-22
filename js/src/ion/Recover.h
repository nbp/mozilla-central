/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_recover_h__
#define jsion_recover_h__

#include "ion/IonTypes.h"
#include "ion/Slots.h"

namespace js {
namespace ion {

class CompactBufferReader;
class SnapshotIterator;

// Emulate a random access vector based on the forward iterator implemented by
// the SnapshotIterator, assuming it is mostly used as a forward iterator.
class SlotVector
{
  public:
    SlotVector()
      : si_(NULL)
    { }

    void init(SnapshotIterator &si) {
        si_ = &si;
    }

    size_t length() const;
    Slot operator [](size_t i) const;

  private:
    SnapshotIterator *si_;
};

class RResumePoint
{
  public:
    RResumePoint(CompactBufferReader &reader);
    void readSlots(SnapshotIterator &it, JSScript *script, JSFunction *fun);

    uint32_t pcOffset() const {
        return pcOffset_;
    }
    size_t numOperands() const {
        return numOperands_;
    }

    size_t numFormalArgs() const {
        return startFixedSlots_ - startFormalArgs_;
    }
    Slot formalArg(size_t i) const {
        JS_ASSERT(i < numFormalArgs());
        return slots_[startFormalArgs_ + i];
    }

    size_t numFixedSlots() const {
        return startStackSlots_ - startFixedSlots_;
    }
    Slot fixedSlot(size_t i) const {
        JS_ASSERT(i < numFixedSlots());
        return slots_[startFixedSlots_ + i];
    }

    size_t numStackSlots() const {
        return numOperands_ - startStackSlots_;
    }
    Slot stackSlot(size_t i) const {
        JS_ASSERT(i < numStackSlots());
        return slots_[startStackSlots_ + i];
    }

    Slot calleeFunction(uint32_t nactual) const {
        // +2: The function is located just before the arguments and |this|.
        uint32_t funIndex = numOperands_ - (nactual + 2);
        Slot s = slots_[funIndex];
        return s;
    }
    Slot calleeActualArgs(uint32_t nactual, size_t i) const {
        size_t arg0Index = numOperands_ - nactual;
        return slots_[arg0Index + i];
    }

    const Slot &scopeChainSlot() const {
        return scopeChainSlot_;
    }
    const Slot &argObjSlot() const {
        return argObjSlot_;
    }
    const Slot &thisSlot() const {
        return thisSlot_;
    }

  private:
    // Meta data extracted from the snapshot iterator.
    uint32_t numOperands_;
    uint32_t pcOffset_;

    // Slots used to hold the locations of the data needed to recover the frame.
    Slot scopeChainSlot_;
    Slot argObjSlot_;
    Slot thisSlot_;

    uint32_t startFormalArgs_; // Index at which formal args are starting.
    uint32_t startFixedSlots_; // Index at which fixed slots are starting.
    uint32_t startStackSlots_; // Index at which stack slots are starting.
    SlotVector slots_;
};

} // namespace ion
} // namespace js

#endif // jsion_recover_h__
