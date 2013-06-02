/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_snapshots_h__
#define jsion_snapshots_h__

#include "IonTypes.h"
#include "IonCode.h"
#include "CompactBuffer.h"
#include "Slots.h"

#include "mozilla/Util.h"

namespace js {
namespace ion {

class IonScript;

#ifdef TRACK_SNAPSHOTS
class LInstruction;
#endif

// A snapshot reader reads the entries out of the compressed snapshot buffer in
// a script. These entries describe the stack state of an Ion frame at a given
// position in JIT code.
class SnapshotReader
{
    CompactBufferReader reader_;

    RecoverOffset recoverOffset_; // Offset from script->code.

    uint32_t slotCount_;          // Number of slots.
    uint32_t slotsRead_;          // Number of slots that have been read.
    BailoutKind bailoutKind_;
    bool resumeAfter_;

#ifdef TRACK_SNAPSHOTS
  private:
    uint32_t pcOpcode_;
    uint32_t mirOpcode_;
    uint32_t mirId_;
    uint32_t lirOpcode_;
    uint32_t lirId_;

    void readLocation();

  public:
    void spewBailingFrom() const;
#endif

  private:
    void readSnapshotHeader();
    void readFrameHeader();

  public:
    SnapshotReader(const uint8_t *buffer, const uint8_t *end);

    RecoverOffset recoverOffset() const {
        return recoverOffset_;
    }
    BailoutKind bailoutKind() const {
        return bailoutKind_;
    }
    Slot readSlot();

    size_t index() const {
        return slotsRead_;
    }
    size_t numSlots() const {
        return slotCount_;
    }

    void restart();
};

class RInstruction;

class RecoverReader
{
    CompactBufferReader reader_;

    uint32_t frameCount_;

    uint32_t operationCount_;
    uint32_t operandCount_;

    uint32_t operationRead_;
    uint32_t operandRead_;

    // Operation
    RInstruction *operation_;
    mozilla::AlignedStorage<RMaxSize> opStorage_;

    void readRecoverHeader();
    void readOperationHeader();

  public:
    RecoverReader(const uint8_t *buffer = NULL, const uint8_t *end = NULL);
    RecoverReader(const IonScript *ion, RecoverOffset offset);

    void readOperand(bool *isSlot, size_t *index);
    void skipOperand() {
        bool isSlot = false;
        size_t index = 0;
        readOperand(&isSlot, &index);
    }
    size_t operandIndex() const {
        return operandRead_;
    }
    bool moreOperand() const {
        return operandRead_ < operandCount_;
    }

    size_t operationIndex() const {
        return operationRead_;
    }
    size_t numOperations() const {
        return operationCount_;
    }
    bool moreOperation() const {
        return operationRead_ < operationCount_;
    }
    void nextOperation() {
        while (moreOperand())
            skipOperand();
        readOperationHeader();
    }

    RInstruction *operation() {
        return operation_;
    }
    const RInstruction *operation() const {
        return operation_;
    }

    size_t frameCount() const {
        return frameCount_;
    }

    void restart();
};

}
}

#endif // jsion_snapshots_h__

