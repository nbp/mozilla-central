/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_AliasSets_h
#define ion_AliasSets_h

#include "js/HashTable.h"

#include "ion/BitSet.h"
#include "ion/InlineList.h"
#include "ion/IonAllocPolicy.h"

#ifdef DEBUG
# define CRAZY_DEBUG 1
#endif

namespace js {
namespace ion {

// Pre-declaration used by MemoryUse.
class MDefinition;
class MemoryOperandList;
class MemoryUseList;
class AliasAnalysisCache;

// An alias identifier is a unique identifier which can be compared with any
// other and which must produce a unique valueHash to identify it. It represents
// one indistinguishable kind of memory.
//
// As soon as all alias identifiers are registered, any alias identifier can be
// used in an alias set.
class AliasId
{
  public:
    enum Category {
        ObjectFields      = 1 << 0, // shape, class, slots, length etc.
        Element           = 1 << 1, // A member of obj->elements.
        DynamicSlot_      = 1 << 2, // A member of obj->slots.
        FixedSlot_        = 1 << 3, // A member of obj->fixedSlots().
        TypedArrayElement = 1 << 4, // A typed array element.
        DOMProperty       = 1 << 5, // A DOM property
        Last              = DOMProperty,
        Any               = Last | (Last - 1),

        NumCategories     = 6,

        // Indicates load or store. This is not a category
        HasSlot_          = 1 << 31
    };

  private:
    AliasId(Category cat, size_t slot)
      : cat_(cat | HasSlot_),
        slot_(slot)
    {
    }

  public:
    AliasId(Category cat)
      : cat_(cat),
        slot_(0)
    {
    }

    static AliasId FixedSlot() {
        return AliasId(FixedSlot_);
    }
    static AliasId FixedSlot(size_t slot) {
        return AliasId(FixedSlot_, slot);
    }

    static AliasId DynamicSlot() {
        return AliasId(DynamicSlot_);
    }
    static AliasId DynamicSlot(size_t slot) {
        return AliasId(DynamicSlot_, slot);
    }

  public:
    virtual HashNumber valueHash() const {
        HashNumber h = cat_;
        h += slot_ << NumCategories;
        return h;
    }

    virtual bool operator==(const AliasId &other) const {
        return cat_ == other.cat_;
    }

    bool isCategory() const {
        return (cat_ & Any) == cat_;
    }
    Category categories() const {
        return Category(cat_ & Any);
    }

  private:
    // Category under which this alias identifier is mapped. This is used to
    // alias a category at once, without having a list of all elements in it.
    size_t cat_;

    // Index of the fixed / dynamic slot.
    size_t slot_;
};

// Map every alias id to its corresponding alias set.
class AliasSetCache
{
  public:
    struct AliasSetOp {
        enum Operation {
            ALIASSET_INTERSECT,
            ALIASSET_UNION,
            ALIASSET_EXCLUDE
        };

        Operation op;
        const BitSet *lhs;
        const BitSet *rhs;

        AliasSetOp(Operation op, const BitSet *lhs, const BitSet *rhs)
          : op(op), lhs(lhs), rhs(rhs)
        { }
    };

  protected:
    struct AliasIdHasher
    {
        typedef AliasId Lookup;
        typedef AliasId Key;
        static HashNumber hash(const Lookup &ins) {
            return ins.valueHash();
        }

        static bool match(const Key &k, const Lookup &l) {
            return k == l;
        }
    };

    struct AliasSetHasher
    {
        typedef BitSet *Lookup;
        typedef BitSet *Key;
        static HashNumber hash(const Lookup &ins) {
            if (!ins)
                return HashNumber(0);

            uint32_t *i = ins->raw();
            uint32_t *e = i + ins->rawLength();
            HashNumber hash = 0;
            uint32_t leadingZero = 0;
            for (; i != e; i++) {
                // This is not exactly leading zero, but it when there are only
                // a few bit sets, and it allow to disntinguish between bit set
                // with only one bit set.
                if (!hash)
                    leadingZero++;
                hash = hash ^ *i;
            }

            // Conditionally add the leading zero such as we can easily identify
            // the empty set to a zero-hash.
            if (hash)
                hash += leadingZero;

            return hash;
        }

        static bool match(const Key &k, const Lookup &l) {
            if (k == l)
                return true;
            if ((!k && l) || (k && !l))
                return false;

            uint32_t *i = k->raw();
            uint32_t *j = l->raw();
            uint32_t *e = i + k->rawLength();
            for (; i != e; i++, j++) {
                if (*i != *j)
                    return false;
            }

            return true;
        }
    };

    struct AliasSetOpHasher
    {
        typedef AliasSetOp Lookup;
        typedef AliasSetOp Key;
        static HashNumber hash(const Lookup &ins) {
            return ins.op ^ reinterpret_cast<size_t>(ins.lhs) ^
                (reinterpret_cast<size_t>(ins.rhs) << 1);
        }

        static bool match(const Key &k, const Lookup &l) {
            return k.op == l.op && k.lhs == l.lhs && k.rhs == l.rhs;
        }
    };

    typedef HashMap<AliasId, BitSet *, AliasIdHasher, IonAllocPolicy> AliasIdMap;
    typedef HashMap<BitSet *, BitSet *, AliasSetHasher, IonAllocPolicy> AliasSetMap;
    typedef HashMap<AliasSetOp, BitSet *, AliasSetOpHasher, IonAllocPolicy> AliasSetOpMap;

  public:
    AliasSetCache()
      : oom_(false),
        nbIndexes_(AliasId::NumCategories),
        any_(NULL),
        temp_(NULL)
    {
    }

    bool oom() {
        return oom_;
    }
    void reportOOM() {
        oom_ = true;
    }

    // Count the number of unique alias ids.
    bool registerId(const AliasId &id);

    // Allocate and fill bit sets of categories and alias ids.
    bool fillCache();

    const BitSet *bitSetOf(const AliasId &id) const {
        if (id.isCategory())
            return bitSetOf(id.categories());
        AliasIdMap::Ptr p = idMap_.lookup(id);
        JS_ASSERT(p.found());
        return p->value;
    }

    const BitSet *bitSetOf(AliasId::Category c) const {
        JS_ASSERT(c);
        size_t catIndex = mozilla::CountTrailingZeroes32(c);
        JS_ASSERT((1 << catIndex) == c);
        return categories_[catIndex];
    }

    const BitSet *any() const {
        JS_ASSERT(any_);
        return any_;
    }

    BitSet *temp() {
        if (!temp_) {
            temp_ = BitSet::New(nbIndexes_);
            if (!temp_)
                reportOOM();
        }
        return temp_;
    }

    typedef typename AliasSetOpMap::AddPtr AliasSetOpCache;
    AliasSetOpCache opCache(const AliasSetOp &op) {
        return opMap_.lookupForAdd(op);
    }
    BitSet *aliasTempResult(const AliasSetOp &op, AliasSetOpCache opPtr) {
        AliasSetMap::AddPtr p = setMap_.lookupForAdd(temp_);
        if (p)
            return p->value;

        BitSet *res = temp_;
        if (!setMap_.add(p, res, res)) {
            reportOOM();
            return NULL;
        }

        JS_ASSERT(!opPtr);
        if (!opMap_.add(opPtr, op, res)) {
            reportOOM();
            return NULL;
        }

        temp_ = NULL;
        return res;
    }

    bool init() {
        return idMap_.init() && setMap_.init() && opMap_.init();
    }

  private:
    // remember if an allocation of an alias set failed.
    bool oom_;

    // Last registered index.
    size_t nbIndexes_;

    // Map alias Ids to bit sets.
    AliasIdMap idMap_;
    AliasSetMap setMap_;
    AliasSetOpMap opMap_;
    BitSet *categories_[AliasId::NumCategories];

    // Bit mask used to cover every alias ids.
    BitSet *any_;
    BitSet *temp_;
};

// Each memory group is given an AliasId, each alias identifer is used to
// distinguish a set of memory manipulation than the others.  To each MIR
// Instruction, we have an alias set which describe the action of the intruction
// in terms of memory manipulation.  A MIR instruction can load/store any data
// into the memory described by an AliasId.  Each AliasId is then used by the
// alias analysis to reconstruct the graph of memory dependencies and adding Phi
// nodes to present the memory dependencies.
class AliasSet
{
  private:
    const BitSet *flags_;

    // Constructors.
  private:
    AliasSet(const BitSet *flags)
      : flags_(flags)
    {
    }

  public:
    AliasSet(const AliasSetCache &sc, const AliasId &id)
      : flags_(sc.bitSetOf(id))
    {
    }

    AliasSet(const AliasSetCache &sc, AliasId::Category cat)
      : flags_(sc.bitSetOf(cat))
    {
    }

    static AliasSet None() {
        return AliasSet(NULL);
    }
    static AliasSet Any(AliasSetCache &sc) {
        return AliasSet(sc.any());
    }

    // Predicates.
  public:
    inline bool isNone() const {
        if (!flags_)
            return true;

        uint32_t *i = flags_->raw();
        uint32_t *e = i + flags_->rawLength();
        uint32_t res = 0;
        for (; i != e; i++)
            res |= *i;
        return res == 0;
    }

    bool isEmptyIntersect(const AliasSet &rhs) const {
        if (!flags_ || !rhs.flags_)
            return true;

        uint32_t *l = flags_->raw();
        uint32_t *r = rhs.flags_->raw();
        uint32_t *e = l + flags_->rawLength();
        uint32_t res = 0;
        for (; !res && l != e; l++, r++)
            res = *l & *r;
        return res == 0;
    }

    bool isSubsetOf(const AliasSet &rhs) const {
        if (!flags_ || !rhs.flags_)
            return isNone();

        uint32_t *l = flags_->raw();
        uint32_t *r = rhs.flags_->raw();
        uint32_t *e = l + flags_->rawLength();
        uint32_t res = 0;
        for (; !res && l != e; l++, r++)
            res = *l &~ *r;
        return res == 0;
    }

  public:
    // Combine 2 alias sets.
    inline AliasSet add(AliasSetCache &sc, const AliasSet &other) const {
        if (!flags_)
            return AliasSet(other.flags_);
        if (!other.flags_)
            return AliasSet(flags_);

        AliasSetCache::AliasSetOp op(AliasSetCache::AliasSetOp::ALIASSET_UNION, flags_, other.flags_);
        AliasSetCache::AliasSetOpCache cache = sc.opCache(op);
        if (cache)
            return AliasSet(cache->value);

        BitSet *b = sc.temp();
        if (!b)
            return None();

        uint32_t *dest = b->raw();
        uint32_t *src1 = flags_->raw();
        uint32_t *src2 = other.flags_->raw();
        uint32_t *end = dest + b->rawLength();
        for (; dest != end; src1++, src2++, dest++)
            *dest = *src1 | *src2;
        b = sc.aliasTempResult(op, cache);
        return AliasSet(b);
    }

    // Intersect 2 alias sets.
    inline AliasSet intersect(AliasSetCache &sc, const AliasSet &other) const {
        if (!flags_ || !other.flags_)
            return AliasSet(NULL);

        AliasSetCache::AliasSetOp op(AliasSetCache::AliasSetOp::ALIASSET_INTERSECT, flags_, other.flags_);
        AliasSetCache::AliasSetOpCache cache = sc.opCache(op);
        if (cache)
            return AliasSet(cache->value);

        BitSet *b = sc.temp();
        if (!b)
            return None();

        uint32_t *dest = b->raw();
        uint32_t *src1 = flags_->raw();
        uint32_t *src2 = other.flags_->raw();
        uint32_t *end = dest + b->rawLength();
        for (; dest != end; src1++, src2++, dest++)
            *dest = *src1 & *src2;
        b = sc.aliasTempResult(op, cache);
        return AliasSet(b);
    }

    // Exclude the other alias set from the current one.
    inline AliasSet exclude(AliasSetCache &sc, const AliasSet &other) const {
        if (!flags_ || !other.flags_)
            return AliasSet(flags_);

        AliasSetCache::AliasSetOp op(AliasSetCache::AliasSetOp::ALIASSET_EXCLUDE, flags_, other.flags_);
        AliasSetCache::AliasSetOpCache cache = sc.opCache(op);
        if (cache)
            return AliasSet(cache->value);

        BitSet *b = sc.temp();
        if (!b)
            return None();

        uint32_t *dest = b->raw();
        uint32_t *src1 = flags_->raw();
        uint32_t *src2 = other.flags_->raw();
        uint32_t *end = dest + b->rawLength();
        for (; dest != end; src1++, src2++, dest++)
            *dest = *src1 &~ *src2;
        b = sc.aliasTempResult(op, cache);
        return AliasSet(b);
    }

  public:
    void printFlags(FILE *fp) const;
};

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
    AliasSet set_;          // Largest common alias set.

#ifdef CRAZY_DEBUG
  public:
    const MemoryOperandList *ownerOList;
    const MemoryUseList *ownerUList;
#endif

  protected:
    MemoryOperand()
      : producer_(NULL),
        consumer_(NULL),
        set_(AliasSet::None())
    { }

  public:
    // Set data inside the Memory use.
    void set(MDefinition *producer, MDefinition *consumer, const AliasSet &set) {
        producer_ = producer;
        consumer_ = consumer;
        set_ = set;
        JS_ASSERT(!set_.isNone());
    }

    void setSet(const AliasSet &set) {
        set_ = set;
        JS_ASSERT(!set_.isNone());
    }

    // Accessors
  public:
    MDefinition *producer() const {
        JS_ASSERT(producer_ != NULL);
        return producer_;
    }
    bool hasConsumer() const {
        return consumer_ != NULL;
    }
    MDefinition *consumer() const {
        JS_ASSERT(consumer_ != NULL);
        return consumer_;
    }
    const AliasSet &set() const {
        JS_ASSERT(!set_.isNone());
        return set_;
    }
};

// Interface manipulated during the addition of a new memory use.
class MemoryUse : public MemoryOperand, public InlineListNode<MemoryUse>
{
  protected:
    friend class TempObjectPool<MemoryUse>;
    MemoryUse()
      : MemoryOperand()
    { }

  public:
    static MemoryUse *
    New(MDefinition *producer, MDefinition *consumer, const AliasSet &set,
        AliasAnalysisCache &aac);
};

// Note: Most of the operations which are dealing with memory uses are splitting
// and merging memory uses in function of their alias sets. To prevent
// allocating too much temporary memory, most functions have an optional last
// argument which serve as a free-list of memory uses.
class MemoryUseList : protected InlineList<MemoryUse>
{
    friend class MemoryUse;
    friend class MemoryOperandList;

  public:
    typedef InlineList<MemoryUse> Parent;
    using Parent::begin;
    using Parent::end;
    using Parent::rbegin;
    using Parent::rend;
    using Parent::empty;
    using Parent::iterator;

    iterator removeAliasingMemoryUse(const AliasSet &set, iterator it,
                                     AliasAnalysisCache &aac);

    // Used by scalar replacement when replacing a load by the memory content
    // introduced by a store. This is used to move the list of uses of a load to
    // the list of uses of the store.
    void moveListInto(MemoryUseList &list, MDefinition *newProducer, MDefinition *oldProducer) {
        JS_ASSERT_IF(!empty(), begin()->producer() == oldProducer);
        for (MemoryUseList::iterator it = begin(); it != end(); it++) {
#ifdef CRAZY_DEBUG
            it->ownerUList = &list;
#endif
            it->set(newProducer, it->consumer(), it->set());
        }
        Parent::moveListInto(list);
    }

    void moveDominatedUsesInto(MemoryUseList &list, MDefinition *newProducer,
                               MDefinition *oldProducer);
};

class MemoryOperandList : protected InlineList<MemoryOperand>
{
    friend class MemoryUse;
    friend class MemoryUseList;

  public:
    typedef InlineList<MemoryOperand> Parent;
    using Parent::begin;
    using Parent::end;
    using Parent::rbegin;
    using Parent::rend;
    using Parent::empty;
    using Parent::iterator;

    // Used by Alias analysis when merging entry list of aliasing stores.
    void moveListInto(MemoryOperandList &list) {
        JS_ASSERT_IF(!empty(), !begin()->hasConsumer());
#ifdef CRAZY_DEBUG
        for (MemoryOperandList::iterator it = begin(); it != end(); it++)
            it->ownerOList = &list;
#endif
        Parent::moveListInto(list);
    }

    // When we are building the list of dependencies in the alias analysis, we
    // keep a list of operands for all alias sets. This function copy the memory
    // operand list by filtering intersecting alias sets. It initialiazes the
    // consumer inside the resulting operand list.
    void extractDependenciesSubset(const MemoryOperandList &operands, const AliasSet &set,
                                   MDefinition *consumer, AliasAnalysisCache &aac);

    // Copy unconditionally an operand list such as mutation are not visible on
    // the copied list.
    void copyDependencies(const MemoryOperandList &operands, AliasAnalysisCache &aac);

    // Replace a producer by another producer within the list of operands of the
    // consumer instruction. This is the analog of replaceOperand.
    void replaceProducer(const AliasSet &set, MDefinition *producer,
                         MDefinition *consumer, AliasAnalysisCache &aac);

    MemoryUseList::iterator
    replaceProducer(const AliasSet &set, MDefinition *producer,
                    MemoryUseList::iterator it, AliasAnalysisCache &aac);

    AliasSet findMatchingSubset(const AliasSet &set, MDefinition *producer,
                                AliasAnalysisCache &aac);


    // When we are walking instructions during the alias analysis, we want to
    // update the last producer for the specified alias set.
    void setProducer(const AliasSet &set, MDefinition *producer, AliasAnalysisCache &aac)
    {
        MDefinition *consumer = NULL;
        replaceProducer(set, producer, consumer, aac);
    }

    // Extract the uniq producer corresponding to an Alias set.  If there is
    // more than one producer or if there is an unknown producer in the alias
    // set, then this function fails with an assertion.
    MDefinition *getUniqProducer(const AliasSet &set, AliasAnalysisCache &aac);

    // This function clear all the dependencies of one instruction and move the
    // memory uses into the free list.
    void clear(AliasAnalysisCache &aac);

  private:
    // Insert a newly created memory use which does not intersect any of the
    // existing alias set, and which does not use the same couple of producer &
    // consumer as another alias set.
    void insertMemoryUse(MemoryUse *use);

    // Remove all MemoryUse which are matching a specific alias set.  This will
    // substract the original alias set of all intersecting MemoryUse and remove
    // the remaining MemoryUse which have an empty alias set.  All removed
    // MemoryUse would be added to the freeList.
    void removeAliasingMemoryUse(const AliasSet &set, AliasAnalysisCache &aac);
};

// Track memory mutations and their uses/overwrite within the control-flow
// graph. A separated list of uses is made to avoid coliding with the data-flow,
// and to track memory mutations within an alias set. As we refine the alias
// set with smaller set of definitions, we want to have a sparse use of the
// number of operands where the index of each operand represent the alias set.
struct MemoryDefinition : public TempObject
{
    // Uses of the mutated memory.
    MemoryUseList uses;

    // Definitions which are potentially mutating the memory used by this
    // instruction.
    MemoryOperandList operands;

    // To be able to fold load & stores, we need to identify if they statically
    // alias (property name, shape, slot, type ...) by checking the alias set
    // and if we can prove that they are manipulating the same object. To do so,
    // we compare the value context (object & index) with a representant.
    //
    // An escaping alias set is not considered as a new representant, only
    // precise alias sets are. If multiple precise alias sets are present, then
    // we do not optimize any in case the object might alias each others.
    MDefinition *context;

    // Before moving any memory operation, we need to make sure that the sum of
    // all the modifications would be more efficient. Each operation taken
    // individualy add more cost, but combined they can remove the overall cost.
    //
    // To find if it is worth optimizing or not, we store an estimate of the
    // relative expected cost.
    //
    // This cost corresponds to sum of all acceses which are added minus all
    // accesses which are removed.  It only deals with the local cost of each
    // operations.  A store is assumed to be removed, but will count as added to
    // every escape call which is following.  If a load is removed, then we will
    // need to count every escape call which is preceding.
    double expectation;
};

// Per-Compilation cache.
class AliasAnalysisCache : public TempObject
{
    TempObjectPool<MemoryUse> freeList_;
    AliasSetCache setCache_;

  public:
    MemoryUse *allocate() {
        return freeList_.allocate();
    }
    void free(MemoryUse *use) {
        freeList_.free(use);
    }

    AliasSetCache &sc() {
        return setCache_;
    }
};

}
}

#endif /* ion_AliasSets_h */
