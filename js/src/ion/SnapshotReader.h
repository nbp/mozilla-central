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

    uint32_t pcOffset_;           // Offset from script->code.
    uint32_t slotCount_;          // Number of slots.
    uint32_t frameCount_;
    BailoutKind bailoutKind_;
    uint32_t framesRead_;         // Number of frame headers that have been read.
    uint32_t slotsRead_;          // Number of slots that have been read.
    bool resumeAfter_;

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

    uint32_t pcOffset() const {
        return pcOffset_;
    }
    uint32_t slots() const {
        return slotCount_;
    }
    BailoutKind bailoutKind() const {
        return bailoutKind_;
    }
    bool resumeAfter() const {
        if (moreFrames())
            return false;
        return resumeAfter_;
    }
    bool moreFrames() const {
        return framesRead_ < frameCount_;
    }
    void nextFrame() {
        readFrameHeader();
    }
    Slot readSlot();

    bool moreSlots() const {
        return slotsRead_ < slotCount_;
    }
    uint32_t frameCount() const {
        return frameCount_;
    }

    void restart() {
        reader_.restart();
        slotCount_ = slotsRead_ = 0;
        readSnapshotHeader();
        nextFrame();
    }
};

}
}

#endif /* ion_SnapshotReader_h */
