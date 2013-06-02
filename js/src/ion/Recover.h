/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_recover_h__
#define jsion_recover_h__

#include "CompactBuffer.h"
#include "Slots.h"

namespace js {
namespace ion {

class MNode;
class SnapshotIterator;

#define RECOVER_KIND_LIST(_)                    \
    _(ResumePoint)

// Forward declarations of Cache kinds.
#define FORWARD_DECLARE(kind) class R##kind;
    RECOVER_KIND_LIST(FORWARD_DECLARE)
#undef FORWARD_DECLARE

// A Recover instruction is an instruction which is only executed when we need
// to explore / restore the stack as it is expected to be by slower execution
// mode.
//
// Recover instructions are encoded with the MIR function writeRInstruction and
// extracted from the recover buffer with the RInstruction::dispatch function.
// The dispatch function do a placement new on the memory given as argument with
// the data read from the recover buffer. The constructor of each RInstruction
// is in charge of reading its meta data.
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

    virtual size_t numOperands() const = 0;
    virtual bool resume(JSContext *cx, HandleScript script, SnapshotIterator &it) const = 0;

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

  protected:
    // Store the resumed value in the resume vector of the snapshot iterator.
    void storeResumedValue(SnapshotIterator &it, Value result) const;

    // Read a slot from the snapshot iterator.
    Slot recoverSlot(SnapshotIterator &it) const;

    // Read a Value from the snapshot iterator.
    Value recoverValue(const SnapshotIterator &it, const Slot &slot) const;

    // Read a Value from the snapshot iterator if the snapshot iterator is able
    // to do so.
    Value maybeRecoverValue(const SnapshotIterator &it, const Slot &slot) const;
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

    void readFrameHeader(SnapshotIterator &it, JSScript *script, bool isFunction);

    bool resume(JSContext *cx, HandleScript script, SnapshotIterator &it) const;

    size_t numOperands() const {
        return numOperands_;
    }

    uint32_t pcOffset() const {
        return pcOffset_;
    }

    bool resumeAfter() const {
        return resumeAfter_;
    }

    // Information set by the recover reader, and use to determine that we
    // are on the last frame. This is needed for checking from where to
    // extract the return value in case of an invalidation.
    void setLastFrame() {
        lastFrame_ = true;
    }
    bool isLastFrame() const {
        return lastFrame_;
    }

    // Recover the slot of resume point headers.
    const Slot &scopeChainSlot() const {
        return scopeChainSlot_;
    }
    const Slot &argObjSlot() const {
        return argObjSlot_;
    }
    const Slot &thisSlot() const {
        return thisSlot_;
    }

    // Recover any slots outside the header of the resume point.
    Slot readAnySlot(SnapshotIterator &it) const {
        return recoverSlot(it);
    }

    // Recover the value of any slot, mostly used for bailouts.
    Value readFormalArg(SnapshotIterator &it) const {
        return recoverValue(it, recoverSlot(it));
    }
    Value readFixedSlot(SnapshotIterator &it) const {
        return recoverValue(it, recoverSlot(it));
    }
    Value readStackSlot(JSContext *cx, SnapshotIterator &it) const;

    Value maybeReadFormalArg(SnapshotIterator &it) const {
        return recoverValue(it, recoverSlot(it));
    }

    // If this frame is doing a JS call, then we can find the callee among
    // the slots which are always recoverable. The number of argument is
    // expected to be the number of arguments of the current frame as we
    // might inline a call to fun.apply(self, arguments) where we cannot
    // extract this the byte code.
    Value recoverCallee(SnapshotIterator &it, JSScript *script, uint32_t *numActualArgs);

    // Read one argument of the callee at-a-time.
    Value readCalleeArg(SnapshotIterator &it) const {
        return recoverValue(it, recoverSlot(it));
    }

  private:
    // Offset from script->code.
    uint32_t pcOffset_;
    uint32_t numOperands_;
    bool resumeAfter_;
    bool lastFrame_;

    // Header of the frame, part of the first operands.
    Slot scopeChainSlot_;
    Slot argObjSlot_;
    Slot thisSlot_;
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
