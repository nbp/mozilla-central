/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_caches_h__
#define jsion_caches_h__

#include "IonCode.h"
#include "TypeOracle.h"
#include "Registers.h"

struct JSFunction;
struct JSScript;

namespace js {
namespace ion {

#define IONCACHE_KIND_LIST(_)                                   \
    _(GetProperty)                                              \
    _(SetProperty)                                              \
    _(GetElement)                                               \
    _(BindName)                                                 \
    _(Name)

// Forward declarations of MIR types.
#define FORWARD_DECLARE(kind) class IonCache##kind;
 IONCACHE_KIND_LIST(FORWARD_DECLARE)
#undef FORWARD_DECLARE

// Common structure encoding the state of a polymorphic inline cache contained
// in the code for an IonScript. IonCaches are used for polymorphic operations
// where multiple implementations may be required.
//
// The cache is initially compiled as a patchable jump to an out of line
// fragment which invokes a cache function to perform the operation. The cache
// function may generate a stub to perform the operation in certain cases
// (e.g. a particular shape for an input object), patch the cache's jump to
// that stub and patch any failure conditions in the stub to jump back to the
// cache fragment. When those failure conditions are hit, the cache function
// may attach new stubs, forming a daisy chain of tests for how to perform the
// operation in different circumstances.
//
// Eventually, if too many stubs are generated the cache function may disable
// the cache, by generating a stub to make a call and perform the operation
// within the VM.
//
// While calls may be made to the cache function and other VM functions, the
// cache may still be treated as pure during optimization passes, such that
// LICM and GVN may be performed on operations around the cache as if the
// operation cannot reenter scripted code through an Invoke() or otherwise have
// unexpected behavior. This restricts the sorts of stubs which the cache can
// generate or the behaviors which called functions can have, and if a called
// function performs a possibly impure operation then the operation will be
// marked as such and the calling script will be recompiled.
//
// Similarly, despite the presence of functions and multiple stubs generated
// for a cache, the cache itself may be marked as idempotent and become hoisted
// or coalesced by LICM or GVN. This also constrains the stubs which can be
// generated for the cache.

struct TypedOrValueRegisterSpace
{
    mozilla::AlignedStorage2<TypedOrValueRegister> data_;
    TypedOrValueRegister &data() {
        return *data_.addr();
    }
    const TypedOrValueRegister &data() const {
        return *data_.addr();
    }
};

struct ConstantOrRegisterSpace
{
    mozilla::AlignedStorage2<ConstantOrRegister> data_;
    ConstantOrRegister &data() {
        return *data_.addr();
    }
    const ConstantOrRegister &data() const {
        return *data_.addr();
    }
};

// An Ion cache can hold both data or code and may implement updateBaseAddress
// and reset to update pointer on the code and to garbage collect all
// information on GC.
class IonCache
{
  public:
    enum Kind {
#   define DEFINE_CACHEKINDS(ickind) Cache_##ickind,
        IONCACHE_KIND_LIST(DEFINE_CACHEKINDS)
#   undef DEFINE_CACHEKINDS
        Cache_Invalid
    };

    // Cache testing and cast.
#   define CACHEKIND_CASTS(ickind)                                      \
    bool is##ickind() const {                                           \
        return kind() == Cache_##ickind;                                \
    }                                                                   \
    inline IonCache##ickind &to##ickind();

    IONCACHE_KIND_LIST(CACHEKIND_CASTS)
#   undef CACHEKIND_CASTS

    virtual Kind kind() const = 0;

  public:

    IonCache() { PodZero(this); }
    virtual ~IonCache() { }

    // Update labels once the code is copied and finalized.
    virtual void updateBaseAddress(IonCode *code, MacroAssembler &masm)
    { }

    // Reset the cache around garbage collection.
    virtual void reset()
    { }
};

#define CACHE_HEADER(ickind)                                            \
    Kind kind() const {                                                 \
        return IonCache::Cache_##ickind;                                \
    }


class IonCodeCache : public IonCache
{
  protected:
    bool pure_ : 1;
    bool idempotent_ : 1;
    size_t stubCount_ : 6;

    CodeLocationJump initialJump_;
    CodeLocationJump lastJump_;
    CodeLocationLabel cacheLabel_;

    // Offset from the initial jump to the rejoin label.
#ifdef JS_CPU_ARM
    static const size_t REJOIN_LABEL_OFFSET = 4;
#else
    static const size_t REJOIN_LABEL_OFFSET = 0;
#endif

    // Registers live after the cache, excluding output registers. The initial
    // value of these registers must be preserved by the cache.
    RegisterSet liveRegs;

    // Location of this operation, NULL for idempotent caches.
    JSScript *script;
    jsbytecode *pc;

    void init(RegisterSet liveRegs,
              CodeOffsetJump initialJump,
              CodeOffsetLabel rejoinLabel,
              CodeOffsetLabel cacheLabel) {
        this->liveRegs = liveRegs;
        this->initialJump_ = initialJump;
        this->lastJump_ = initialJump;
        this->cacheLabel_ = cacheLabel;

        JS_ASSERT(rejoinLabel.offset() == initialJump.offset() + REJOIN_LABEL_OFFSET);
    }

  public:

    IonCodeCache() { PodZero(this); }

    // Specialize updateBaseAddress and reset function for discarding
    // out-of-line code caches.
    void updateBaseAddress(IonCode *code, MacroAssembler &masm);
    void reset();

    CodeLocationJump lastJump() const { return lastJump_; }
    CodeLocationLabel cacheLabel() const { return cacheLabel_; }

    CodeLocationLabel rejoinLabel() const {
        uint8 *ptr = initialJump_.raw();
#ifdef JS_CPU_ARM
        uint32 i = 0;
        while (i < REJOIN_LABEL_OFFSET)
            ptr = Assembler::nextInstruction(ptr, &i);
#endif
        return CodeLocationLabel(ptr);
    }

    bool pure() {
        return pure_;
    }
    bool idempotent() {
        return idempotent_;
    }
    void setIdempotent() {
        JS_ASSERT(!idempotent_);
        JS_ASSERT(!script);
        JS_ASSERT(!pc);
        idempotent_ = true;
    }

    void updateLastJump(CodeLocationJump jump) {
        lastJump_ = jump;
    }

    size_t stubCount() const {
        return stubCount_;
    }
    void incrementStubCount() {
        // The IC should stop generating stubs before wrapping stubCount.
        stubCount_++;
        JS_ASSERT(stubCount_);
    }

    void setScriptedLocation(JSScript *script, jsbytecode *pc) {
        JS_ASSERT(!idempotent_);
        this->script = script;
        this->pc = pc;
    }

    void getScriptedLocation(MutableHandleScript pscript, jsbytecode **ppc) {
        pscript.set(script);
        *ppc = pc;
    }
};

// Subclasses of IonCache for the various kinds of caches. These do not define
// new data members; all caches must be of the same size.

class IonCacheGetProperty : public IonCodeCache
{
  protected:
    Register object_;
    PropertyName *name_;
    TypedOrValueRegisterSpace output_;
    bool allowGetters_;

  public:
    IonCacheGetProperty(CodeOffsetJump initialJump,
                        CodeOffsetLabel rejoinLabel,
                        CodeOffsetLabel cacheLabel,
                        RegisterSet liveRegs,
                        Register object, PropertyName *name,
                        TypedOrValueRegister output,
                        bool allowGetters)
    {
        init(liveRegs, initialJump, rejoinLabel, cacheLabel);
        object_ = object;
        name_ = name;
        output_.data() = output;
        allowGetters_ = allowGetters;
    }

    CACHE_HEADER(GetProperty);

    Register object() const { return object_; }
    PropertyName *name() const { return name_; }
    TypedOrValueRegister output() const { return output_.data(); }
    bool allowGetters() const { return allowGetters_; }

    bool attachReadSlot(JSContext *cx, IonScript *ion, JSObject *obj, JSObject *holder,
                        const Shape *shape);
    bool attachCallGetter(JSContext *cx, IonScript *ion, JSObject *obj, JSObject *holder,
                          const Shape *shape,
                          const SafepointIndex *safepointIndex, void *returnAddr);
};

class IonCacheSetProperty : public IonCodeCache
{
  protected:
    Register object_;
    PropertyName *name_;
    ConstantOrRegisterSpace value_;
    bool strict_;

  public:
    IonCacheSetProperty(CodeOffsetJump initialJump,
                        CodeOffsetLabel rejoinLabel,
                        CodeOffsetLabel cacheLabel,
                        RegisterSet liveRegs,
                        Register object, PropertyName *name,
                        ConstantOrRegister value,
                        bool strict)
    {
        init(liveRegs, initialJump, rejoinLabel, cacheLabel);
        object_ = object;
        name_ = name;
        value_.data() = value;
        strict_ = strict;
    }

    CACHE_HEADER(SetProperty);

    Register object() const { return object_; }
    PropertyName *name() const { return name_; }
    ConstantOrRegister value() const { return value_.data(); }
    bool strict() const { return strict_; }

    bool attachNativeExisting(JSContext *cx, IonScript *ion, HandleObject obj, HandleShape shape);
    bool attachSetterCall(JSContext *cx, IonScript *ion, HandleObject obj,
                          HandleObject holder, HandleShape shape, void *returnAddr);
    bool attachNativeAdding(JSContext *cx, IonScript *ion, JSObject *obj, const Shape *oldshape,
                            const Shape *newshape, const Shape *propshape);
};

class IonCacheGetElement : public IonCodeCache
{
  protected:
    Register object_;
    ConstantOrRegisterSpace index_;
    TypedOrValueRegisterSpace output_;
    bool monitoredResult_ : 1;
    bool hasDenseArrayStub_ : 1;

  public:
    IonCacheGetElement(CodeOffsetJump initialJump,
                       CodeOffsetLabel rejoinLabel,
                       CodeOffsetLabel cacheLabel,
                       RegisterSet liveRegs,
                       Register object, ConstantOrRegister index,
                       TypedOrValueRegister output, bool monitoredResult)
    {
        init(liveRegs, initialJump, rejoinLabel, cacheLabel);
        object_ = object;
        index_.data() = index;
        output_.data() = output;
        monitoredResult_ = monitoredResult;
        hasDenseArrayStub_ = false;
    }

    CACHE_HEADER(GetElement);

    Register object() const {
        return object_;
    }
    ConstantOrRegister index() const {
        return index_.data();
    }
    TypedOrValueRegister output() const {
        return output_.data();
    }
    bool monitoredResult() const {
        return monitoredResult_;
    }
    bool hasDenseArrayStub() const {
        return hasDenseArrayStub_;
    }
    void setHasDenseArrayStub() {
        JS_ASSERT(!hasDenseArrayStub());
        hasDenseArrayStub_ = true;
    }

    bool attachGetProp(JSContext *cx, IonScript *ion, HandleObject obj, const Value &idval, PropertyName *name);
    bool attachDenseArray(JSContext *cx, IonScript *ion, JSObject *obj, const Value &idval);
};

class IonCacheBindName : public IonCodeCache
{
  protected:
    Register scopeChain_;
    PropertyName *name_;
    Register output_;

  public:
    IonCacheBindName(CodeOffsetJump initialJump,
                     CodeOffsetLabel rejoinLabel,
                     CodeOffsetLabel cacheLabel,
                     RegisterSet liveRegs,
                     Register scopeChain, PropertyName *name,
                     Register output)
    {
        init(liveRegs, initialJump, rejoinLabel, cacheLabel);
        scopeChain_ = scopeChain;
        name_ = name;
        output_ = output;
    }

    CACHE_HEADER(BindName);

    Register scopeChainReg() const {
        return scopeChain_;
    }
    HandlePropertyName name() const {
        // TODO: are we marking IC?
        return HandlePropertyName::fromMarkedLocation(&name_);
    }
    Register outputReg() const {
        return output_;
    }

    bool attachGlobal(JSContext *cx, IonScript *ion, JSObject *scopeChain);
    bool attachNonGlobal(JSContext *cx, IonScript *ion, JSObject *scopeChain, JSObject *holder);
};

class IonCacheName : public IonCodeCache
{
  protected:
    bool typeOf_;
    Register scopeChain_;
    PropertyName *name_;
    TypedOrValueRegisterSpace output_;

  public:
    IonCacheName(bool typeOf,
                 CodeOffsetJump initialJump,
                 CodeOffsetLabel rejoinLabel,
                 CodeOffsetLabel cacheLabel,
                 RegisterSet liveRegs,
                 Register scopeChain, PropertyName *name,
                 TypedOrValueRegister output)
    {
        init(liveRegs, initialJump, rejoinLabel, cacheLabel);
        typeOf_ = typeOf;
        scopeChain_ = scopeChain;
        name_ = name;
        output_.data() = output;
    }

    CACHE_HEADER(Name);

    Register scopeChainReg() const {
        return scopeChain_;
    }
    HandlePropertyName name() const {
        return HandlePropertyName::fromMarkedLocation(&name_);
    }
    TypedOrValueRegister outputReg() const {
        return output_.data();
    }
    bool isTypeOf() const {
        return typeOf_;
    }

    bool attach(JSContext *cx, IonScript *ion, HandleObject scopeChain, HandleObject obj,
                Shape *shape);
};

#undef CACHE_HEADER

// Implement cache casts now that the compiler can see the inheritance.
#define CACHE_CASTS(ickind)                                             \
    IonCache##ickind &IonCache::to##ickind()                            \
    {                                                                   \
        JS_ASSERT(is##ickind());                                        \
        return *static_cast<IonCache##ickind *>(this);                  \
    }
IONCACHE_KIND_LIST(CACHE_CASTS)
#undef OPCODE_CASTS


bool
GetPropertyCache(JSContext *cx, size_t cacheIndex, HandleObject obj, MutableHandleValue vp);

bool
SetPropertyCache(JSContext *cx, size_t cacheIndex, HandleObject obj, HandleValue value,
                 bool isSetName);

bool
GetElementCache(JSContext *cx, size_t cacheIndex, HandleObject obj, HandleValue idval,
                MutableHandleValue vp);

JSObject *
BindNameCache(JSContext *cx, size_t cacheIndex, HandleObject scopeChain);

bool
GetNameCache(JSContext *cx, size_t cacheIndex, HandleObject scopeChain, MutableHandleValue vp);

} // namespace ion
} // namespace js

#endif // jsion_caches_h__
