/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Recover.h"

#include "IonSpewer.h"
#include "MIR.h"
#include "MIRGraph.h"
#include "Slots.h"
#include "IonFrameIterator.h"

using namespace js;
using namespace js::ion;

RInstruction *
RInstruction::dispatch(void *mem, CompactBufferReader &reader)
{
    RInstruction *ins = NULL;
    RecoverKind type = RecoverKind(reader.readUnsigned());
    switch (type) {
      case Recover_ResumePoint:
        ins = new (mem) RResumePoint();
        break;
    }

    ins->read(reader);
    return ins;
}

Slot
RInstruction::recoverSlot(SnapshotIterator &it)
{
    bool isSlot = false;
    size_t index = 0;
    it.recover_.readOperand(&isSlot, &index);

    // The slot refers to the result of an operation.
    if (!isSlot)
        return Slot(Slot::RESUME_OPERATION, index);

    // Ensure we read in the right order.
    JS_ASSERT(it.index() <= index);

    // Skip un-read slot of the snapshot.
    while (it.index() < index)
        it.nextSlot();
    Slot s(it.slot_);
    it.nextSlot();
    return s;
}

Value
RInstruction::recoverValue(const SnapshotIterator &it, const Slot &slot) const
{
    return it.slotValue(slot);
}

void
MResumePoint::writeRInstruction(CompactBufferWriter &writer) const
{
    writer.writeUnsigned(Recover_ResumePoint);

    uint32_t bits = 0;
    bits = pc() - block()->info().script()->code;
    bits = bits << 1;
    bits = bits | (mode() == MResumePoint::ResumeAfter ? 1 : 0);
    writer.writeUnsigned(bits);
    writer.writeUnsigned(numOperands());
}

void
RResumePoint::read(CompactBufferReader &reader)
{
    JS_STATIC_ASSERT(sizeof(*this) <= RMaxSize);
    uint32_t bits = reader.readUnsigned();
    resumeAfter_ = bits & 1;
    pcOffset_ = bits >> 1;
    numOperands_ = reader.readUnsigned();

    IonSpew(IonSpew_Snapshots, "RResumePoint: pc offset %u, noperands %u",
            pcOffset_, numOperands_);
}

void
RResumePoint::fillOperands(SnapshotIterator &it, JSScript *script, JSFunction *fun)
{
    scopeChainSlot_ = recoverSlot(it);

    if (fun)
        thisSlot_ = recoverSlot(it);

    if (script->argumentsHasVarBinding()) {
        // do something here.
    }
}

Value
RResumePoint::recoverCallee(SnapshotIterator &it, JSScript *script, uint32_t *numActualArgs)
{
    JS_ASSERT(!isLastFrame());
    jsbytecode *pc = script->code + pcOffset();

    // Recover the number of actual arguments from the script unless we inlined
    // a call made with fun.apply, in which case we recover the number of
    // argument from the previous call.
    JS_ASSERT(js_CodeSpec[*pc].format & JOF_INVOKE);
    if (JSOp(*pc) != JSOP_FUNAPPLY)
        *numActualArgs = GET_ARGC(pc);

    JS_ASSERT(*numActualArgs != 0xbadbad);

    // Do not consume |fun|, |this| and the arguments of the callee.
    unsigned calleeArgs = *numActualArgs + 2;

    // Skip over not-yet consumed operands.
    JS_ASSERT(it.recover_.operandIndex() + calleeArgs <= numOperands());
    unsigned skipCount = numOperands() - calleeArgs - it.recover_.operandIndex();
    for (unsigned j = 0; j < skipCount; j++)
        recoverSlot(it);

    return it.slotValue(recoverSlot(it));
}
