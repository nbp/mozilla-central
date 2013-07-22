/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_SnapshotReader_h
#define ion_SnapshotReader_h

#include "ion/CompactBuffer.h"
#include "ion/IonTypes.h"
#include "ion/IonCode.h"
#include "ion/Slots.h"

namespace js {
namespace ion {

#ifdef TRACK_SNAPSHOTS
class LInstruction;
#endif

// A snapshot reader reads the entries out of the compressed snapshot buffer in
// a script. These entries describe the stack state of an Ion frame at a given
// position in JIT code.
class SnapshotReader
{
    CompactBufferReader reader_;

    RecoverOffset recoverOffset_;

    uint32_t slotCount_;          // Number of slots.
    uint32_t slotsRead_;          // Number of slots that have been read.
    BailoutKind bailoutKind_;
    bool lastFrameResumeAfter_;

#ifdef TRACK_SNAPSHOTS
  private:
    uint32_t pcOpcode_;
    uint32_t mirOpcode_;
    uint32_t mirId_;
    uint32_t lirOpcode_;
    uint32_t lirId_;
  public:
    void spewBailingFrom() const;
#endif

  private:

    void readSnapshotHeader();
    void readFrameHeader();

    template <typename T> inline T readVariableLength();

  public:
    SnapshotReader(const uint8_t *buffer, const uint8_t *end);

    RecoverOffset recoverOffset() const {
        return recoverOffset_;
    }
    BailoutKind bailoutKind() const {
        return bailoutKind_;
    }
    bool lastFrameResumeAfter() const {
        return lastFrameResumeAfter_;
    }
    Slot readSlot();

    size_t index() const {
        return slotsRead_;
    }
    bool moreSlots() const {
        return slotsRead_ < slotCount_;
    }

    void restart() {
        reader_.restart();
        slotsRead_ = 0;
        readSnapshotHeader();
    }
    void resetOn(const IonScript *ion, SnapshotOffset offset);
};

// A Recover reader reads the layout of stack and give a structure to the
// content of the snapshot reader.
class RecoverReader
{
    CompactBufferReader reader_;

    uint32_t frameCount_;
    uint32_t operandCount_;

    uint32_t frameRead_;
    uint32_t operandRead_;

    uint32_t slotIndex_;
    uint32_t pcOffset_;

    void readRecoverHeader();
    void readFrameHeader();
    void init();

  public:
    RecoverReader(const uint8_t *buffer = NULL, const uint8_t *end = NULL);
    RecoverReader(const IonScript *ion, RecoverOffset offset);

    // Return the index of the operand within the snapshot.
    size_t readOperandSlotIndex() {
        JS_ASSERT(moreOperands());
        return slotIndex_ + operandRead_++;
    }
    size_t numOperands() const {
        return operandCount_;
    }
    size_t nextOperandIndex() const {
        return operandRead_;
    }
    bool moreOperands() const {
        return operandRead_ < operandCount_;
    }

    void nextFrame() {
        readFrameHeader();
    }
    size_t frameIndex() const {
        return frameRead_;
    }
    size_t numFrames() const {
        return frameCount_;
    }
    bool moreFrames() const {
        return frameRead_ < frameCount_;
    }

    uint32_t pcOffset() const {
        return pcOffset_;
    }

    void restart();
    void resetOn(const IonScript *ion, RecoverOffset offset);
};

}
}

#endif /* ion_SnapshotReader_h */
