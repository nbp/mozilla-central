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
RInstruction::recoverSlot(SnapshotIterator &it) const
{
    return it.readOperand();
}

Value
RInstruction::recoverValue(const SnapshotIterator &it, const Slot &slot) const
{
    return it.slotValue(slot);
}

Value
RInstruction::maybeRecoverValue(const SnapshotIterator &it, const Slot &slot) const
{
    return it.maybeReadFromSlot(slot);
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
RResumePoint::fillOperands(SnapshotIterator &it, JSScript *script, bool isFunction)
{
    scopeChainSlot_ = recoverSlot(it);

    if (script->argumentsHasVarBinding())
        argObjSlot_ = recoverSlot(it);

    if (isFunction)
        thisSlot_ = recoverSlot(it);
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
    JS_ASSERT(it.operandIndex() + calleeArgs <= numOperands());
    unsigned skipCount = numOperands() - calleeArgs - it.operandIndex();
    for (unsigned j = 0; j < skipCount; j++)
        recoverSlot(it);

    return recoverValue(it, recoverSlot(it));
}

Value
RResumePoint::readStackSlot(JSContext *cx, SnapshotIterator &it) const
{
    Slot slot = recoverSlot(it);

    // If coming from an invalidation bailout, and this is the topmost value,
    // and a value override has been specified, don't read from the
    // iterator. Otherwise, we risk using a garbage value.
    if (it.operandIndex() == numOperands() && isLastFrame() &&
        cx->runtime->hasIonReturnOverride())
    {
        return cx->runtime->takeIonReturnOverride();
    }

    return recoverValue(it, slot);
}
