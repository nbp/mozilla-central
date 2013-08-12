/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_AliasSets_h
#define ion_AliasSets_h

#include "ion/InlineList.h"

namespace js {
namespace ion {

// Pre-declaration used by MemoryUse.
class MDefinition;

// Each memory group is given an AliasId, each alias identifer is used to
// distinguish a set of memory manipulation than the others.  To each MIR
// Instruction, we have an alias set which describe the action of the intruction
// in terms of memory manipulation.  A MIR instruction can load/store any data
// into the memory described by an AliasId.  Each AliasId is then used by the
// alias analysis to reconstruct the graph of memory dependencies and adding Phi
// nodes to present the memory dependencies.
class AliasSet {
  private:
    uint32_t flags_;

  public:
    enum AliasId {
        None_             = 0,
        ObjectFields      = 1 << 0, // shape, class, slots, length etc.
        Element           = 1 << 1, // A member of obj->elements.
        DynamicSlot       = 1 << 2, // A member of obj->slots.
        FixedSlot         = 1 << 3, // A member of obj->fixedSlots().
        TypedArrayElement = 1 << 4, // A typed array element.
        DOMProperty       = 1 << 5, // A DOM property
        Last              = DOMProperty,
        Any               = Last | (Last - 1),

        NumCategories     = 6,

        // Indicates load or store.
        Store_            = 1 << 31
    };

    // Constructors.
  public:
    AliasSet(uint32_t flags)
      : flags_(flags)
    {
        JS_STATIC_ASSERT((1 << NumCategories) - 1 == Any);
    }

    static AliasSet None() {
        return AliasSet(None_);
    }
    static AliasSet Load(uint32_t flags) {
        JS_ASSERT(flags && !(flags & Store_));
        return AliasSet(flags);
    }
    static AliasSet Store(uint32_t flags) {
        JS_ASSERT(flags && !(flags & Store_));
        return AliasSet(flags | Store_);
    }

    // Predicates.
  public:
    inline bool isNone() const {
        return flags_ == None_;
    }
    inline bool isStore() const {
        return !!(flags_ & Store_);
    }
    inline bool isLoad() const {
        return !isStore() && !isNone();
    }

  public:
    // used by the AliasSetIterator.
    uint32_t flags() const {
        return flags_ & Any;
    }

  public:
    // Combine 2 alias sets.
    inline AliasSet operator |(const AliasSet &other) const {
        return AliasSet(flags_ | other.flags_);
    }

    // Intersect 2 alias sets.
    inline AliasSet operator &(const AliasSet &other) const {
        return AliasSet(flags_ & other.flags_);
    }

    // Exclude the other alias set from the current one.
    inline AliasSet exclude(const AliasSet &other) const {
        return AliasSet(flags_ & ~other.flags_);
    }
};

// Iterator used to iterates over the list of alias ids.
class AliasSetIterator;

// A MemoryUse links a producer with its consumer.  This link must at least been
// shared by one AliasId.  As the same producer-consumer link might frequently
// exists for multiple AliasId at a time, we index the memory use by the largest
// non-empty alias set shared between the consumer and the producer.
//
// Note: The memory operand contains all the data, but we need the memory use
// class such as we can share one memory relation within 2 linked list.
class MemoryOperand : public TempObject, public InlineListNode<MemoryOperand>
{
    MDefinition *producer_; // MDefinition that is being used.
    MDefinition *consumer_; // The node that is using this operand.
    AliasSet intersect_;    // Largest common alias set.

    MemoryOperand(MDefinition *producer, MDefinition *consumer, const AliasSet &intersect)
      : producer_(producer),
        consumer_(consumer),
        intersect_(intersect)
    {
        JS_ASSERT(intersect_ != AliasSet::None());
    }

  public:
    // Set data inside the Memory use.
    void set(MDefinition *producer, MDefinition *consumer, const AliasSet &intersect) {
        producer_ = producer;
        consumer_ = consumer;
        intersect_ = intersect;
        JS_ASSERT(intersect_ != AliasSet::None());
    }

    // Accessors
  public:
    MDefinition *producer() const {
        JS_ASSERT(producer_ != NULL);
        return producer_;
    }
    // TODO: is this function needed ?
    bool hasProducer() const {
        return producer_ != NULL;
    }
    MDefinition *consumer() const {
        JS_ASSERT(consumer_ != NULL);
        return consumer_;
    }
    const AliasSet &intersect() const {
        JS_ASSERT(intersect_ != AliasSet::None());
        return intersect_;
    }
};

// Interface manipulated during the addition of a new memory use.
class MemoryUse : public MemoryOperand, public InlineListNode<MemoryUse>
{
    MemoryUse(MDefinition *producer, MDefinition *consumer, const AliasSet &intersect)
      : MemoryOperand(producer, consumer, intersect)
    {
    }

  public:
    static inline MemoryUse *New(MDefinition *producer, MDefinition *consumer,
                                     const AliasSet &intersect) {
        return new MemoryUse(producer, consumer, intersect);
    }
};

// Note: Most of the operations which are dealing with memory uses are splitting
// and merging memory uses in function of their alias sets. To prevent
// allocating too much temporary memory, most functions have an optional last
// argument which serve as a free-list of memory uses.
class MemoryOperandList : protected InlineList<MemoryOperand>
{
  public:
    typedef InlineList<MemoryOperand> Parent;
    using Parent::begin;
    using Parent::end;
    using Parent::rbegin;
    using Parent::rend;
    using Parent::empty;

    // When we are building the list of dependencies in the alias analysis, we
    // keep a list of operands for all alias sets. This function copy the memory
    // operand list by filtering intersecting alias sets. It initialiaze the
    // consumer inside the resulting operand list.
    void extractDependenciesSubset(const MemoryOperandList &operands, const AliasSet &intersect,
                                   MDefinition *consumer, MemoryOperandList *freeList = NULL)
    {
        // static?
        // TODO
    }

    // Copy unconditionally an operand list such as mutation are not visible on
    // the copied list.
    void copyDependencies(const MemoryOperandList &operands, MemoryOperandList *freeList = NULL)
    {
        extractDependenciesSubset(operands, AliasSet::Store(AliasSet::Any), NULL, freeList);
    }

    // When we are walking instructions during the alias analysis, we want to
    // update the last producer for the specified alias set. This operation
    // might cause the allocation or fusion of MemoryUse classes.
    void setMutator(const AliasSet &set, MDefinition *mutator,
                    MemoryOperandList *freeList = NULL)
    {
        // TODO: Add an extra argument to serve as a pool of reusable MemoryUse which
        // have been previously allocated.

        // TODO
    }

    // Replace a mutator by another mutator within the list of operands. This
    // implies that the memory uses would be removed from the previous mutator
    // and attached to the new one. After this operation we need to merge/split
    // the memory uses with the current one in the list to maintain the
    // invariants.
    void replaceMutator(const AliasSet &set, MDefinition *mutator,
                        MemoryOperandList *freeList = NULL)
    {
        // TODO
    }
};

class MemoryUseList : protected InlineList<MemoryUse>
{
    friend class MemoryOperandList;

  public:
    typedef InlineList<MemoryUse> Parent;
    using Parent::begin;
    using Parent::end;
    using Parent::rbegin;
    using Parent::rend;
    using Parent::empty;
};

}
}

#endif /* ion_AliasSets_h */
