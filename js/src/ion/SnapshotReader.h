/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_SnapshotReader_h
#define ion_SnapshotReader_h

#include "mozilla/Util.h"

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

    class SlotPosition {
        friend class SnapshotReader;

        CompactBufferReader::BufferPosition reader_;
        uint32_t slotCount_;
        uint32_t slotsRead_;
    };

    void savePosition(SlotPosition &pos) {
        reader_.savePosition(pos.reader_);
        pos.slotCount_ = slotCount_;
        pos.slotsRead_ = slotsRead_;
    }

    void restorePosition(SlotPosition &pos) {
        reader_.restorePosition(pos.reader_);
        slotCount_ = pos.slotCount_;
        slotsRead_ = pos.slotsRead_;
    }
};

class RResumePoint;

// A Recover reader reads the layout of stack and give a structure to the
// content of the snapshot reader.
class RecoverReader
{
    CompactBufferReader reader_;

    uint32_t frameCount_;
    uint32_t frameRead_;

    mozilla::AlignedStorage<RInstructionMaxSize> mem_;

    void readRecoverHeader();
    void readFrameHeader();
    void init();

  public:
    RecoverReader(const uint8_t *buffer = NULL, const uint8_t *end = NULL);
    RecoverReader(const IonScript *ion, RecoverOffset offset);

    const RResumePoint &currentFrame() const {
        return *reinterpret_cast<const RResumePoint *>(mem_.addr());
    }
    RResumePoint &currentFrame() {
        return *reinterpret_cast<RResumePoint *>(mem_.addr());
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

    void restart();
    void resetOn(const IonScript *ion, RecoverOffset offset);

  private:
    // Forbid copy as we manipulate raw memory.
    RecoverReader(const RecoverReader& other) MOZ_DELETE;
    const RecoverReader& operator=(const RecoverReader& other) MOZ_DELETE;
};

}
}

#endif /* ion_SnapshotReader_h */
