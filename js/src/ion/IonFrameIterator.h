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
    friend class RResumePoint;

    SnapshotReader snapshot_;    // Read allocations.

    RecoverReader recover_;      // Read operations.
    Slot slot_; // :TODO: remove ?

    MachineState machine_;       // Read data from allocations.
    IonJSFrameLayout *fp_;

    IonScript *ionScript_;       // Read from the constant pool.

  private:
    bool hasLocation(const Slot::Location &loc);
    uintptr_t fromLocation(const Slot::Location &loc);
    static Value FromTypedPayload(JSValueType type, uintptr_t payload);

    Value slotValue(const Slot &slot);
    bool slotReadable(const Slot &slot);
    void warnUnreadableSlot();

    void init();

  public:
    SnapshotIterator(IonScript *ionScript, SnapshotOffset snapshotOffset,
                     IonJSFrameLayout *fp, const MachineState &machine);
    SnapshotIterator(const IonFrameIterator &iter);
    SnapshotIterator(const IonBailoutIterator &iter);
    SnapshotIterator();

    void nextSlot() {
        JS_ASSERT(!slot_.isInvalid());
        slot_ = snapshot_.readSlot();
    }

    void nextOperation() {
        recover_.nextOperation();
    }

    RInstruction *operation() {
        return recover_.operation();
    }
    const RInstruction *operation() const {
        return recover_.operation();
    }

    Value read() {
        return slotValue(slot_);
    }

    size_t index() const {
        JS_ASSERT(!slot_.isInvalid());
        return snapshot_.index() - 1;
    }

    bool isOptimizedOut() {
        return !slotReadable(slot_);
    }

    Value maybeRead(bool silentFailure = false) {
        if (slotReadable(slot_))
            return slotValue(slot_);
        if (!silentFailure)
            warnUnreadableSlot();
        return UndefinedValue();
    }

    void restart();

    // Data extractted from the snapshot, should probably be part of the frame iterator.
    BailoutKind bailoutKind() const {
        return snapshot_.bailoutKind();
    }
    RecoverOffset recoverOffset() const {
        return snapshot_.recoverOffset();
    }
#ifdef TRACK_SNAPSHOTS
    void spewBailingFrom() const {
        return snapshot_.spewBailingFrom();
    }
#endif

    bool isFrame() const {
        return recover_.isFrame();
    }
    size_t frameCount() const {
        return recover_.frameCount();
    }
    void settleOnNextFrame() {
        recover_.settleOnNextFrame();
    }

    static Slot invalidSlot() {
        return Slot(Slot::INVALID_SLOT);
    }
};

// Reads frame information in callstack order (that is, innermost frame to
// outermost frame).
template <AllowGC allowGC=CanGC>
class InlineFrameIteratorMaybeGC
{
    const IonFrameIterator *frame_;
    // SnapshotIterator start_;
    SnapshotIterator si_;
    // RecoverReader ri_;
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
    Value maybeReadSlotByIndex(size_t index) const;

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

    template <class Op>
    inline void forEachCanonicalActualArg(JSContext *cx, Op op, unsigned start, unsigned count) const;

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
    inline JSObject *scopeChain() const;
    inline JSObject *thisObject() const;
    inline InlineFrameIteratorMaybeGC &operator++();

    void dump() const;

    void resetOn(const IonFrameIterator *iter);

  private:
    InlineFrameIteratorMaybeGC() MOZ_DELETE;
    InlineFrameIteratorMaybeGC(const InlineFrameIteratorMaybeGC &iter) MOZ_DELETE;
};
typedef InlineFrameIteratorMaybeGC<CanGC> InlineFrameIterator;
typedef InlineFrameIteratorMaybeGC<NoGC> InlineFrameIteratorNoGC;

template <class Op>
static void
ReadFrameArgs(Op &op, const Value *argv, Value *scopeChain, Value *thisv,
              unsigned start, unsigned formalEnd, unsigned iterEnd,
              JSScript *script, SnapshotIterator &s);

} // namespace ion
} // namespace js

#endif // jsion_frames_iterator_h__

