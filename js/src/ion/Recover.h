/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_recover_h__
#define jsion_recover_h__

#include "CompactBuffer.h"

namespace js {
namespace ion {

enum RecoverKind
{
    // Logic used to recover one frame.
    Recover_ResumePoint
};

class MNode;
typedef void (*RWriter)(CompactBufferWriter &writer_, MNode *ins);

struct RResumePoint;

struct RInstruction
{
    virtual void read(CompactBufferReader &reader) = 0;
    virtual size_t numOperands() const = 0;
    static RInstruction *dispatch(void *mem, CompactBufferReader &read);

    virtual bool isResumePoint() const {
        return false;
    }

    RResumePoint *toResumePoint() {
        return reinterpret_cast<RResumePoint *>(this);
    }
};

struct RResumePoint : public RInstruction
{
    static void write(CompactBufferWriter &writer, MNode *ins);
    void read(CompactBufferReader &reader);

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

    // Offset from script->code.
    uint32_t pcOffset_;
    uint32_t numOperands_;
    bool resumeAfter_;
    bool lastFrame_;
};

} // namespace ion
} // namespace js

#endif // jsion_recover_h__
