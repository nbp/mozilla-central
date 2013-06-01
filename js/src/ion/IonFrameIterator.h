/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_frame_iterator_h__
#define jsion_frame_iterator_h__

#include "jstypes.h"
#include "IonCode.h"
#include "Slots.h"
#include "SnapshotReader.h"

class JSFunction;
class JSScript;

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
    // IonActivation.
    IonFrame_Exit,

    // An OSR frame is added when performing OSR from within a bailout. It
    // looks like a JS frame, but does not push scripted arguments, as OSR
    // reads arguments from a js::StackFrame.
    IonFrame_Osr
};

// Define that to avoid including Recover.h into all header which are iterating
// over the arguments of Ion frames.
struct AbstractPush
{
    virtual void operator()(const Value &src) = 0;
};

class IonCommonFrameLayout;
class IonJSFrameLayout;
class IonExitFrameLayout;

class IonActivation;
class IonActivationIterator;

class BaselineFrame;

class IonFrameIterator
{
  protected:
    uint8_t *current_;
    FrameType type_;
    uint8_t *returnAddressToFp_;
    size_t frameSize_;

  private:
    mutable const SafepointIndex *cachedSafepointIndex_;
    const IonActivation *activation_;

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

    IonFrameIterator(const IonActivationIterator &activations);
    IonFrameIterator(IonJSFrameLayout *fp);

    // Current frame information.
    FrameType type() const {
        return type_;
    }
    uint8_t *fp() const {
        return current_;
    }

    inline IonCommonFrameLayout *current() const;
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
    inline size_t frameSize() const;

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
    inline void forEachCanonicalActualArg(Op op, unsigned start, unsigned count) const;

    void dump() const;

    inline BaselineFrame *baselineFrame() const;
};

class IonActivationIterator
{
    uint8_t *top_;
    IonActivation *activation_;

  private:
    void settle();

  public:
    IonActivationIterator(JSContext *cx);
    IonActivationIterator(JSRuntime *rt);

    IonActivationIterator &operator++();

    IonActivation *activation() const {
        return activation_;
    }
    uint8_t *top() const {
        return top_;
    }
    bool more() const;

    // Returns the bottom and top addresses of the current activation.
    void ionStackRange(uintptr_t *&min, uintptr_t *&end);
};

class IonJSFrameLayout;
class IonBailoutIterator;

class RInstruction;
class RResumePoint;

// Reads frame information in snapshot-encoding order (that is, outermost frame
// to innermost frame).
class SnapshotIterator
{
    friend class RInstruction;

  private:
    SnapshotReader snapshot_;  // Read allocations.
    RecoverReader recover_;    // Read operations.
    MachineState machine_;     // Read data from registers.
    IonJSFrameLayout *fp_;     // Read data from stack slots.
    IonScript *ionScript_;     // Read data from the constant pool.
    AutoValueVector *resumed_; // Read data from the result of recover
                               // instructions.

  private:
    bool hasLocation(const Slot::Location &loc) const;
    uintptr_t fromLocation(const Slot::Location &loc) const;
    static Value FromTypedPayload(JSValueType type, uintptr_t payload);

    Value slotValue(const Slot &slot) const;
    bool slotReadable(const Slot &slot) const;

    Slot readOperand();

    void warnUnreadableSlot() const;

  public:
    SnapshotIterator(const IonFrameIterator &iter);
    SnapshotIterator(const IonBailoutIterator &iter);
    SnapshotIterator();

    // Start iterating again from the beginning of the snapshot.
    void restart();
    void initResumedResults(AutoValueVector *);

    // Iterate on all operations contained in the recover structure.
    bool moreOperation() const {
        return recover_.moreOperation();
    }
    void nextOperation() {
        recover_.nextOperation();
    }

    // Get the current operation.
    RInstruction *operation() {
        return recover_.operation();
    }
    const RInstruction *operation() const {
        return recover_.operation();
    }

    size_t operandIndex() const {
        return recover_.operandIndex();
    }

    // Convenience functions for iterations which do not need to visit all
    // operations.
    bool isFrame() const;
    void settleOnNextFrame();
    size_t frameCount() const {
        return recover_.frameCount();
    }

    // Check if the slot are accessbile and provide accessors to read the slot
    // value out-of the different location in which the value can be stored.
    bool isOptimizedOut(const Slot &slot) {
        return !slotReadable(slot);
    }

    Value readFromSlot(const Slot &slot) const {
        return slotValue(slot);
    }
    Value maybeReadFromSlot(const Slot &slot, bool silentFailure = false) const {
        if (slotReadable(slot))
            return slotValue(slot);
        if (!silentFailure)
            warnUnreadableSlot();
        return UndefinedValue();
    }

    // Data extracted from the snapshot, should probably be part of the frame
    // iterator.
    BailoutKind bailoutKind() const {
        return snapshot_.bailoutKind();
    }
#ifdef TRACK_SNAPSHOTS
    void spewBailingFrom() const {
        return snapshot_.spewBailingFrom();
    }
#endif
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
    inline InlineFrameIteratorMaybeGC(JSContext *cx, const IonFrameIterator *iter);
    inline InlineFrameIteratorMaybeGC(JSContext *cx, const IonBailoutIterator *iter);
    inline InlineFrameIteratorMaybeGC(JSContext *cx, const InlineFrameIteratorMaybeGC *iter);

    size_t numSlots() const;

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
    inline unsigned numActualArgs() const;

    void forEachCanonicalActualArg(JSContext *cx, AbstractPush &op, unsigned start, unsigned count) const;

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
    JSObject *scopeChain() const;
    JSObject *thisObject() const;
    inline InlineFrameIteratorMaybeGC &operator++();

    Value maybeReadOperandByIndex(size_t index, bool fallible = true) const;
    void dump() const;

    void resetOn(const IonFrameIterator *iter);

  private:
    InlineFrameIteratorMaybeGC() MOZ_DELETE;
    InlineFrameIteratorMaybeGC(const InlineFrameIteratorMaybeGC &iter) MOZ_DELETE;
};
typedef InlineFrameIteratorMaybeGC<CanGC> InlineFrameIterator;
typedef InlineFrameIteratorMaybeGC<NoGC> InlineFrameIteratorNoGC;

void
ReadFrameArgs(AbstractPush &op, const Value *argv,
              unsigned formalEnd, unsigned iterEnd,
              SnapshotIterator &s);

} // namespace ion
} // namespace js

#endif // jsion_frames_iterator_h__

