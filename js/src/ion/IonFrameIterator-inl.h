/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_frame_iterator_inl_h__
#define jsion_frame_iterator_inl_h__

#include "ion/BaselineFrame.h"
#include "ion/IonFrameIterator.h"
#include "ion/Bailouts.h"
#include "ion/Ion.h"

namespace js {
namespace ion {

template <AllowGC allowGC>
inline
InlineFrameIteratorMaybeGC<allowGC>::InlineFrameIteratorMaybeGC(
                                JSContext *cx, const IonFrameIterator *iter)
  : callee_(cx),
    script_(cx)
{
    resetOn(iter);
}

template <AllowGC allowGC>
inline
InlineFrameIteratorMaybeGC<allowGC>::InlineFrameIteratorMaybeGC(
        JSContext *cx,
        const InlineFrameIteratorMaybeGC<allowGC> *iter)
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

template <AllowGC allowGC>
inline unsigned
InlineFrameIteratorMaybeGC<allowGC>::numActualArgs() const
{
    // The number of actual arguments of inline frames is recovered by the
    // iteration process. It is recovered from the bytecode because this
    // property still hold since the for inlined frames. This property does not
    // hold for the parent frame because it can have optimize a call to
    // js_fun_call or js_fun_apply.
    if (more())
        return numActualArgs_;

    return frame_->numActualArgs();
}

template <AllowGC allowGC>
inline
InlineFrameIteratorMaybeGC<allowGC>::InlineFrameIteratorMaybeGC(
                                                JSContext *cx, const IonBailoutIterator *iter)
  : frame_(iter),
    framesRead_(0),
    callee_(cx),
    script_(cx)
{
    if (iter) {
        si_ = SnapshotIterator(*iter);
        findNextFrame();
    }
}

template <AllowGC allowGC>
inline InlineFrameIteratorMaybeGC<allowGC> &
InlineFrameIteratorMaybeGC<allowGC>::operator++()
{
    findNextFrame();
    return *this;
}

template <class Op>
inline void
IonFrameIterator::forEachCanonicalActualArg(Op op, unsigned start, unsigned count) const
{
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

inline BaselineFrame *
IonFrameIterator::baselineFrame() const
{
    JS_ASSERT(isBaselineJS());
    return (BaselineFrame *)(fp() - BaselineFrame::FramePointerOffset - BaselineFrame::Size());
}

} // namespace ion
} // namespace js

#endif // jsion_frame_iterator_inl_h__
