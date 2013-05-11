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

enum RecoverKind
{
    // Logic used to recover one frame.
    Recover_ResumePoint
};

class MNode;
class SnapshotIterator;

struct RResumePoint;

struct RInstruction
{
    virtual void read(CompactBufferReader &reader) = 0;
    virtual size_t numOperands() const = 0;
    static RInstruction *dispatch(void *mem, CompactBufferReader &read);

    Slot recoverSlot(SnapshotIterator &it) const;
    Value recoverValue(const SnapshotIterator &it, const Slot &slot) const;
    Value maybeRecoverValue(const SnapshotIterator &it, const Slot &slot) const;

    virtual bool isResumePoint() const {
        return false;
    }

    RResumePoint *toResumePoint() {
        return reinterpret_cast<RResumePoint *>(this);
    }
    const RResumePoint *toResumePoint() const {
        return reinterpret_cast<const RResumePoint *>(this);
    }
};

struct RResumePoint : public RInstruction
{
    static void write(CompactBufferWriter &writer, MNode *ins);
    void read(CompactBufferReader &reader);
    void fillOperands(SnapshotIterator &it, JSScript *script, bool isFunction);
    Value recoverCallee(SnapshotIterator &it, JSScript *script, uint32_t *numActualArgs);

    bool isResumePoint() const {
        return true;
    }

    size_t numOperands() const {
        return numOperands_;
    }

    uint32_t pcOffset() const {
        return pcOffset_;
    }

    bool resumeAfter() const {
        return resumeAfter_;
    }

    void setLastFrame() {
        lastFrame_ = true;
    }
    bool isLastFrame() const {
        return lastFrame_;
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

    Slot readAnySlot(SnapshotIterator &it) const {
        return recoverSlot(it);
    }

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
    Value readCalleeArg(SnapshotIterator &it) const {
        return recoverValue(it, recoverSlot(it));
    }

    // Offset from script->code.
    uint32_t pcOffset_;
    uint32_t numOperands_;
    bool resumeAfter_;
    bool lastFrame_;

    Slot scopeChainSlot_;
    Slot argObjSlot_;
    Slot thisSlot_;
};

} // namespace ion
} // namespace js

#endif // jsion_recover_h__
