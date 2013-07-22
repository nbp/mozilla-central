/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ion/Recover.h"

#include "jsfun.h"

#include "ion/CompactBuffer.h"
#include "ion/MIR.h"
#include "ion/MIRGraph.h"
#include "ion/SnapshotWriter.h"
#include "ion/IonFrameIterator.h"

using namespace js;
using namespace js::ion;

void
RecoverWriter::writeRecover(const MResumePoint *rp)
{
    rp->writeRecover(writer_);
}

size_t
SlotVector::length() const
{
    return si_->slots();
}

Slot
SlotVector::operator [](size_t i) const
{
    SnapshotIterator &si = *const_cast<SlotVector *>(this)->si_;
    JS_ASSERT(i < si.slots());
    if (MOZ_LIKELY(si.nextOperandIndex() == i))
        return si.readSlot();
    if (MOZ_UNLIKELY(si.nextOperandIndex() > i))
        si.restartFrame();
    while (si.nextOperandIndex() < i)
        si.readSlot();
    return si.readSlot();
}

void
MResumePoint::writeRecover(CompactBufferWriter &writer) const
{
    size_t pcOffset = pc() - block()->info().script()->code;
    writer.writeUnsigned(pcOffset);
    writer.writeUnsigned(numOperands());
}

RResumePoint::RResumePoint(CompactBufferReader &reader)
{
    JS_ASSERT(sizeof(*this) <= RInstructionMaxSize);
    pcOffset_ = reader.readUnsigned();
    numOperands_ = reader.readUnsigned();

    IonSpew(IonSpew_Snapshots, "Recover ResumePoint: pc offset %u, noperands %u",
            pcOffset_, numOperands_);
}

void
RResumePoint::readSlots(SnapshotIterator &si, JSScript *script, JSFunction *fun)
{
    pcOffset_ = si.pcOffset();
    scopeChainSlot_ = si.readSlot();

    if (script->argumentsHasVarBinding())
        argObjSlot_ = si.readSlot();
    else
        argObjSlot_ = Slot();

    size_t nargs = 0;
    if (fun) {
        thisSlot_ = si.readSlot();
        nargs = fun->nargs;
    } else
        thisSlot_ = Slot();

    startFormalArgs_ = si.nextOperandIndex();
    startFixedSlots_ = startFormalArgs_ + nargs;
    startStackSlots_ = startFixedSlots_ + script->nfixed;
    numOperands_ = si.slots();
    slots_.init(si);
}
