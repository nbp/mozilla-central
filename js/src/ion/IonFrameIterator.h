/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_IonFrameIterator_h
#define ion_IonFrameIterator_h

#ifdef JS_ION

#include "jstypes.h"
#include "ion/IonCode.h"
#include "ion/Slots.h"
#include "ion/SnapshotReader.h"

class JSFunction;
class JSScript;

namespace js {
    class ActivationIterator;
};

namespace js {
namespace ion {

enum FrameType
{
    // A JS frame is analagous to a js::StackFrame, representing one scripted
    // functon activation. OptimizedJS frames are used by the optimizing compiler.
    IonFrame_OptimizedJS,

    // JS frame used by the baseline JIT.
    IonFrame_BaselineJS,

    // Frame pushed for baseline JIT stubs that make non-tail calls, so that the
    // return address -> ICEntry mapping works.
    IonFrame_BaselineStub,

    // The entry frame is the initial prologue block transitioning from the VM
    // into the Ion world.
    IonFrame_Entry,

    // A rectifier frame sits in between two JS frames, adapting argc != nargs
    // mismatches in calls.
    IonFrame_Rectifier,

    // An unwound JS frame is a JS frame signalling that its callee frame has been
    // turned into an exit frame (see EnsureExitFrame). Used by Ion bailouts and
    // Baseline exception unwinding.
    IonFrame_Unwound_OptimizedJS,

    // Like Unwound_OptimizedJS, but the caller is a baseline stub frame.
    IonFrame_Unwound_BaselineStub,

    // An unwound rectifier frame is a rectifier frame signalling that its callee
    // frame has been turned into an exit frame (see EnsureExitFrame).
    IonFrame_Unwound_Rectifier,

    // An exit frame is necessary for transitioning from a JS frame into C++.
    // From within C++, an exit frame is always the last frame in any
    // JitActivation.
    IonFrame_Exit,

    // An OSR frame is added when performing OSR from within a bailout. It
    // looks like a JS frame, but does not push scripted arguments, as OSR
    // reads arguments from a js::StackFrame.
    IonFrame_Osr
};

class IonCommonFrameLayout;
class IonJSFrameLayout;
class IonExitFrameLayout;

class BaselineFrame;

class JitActivation;

class IonFrameIterator
{
  protected:
    uint8_t *current_;
    FrameType type_;
    uint8_t *returnAddressToFp_;
    size_t frameSize_;

  private:
    mutable const SafepointIndex *cachedSafepointIndex_;
    const JitActivation *activation_;

    void dumpBaseline() const;

  public:
    IonFrameIterator(uint8_t *top)
      : current_(top),
        type_(IonFrame_Exit),
        returnAddressToFp_(NULL),
        frameSize_(0),
        cachedSafepointIndex_(NULL),
        activation_(NULL)
    { }

    IonFrameIterator(const ActivationIterator &activations);
    IonFrameIterator(IonJSFrameLayout *fp);

    // Current frame information.
    FrameType type() const {
        return type_;
    }
    uint8_t *fp() const {
        return current_;
    }

    IonCommonFrameLayout *current() const {
        return (IonCommonFrameLayout *)current_;
    }

    inline uint8_t *returnAddress() const;

    IonJSFrameLayout *jsFrame() const {
        JS_ASSERT(isScripted());
        return (IonJSFrameLayout *) fp();
    }

    // Returns true iff this exit frame was created using EnsureExitFrame.
    inline bool isFakeExitFrame() const;

    inline IonExitFrameLayout *exitFrame() const;

    // Returns whether the JS frame has been invalidated and, if so,
    // places the invalidated Ion script in |ionScript|.
    bool checkInvalidation(IonScript **ionScript) const;
    bool checkInvalidation() const;

    bool isScripted() const {
        return type_ == IonFrame_BaselineJS || type_ == IonFrame_OptimizedJS;
    }
    bool isBaselineJS() const {
        return type_ == IonFrame_BaselineJS;
    }
    bool isOptimizedJS() const {
        return type_ == IonFrame_OptimizedJS;
    }
    bool isBaselineStub() const {
        return type_ == IonFrame_BaselineStub;
    }
    bool isNative() const;
    bool isOOLNativeGetter() const;
    bool isOOLPropertyOp() const;
    bool isOOLProxyGet() const;
    bool isDOMExit() const;
    bool isEntry() const {
        return type_ == IonFrame_Entry;
    }
    bool isFunctionFrame() const;
    bool isParallelFunctionFrame() const;

    bool isConstructing() const;

    bool isEntryJSFrame() const;

    void *calleeToken() const;
    JSFunction *callee() const;
    JSFunction *maybeCallee() const;
    unsigned numActualArgs() const;
    JSScript *script() const;
    void baselineScriptAndPc(JSScript **scriptRes, jsbytecode **pcRes) const;
    Value *nativeVp() const;
    Value *actualArgs() const;

    // Returns the return address of the frame above this one (that is, the
    // return address that returns back to the current frame).
    uint8_t *returnAddressToFp() const {
        return returnAddressToFp_;
    }

    // Previous frame information extracted from the current frame.
    inline size_t prevFrameLocalSize() const;
    inline FrameType prevType() const;
    uint8_t *prevFp() const;

    // Returns the stack space used by the current frame, in bytes. This does
    // not include the size of its fixed header.
    size_t frameSize() const {
        JS_ASSERT(type_ != IonFrame_Exit);
        return frameSize_;
    }

    // Functions used to iterate on frames. When prevType is IonFrame_Entry,
    // the current frame is the last frame.
    inline bool done() const {
        return type_ == IonFrame_Entry;
    }
    IonFrameIterator &operator++();

    // Returns the IonScript associated with this JS frame.
    IonScript *ionScript() const;

    // Returns the Safepoint associated with this JS frame. Incurs a lookup
    // overhead.
    const SafepointIndex *safepoint() const;

    // Returns the OSI index associated with this JS frame. Incurs a lookup
    // overhead.
    const OsiIndex *osiIndex() const;

    uintptr_t *spillBase() const;
    MachineState machineState() const;

    template <class Op>
    void forEachCanonicalActualArg(Op op, unsigned start, unsigned count) const {
        JS_ASSERT(isBaselineJS());

        unsigned nactual = numActualArgs();
        if (count == unsigned(-1))
            count = nactual - start;

        unsigned end = start + count;
        JS_ASSERT(start <= end && end <= nactual);

        Value *argv = actualArgs();
        for (unsigned i = start; i < end; i++)
            op(argv[i]);
    }

    void dump() const;

    inline BaselineFrame *baselineFrame() const;
};

class IonJSFrameLayout;
class IonBailoutIterator;

// Reads frame information in snapshot-encoding order (that is, outermost frame
// to innermost frame).
class SnapshotIterator
{
    SnapshotReader snapshot_;
    RecoverReader recover_;
    IonJSFrameLayout *fp_;
    MachineState machine_;
    IonScript *ionScript_;

  private:
    bool hasLocation(const Slot::Location &loc) const ;
    uintptr_t fromLocation(const Slot::Location &loc) const;
    static Value FromTypedPayload(JSValueType type, uintptr_t payload);

    void warnUnreadableSlot() const;

  public:
    SnapshotIterator(IonScript *ionScript, SnapshotOffset snapshotOffset,
                     IonJSFrameLayout *fp, const MachineState &machine);
    SnapshotIterator(const IonFrameIterator &iter);
    SnapshotIterator(const IonBailoutIterator &iter);
    SnapshotIterator();

    // Return the Value from the location indicated by the Slot.
    Value slotValue(const Slot &slot) const;

    // Determine if a slot indicates a readable location. A Value might not be
    // readbale if it has been optimized out, in which case it can only be
    // recovered during a bailout.
    bool slotReadable(const Slot &slot) const;

    Value maybeRead(bool silentFailure = false) {
        Slot s = readSlot();
        if (slotReadable(s))
            return slotValue(s);
        if (!silentFailure)
            warnUnreadableSlot();
        return UndefinedValue();
    }

    // As soon as we can removed these functions, we should be able to split the
    // SnapshotIterator in 2 parts, one for readings slots and one for reading
    // Values.
    Value read() {
        return slotValue(readSlot());
    }
    void skip() {
        readSlot();
    }

  public:
    //
    // dispatch to the RecoverReader.
    //

    inline uint32_t slots() const {
        return recover_.numOperands();
    }
    inline bool moreSlots() const {
        return recover_.moreOperands();
    }

    inline void nextFrame() {
        recover_.nextFrame();
    }
    inline uint32_t frameCount() const {
        return recover_.numFrames();
    }
    inline bool moreFrames() const {
        return recover_.moreFrames();
    }

    inline uint32_t pcOffset() const {
        return recover_.pcOffset();
    }

  public:
    //
    // dispatch to the SnapshotReader.
    //

    inline Slot readSlot() {
        recover_.readOperandSlotIndex();
        return snapshot_.readSlot();
    }

    inline bool resumeAfter() const {
        if (moreFrames())
            return false;
        return snapshot_.lastFrameResumeAfter();
    }
    inline BailoutKind bailoutKind() const {
        return snapshot_.bailoutKind();
    }

  public:
    inline void restart() {
        snapshot_.restart();
        recover_.restart();
    }
};

// Reads frame information in callstack order (that is, innermost frame to
// outermost frame).
template <AllowGC allowGC=CanGC>
class InlineFrameIteratorMaybeGC
{
    const IonFrameIterator *frame_;
    SnapshotIterator si_;
    unsigned framesRead_;
    typename MaybeRooted<JSFunction*, allowGC>::RootType callee_;
    typename MaybeRooted<JSScript*, allowGC>::RootType script_;
    jsbytecode *pc_;
    uint32_t numActualArgs_;

  private:
    void findNextFrame();

  public:
    InlineFrameIteratorMaybeGC(JSContext *cx, const IonFrameIterator *iter)
      : callee_(cx),
        script_(cx)
    {
        resetOn(iter);
    }

    InlineFrameIteratorMaybeGC(JSContext *cx, const IonBailoutIterator *iter);

    InlineFrameIteratorMaybeGC(JSContext *cx, const InlineFrameIteratorMaybeGC *iter)
      : frame_(iter ? iter->frame_ : NULL),
        framesRead_(0),
        callee_(cx),
        script_(cx)
    {
        if (frame_) {
            si_ = SnapshotIterator(*frame_);
            // findNextFrame will iterate to the next frame and init. everything.
            // Therefore to settle on the same frame, we report one frame less readed.
            framesRead_ = iter->framesRead_ - 1;
            findNextFrame();
        }
    }

    bool more() const {
        return frame_ && framesRead_ < si_.frameCount();
    }
    JSFunction *callee() const {
        JS_ASSERT(callee_);
        return callee_;
    }
    JSFunction *maybeCallee() const {
        return callee_;
    }

    unsigned numActualArgs() const {
        // The number of actual arguments of inline frames is recovered by the
        // iteration process. It is recovered from the bytecode because this
        // property still hold since the for inlined frames. This property does not
        // hold for the parent frame because it can have optimize a call to
        // js_fun_call or js_fun_apply.
        if (more())
            return numActualArgs_;

        return frame_->numActualArgs();
    }

    JSScript *script() const {
        return script_;
    }
    jsbytecode *pc() const {
        return pc_;
    }
    SnapshotIterator snapshotIterator() const {
        return si_;
    }
    bool isFunctionFrame() const;
    bool isConstructing() const;

    JSObject *scopeChain() const {
        SnapshotIterator s(si_);

        // scopeChain
        Value v = s.read();
        if (v.isObject()) {
            JS_ASSERT_IF(script()->hasAnalysis(), script()->analysis()->usesScopeChain());
            return &v.toObject();
        }

        return callee()->environment();
    }

    JSObject *thisObject() const {
        // JS_ASSERT(isConstructing(...));
        SnapshotIterator s(si_);

        // scopeChain
        s.skip();

        // Arguments object.
        if (script()->argumentsHasVarBinding())
            s.skip();

        // In strict modes, |this| may not be an object and thus may not be
        // readable which can either segv in read or trigger the assertion.
        Value v = s.read();
        JS_ASSERT(v.isObject());
        return &v.toObject();
    }

    Value maybeReadSlotByIndex(size_t index) const {
        SnapshotIterator s(si_);
        while (index--) {
            JS_ASSERT(s.moreSlots());
            s.skip();
        }

        Value v = s.maybeRead(true);

        while (s.moreSlots())
            s.skip();

        return v;
    }

    InlineFrameIteratorMaybeGC &operator++() {
        findNextFrame();
        return *this;
    }

    void dump() const;

    void resetOn(const IonFrameIterator *iter);

  private:
    void readFrameContext(SnapshotIterator &s, JSScript *script) const
    {
        // skip slot for the scope chain
        s.skip();

        // Skip slot for arguments object.
        if (script->argumentsHasVarBinding())
            s.skip();

        // skip slot for the |this|
        s.skip();
    }

    // Read formal argument of a frame.
    template <class Op>
    void readFormalFrameArgs(SnapshotIterator &s, Op &op,
                             unsigned nformal, unsigned nactual) const
    {
        unsigned i = 0;
        unsigned effective_end = (nactual < nformal) ? nactual : nformal;
        for (; i < effective_end; i++) {
            // We are not always able to read values from the snapshots, some values
            // such as non-gc things may still be live in registers and cause an
            // error while reading the machine state.
            Value v = s.maybeRead();
            op(v);
        }
    }

  public:
    template <class Op>
    void forEachCanonicalActualArg(JSContext *cx, Op op, unsigned start, unsigned count) const {
        // There is no other use case.
        JS_ASSERT(start == 0 && count == unsigned(-1));
        unsigned nactual = numActualArgs();
        unsigned nformal = callee()->nargs;

        // Get the non overflown arguments
        SnapshotIterator s(si_);
        readFrameContext(s, script());
        readFormalFrameArgs(s, op, nformal, nactual);

        // There is no overflow of argument, which means that we have recovered
        // all actual arguments.
        if (nactual <= nformal)
            return;

        if (more()) {
            // There is still a parent frame of this inlined frame.
            // The not overflown arguments are taken from the inlined frame,
            // because it will have the updated value when JSOP_SETARG is done.
            // All arguments (also the overflown) are the last pushed values in the parent frame.
            // To get the overflown arguments, we need to take them from there.


            // The overflown arguments are not available in current frame.
            // They are the last pushed arguments in the parent frame of this inlined frame.
            InlineFrameIteratorMaybeGC it(cx, this);
            ++it;
            SnapshotIterator parent_s(it.snapshotIterator());

            // Skip over all slots except for the arguments of the callee in the
            // caller frame. Skip over the [scope chain], [argsObj] and [this]
            // which have already been recovered..
            JS_ASSERT(parent_s.slots() >= nactual);
            unsigned skip = parent_s.slots() - nactual;
            for (unsigned j = 0; j < skip; j++)
                parent_s.skip();

            // Skip formal arguments which are already recovered from the
            // callee frame.
            unsigned i = 0;
            for (; i < nformal; i++)
                parent_s.skip();

            // Copy un-mutated overflow of arguments from the caller frame.
            for (; i < nactual; i++) {
                Value v = parent_s.maybeRead();
                op(v);
            }
        } else {
            Value *argv = frame_->actualArgs();
            for (unsigned i = 0; i < nactual - nformal; i++)
                op(argv[i]);
        }
    }

  private:
    InlineFrameIteratorMaybeGC() MOZ_DELETE;
    InlineFrameIteratorMaybeGC(const InlineFrameIteratorMaybeGC &iter) MOZ_DELETE;
};
typedef InlineFrameIteratorMaybeGC<CanGC> InlineFrameIterator;
typedef InlineFrameIteratorMaybeGC<NoGC> InlineFrameIteratorNoGC;

} // namespace ion
} // namespace js

#endif // JS_ION

#endif /* ion_IonFrameIterator_h */
