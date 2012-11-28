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

// Forward declarations of Cache kinds.
#define FORWARD_DECLARE(kind) class kind##IC;
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

// * IonCache usage
//
// An IonCache is the base structure of a cache which is generating code stubs
// with its update function. A cache derive from the IonCache class and use
// CACHE_HEADER to pre-declare a few members such as UpdateInfo (VMFunction) and
// UpdateData (out-of-line code generation traits). It must at least provide a
// static update function which prototype should match UpdateData::Fn type. The
// update function expect at least 2 arguments, the first one is the JSContext*,
// and the second one is the cacheIndex. The rest of the prototype is restricted
// by the VMFunction call mechanism.
//
// examples:
// [1]   bool      update(JSContext *, size_t, HandleObject, HandleValue)
// [2]   JSObject *update(JSContext *, size_t, HandleObject, HandleValue)
// [3]   bool      update(JSContext *, size_t, HandleObject, MutableHandleValue)
//
// To use the generic mechanism for calling inline caches, the UpdateData
// structure of the cache must define UpdateData::Args and UpdateData::Output in
// the CodeGenerator files.  UpdateData::Args should be a typedef on
// ICArgSeq<...> which is a "variadic" template where each parameter decribes
// where to find an argument used to call the update function.
//
// For the update function (1), we can expect the following typedef inside the
// UpdateData structure of the MyIC cache:
//
//    typedef ICArgSeq<ICField<CACHE_FIELD(MyIC, Register, object_)>,
//                     ICField<CACHE_FIELD(MyIC, ConstantOrRegister, value_)>
//                     > Args;
//
// where object_ is a Register field on MyIC which contains the location of the
// register with which the update function should be called.  As it is first in
// the ICArgSeq, it will be used for the first argument after the cacheIndex,
// i-e the HandleObject argument.  The value_ field will be used for the
// HandleValue argument.  A similar Args typedef will be used for update
// function (2).
//
// The return type of the update function is expressed with UpdateData::Output
// typedef which is either defined to ICStoreNothing, ICStoreRegisterTo<...> or
// ICStoreValueTo<...>.  for the 3 examples of the update functions we will
// expect something similar to:
//
// [1] typedef ICStoreNothing Output;
// [2] typedef ICStoreRegisterTo<CACHE_FIELD(MyIC, Register, output_)> Output;
// [3] typedef ICStoreValueTo<CACHE_FIELD(MyIC, TypedOrValueRegister, output_)> Output;
//
// where output_ is a field of MyIC which contains the location of the result of
// the LIR instructions to which this inline cache is attached to.
//
// Once all typedefs are defined, the cache can be invoke simply with the
// "inlineCache" function of the CodeGeneratorShared which will allocate the
// cache, bind it to its code location, set it as idempotent (if there is no
// resume point) and generate an out-of-line call to the update
// function.
//
// Code generator example:
//
//     Register objReg = ...;
//     ConstantOrRegister valueInput = ...;
//     Register outputReg = ...;
//
//     MyIC cache(objReg, valueInput, outputReg);
//     if (!inlineCache(ins, cache))
//         return false;
//
// Warning: Once the call to "inlineCache" function is done any modification to
// the cache variable would not be ignored at runtime.
//
// All inputs and output location used for calling the update function should
// also be useful inputs for generated stubs.
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
    inline ickind##IC &to##ickind();

    IONCACHE_KIND_LIST(CACHEKIND_CASTS)
#   undef CACHEKIND_CASTS

    virtual Kind kind() const = 0;

  public:

    static const char *CacheName(Kind kind);

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

    // Location of this operation, NULL for idempotent caches.
    JSScript *script;
    jsbytecode *pc;

  private:
    static const size_t MAX_STUBS;
    void incrementStubCount() {
        // The IC should stop generating stubs before wrapping stubCount.
        stubCount_++;
        JS_ASSERT(stubCount_);
    }

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

  public:

    IonCache()
      : pure_(false),
        idempotent_(false),
        stubCount_(0),
        initialJump_(),
        lastJump_(),
        cacheLabel_(),
        script(NULL),
        pc(NULL)
    {
    }

    // Bind inline boundaries of the cache. This include the patchable jump
    // location and the location where the successful exit rejoin the inline
    // path.
    void bindInline(CodeOffsetJump initialJump, CodeOffsetLabel rejoinLabel) {
        initialJump_ = initialJump;
        lastJump_ = initialJump;

        JS_ASSERT(rejoinLabel.offset() == initialJump.offset() + REJOIN_LABEL_OFFSET);
    }

    // Bind out-of-line boundaries of the cache. This include the location of
    // the update function call.  This location will be set to the exitJump of
    // the last generated stub.
    void bindOutOfLine(CodeOffsetLabel cacheLabel) {
        cacheLabel_ = cacheLabel;
    }

    // Update labels once the code is copied and finalized.
    void updateBaseAddress(IonCode *code, MacroAssembler &masm);

    // Reset the cache around garbage collection.
    void reset();

    bool canAttachStub() const {
        return stubCount_ < MAX_STUBS;
    }

    // Value used to identify code which has to be patched with the generated
    // stub address. This address will later be used for marking the stub if it
    // does a call, even if all the stub of the IC have been flushed.
    static const ImmWord CODE_MARK;

#ifdef DEBUG
    // Cast mark to a pointer such as it can be compared to what is read from
    // the stack during the marking phase.
    static IonCode *codeMark() {
        return reinterpret_cast<IonCode *>(const_cast<ImmWord*>(&CODE_MARK)->asPointer());
    }
#endif

    // Return value of linkCode (see linkCode).
    static IonCode * const CACHE_FLUSHED;

    // Use the Linker to link the generated code and check if any
    // monitoring/allocation caused an invalidation of the running ion
    // script. If there is no allocation issue, but the code cannot be attached
    // later, this function will return CACHE_FLUSHED.  If there is any fatal
    // error, this function will return a NULL pointer.
    IonCode *linkCode(JSContext *cx, MacroAssembler &masm, IonScript *ion);

    // Fixup variables and update jumps in the list of stubs.  Increment the
    // number of attached stubs accordingly.
    void attachStub(MacroAssembler &masm, IonCode *code, CodeOffsetJump &rejoinOffset,
                    CodeOffsetJump *exitOffset, CodeOffsetLabel *stubOffset = NULL);

    // Combine both linkCode and attachStub into one function. In addition, it
    // produces a spew augmented with the attachKind string.
    bool linkAndAttachStub(JSContext *cx, MacroAssembler &masm, IonScript *ion,
                           const char *attachKind, CodeOffsetJump &rejoinOffset,
                           CodeOffsetJump *exitOffset, CodeOffsetLabel *stubOffset = NULL);

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

// Define the cache kind and pre-declare data structures used for calling inline
// caches.
#define CACHE_HEADER(ickind)                    \
    Kind kind() const {                         \
        return IonCache::Cache_##ickind;        \
    }                                           \
                                                \
    struct UpdateData;                          \
    static const VMFunction UpdateInfo;

// Subclasses of IonCache for the various kinds of caches. These do not define
// new data members; all caches must be of the same size.

class GetPropertyIC : public IonCache
{
  protected:
    // Registers live after the cache, excluding output registers. The initial
    // value of these registers must be preserved by the cache.
    RegisterSet liveRegs_;

    Register object_;
    PropertyName *name_;
    TypedOrValueRegister output_;
    bool allowGetters_;

  public:
    GetPropertyIC(RegisterSet liveRegs,
                  Register object, PropertyName *name,
                  TypedOrValueRegister output,
                  bool allowGetters)
      : liveRegs_(liveRegs),
        object_(object),
        name_(name),
        output_(output),
        allowGetters_(allowGetters)
    {
    }

    CACHE_HEADER(GetProperty)

    Register object() const { return object_; }
    PropertyName *name() const { return name_; }
    TypedOrValueRegister output() const { return output_; }
    bool allowGetters() const { return allowGetters_; }

    bool attachReadSlot(JSContext *cx, IonScript *ion, JSObject *obj, JSObject *holder,
                        const Shape *shape);
    bool attachCallGetter(JSContext *cx, IonScript *ion, JSObject *obj, JSObject *holder,
                          const Shape *shape,
                          const SafepointIndex *safepointIndex, void *returnAddr);

    static bool update(JSContext *cx, size_t cacheIndex, HandleObject obj, MutableHandleValue vp);
};

class SetPropertyIC : public IonCache
{
  protected:
    // Registers live after the cache, excluding output registers. The initial
    // value of these registers must be preserved by the cache.
    RegisterSet liveRegs_;

    Register object_;
    PropertyName *name_; // rooting issues ?!
    ConstantOrRegister value_;
    bool isSetName_;
    bool strict_;

  public:
    SetPropertyIC(RegisterSet liveRegs, Register object, PropertyName *name,
                  ConstantOrRegister value, bool isSetName, bool strict)
      : liveRegs_(liveRegs),
        object_(object),
        name_(name),
        value_(value),
        isSetName_(isSetName),
        strict_(strict)
    {
    }

    CACHE_HEADER(SetProperty)

    Register object() const { return object_; }
    PropertyName *name() const { return name_; }
    ConstantOrRegister value() const { return value_; }
    bool isSetName() const { return isSetName_; }
    bool strict() const { return strict_; }

    bool attachNativeExisting(JSContext *cx, IonScript *ion, HandleObject obj, HandleShape shape);
    bool attachSetterCall(JSContext *cx, IonScript *ion, HandleObject obj,
                          HandleObject holder, HandleShape shape, void *returnAddr);
    bool attachNativeAdding(JSContext *cx, IonScript *ion, JSObject *obj, const Shape *oldshape,
                            const Shape *newshape, const Shape *propshape);

    static bool
    update(JSContext *cx, size_t cacheIndex, HandleObject obj, HandleValue value);
};

class GetElementIC : public IonCache
{
  protected:
    Register object_;
    ConstantOrRegister index_;
    TypedOrValueRegister output_;
    bool monitoredResult_ : 1;
    bool hasDenseArrayStub_ : 1;

  public:
    GetElementIC(Register object, ConstantOrRegister index,
                 TypedOrValueRegister output, bool monitoredResult)
      : object_(object),
        index_(index),
        output_(output),
        monitoredResult_(monitoredResult),
        hasDenseArrayStub_(false)
    {
    }

    CACHE_HEADER(GetElement)

    Register object() const {
        return object_;
    }
    ConstantOrRegister index() const {
        return index_;
    }
    TypedOrValueRegister output() const {
        return output_;
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

    static bool
    update(JSContext *cx, size_t cacheIndex, HandleObject obj, HandleValue idval,
                MutableHandleValue vp);
};

class BindNameIC : public IonCache
{
  protected:
    Register scopeChain_;
    PropertyName *name_;
    Register output_;

  public:
    BindNameIC(Register scopeChain, PropertyName *name, Register output)
      : scopeChain_(scopeChain),
        name_(name),
        output_(output)
    {
    }

    CACHE_HEADER(BindName)

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

    static JSObject *
    update(JSContext *cx, size_t cacheIndex, HandleObject scopeChain);
};

class NameIC : public IonCache
{
  protected:
    bool typeOf_;
    Register scopeChain_;
    PropertyName *name_;
    TypedOrValueRegister output_;

  public:
    NameIC(bool typeOf,
           Register scopeChain, PropertyName *name,
           TypedOrValueRegister output)
      : typeOf_(typeOf),
        scopeChain_(scopeChain),
        name_(name),
        output_(output)
    {
    }

    CACHE_HEADER(Name)

    Register scopeChainReg() const {
        return scopeChain_;
    }
    HandlePropertyName name() const {
        return HandlePropertyName::fromMarkedLocation(&name_);
    }
    TypedOrValueRegister outputReg() const {
        return output_;
    }
    bool isTypeOf() const {
        return typeOf_;
    }

    bool attach(JSContext *cx, IonScript *ion, HandleObject scopeChain, HandleObject obj,
                Shape *shape);

    static bool
    update(JSContext *cx, size_t cacheIndex, HandleObject scopeChain, MutableHandleValue vp);
};

#undef CACHE_HEADER

// Implement cache casts now that the compiler can see the inheritance.
#define CACHE_CASTS(ickind)                                             \
    ickind##IC &IonCache::to##ickind()                                  \
    {                                                                   \
        JS_ASSERT(is##ickind());                                        \
        return *static_cast<ickind##IC *>(this);                        \
    }
IONCACHE_KIND_LIST(CACHE_CASTS)
#undef OPCODE_CASTS

} // namespace ion
} // namespace js

#endif // jsion_caches_h__
