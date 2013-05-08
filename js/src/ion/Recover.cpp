/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Recover.h"

#include "MIR.h"
#include "MIRGraph.h"

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
    uint32_t bits = reader.readUnsigned();
    resumeAfter_ = bits & 1;
    pcOffset_ = bits >> 1;
    numOperands_ = reader.readUnsigned();
}
