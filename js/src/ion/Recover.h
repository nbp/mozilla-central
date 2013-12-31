/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_recover_h__
#define jsion_recover_h__

#include "js/RootingAPI.h"

#include "ion/IonTypes.h"
#include "ion/Slots.h"

namespace js {
namespace ion {

class CompactBufferReader;
class SnapshotIterator;
class MNode;

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

#define RECOVER_KIND_LIST(_)                    \
    _(ResumePoint)                              \
    _(StoreFixedSlot)

// Forward declarations of Cache kinds.
#define FORWARD_DECLARE(kind) class R##kind;
    RECOVER_KIND_LIST(FORWARD_DECLARE)
#undef FORWARD_DECLARE

// A Recover instruction is an instruction which is only executed when we need
// to explore / restore the stack as it is expected to be by slower execution
// mode.
//
// Recover instructions are encoded with the MIR function writeRecover and
// extracted from the recover buffer with the RInstruction::dispatch function.
// The dispatch function do a placement new on the memory given as argument with
// the data read from the recover buffer. The constructor of each RInstruction
// is in charge of reading its own data.
//
// A recover instruction must provide a resume function which is in charge to
// read its operands and to store the resumed value.  A recover instruction
// should only be resumed once.  Each RInstruction must provide a numOperands
// function which should match the number of operands encoded by the
// writeRInstructio method.
class RInstruction
{
  public:
    static RInstruction *dispatch(void *mem, CompactBufferReader &read);

    // Return the number of operands which have to be read out of the snapshot.
    virtual size_t numOperands() const = 0;

    // A resume an instruction and store the result of this operation back on
    // the snapshot iterator by using the function setRecoveredValue.
    virtual bool resume(JSContext *cx, SnapshotIterator &si, HandleScript script) const = 0;

    enum Kind {
#   define DEFINE_RECOVER_KIND(op) Recover_##op,
        RECOVER_KIND_LIST(DEFINE_RECOVER_KIND)
#   undef DEFINE_RECOVER_KIND
        Recover_Invalid
    };

    virtual Kind kind() const = 0;
    virtual const char *recoverName() const = 0;

#   define RECOVER_CASTS(ins)                   \
    inline bool is##ins() const {               \
        return kind() == Recover_##ins;         \
    }                                           \
                                                \
    inline R##ins *to##ins();                   \
    inline const R##ins *to##ins() const;

    RECOVER_KIND_LIST(RECOVER_CASTS)
#   undef RECOVER_CASTS
};

#define RECOVER_HEADER(ins)                     \
    R##ins(CompactBufferReader &reader);        \
    Kind kind() const {                         \
        return RInstruction::Recover_##ins;     \
    }                                           \
    const char *recoverName() const {           \
        return #ins;                            \
    }

class RResumePoint : public RInstruction
{
  public:
    RECOVER_HEADER(ResumePoint)
    void readSlots(SnapshotIterator &it, JSScript *script, JSFunction *fun);
    bool resume(JSContext *cx, SnapshotIterator &si, HandleScript script) const;

    uint32_t pcOffset() const {
        return pcOffset_;
    }
    virtual size_t numOperands() const MOZ_FINAL MOZ_OVERRIDE {
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

struct RStoreFixedSlot : public RInstruction
{
    RECOVER_HEADER(StoreFixedSlot)

    bool resume(JSContext *cx, SnapshotIterator &si, HandleScript script) const;

    size_t numOperands() const {
        return 2;
    }

  private:
    uint32_t slot_;
};

#undef RECOVER_HEADER

#define RECOVER_CASTS(ins)                              \
    R##ins *RInstruction::to##ins() {                   \
        JS_ASSERT(is##ins());                           \
        return static_cast<R##ins *>(this);             \
    }                                                   \
    const R##ins *RInstruction::to##ins() const {       \
        JS_ASSERT(is##ins());                           \
        return static_cast<const R##ins *>(this);       \
    }

    RECOVER_KIND_LIST(RECOVER_CASTS)
#undef RECOVER_CASTS

} // namespace ion
} // namespace js

#endif // jsion_recover_h__
