/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_SnapshotWriter_h
#define ion_SnapshotWriter_h

#include "ion/Ion.h"
#include "ion/IonCode.h"
#include "ion/Registers.h"
#include "ion/CompactBuffer.h"
#include "ion/Bailouts.h"

namespace js {
namespace ion {

// Collects snapshots in a contiguous buffer, which is copied into IonScript
// memory after code generation.
class SnapshotWriter
{
    CompactBufferWriter writer_;

    // These are only used to assert sanity.
    uint32_t nslots_;
    uint32_t slotsWritten_;
    SnapshotOffset lastStart_;

    void writeSlotHeader(JSValueType type, uint32_t regCode);

  public:
    SnapshotOffset startSnapshot(BailoutKind kind, bool resumeAfter,
                                 RecoverOffset offset, uint32_t numSlots);
#ifdef TRACK_SNAPSHOTS
    void trackFrame(uint32_t pcOpcode, uint32_t mirOpcode, uint32_t mirId,
                                     uint32_t lirOpcode, uint32_t lirId);
#endif

    void addSlot(const FloatRegister &reg);
    void addSlot(JSValueType type, const Register &reg);
    void addSlot(JSValueType type, int32_t stackIndex);
    void addUndefinedSlot();
    void addNullSlot();
    void addInt32Slot(int32_t value);
    void addConstantPoolSlot(uint32_t index);
#if defined(JS_NUNBOX32)
    void addSlot(const Register &type, const Register &payload);
    void addSlot(const Register &type, int32_t payloadStackIndex);
    void addSlot(int32_t typeStackIndex, const Register &payload);
    void addSlot(int32_t typeStackIndex, int32_t payloadStackIndex);
#elif defined(JS_PUNBOX64)
    void addSlot(const Register &value);
    void addSlot(int32_t valueStackSlot);
#endif
    void addRecoverSlot(size_t opIndex);
    void endSnapshot();

    bool oom() const {
        return writer_.oom() || writer_.length() >= MAX_BUFFER_SIZE;
    }

    size_t size() const {
        return writer_.length();
    }
    const uint8_t *buffer() const {
        return writer_.buffer();
    }
};

class MNode;

// Collect the layout of the stack into a compacting buffer which is copied into
// IonScript memory after code generation.
class RecoverWriter
{
    CompactBufferWriter writer_;

  public:
    RecoverOffset startRecover(uint32_t frameCount, uint32_t instCount);

    void writeRecover(const MNode *ins);

    void endRecover() {
    }

    size_t size() const {
        return writer_.length();
    }
    const uint8_t *buffer() const {
        return writer_.buffer();
    }

    bool oom() const {
        return writer_.oom() || writer_.length() >= MAX_BUFFER_SIZE;
    }
};

}
}

#endif /* ion_SnapshotWriter_h */
