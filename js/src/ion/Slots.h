/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsion_slots_h__
#define jsion_slots_h__

#include "Registers.h"

namespace js {
namespace ion {

class SnapshotReader;
class RInstruction;
class SnapshotIterator;

// A Slot represent a the location and the encoding of a Value in multiple
// locations, such as the pool of constant of an IonScript, a register, a stack
// location, or even the index of a resume instruction.
class Slot
{
    friend class SnapshotReader;
    friend class RInstruction;
    friend class SnapshotIterator;

  public:
    class Location
    {
        friend class SnapshotReader;

        Register::Code reg_;
        int32_t stackSlot_;

        static Location From(const Register &reg) {
            Location loc;
            loc.reg_ = reg.code();
            loc.stackSlot_ = INVALID_STACK_SLOT;
            return loc;
        }
        static Location From(int32_t stackSlot) {
            Location loc;
            loc.reg_ = Register::Code(0);      // Quell compiler warnings.
            loc.stackSlot_ = stackSlot;
            return loc;
        }

      public:
        Register reg() const {
            JS_ASSERT(!isStackSlot());
            return Register::FromCode(reg_);
        }
        int32_t stackSlot() const {
            JS_ASSERT(isStackSlot());
            return stackSlot_;
        }
        bool isStackSlot() const {
            return stackSlot_ != INVALID_STACK_SLOT;
        }
    };

    enum SlotMode
    {
        CONSTANT,           // An index into the constant pool.
        DOUBLE_REG,         // Type is double, payload is in a register.
        TYPED_REG,          // Type is constant, payload is in a register.
        TYPED_STACK,        // Type is constant, payload is on the stack.
        UNTYPED,            // Type is not known.
        JS_UNDEFINED,       // UndefinedValue()
        JS_NULL,            // NullValue()
        JS_INT32,           // Int32Value(n)

        RESUME_OPERATION,   // Value removed from the MIR.
        INVALID_SLOT        // Value read after the last slots.
    };

  private:
    SlotMode mode_;

    union {
        FloatRegister::Code fpu_;
        struct {
            JSValueType type;
            Location payload;
        } known_type_;
#if defined(JS_NUNBOX32)
        struct {
            Location type;
            Location payload;
        } unknown_type_;
#elif defined(JS_PUNBOX64)
        struct {
            Location value;
        } unknown_type_;
#endif
        int32_t value_;
    };

  protected:
    Slot(SlotMode mode, JSValueType type, const Location &loc)
      : mode_(mode)
    {
        known_type_.type = type;
        known_type_.payload = loc;
    }
    Slot(const FloatRegister &reg)
      : mode_(DOUBLE_REG)
    {
        fpu_ = reg.code();
    }
    Slot(SlotMode mode)
      : mode_(mode)
    { }
    Slot(SlotMode mode, uint32_t index)
      : mode_(mode)
    {
        JS_ASSERT(mode == CONSTANT || mode == JS_INT32 || mode == RESUME_OPERATION);
        value_ = index;
    }

  public:
    Slot()
      : mode_(INVALID_SLOT)
    { }

    SlotMode mode() const {
        return mode_;
    }
    uint32_t constantIndex() const {
        JS_ASSERT(mode() == CONSTANT);
        return value_;
    }
    uint32_t operationIndex() const {
        JS_ASSERT(mode() == RESUME_OPERATION);
        return value_;
    }
    int32_t int32Value() const {
        JS_ASSERT(mode() == JS_INT32);
        return value_;
    }
    JSValueType knownType() const {
        JS_ASSERT(mode() == TYPED_REG || mode() == TYPED_STACK);
        return known_type_.type;
    }
    Register reg() const {
        JS_ASSERT(mode() == TYPED_REG && knownType() != JSVAL_TYPE_DOUBLE);
        return known_type_.payload.reg();
    }
    FloatRegister floatReg() const {
        JS_ASSERT(mode() == DOUBLE_REG);
        return FloatRegister::FromCode(fpu_);
    }
    int32_t stackSlot() const {
        JS_ASSERT(mode() == TYPED_STACK);
        return known_type_.payload.stackSlot();
    }
#if defined(JS_NUNBOX32)
    Location payload() const {
        JS_ASSERT(mode() == UNTYPED);
        return unknown_type_.payload;
    }
    Location type() const {
        JS_ASSERT(mode() == UNTYPED);
        return unknown_type_.type;
    }
#elif defined(JS_PUNBOX64)
    Location value() const {
        JS_ASSERT(mode() == UNTYPED);
        return unknown_type_.value;
    }
#endif
    bool isInvalid() const {
        return mode() == INVALID_SLOT;
    }
};

} // namespace ion
} // namespace js

#endif // jsion_slots_h__
