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

struct RInstruction
{
    static RInstruction *dispatch(void *mem, CompactBufferReader &read);

    virtual size_t numOperands() const = 0;
    virtual bool resume(JSContext *cx, HandleScript script, SnapshotIterator &it) const = 0;

    Slot recoverSlot(SnapshotIterator &it) const;
    Value recoverValue(const SnapshotIterator &it, const Slot &slot) const;
    Value maybeRecoverValue(const SnapshotIterator &it, const Slot &slot) const;

  public:
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

struct RResumePoint : public RInstruction
{
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
