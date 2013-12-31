/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ion/IonFrames-inl.h"

#include "jsfun.h"
#include "jsobj.h"
#include "jsscript.h"

#include "gc/Marking.h"
#include "ion/BaselineFrame.h"
#include "ion/BaselineIC.h"
#include "ion/BaselineJIT.h"
#include "ion/Ion.h"
#include "ion/IonCompartment.h"
#include "ion/IonMacroAssembler.h"
#include "ion/IonSpewer.h"
#include "ion/PcScriptCache.h"
#include "ion/Safepoints.h"
#include "ion/Slots.h"
#include "ion/SnapshotReader.h"
#include "ion/VMFunctions.h"

#include "jsfuninlines.h"

#include "ion/IonFrameIterator-inl.h"
#include "vm/Probes-inl.h"

namespace js {
namespace ion {

IonFrameIterator::IonFrameIterator(const ActivationIterator &activations)
    : current_(activations.jitTop()),
      type_(IonFrame_Exit),
      returnAddressToFp_(NULL),
      frameSize_(0),
      cachedSafepointIndex_(NULL),
      activation_(activations.activation()->asJit())
{
}

IonFrameIterator::IonFrameIterator(IonJSFrameLayout *fp)
  : current_((uint8_t *)fp),
    type_(IonFrame_OptimizedJS),
    returnAddressToFp_(fp->returnAddress()),
    frameSize_(fp->prevFrameLocalSize())
{
}

bool
IonFrameIterator::checkInvalidation() const
{
    IonScript *dummy;
    return checkInvalidation(&dummy);
}

bool
IonFrameIterator::checkInvalidation(IonScript **ionScriptOut) const
{
    uint8_t *returnAddr = returnAddressToFp();
    JSScript *script = this->script();
    // N.B. the current IonScript is not the same as the frame's
    // IonScript if the frame has since been invalidated.
    bool invalidated;
    if (isParallelFunctionFrame()) {
        invalidated = !script->hasParallelIonScript() ||
            !script->parallelIonScript()->containsReturnAddress(returnAddr);
    } else {
        invalidated = !script->hasIonScript() ||
            !script->ionScript()->containsReturnAddress(returnAddr);
    }
    if (!invalidated)
        return false;

    int32_t invalidationDataOffset = ((int32_t *) returnAddr)[-1];
    uint8_t *ionScriptDataOffset = returnAddr + invalidationDataOffset;
    IonScript *ionScript = (IonScript *) Assembler::getPointer(ionScriptDataOffset);
    JS_ASSERT(ionScript->containsReturnAddress(returnAddr));
    *ionScriptOut = ionScript;
    return true;
}

CalleeToken
IonFrameIterator::calleeToken() const
{
    return ((IonJSFrameLayout *) current_)->calleeToken();
}

JSFunction *
IonFrameIterator::callee() const
{
    JS_ASSERT(isScripted());
    JS_ASSERT(isFunctionFrame() || isParallelFunctionFrame());
    if (isFunctionFrame())
        return CalleeTokenToFunction(calleeToken());
    return CalleeTokenToParallelFunction(calleeToken());
}

JSFunction *
IonFrameIterator::maybeCallee() const
{
    if (isScripted() && (isFunctionFrame() || isParallelFunctionFrame()))
        return callee();
    return NULL;
}

bool
IonFrameIterator::isNative() const
{
    if (type_ != IonFrame_Exit || isFakeExitFrame())
        return false;
    return exitFrame()->footer()->ionCode() == NULL;
}

bool
IonFrameIterator::isOOLNativeGetter() const
{
    if (type_ != IonFrame_Exit)
        return false;
    return exitFrame()->footer()->ionCode() == ION_FRAME_OOL_NATIVE_GETTER;
}

bool
IonFrameIterator::isOOLPropertyOp() const
{
    if (type_ != IonFrame_Exit)
        return false;
    return exitFrame()->footer()->ionCode() == ION_FRAME_OOL_PROPERTY_OP;
}

bool
IonFrameIterator::isOOLProxyGet() const
{
    if (type_ != IonFrame_Exit)
        return false;
    return exitFrame()->footer()->ionCode() == ION_FRAME_OOL_PROXY_GET;
}

bool
IonFrameIterator::isDOMExit() const
{
    if (type_ != IonFrame_Exit)
        return false;
    return exitFrame()->isDomExit();
}

bool
IonFrameIterator::isFunctionFrame() const
{
    return CalleeTokenIsFunction(calleeToken());
}

bool
IonFrameIterator::isParallelFunctionFrame() const
{
    return GetCalleeTokenTag(calleeToken()) == CalleeToken_ParallelFunction;
}

bool
IonFrameIterator::isEntryJSFrame() const
{
    if (prevType() == IonFrame_OptimizedJS || prevType() == IonFrame_Unwound_OptimizedJS)
        return false;

    if (prevType() == IonFrame_BaselineStub || prevType() == IonFrame_Unwound_BaselineStub)
        return false;

    if (prevType() == IonFrame_Entry)
        return true;

    IonFrameIterator iter(*this);
    ++iter;
    for (; !iter.done(); ++iter) {
        if (iter.isScripted())
            return false;
    }
    return true;
}

JSScript *
IonFrameIterator::script() const
{
    JS_ASSERT(isScripted());
    if (isBaselineJS())
        return baselineFrame()->script();
    JSScript *script = ScriptFromCalleeToken(calleeToken());
    JS_ASSERT(script);
    return script;
}

void
IonFrameIterator::baselineScriptAndPc(JSScript **scriptRes, jsbytecode **pcRes) const
{
    JS_ASSERT(isBaselineJS());
    JSScript *script = this->script();
    if (scriptRes)
        *scriptRes = script;
    uint8_t *retAddr = returnAddressToFp();
    if (pcRes) {
        // If the return address is into the prologue entry addr, then assume PC 0.
        if (retAddr == script->baselineScript()->prologueEntryAddr()) {
            *pcRes = 0;
            return;
        }

        // The return address _may_ be a return from a callVM or IC chain call done for
        // some op.
        ICEntry *icEntry = script->baselineScript()->maybeICEntryFromReturnAddress(retAddr);
        if (icEntry) {
            *pcRes = icEntry->pc(script);
            return;
        }

        // If not, the return address _must_ be the start address of an op, which can
        // be computed from the pc mapping table.
        *pcRes = script->baselineScript()->pcForReturnAddress(script, retAddr);
    }
}

Value *
IonFrameIterator::nativeVp() const
{
    JS_ASSERT(isNative());
    return exitFrame()->nativeExit()->vp();
}

Value *
IonFrameIterator::actualArgs() const
{
    return jsFrame()->argv() + 1;
}

static inline size_t
SizeOfFramePrefix(FrameType type)
{
    switch (type) {
      case IonFrame_Entry:
        return IonEntryFrameLayout::Size();
      case IonFrame_BaselineJS:
      case IonFrame_OptimizedJS:
      case IonFrame_Unwound_OptimizedJS:
        return IonJSFrameLayout::Size();
      case IonFrame_BaselineStub:
        return IonBaselineStubFrameLayout::Size();
      case IonFrame_Rectifier:
        return IonRectifierFrameLayout::Size();
      case IonFrame_Unwound_Rectifier:
        return IonUnwoundRectifierFrameLayout::Size();
      case IonFrame_Exit:
        return IonExitFrameLayout::Size();
      case IonFrame_Osr:
        return IonOsrFrameLayout::Size();
      default:
        MOZ_ASSUME_UNREACHABLE("unknown frame type");
    }
}

uint8_t *
IonFrameIterator::prevFp() const
{
    size_t currentSize = SizeOfFramePrefix(type_);
    // This quick fix must be removed as soon as bug 717297 land.  This is
    // needed because the descriptor size of JS-to-JS frame which is just after
    // a Rectifier frame should not change. (cf EnsureExitFrame function)
    if (isFakeExitFrame()) {
        JS_ASSERT(SizeOfFramePrefix(IonFrame_BaselineJS) ==
                  SizeOfFramePrefix(IonFrame_OptimizedJS));
        currentSize = SizeOfFramePrefix(IonFrame_OptimizedJS);
    }
    currentSize += current()->prevFrameLocalSize();
    return current_ + currentSize;
}

IonFrameIterator &
IonFrameIterator::operator++()
{
    JS_ASSERT(type_ != IonFrame_Entry);

    frameSize_ = prevFrameLocalSize();
    cachedSafepointIndex_ = NULL;

    // If the next frame is the entry frame, just exit. Don't update current_,
    // since the entry and first frames overlap.
    if (current()->prevType() == IonFrame_Entry) {
        type_ = IonFrame_Entry;
        return *this;
    }

    // Note: prevFp() needs the current type, so set it after computing the
    // next frame.
    uint8_t *prev = prevFp();
    type_ = current()->prevType();
    if (type_ == IonFrame_Unwound_OptimizedJS)
        type_ = IonFrame_OptimizedJS;
    else if (type_ == IonFrame_Unwound_BaselineStub)
        type_ = IonFrame_BaselineStub;
    returnAddressToFp_ = current()->returnAddress();
    current_ = prev;
    return *this;
}

uintptr_t *
IonFrameIterator::spillBase() const
{
    // Get the base address to where safepoint registers are spilled.
    // Out-of-line calls do not unwind the extra padding space used to
    // aggregate bailout tables, so we use frameSize instead of frameLocals,
    // which would only account for local stack slots.
    return reinterpret_cast<uintptr_t *>(fp() - ionScript()->frameSize());
}

MachineState
IonFrameIterator::machineState() const
{
    SafepointReader reader(ionScript(), safepoint());
    uintptr_t *spill = spillBase();

    // see CodeGeneratorShared::saveLive, we are only copying GPRs for now, FPUs
    // are stored after but are not saved in the safepoint.  This means that we
    // are unable to restore any FPUs registers from an OOL VM call.  This can
    // cause some trouble for f.arguments.
    MachineState machine;
    for (GeneralRegisterBackwardIterator iter(reader.allSpills()); iter.more(); iter++)
        machine.setRegisterLocation(*iter, --spill);

    return machine;
}

static void
CloseLiveIterator(JSContext *cx, const InlineFrameIterator &frame, uint32_t localSlot)
{
    const SnapshotIterator &s = frame.snapshotIterator();
    const RResumePoint &rp = frame.resumePoint();
    Value v = s.slotValue(rp.stackSlot(localSlot - 1));
    RootedObject obj(cx, &v.toObject());

    if (cx->isExceptionPending())
        UnwindIteratorForException(cx, obj);
    else
        UnwindIteratorForUncatchableException(cx, obj);
}

static void
HandleExceptionIon(JSContext *cx, const InlineFrameIterator &frame, ResumeFromException *rfe,
                   bool *overrecursed)
{
    RootedScript script(cx, frame.script());
    jsbytecode *pc = frame.pc();

    if (!script->hasTrynotes())
        return;

    JSTryNote *tn = script->trynotes()->vector;
    JSTryNote *tnEnd = tn + script->trynotes()->length;

    uint32_t pcOffset = uint32_t(pc - script->main());
    for (; tn != tnEnd; ++tn) {
        if (pcOffset < tn->start)
            continue;
        if (pcOffset >= tn->start + tn->length)
            continue;

        switch (tn->kind) {
          case JSTRY_ITER: {
            JS_ASSERT(JSOp(*(script->main() + tn->start + tn->length)) == JSOP_ENDITER);
            JS_ASSERT(tn->stackDepth > 0);

            uint32_t localSlot = tn->stackDepth;
            CloseLiveIterator(cx, frame, localSlot);
            break;
          }

          case JSTRY_LOOP:
            break;

          case JSTRY_CATCH:
            if (cx->isExceptionPending()) {
                // Bailout at the start of the catch block.
                jsbytecode *catchPC = script->main() + tn->start + tn->length;

                ExceptionBailoutInfo excInfo;
                excInfo.frameNo = frame.frameNo();
                excInfo.resumePC = catchPC;
                excInfo.numExprSlots = tn->stackDepth;

                BaselineBailoutInfo *info = NULL;
                uint32_t retval = ExceptionHandlerBailout(cx, frame, excInfo, &info);

                if (retval == BAILOUT_RETURN_OK) {
                    JS_ASSERT(info);
                    rfe->kind = ResumeFromException::RESUME_BAILOUT;
                    rfe->target = cx->runtime()->ionRuntime()->getBailoutTail()->raw();
                    rfe->bailoutInfo = info;
                    return;
                }

                // Bailout failed. If there was a fatal error, clear the
                // exception to turn this into an uncatchable error. If the
                // overrecursion check failed, continue popping all inline
                // frames and have the caller report an overrecursion error.
                JS_ASSERT(!info);
                cx->clearPendingException();

                if (retval == BAILOUT_RETURN_OVERRECURSED)
                    *overrecursed = true;
                else
                    JS_ASSERT(retval == BAILOUT_RETURN_FATAL_ERROR);
            }
            break;

          default:
            MOZ_ASSUME_UNREACHABLE("Unexpected try note");
        }
    }
}

static void
HandleExceptionBaseline(JSContext *cx, const IonFrameIterator &frame, ResumeFromException *rfe,
                        bool *calledDebugEpilogue)
{
    JS_ASSERT(frame.isBaselineJS());
    JS_ASSERT(!*calledDebugEpilogue);

    RootedScript script(cx);
    jsbytecode *pc;
    frame.baselineScriptAndPc(script.address(), &pc);

    if (cx->isExceptionPending() && cx->compartment()->debugMode()) {
        BaselineFrame *baselineFrame = frame.baselineFrame();
        JSTrapStatus status = DebugExceptionUnwind(cx, baselineFrame, pc);
        switch (status) {
          case JSTRAP_ERROR:
            // Uncatchable exception.
            JS_ASSERT(!cx->isExceptionPending());
            break;

          case JSTRAP_CONTINUE:
          case JSTRAP_THROW:
            JS_ASSERT(cx->isExceptionPending());
            break;

          case JSTRAP_RETURN:
            JS_ASSERT(baselineFrame->hasReturnValue());
            if (ion::DebugEpilogue(cx, baselineFrame, true)) {
                rfe->kind = ResumeFromException::RESUME_FORCED_RETURN;
                rfe->framePointer = frame.fp() - BaselineFrame::FramePointerOffset;
                rfe->stackPointer = reinterpret_cast<uint8_t *>(baselineFrame);
                return;
            }

            // DebugEpilogue threw an exception. Propagate to the caller frame.
            *calledDebugEpilogue = true;
            return;

          default:
            MOZ_ASSUME_UNREACHABLE("Invalid trap status");
        }
    }

    if (!script->hasTrynotes())
        return;

    JSTryNote *tn = script->trynotes()->vector;
    JSTryNote *tnEnd = tn + script->trynotes()->length;

    uint32_t pcOffset = uint32_t(pc - script->main());
    for (; tn != tnEnd; ++tn) {
        if (pcOffset < tn->start)
            continue;
        if (pcOffset >= tn->start + tn->length)
            continue;

        // Skip if the try note's stack depth exceeds the frame's stack depth.
        // See the big comment in TryNoteIter::settle for more info.
        JS_ASSERT(frame.baselineFrame()->numValueSlots() >= script->nfixed);
        size_t stackDepth = frame.baselineFrame()->numValueSlots() - script->nfixed;
        if (tn->stackDepth > stackDepth)
            continue;

        // Unwind scope chain (pop block objects).
        if (cx->isExceptionPending())
            UnwindScope(cx, frame.baselineFrame(), tn->stackDepth);

        // Compute base pointer and stack pointer.
        rfe->framePointer = frame.fp() - BaselineFrame::FramePointerOffset;
        rfe->stackPointer = rfe->framePointer - BaselineFrame::Size() -
            (script->nfixed + tn->stackDepth) * sizeof(Value);

        switch (tn->kind) {
          case JSTRY_CATCH:
            if (cx->isExceptionPending()) {
                // Resume at the start of the catch block.
                rfe->kind = ResumeFromException::RESUME_CATCH;
                jsbytecode *catchPC = script->main() + tn->start + tn->length;
                rfe->target = script->baselineScript()->nativeCodeForPC(script, catchPC);
                return;
            }
            break;

          case JSTRY_FINALLY:
            if (cx->isExceptionPending()) {
                rfe->kind = ResumeFromException::RESUME_FINALLY;
                jsbytecode *finallyPC = script->main() + tn->start + tn->length;
                rfe->target = script->baselineScript()->nativeCodeForPC(script, finallyPC);
                rfe->exception = cx->getPendingException();
                cx->clearPendingException();
                return;
            }
            break;

          case JSTRY_ITER: {
            Value iterValue(* (Value *) rfe->stackPointer);
            RootedObject iterObject(cx, &iterValue.toObject());
            if (cx->isExceptionPending())
                UnwindIteratorForException(cx, iterObject);
            else
                UnwindIteratorForUncatchableException(cx, iterObject);
            break;
          }

          case JSTRY_LOOP:
            break;

          default:
            MOZ_ASSUME_UNREACHABLE("Invalid try note");
        }
    }

}

void
HandleException(ResumeFromException *rfe)
{
    JSContext *cx = GetIonContext()->cx;

    rfe->kind = ResumeFromException::RESUME_ENTRY_FRAME;

    IonSpew(IonSpew_Invalidate, "handling exception");

    // Clear any Ion return override that's been set.
    // This may happen if a callVM function causes an invalidation (setting the
    // override), and then fails, bypassing the bailout handlers that would
    // otherwise clear the return override.
    if (cx->runtime()->hasIonReturnOverride())
        cx->runtime()->takeIonReturnOverride();

    IonFrameIterator iter(cx->mainThread().ionTop);
    while (!iter.isEntry()) {
        bool overrecursed = false;
        if (iter.isOptimizedJS()) {
            // Search each inlined frame for live iterator objects, and close
            // them.
            InlineFrameIterator frames(cx, &iter);
            for (;;) {
                HandleExceptionIon(cx, frames, rfe, &overrecursed);
                if (rfe->kind != ResumeFromException::RESUME_ENTRY_FRAME)
                    return;

                // When profiling, each frame popped needs a notification that
                // the function has exited, so invoke the probe that a function
                // is exiting.
                JSScript *script = frames.script();
                Probes::exitScript(cx, script, script->function(), NULL);
                if (!frames.more())
                    break;
                ++frames;
            }

            IonScript *ionScript = NULL;
            if (iter.checkInvalidation(&ionScript))
                ionScript->decref(cx->runtime()->defaultFreeOp());

        } else if (iter.isBaselineJS()) {
            // It's invalid to call DebugEpilogue twice for the same frame.
            bool calledDebugEpilogue = false;

            HandleExceptionBaseline(cx, iter, rfe, &calledDebugEpilogue);
            if (rfe->kind != ResumeFromException::RESUME_ENTRY_FRAME)
                return;

            // Unwind profiler pseudo-stack
            JSScript *script = iter.script();
            Probes::exitScript(cx, script, script->function(), iter.baselineFrame());
            // After this point, any pushed SPS frame would have been popped if it needed
            // to be.  Unset the flag here so that if we call DebugEpilogue below,
            // it doesn't try to pop the SPS frame again.
            iter.baselineFrame()->unsetPushedSPSFrame();
 
            if (cx->compartment()->debugMode() && !calledDebugEpilogue) {
                // If DebugEpilogue returns |true|, we have to perform a forced
                // return, e.g. return frame->returnValue() to the caller.
                BaselineFrame *frame = iter.baselineFrame();
                if (ion::DebugEpilogue(cx, frame, false)) {
                    JS_ASSERT(frame->hasReturnValue());
                    rfe->kind = ResumeFromException::RESUME_FORCED_RETURN;
                    rfe->framePointer = iter.fp() - BaselineFrame::FramePointerOffset;
                    rfe->stackPointer = reinterpret_cast<uint8_t *>(frame);
                    return;
                }
            }
        }

        IonJSFrameLayout *current = iter.isScripted() ? iter.jsFrame() : NULL;

        ++iter;

        if (current) {
            // Unwind the frame by updating ionTop. This is necessary so that
            // (1) debugger exception unwind and leave frame hooks don't see this
            // frame when they use ScriptFrameIter, and (2) ScriptFrameIter does
            // not crash when accessing an IonScript that's destroyed by the
            // ionScript->decref call.
            EnsureExitFrame(current);
            cx->mainThread().ionTop = (uint8_t *)current;
        }

        if (overrecursed) {
            // We hit an overrecursion error during bailout. Report it now.
            js_ReportOverRecursed(cx);
        }
    }

    rfe->stackPointer = iter.fp();
}

void
HandleParallelFailure(ResumeFromException *rfe)
{
    ForkJoinSlice *slice = ForkJoinSlice::Current();
    IonFrameIterator iter(slice->perThreadData->ionTop);

    parallel::Spew(parallel::SpewBailouts, "Bailing from VM reentry");

    while (!iter.isEntry()) {
        if (iter.isScripted()) {
            slice->bailoutRecord->setCause(ParallelBailoutFailedIC,
                                           iter.script(), iter.script(), NULL);
            break;
        }
        ++iter;
    }

    while (!iter.isEntry()) {
        if (iter.isScripted())
            PropagateAbortPar(iter.script(), iter.script());
        ++iter;
    }

    rfe->kind = ResumeFromException::RESUME_ENTRY_FRAME;
    rfe->stackPointer = iter.fp();
}

void
EnsureExitFrame(IonCommonFrameLayout *frame)
{
    if (frame->prevType() == IonFrame_Unwound_OptimizedJS ||
        frame->prevType() == IonFrame_Unwound_BaselineStub ||
        frame->prevType() == IonFrame_Unwound_Rectifier)
    {
        // Already an exit frame, nothing to do.
        return;
    }

    if (frame->prevType() == IonFrame_Entry) {
        // The previous frame type is the entry frame, so there's no actual
        // need for an exit frame.
        return;
    }

    if (frame->prevType() == IonFrame_Rectifier) {
        // The rectifier code uses the frame descriptor to discard its stack,
        // so modifying its descriptor size here would be dangerous. Instead,
        // we change the frame type, and teach the stack walking code how to
        // deal with this edge case. bug 717297 would obviate the need
        frame->changePrevType(IonFrame_Unwound_Rectifier);
        return;
    }

    if (frame->prevType() == IonFrame_BaselineStub) {
        frame->changePrevType(IonFrame_Unwound_BaselineStub);
        return;
    }

    JS_ASSERT(frame->prevType() == IonFrame_OptimizedJS);
    frame->changePrevType(IonFrame_Unwound_OptimizedJS);
}

CalleeToken
MarkCalleeToken(JSTracer *trc, CalleeToken token)
{
    switch (GetCalleeTokenTag(token)) {
      case CalleeToken_Function:
      {
        JSFunction *fun = CalleeTokenToFunction(token);
        MarkObjectRoot(trc, &fun, "ion-callee");
        return CalleeToToken(fun);
      }
      case CalleeToken_Script:
      {
        JSScript *script = CalleeTokenToScript(token);
        MarkScriptRoot(trc, &script, "ion-entry");
        return CalleeToToken(script);
      }
      default:
        MOZ_ASSUME_UNREACHABLE("unknown callee token type");
    }
}

static inline uintptr_t
ReadAllocation(const IonFrameIterator &frame, const LAllocation *a)
{
    if (a->isGeneralReg()) {
        Register reg = a->toGeneralReg()->reg();
        return frame.machineState().read(reg);
    }
    if (a->isStackSlot()) {
        uint32_t slot = a->toStackSlot()->slot();
        return *frame.jsFrame()->slotRef(slot);
    }
    uint32_t index = a->toArgument()->index();
    uint8_t *argv = reinterpret_cast<uint8_t *>(frame.jsFrame()->argv());
    return *reinterpret_cast<uintptr_t *>(argv + index);
}

static void
MarkActualArguments(JSTracer *trc, const IonFrameIterator &frame)
{
    IonJSFrameLayout *layout = frame.jsFrame();
    JS_ASSERT(CalleeTokenIsFunction(layout->calleeToken()));

    size_t nargs = frame.numActualArgs();

    // Trace function arguments. Note + 1 for thisv.
    Value *argv = layout->argv();
    for (size_t i = 0; i < nargs + 1; i++)
        gc::MarkValueRoot(trc, &argv[i], "ion-argv");
}

static inline void
WriteAllocation(const IonFrameIterator &frame, const LAllocation *a, uintptr_t value)
{
    if (a->isGeneralReg()) {
        Register reg = a->toGeneralReg()->reg();
        frame.machineState().write(reg, value);
        return;
    }
    if (a->isStackSlot()) {
        uint32_t slot = a->toStackSlot()->slot();
        *frame.jsFrame()->slotRef(slot) = value;
        return;
    }
    uint32_t index = a->toArgument()->index();
    uint8_t *argv = reinterpret_cast<uint8_t *>(frame.jsFrame()->argv());
    *reinterpret_cast<uintptr_t *>(argv + index) = value;
}

static void
MarkIonJSFrame(JSTracer *trc, const IonFrameIterator &frame)
{
    IonJSFrameLayout *layout = (IonJSFrameLayout *)frame.fp();

    layout->replaceCalleeToken(MarkCalleeToken(trc, layout->calleeToken()));

    IonScript *ionScript = NULL;
    if (frame.checkInvalidation(&ionScript)) {
        // This frame has been invalidated, meaning that its IonScript is no
        // longer reachable through the callee token (JSFunction/JSScript->ion
        // is now NULL or recompiled). Manually trace it here.
        IonScript::Trace(trc, ionScript);
    } else if (CalleeTokenIsFunction(layout->calleeToken())) {
        ionScript = CalleeTokenToFunction(layout->calleeToken())->nonLazyScript()->ionScript();
    } else {
        ionScript = CalleeTokenToScript(layout->calleeToken())->ionScript();
    }

    if (CalleeTokenIsFunction(layout->calleeToken()))
        MarkActualArguments(trc, frame);

    const SafepointIndex *si = ionScript->getSafepointIndex(frame.returnAddressToFp());

    SafepointReader safepoint(ionScript, si);

    // Scan through slots which contain pointers (or on punboxing systems,
    // actual values).
    uint32_t slot;
    while (safepoint.getGcSlot(&slot)) {
        uintptr_t *ref = layout->slotRef(slot);
        gc::MarkGCThingRoot(trc, reinterpret_cast<void **>(ref), "ion-gc-slot");
    }

    while (safepoint.getValueSlot(&slot)) {
        Value *v = (Value *)layout->slotRef(slot);
        gc::MarkValueRoot(trc, v, "ion-gc-slot");
    }

    uintptr_t *spill = frame.spillBase();
    GeneralRegisterSet gcRegs = safepoint.gcSpills();
    GeneralRegisterSet valueRegs = safepoint.valueSpills();
    for (GeneralRegisterBackwardIterator iter(safepoint.allSpills()); iter.more(); iter++) {
        --spill;
        if (gcRegs.has(*iter))
            gc::MarkGCThingRoot(trc, reinterpret_cast<void **>(spill), "ion-gc-spill");
        else if (valueRegs.has(*iter))
            gc::MarkValueRoot(trc, reinterpret_cast<Value *>(spill), "ion-value-spill");
    }

#ifdef JS_NUNBOX32
    LAllocation type, payload;
    while (safepoint.getNunboxSlot(&type, &payload)) {
        jsval_layout layout;
        layout.s.tag = (JSValueTag)ReadAllocation(frame, &type);
        layout.s.payload.uintptr = ReadAllocation(frame, &payload);

        Value v = IMPL_TO_JSVAL(layout);
        gc::MarkValueRoot(trc, &v, "ion-torn-value");

        if (v != IMPL_TO_JSVAL(layout)) {
            // GC moved the value, replace the stored payload.
            layout = JSVAL_TO_IMPL(v);
            WriteAllocation(frame, &payload, layout.s.payload.uintptr);
        }
    }
#endif

#ifdef JSGC_GENERATIONAL
    if (trc->runtime->isHeapMinorCollecting()) {
        // Minor GCs may move slots/elements allocated in the nursery. Update
        // any slots/elements pointers stored in this frame.

        GeneralRegisterSet slotsRegs = safepoint.slotsOrElementsSpills();
        spill = frame.spillBase();
        for (GeneralRegisterBackwardIterator iter(safepoint.allSpills()); iter.more(); iter++) {
            --spill;
            if (slotsRegs.has(*iter))
                trc->runtime->gcNursery.forwardBufferPointer(reinterpret_cast<HeapSlot **>(spill));
        }

        while (safepoint.getSlotsOrElementsSlot(&slot)) {
            HeapSlot **slots = reinterpret_cast<HeapSlot **>(layout->slotRef(slot));
            trc->runtime->gcNursery.forwardBufferPointer(slots);
        }
    }
#endif
}

static void
MarkBaselineStubFrame(JSTracer *trc, const IonFrameIterator &frame)
{
    // Mark the ICStub pointer stored in the stub frame. This is necessary
    // so that we don't destroy the stub code after unlinking the stub.

    JS_ASSERT(frame.type() == IonFrame_BaselineStub);
    IonBaselineStubFrameLayout *layout = (IonBaselineStubFrameLayout *)frame.fp();

    if (ICStub *stub = layout->maybeStubPtr()) {
        JS_ASSERT(ICStub::CanMakeCalls(stub->kind()));
        stub->trace(trc);
    }
}

void
JitActivationIterator::jitStackRange(uintptr_t *&min, uintptr_t *&end)
{
    IonFrameIterator frames(jitTop());

    if (frames.isFakeExitFrame()) {
        min = reinterpret_cast<uintptr_t *>(frames.fp());
    } else {
        IonExitFrameLayout *exitFrame = frames.exitFrame();
        IonExitFooterFrame *footer = exitFrame->footer();
        const VMFunction *f = footer->function();
        if (exitFrame->isWrapperExit() && f->outParam == Type_Handle) {
            switch (f->outParamRootType) {
              case VMFunction::RootNone:
                MOZ_ASSUME_UNREACHABLE("Handle outparam must have root type");
              case VMFunction::RootObject:
              case VMFunction::RootString:
              case VMFunction::RootPropertyName:
              case VMFunction::RootFunction:
              case VMFunction::RootCell:
                // These are all handles to GCThing pointers.
                min = reinterpret_cast<uintptr_t *>(footer->outParam<void *>());
                break;
              case VMFunction::RootValue:
                min = reinterpret_cast<uintptr_t *>(footer->outParam<Value>());
                break;
            }
        } else {
            min = reinterpret_cast<uintptr_t *>(footer);
        }
    }

    while (!frames.done())
        ++frames;

    end = reinterpret_cast<uintptr_t *>(frames.prevFp());
}

static void
MarkIonExitFrame(JSTracer *trc, const IonFrameIterator &frame)
{
    // Ignore fake exit frames created by EnsureExitFrame.
    if (frame.isFakeExitFrame())
        return;

    IonExitFooterFrame *footer = frame.exitFrame()->footer();

    // Mark the code of the code handling the exit path.  This is needed because
    // invalidated script are no longer marked because data are erased by the
    // invalidation and relocation data are no longer reliable.  So the VM
    // wrapper or the invalidation code may be GC if no IonCode keep reference
    // on them.
    JS_ASSERT(uintptr_t(footer->ionCode()) != uintptr_t(-1));

    // This correspond to the case where we have build a fake exit frame in
    // CodeGenerator.cpp which handle the case of a native function call. We
    // need to mark the argument vector of the function call.
    if (frame.isNative()) {
        IonNativeExitFrameLayout *native = frame.exitFrame()->nativeExit();
        size_t len = native->argc() + 2;
        Value *vp = native->vp();
        gc::MarkValueRootRange(trc, len, vp, "ion-native-args");
        return;
    }

    if (frame.isOOLNativeGetter()) {
        IonOOLNativeGetterExitFrameLayout *oolgetter = frame.exitFrame()->oolNativeGetterExit();
        gc::MarkIonCodeRoot(trc, oolgetter->stubCode(), "ion-ool-getter-code");
        gc::MarkValueRoot(trc, oolgetter->vp(), "ion-ool-getter-callee");
        gc::MarkValueRoot(trc, oolgetter->thisp(), "ion-ool-getter-this");
        return;
    }

    if (frame.isOOLPropertyOp()) {
        IonOOLPropertyOpExitFrameLayout *oolgetter = frame.exitFrame()->oolPropertyOpExit();
        gc::MarkIonCodeRoot(trc, oolgetter->stubCode(), "ion-ool-property-op-code");
        gc::MarkValueRoot(trc, oolgetter->vp(), "ion-ool-property-op-vp");
        gc::MarkIdRoot(trc, oolgetter->id(), "ion-ool-property-op-id");
        gc::MarkObjectRoot(trc, oolgetter->obj(), "ion-ool-property-op-obj");
        return;
    }

    if (frame.isOOLProxyGet()) {
        IonOOLProxyGetExitFrameLayout *oolproxyget = frame.exitFrame()->oolProxyGetExit();
        gc::MarkIonCodeRoot(trc, oolproxyget->stubCode(), "ion-ool-proxy-get-code");
        gc::MarkValueRoot(trc, oolproxyget->vp(), "ion-ool-proxy-get-vp");
        gc::MarkIdRoot(trc, oolproxyget->id(), "ion-ool-proxy-get-id");
        gc::MarkObjectRoot(trc, oolproxyget->proxy(), "ion-ool-proxy-get-proxy");
        gc::MarkObjectRoot(trc, oolproxyget->receiver(), "ion-ool-proxy-get-receiver");
        return;
    }

    if (frame.isDOMExit()) {
        IonDOMExitFrameLayout *dom = frame.exitFrame()->DOMExit();
        gc::MarkObjectRoot(trc, dom->thisObjAddress(), "ion-dom-args");
        if (dom->isMethodFrame()) {
            IonDOMMethodExitFrameLayout *method =
                reinterpret_cast<IonDOMMethodExitFrameLayout *>(dom);
            size_t len = method->argc() + 2;
            Value *vp = method->vp();
            gc::MarkValueRootRange(trc, len, vp, "ion-dom-args");
        } else {
            gc::MarkValueRoot(trc, dom->vp(), "ion-dom-args");
        }
        return;
    }

    MarkIonCodeRoot(trc, footer->addressOfIonCode(), "ion-exit-code");

    const VMFunction *f = footer->function();
    if (f == NULL || f->explicitArgs == 0)
        return;

    // Mark arguments of the VM wrapper.
    uint8_t *argBase = frame.exitFrame()->argBase();
    for (uint32_t explicitArg = 0; explicitArg < f->explicitArgs; explicitArg++) {
        switch (f->argRootType(explicitArg)) {
          case VMFunction::RootNone:
            break;
          case VMFunction::RootObject: {
            // Sometimes we can bake in HandleObjects to NULL.
            JSObject **pobj = reinterpret_cast<JSObject **>(argBase);
            if (*pobj)
                gc::MarkObjectRoot(trc, pobj, "ion-vm-args");
            break;
          }
          case VMFunction::RootString:
          case VMFunction::RootPropertyName:
            gc::MarkStringRoot(trc, reinterpret_cast<JSString**>(argBase), "ion-vm-args");
            break;
          case VMFunction::RootFunction:
            gc::MarkObjectRoot(trc, reinterpret_cast<JSFunction**>(argBase), "ion-vm-args");
            break;
          case VMFunction::RootValue:
            gc::MarkValueRoot(trc, reinterpret_cast<Value*>(argBase), "ion-vm-args");
            break;
          case VMFunction::RootCell:
            gc::MarkGCThingRoot(trc, reinterpret_cast<void **>(argBase), "ion-vm-args");
            break;
        }

        switch (f->argProperties(explicitArg)) {
          case VMFunction::WordByValue:
          case VMFunction::WordByRef:
            argBase += sizeof(void *);
            break;
          case VMFunction::DoubleByValue:
          case VMFunction::DoubleByRef:
            argBase += 2 * sizeof(void *);
            break;
        }
    }

    if (f->outParam == Type_Handle) {
        switch (f->outParamRootType) {
          case VMFunction::RootNone:
            MOZ_ASSUME_UNREACHABLE("Handle outparam must have root type");
          case VMFunction::RootObject:
            gc::MarkObjectRoot(trc, footer->outParam<JSObject *>(), "ion-vm-out");
            break;
          case VMFunction::RootString:
          case VMFunction::RootPropertyName:
            gc::MarkStringRoot(trc, footer->outParam<JSString *>(), "ion-vm-out");
            break;
          case VMFunction::RootFunction:
            gc::MarkObjectRoot(trc, footer->outParam<JSFunction *>(), "ion-vm-out");
            break;
          case VMFunction::RootValue:
            gc::MarkValueRoot(trc, footer->outParam<Value>(), "ion-vm-outvp");
            break;
          case VMFunction::RootCell:
            gc::MarkGCThingRoot(trc, footer->outParam<void *>(), "ion-vm-out");
            break;
        }
    }
}

static void
MarkJitActivation(JSTracer *trc, const JitActivationIterator &activations)
{
    for (IonFrameIterator frames(activations); !frames.done(); ++frames) {
        switch (frames.type()) {
          case IonFrame_Exit:
            MarkIonExitFrame(trc, frames);
            break;
          case IonFrame_BaselineJS:
            frames.baselineFrame()->trace(trc);
            break;
          case IonFrame_BaselineStub:
            MarkBaselineStubFrame(trc, frames);
            break;
          case IonFrame_OptimizedJS:
            MarkIonJSFrame(trc, frames);
            break;
          case IonFrame_Unwound_OptimizedJS:
            MOZ_ASSUME_UNREACHABLE("invalid");
          case IonFrame_Rectifier:
          case IonFrame_Unwound_Rectifier:
            break;
          case IonFrame_Osr:
            // The callee token will be marked by the callee JS frame;
            // otherwise, it does not need to be marked, since the frame is
            // dead.
            break;
          default:
            MOZ_ASSUME_UNREACHABLE("unexpected frame type");
        }
    }
}

void
MarkJitActivations(JSRuntime *rt, JSTracer *trc)
{
    for (JitActivationIterator activations(rt); !activations.done(); ++activations)
        MarkJitActivation(trc, activations);
}

void
AutoTempAllocatorRooter::trace(JSTracer *trc)
{
    for (CompilerRootNode *root = temp->rootList(); root != NULL; root = root->next)
        gc::MarkGCThingRoot(trc, root->address(), "ion-compiler-root");
}

void
GetPcScript(JSContext *cx, JSScript **scriptRes, jsbytecode **pcRes)
{
    IonSpew(IonSpew_Snapshots, "Recover PC & Script from the last frame.");

    JSRuntime *rt = cx->runtime();

    // Recover the return address.
    IonFrameIterator it(rt->mainThread.ionTop);

    // If the previous frame is a rectifier frame (maybe unwound),
    // skip past it.
    if (it.prevType() == IonFrame_Rectifier || it.prevType() == IonFrame_Unwound_Rectifier) {
        ++it;
        JS_ASSERT(it.prevType() == IonFrame_BaselineStub ||
                  it.prevType() == IonFrame_BaselineJS ||
                  it.prevType() == IonFrame_OptimizedJS);
    }

    // If the previous frame is a stub frame, skip the exit frame so that
    // returnAddress below gets the return address into the BaselineJS
    // frame.
    if (it.prevType() == IonFrame_BaselineStub || it.prevType() == IonFrame_Unwound_BaselineStub) {
        ++it;
        JS_ASSERT(it.prevType() == IonFrame_BaselineJS);
    }

    uint8_t *retAddr = it.returnAddress();
    uint32_t hash = PcScriptCache::Hash(retAddr);
    JS_ASSERT(retAddr != NULL);

    // Lazily initialize the cache. The allocation may safely fail and will not GC.
    if (JS_UNLIKELY(rt->ionPcScriptCache == NULL)) {
        rt->ionPcScriptCache = (PcScriptCache *)js_malloc(sizeof(struct PcScriptCache));
        if (rt->ionPcScriptCache)
            rt->ionPcScriptCache->clear(rt->gcNumber);
    }

    // Attempt to lookup address in cache.
    if (rt->ionPcScriptCache && rt->ionPcScriptCache->get(rt, hash, retAddr, scriptRes, pcRes))
        return;

    // Lookup failed: undertake expensive process to recover the innermost inlined frame.
    ++it; // Skip exit frame.
    jsbytecode *pc = NULL;

    if (it.isOptimizedJS()) {
        InlineFrameIterator ifi(cx, &it);
        *scriptRes = ifi.script();
        pc = ifi.pc();
    } else {
        JS_ASSERT(it.isBaselineJS());
        it.baselineScriptAndPc(scriptRes, &pc);
    }

    if (pcRes)
        *pcRes = pc;

    // Add entry to cache.
    if (rt->ionPcScriptCache)
        rt->ionPcScriptCache->add(hash, retAddr, pc, *scriptRes);
}

void
OsiIndex::fixUpOffset(MacroAssembler &masm)
{
    callPointDisplacement_ = masm.actualOffset(callPointDisplacement_);
}

uint32_t
OsiIndex::returnPointDisplacement() const
{
    // In general, pointer arithmetic on code is bad, but in this case,
    // getting the return address from a call instruction, stepping over pools
    // would be wrong.
    return callPointDisplacement_ + Assembler::patchWrite_NearCallSize();
}

SnapshotIterator::SnapshotIterator(IonScript *ionScript, SnapshotOffset snapshotOffset,
                                   IonJSFrameLayout *fp, const MachineState &machine)
  : snapshot_(ionScript->snapshots() + snapshotOffset,
              ionScript->snapshots() + ionScript->snapshotsSize()),
    recover_(ionScript, snapshot_.recoverOffset()),
    nextOperandIndex_(0),
    currentFrameIndex_(0),
    fp_(fp),
    machine_(machine),
    ionScript_(ionScript),
    recovered_(NULL)
{
    JS_ASSERT(snapshotOffset < ionScript->snapshotsSize());
    savePosition();
}

SnapshotIterator::SnapshotIterator(const IonFrameIterator &iter)
  : snapshot_(iter.ionScript()->snapshots() + iter.osiIndex()->snapshotOffset(),
              iter.ionScript()->snapshots() + iter.ionScript()->snapshotsSize()),
    recover_(iter.ionScript(), snapshot_.recoverOffset()),
    nextOperandIndex_(0),
    currentFrameIndex_(0),
    fp_(iter.jsFrame()),
    machine_(iter.machineState()),
    ionScript_(iter.ionScript()),
    recovered_(NULL)
{
    savePosition();
}

SnapshotIterator::SnapshotIterator()
  : snapshot_(NULL, NULL),
    recover_(),
    nextOperandIndex_(0),
    currentFrameIndex_(0),
    fp_(NULL),
    ionScript_(NULL),
    recovered_(NULL)
{
}

void
SnapshotIterator::resetOn(const IonFrameIterator &iter)
{
    snapshot_.resetOn(iter.ionScript(), iter.osiIndex()->snapshotOffset());
    recover_.resetOn(iter.ionScript(), snapshot_.recoverOffset());
    nextOperandIndex_ = 0;
    currentFrameIndex_ = 0,
    fp_ = iter.jsFrame();
    machine_ = iter.machineState();
    ionScript_ = iter.ionScript();
    if (recovered_)
        recovered_->clear();
}

bool
SnapshotIterator::initRecoveredResults(AutoValueVector *recovered)
{
    size_t reserve = recover_.numInstructions() - recover_.numFrames();
    if (!reserve)
        return true;

    if (!recovered->reserve(reserve))
        return false;

    recovered_ = recovered;
    return true;
}


bool
SnapshotIterator::hasLocation(const Slot::Location &loc) const
{
    return loc.isStackSlot() || machine_.has(loc.reg());
}

uintptr_t
SnapshotIterator::fromLocation(const Slot::Location &loc) const
{
    if (loc.isStackSlot())
        return ReadFrameSlot(fp_, loc.stackSlot());
    return machine_.read(loc.reg());
}

Value
SnapshotIterator::FromTypedPayload(JSValueType type, uintptr_t payload)
{
    switch (type) {
      case JSVAL_TYPE_INT32:
        return Int32Value(payload);
      case JSVAL_TYPE_BOOLEAN:
        return BooleanValue(!!payload);
      case JSVAL_TYPE_STRING:
        return StringValue(reinterpret_cast<JSString *>(payload));
      case JSVAL_TYPE_OBJECT:
        return ObjectValue(*reinterpret_cast<JSObject *>(payload));
      default:
        MOZ_ASSUME_UNREACHABLE("unexpected type - needs payload");
    }
}

bool
SnapshotIterator::slotReadable(const Slot &slot) const
{
    switch (slot.mode()) {
      case Slot::DOUBLE_REG:
        return machine_.has(slot.floatReg());

      case Slot::TYPED_REG:
        return machine_.has(slot.reg());

      case Slot::UNTYPED:
#if defined(JS_NUNBOX32)
          return hasLocation(slot.type()) && hasLocation(slot.payload());
#elif defined(JS_PUNBOX64)
          return hasLocation(slot.value());
#endif

      case Slot::RECOVER:
        return !!recovered_;

      default:
        return true;
    }
}

Value
SnapshotIterator::slotValue(const Slot &slot) const
{
    switch (slot.mode()) {
      case Slot::DOUBLE_REG:
        return DoubleValue(machine_.read(slot.floatReg()));

      case Slot::TYPED_REG:
        return FromTypedPayload(slot.knownType(), machine_.read(slot.reg()));

      case Slot::TYPED_STACK:
      {
        JSValueType type = slot.knownType();
        if (type == JSVAL_TYPE_DOUBLE)
            return DoubleValue(ReadFrameDoubleSlot(fp_, slot.stackSlot()));
        return FromTypedPayload(type, ReadFrameSlot(fp_, slot.stackSlot()));
      }

      case Slot::UNTYPED:
      {
          jsval_layout layout;
#if defined(JS_NUNBOX32)
          layout.s.tag = (JSValueTag)fromLocation(slot.type());
          layout.s.payload.word = fromLocation(slot.payload());
#elif defined(JS_PUNBOX64)
          layout.asBits = fromLocation(slot.value());
#endif
          return IMPL_TO_JSVAL(layout);
      }

      case Slot::JS_UNDEFINED:
        return UndefinedValue();

      case Slot::JS_NULL:
        return NullValue();

      case Slot::JS_INT32:
        return Int32Value(slot.int32Value());

      case Slot::CONSTANT:
        return ionScript_->getConstant(slot.constantIndex());

      case Slot::RECOVER:
        return (*recovered_)[slot.recoverIndex()];

      case Slot::UNINITIALIZED:
        MOZ_ASSUME_UNREACHABLE("Slot is not initialize.");

      default:
        MOZ_ASSUME_UNREACHABLE("huh?");
    }
}

void
SnapshotIterator::setRecoveredValue(Value v)
{
    JS_ASSERT(!isFrame());
    JS_ASSERT(recover_.instructionIndex() >= currentFrameIndex_);
    size_t opIndex = recover_.instructionIndex() - currentFrameIndex_;
    (*recovered_)[opIndex] = v;
}

IonScript *
IonFrameIterator::ionScript() const
{
    JS_ASSERT(type() == IonFrame_OptimizedJS);

    IonScript *ionScript = NULL;
    if (checkInvalidation(&ionScript))
        return ionScript;
    switch (GetCalleeTokenTag(calleeToken())) {
      case CalleeToken_Function:
      case CalleeToken_Script:
        return script()->ionScript();
      case CalleeToken_ParallelFunction:
        return script()->parallelIonScript();
      default:
        MOZ_ASSUME_UNREACHABLE("unknown callee token type");
    }
}

const SafepointIndex *
IonFrameIterator::safepoint() const
{
    if (!cachedSafepointIndex_)
        cachedSafepointIndex_ = ionScript()->getSafepointIndex(returnAddressToFp());
    return cachedSafepointIndex_;
}

const OsiIndex *
IonFrameIterator::osiIndex() const
{
    SafepointReader reader(ionScript(), safepoint());
    return ionScript()->getOsiIndex(reader.osiReturnPointOffset());
}

template <AllowGC allowGC>
void
InlineFrameIteratorMaybeGC<allowGC>::resetOn(const IonFrameIterator *iter)
{
    frame_ = iter;
    framesRead_ = 0;

    if (iter) {
        si_.resetOn(*iter);
        findNextFrame();
    }
}
template void InlineFrameIteratorMaybeGC<NoGC>::resetOn(const IonFrameIterator *iter);
template void InlineFrameIteratorMaybeGC<CanGC>::resetOn(const IonFrameIterator *iter);

template <AllowGC allowGC>
void
InlineFrameIteratorMaybeGC<allowGC>::findNextFrame()
{
    JS_ASSERT(more());

    si_.restart();

    // Read the initial frame.
    callee_ = frame_->maybeCallee();
    script_ = frame_->script();

    RResumePoint &rp = si_.resumePoint();
    rp.readSlots(si_, script_, callee_);
    pc_ = script_->code + rp.pcOffset();
#ifdef DEBUG
    numActualArgs_ = 0xbadbad;
#endif

    // This unfortunately is O(n*m), because we must skip over outer frames
    // before reading inner ones.
    unsigned remaining = si_.frameCount() - framesRead_ - 1;
    for (unsigned i = 0; i < remaining; i++) {
        JS_ASSERT(IsIonInlinablePC(pc_));

        // Recover the number of actual arguments from the script.
        if (JSOp(*pc_) != JSOP_FUNAPPLY)
            numActualArgs_ = GET_ARGC(pc_);
        if (JSOp(*pc_) == JSOP_FUNCALL) {
            JS_ASSERT(GET_ARGC(pc_) > 0);
            numActualArgs_ = GET_ARGC(pc_) - 1;
        } else if (IsGetterPC(pc_)) {
            numActualArgs_ = 0;
        } else if (IsSetterPC(pc_)) {
            numActualArgs_ = 1;
        }

        JS_ASSERT(numActualArgs_ != 0xbadbad);

        Value funval = si_.slotValue(rp.calleeFunction(numActualArgs_));

        si_.nextFrame();
        callee_ = &funval.toObject().as<JSFunction>();

        // Inlined functions may be clones that still point to the lazy script
        // for the executed script, if they are clones. The actual script
        // exists though, just make sure the function points to it.
        script_ = callee_->existingScript();

        rp.readSlots(si_, script_, callee_);
        pc_ = script_->code + rp.pcOffset();
    }

    framesRead_++;
}
template void InlineFrameIteratorMaybeGC<NoGC>::findNextFrame();
template void InlineFrameIteratorMaybeGC<CanGC>::findNextFrame();

template <AllowGC allowGC>
bool
InlineFrameIteratorMaybeGC<allowGC>::isFunctionFrame() const
{
    return !!callee_;
}
template bool InlineFrameIteratorMaybeGC<NoGC>::isFunctionFrame() const;
template bool InlineFrameIteratorMaybeGC<CanGC>::isFunctionFrame() const;

MachineState
MachineState::FromBailout(uintptr_t regs[Registers::Total],
                          double fpregs[FloatRegisters::Total])
{
    MachineState machine;

    for (unsigned i = 0; i < Registers::Total; i++)
        machine.setRegisterLocation(Register::FromCode(i), &regs[i]);
    for (unsigned i = 0; i < FloatRegisters::Total; i++)
        machine.setRegisterLocation(FloatRegister::FromCode(i), &fpregs[i]);

    return machine;
}

template <AllowGC allowGC>
bool
InlineFrameIteratorMaybeGC<allowGC>::isConstructing() const
{
    // Skip the current frame and look at the caller's.
    if (more()) {
        InlineFrameIteratorMaybeGC<allowGC> parent(GetIonContext()->cx, this);
        ++parent;

        // Inlined Getters and Setters are never constructing.
        if (IsGetterPC(parent.pc()) || IsSetterPC(parent.pc()))
            return false;

        // In the case of a JS frame, look up the pc from the snapshot.
        JS_ASSERT(IsCallPC(parent.pc()));

        return (JSOp)*parent.pc() == JSOP_NEW;
    }

    return frame_->isConstructing();
}
template bool InlineFrameIteratorMaybeGC<NoGC>::isConstructing() const;
template bool InlineFrameIteratorMaybeGC<CanGC>::isConstructing() const;

bool
IonFrameIterator::isConstructing() const
{
    IonFrameIterator parent(*this);

    // Skip the current frame and look at the caller's.
    do {
        ++parent;
    } while (!parent.done() && !parent.isScripted());

    if (parent.isOptimizedJS()) {
        // In the case of a JS frame, look up the pc from the snapshot.
        InlineFrameIterator inlinedParent(GetIonContext()->cx, &parent);

        //Inlined Getters and Setters are never constructing.
        if (IsGetterPC(inlinedParent.pc()) || IsSetterPC(inlinedParent.pc()))
            return false;

        JS_ASSERT(IsCallPC(inlinedParent.pc()));

        return (JSOp)*inlinedParent.pc() == JSOP_NEW;
    }

    if (parent.isBaselineJS()) {
        jsbytecode *pc;
        parent.baselineScriptAndPc(NULL, &pc);

        //Inlined Getters and Setters are never constructing.
        if (IsGetterPC(pc) || IsSetterPC(pc))
            return false;

        JS_ASSERT(IsCallPC(pc));

        return JSOp(*pc) == JSOP_NEW;
    }

    JS_ASSERT(parent.done());
    return activation_->firstFrameIsConstructing();
}

unsigned
IonFrameIterator::numActualArgs() const
{
    if (isScripted())
        return jsFrame()->numActualArgs();

    JS_ASSERT(isNative());
    return exitFrame()->nativeExit()->argc();
}

void
SnapshotIterator::warnUnreadableSlot() const
{
    fprintf(stderr, "Warning! Tried to access unreadable IonMonkey slot (possible f.arguments).\n");
}

#ifdef DEBUG
# define DUMP_VALUE(val, _) js_DumpValue(val)
# define DUMP_OBJECT(obj, _) js_DumpObject(obj)
#else
# define DUMP_VALUE(_, txt)  fprintf(stderr, txt)
# define DUMP_OBJECT(_, txt)  fprintf(stderr, txt)
#endif

struct DumpOp {
    DumpOp(unsigned int i) : i_(i) {}

    unsigned int i_;
    void operator()(const Value& v) {
        fprintf(stderr, "  actual (arg %d): ", i_);
        DUMP_VALUE(v, "?\n");
        i_++;
    }
};

void
IonFrameIterator::dumpBaseline() const
{
    JS_ASSERT(isBaselineJS());

    fprintf(stderr, " JS Baseline frame\n");
    if (isFunctionFrame()) {
        fprintf(stderr, "  callee fun: ");
        DUMP_OBJECT(callee(), "?\n");
    } else {
        fprintf(stderr, "  global frame, no callee\n");
    }

    fprintf(stderr, "  file %s line %u\n",
            script()->filename(), (unsigned) script()->lineno);

    JSContext *cx = GetIonContext()->cx;
    RootedScript script(cx);
    jsbytecode *pc;
    baselineScriptAndPc(script.address(), &pc);

    fprintf(stderr, "  script = %p, pc = %p (offset %u)\n", (void *)script, pc, uint32_t(pc - script->code));
    fprintf(stderr, "  current op: %s\n", js_CodeName[*pc]);

    fprintf(stderr, "  actual args: %d\n", numActualArgs());

    BaselineFrame *frame = baselineFrame();

    for (unsigned i = 0; i < frame->numValueSlots(); i++) {
        fprintf(stderr, "  slot %u: ", i);
        Value *v = frame->valueSlot(i);
        DUMP_VALUE(*v, "?\n");
    }
}

template <AllowGC allowGC>
void
InlineFrameIteratorMaybeGC<allowGC>::dump() const
{
    if (more())
        fprintf(stderr, " JS frame (inlined)\n");
    else
        fprintf(stderr, " JS frame\n");

    bool isFunction = false;
    if (isFunctionFrame()) {
        isFunction = true;
        fprintf(stderr, "  callee fun: ");
        DUMP_OBJECT(callee(), "?\n");
    } else {
        fprintf(stderr, "  global frame, no callee\n");
    }

    fprintf(stderr, "  file %s line %u\n",
            script()->filename(), (unsigned) script()->lineno);

    fprintf(stderr, "  script = %p, pc = %p\n", (void*) script(), pc());
    fprintf(stderr, "  current op: %s\n", js_CodeName[*pc()]);

    if (!more()) {
        numActualArgs();
    }

    const RResumePoint &rp = resumePoint();

    fprintf(stderr, "  slots: %u\n", si_.slots() - 1);
    if (rp.scopeChainSlot().isInitialized()) {
        fprintf(stderr, "  scope chain: ");
        DUMP_VALUE(si_.maybeSlotValue(rp.scopeChainSlot()), "?\n");
    }

    if (rp.argObjSlot().isInitialized()) {
        fprintf(stderr, "  argument object: ");
        DUMP_VALUE(si_.maybeSlotValue(rp.argObjSlot()), "?\n");
    }

    if (rp.thisSlot().isInitialized()) {
        fprintf(stderr, "  this: ");
        DUMP_VALUE(si_.maybeSlotValue(rp.thisSlot()), "?\n");
    }

    for (size_t i = 0; i < rp.numFormalArgs(); i++) {
        fprintf(stderr, "  formal arg %lu: ", i);
        DUMP_VALUE(si_.maybeSlotValue(rp.formalArg(i)), "?\n");
    }

    for (size_t i = 0; i < rp.numFixedSlots(); i++) {
        fprintf(stderr, "  fixed slot %lu: ", i);
        DUMP_VALUE(si_.maybeSlotValue(rp.fixedSlot(i)), "?\n");
    }

    for (size_t i = 0; i < rp.numStackSlots(); i++) {
        fprintf(stderr, "  stack slot %lu: ", i);
        DUMP_VALUE(si_.maybeSlotValue(rp.stackSlot(i)), "?\n");
    }

    fputc('\n', stderr);
}
template void InlineFrameIteratorMaybeGC<NoGC>::dump() const;
template void InlineFrameIteratorMaybeGC<CanGC>::dump() const;

void
IonFrameIterator::dump() const
{
    switch (type_) {
      case IonFrame_Entry:
        fprintf(stderr, " Entry frame\n");
        fprintf(stderr, "  Frame size: %u\n", unsigned(current()->prevFrameLocalSize()));
        break;
      case IonFrame_BaselineJS:
        dumpBaseline();
        break;
      case IonFrame_BaselineStub:
      case IonFrame_Unwound_BaselineStub:
        fprintf(stderr, " Baseline stub frame\n");
        fprintf(stderr, "  Frame size: %u\n", unsigned(current()->prevFrameLocalSize()));
        break;
      case IonFrame_OptimizedJS:
      {
        InlineFrameIterator frames(GetIonContext()->cx, this);
        for (;;) {
            frames.dump();
            if (!frames.more())
                break;
            ++frames;
        }
        break;
      }
      case IonFrame_Rectifier:
      case IonFrame_Unwound_Rectifier:
        fprintf(stderr, " Rectifier frame\n");
        fprintf(stderr, "  Frame size: %u\n", unsigned(current()->prevFrameLocalSize()));
        break;
      case IonFrame_Unwound_OptimizedJS:
        fprintf(stderr, "Warning! Unwound JS frames are not observable.\n");
        break;
      case IonFrame_Exit:
        break;
      case IonFrame_Osr:
        fprintf(stderr, "Warning! OSR frame are not defined yet.\n");
        break;
    };
    fputc('\n', stderr);
}

} // namespace ion
} // namespace js
