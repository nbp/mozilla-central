/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ion/AliasAnalysis.h"

#include <stdio.h>

#include "ion/Ion.h"
#include "ion/IonBuilder.h"
#include "ion/IonSpewer.h"
#include "ion/MIR.h"
#include "ion/MIRGraph.h"

using namespace js;
using namespace js::ion;

using mozilla::Array;

namespace {

// Iterates over the flags in an AliasSet.
class AliasSetIterator
{
  private:
    uint32_t flags;
    unsigned pos;

  public:
    AliasSetIterator(AliasSet set)
      : flags(set.flags()), pos(0)
    {
        while (flags && (flags & 1) == 0) {
            flags >>= 1;
            pos++;
        }
    }
    AliasSetIterator& operator ++(int) {
        do {
            flags >>= 1;
            pos++;
        } while (flags && (flags & 1) == 0);
        return *this;
    }
    operator bool() const {
        return !!flags;
    }
    unsigned operator *() const {
        JS_ASSERT(pos < AliasSet::NumCategories);
        return pos;
    }
};

} /* anonymous namespace */

AliasAnalysis::AliasAnalysis(MIRGenerator *mir, MIRGraph &graph)
  : mir(mir),
    graph_(graph),
    loop_(NULL)
{
}

static void
IonSpewDependency(MDefinition *load, MDefinition *store, const char *verb, const char *reason)
{
    if (!IonSpewEnabled(IonSpew_Alias) || !load)
        return;

    fprintf(IonSpewFile, "Load ");
    load->printName(IonSpewFile);
    fprintf(IonSpewFile, " %s on store ", verb);
    store->printName(IonSpewFile);
    fprintf(IonSpewFile, " (%s)\n", reason);
}

static void
IonSpewAliasInfo(const char *pre, MDefinition *ins, const char *post)
{
    if (!IonSpewEnabled(IonSpew_Alias))
        return;

    fprintf(IonSpewFile, "%s ", pre);
    ins->printName(IonSpewFile);
    fprintf(IonSpewFile, " %s\n", post);
}

/*
bool
MBasicBlock::initNumAliasSets(uint32_t num, MDefinition *store)
{
    if (slots_.length() < num) {
        if (!slots_.growBy(num - slots_.length()))
            return false;
    } else if (slots_.length() > num) {
        slots_.shrink(slots_.length() - num);
    }

    for (size_t i = 0; i < num; i++)
        slots_[i] = store;

    return true;
}
*/

static MemoryUse *
MemoryUse::New(MDefinition *producer, MDefinition *consumer, const AliasSet &intersect,
               MemoryUseList *freeList)
{
    if (!freeList || freeList->empty())
        return new MemoryUse(producer, consumer, intersect);

    MemoryUse *use = freeList->popFront();
    use->set(producer, consumer, intersect);
    return use;
}

MemoryOperandList::MemoryOperandList()
{
}

void
MemoryOperandList::insertMemoryUse(MemoryUse *use)
{
    pushBack(use);

    // If this memory use is the operand of an instruction then attach at among
    // the memory use list of the producer.
    if (use->consumer()) {
        use->producer()->memUses()->pushBack(use);
        // TODO: Assert that after this operation both the producer and the
        // consumer has only one use/operand with this alias set, and that no other
        // alias set use the same couple (producer, consumer), in which case
        // this means that we have a merge issue.
    }
}

// Should probably find a uniq name, and parametrized with the iterator to be used in the next function.
// This function sounds more like unlink MemoryUse.
MemoryUseList::iterator
MemoryUseList::removeAliasingMemoryUse(const AliasSet &set, MemoryUseList::iterator it,
                                       MemoryUseList *freeList)
{
    // Substract the given alias set from the memory use alias set.
    AliasSet newIntersect = it->intersect().exclude(set);
    if (newIntersect != AliasSet::None()) {
        it->setIntersect(newIntersect);
        it++;
        return it;
    }

    // Remove it from both the list of operands and the list of uses.
    MemoryUse *use = static_cast<MemoryUse *>(*it);
    remove(use);
    JS_ASSERT(use->consumer());
    it = use->producer()->memUses()->removeAt(it);

    // Add it to the freeList.
    if (freeList)
        freeList->pushFront(use);

    return it;
}

void
MemoryOperandList::removeAliasingMemoryUse(const AliasSet &set, MemoryUseList *freeList)
{
    for (MemoryOperandList::iterator it = operands.begin(); it != operands.end(); ) {
        // Substract the given alias set from the memory use alias set.
        AliasSet newIntersect = it->intersect().exclude(set);
        if (newIntersect != AliasSet::None()) {
            it->setIntersect(newIntersect);
            it++;
            continue;
        }

        // Remove it from both the list of operands and the list of uses.
        MemoryUse *use = static_cast<MemoryUse *>(*it);
        it = removeAt(it);
        if (use->consumer())
            use->producer()->memUses()->remove(use);

        // Add it to the freeList.
        if (freeList)
            freeList->pushFront(use);
    }
}

void
MemoryOperandList::extractDependenciesSubset(const MemoryOperandList &operands,
                                             const AliasSet &set,
                                             MDefinition *consumer,
                                             MemoryUseList *freeList)
{
    for (MemoryOperandList::iterator it = operands.begin(); it != operands.end(); it++) {
        AliasSet intersect = it->intersect() & aliasSet;
        if (intersect == AliasSet::None())
            continue;

        IonSpewDependency(consumer, it->producer, "depends", "");
        MemoryUse *use = MemoryUse::New(it->producer, consumer, intersect, freeList);
        insertMemoryUse(use);
    }
}

MemoryUseList::iterator
MemoryOperandList::replaceProducer(const AliasSet &set, MDefinition *producer,
                                   MemoryOperandList::iterator it, MemoryUseList *freeList)
{
    // 1. Remove all included sets.
    MemoryUse *use = static_cast<MemoryUse *>(*it);
    MDefinition *consumer = use->consumer();
    it = removeAliasingMemoryUse(set, it, freeList);

    // 2. Try to extend a memory use which has the same producer.
    MemoryUseList *uses = consumer->memOperands();
    for (MemoryUseList::iterator it = uses->begin(); it != uses->end(); it++) {
        if (it->producer() == producer) {
            it->setIntersect(it->intersect() | set);
            return;
        }
    }

    // 3. Cannot extend any, then create a new memory use.
    MemoryUse *use = MemoryUse::New(producer, consumer, set, freeList);
    insertMemoryUse(use);
}

AliasSet
MemoryOperandList::findMatchingSubset(const AliasSet &set, MDefinition *producer)
{
    for (MemoryOperandList::iterator it = begin(); it != end(); it++) {
        if (it->producer() == producer)
            return set & it->intersect();
    }

    return AliasSet::None();
}

MDefinition *
MemoryOperandList::getUniqProducer(const AliasSet &set)
{
    for (MemoryOperandList::iterator it = operands.begin(); it != operands.end(); it++) {
        AliasSet intersect = it->intersect() | set;
        if (intersect == AliasSet::None())
            continue;

        // The set must be fully covered by the MemoryOperand alias
        // set. Otherwise we will have multiple producers for the given alias
        // set.
        JS_ASSERT(set.exclude(it->intersect()) == AliasSet::None());
        return it->producer();
    }

    return NULL;
}

// Copied from Range Analysis.cpp
static bool
IsDominatedUse(MBasicBlock *block, MUse *use)
{
    MNode *n = use->consumer();
    bool isPhi = n->isDefinition() && n->toDefinition()->isPhi();

    if (isPhi)
        return block->dominates(n->block()->getPredecessor(use->index()));

    return block->dominates(n->block());
}

// Copied & Modified from Range Analysis.cpp
/*
static void
ReplaceDominatedMemoryUsesWith(uint32_t aliasSetId, MDefinition *orig,
                               MDefinition *dom, MBasicBlock *block)
{
    for (MUseIterator i(orig->memUsesBegin()); i != orig->memUsesEnd(); ) {
        if (i->index() != aliasSetId) {
            i++;
            continue;
        }

        if (i->consumer() != dom && IsDominatedUse(block, *i))
            i = i->consumer()->toDefinition()->replaceAliasSetDependency(i, dom);
        else
            i++;
    }
}
*/

static void
ReplaceDominatedMemoryUses(AliasSet &set, MDefinition *orig,
                           MDefinition *dom, MBasicBlock *block,
                           MemoryUseList *freeList)
{
    JS_ASSERT(orig != dom);

    // Replace any memory use.
    MemoryUseList *uses = orig->memUses();
    for (MemoryUseList it = uses->begin(); it != uses->end(); ) {
        JS_ASSERT(it->producer() == orig);
        AliasSet intersect = it->intersect() & set;
        MDefinition *consumer = it->consumer();
        if (intersect == AliasSet::None() || consumer != dom ||
            !IsDominatedUse(block, consumer))
        {
            it++;
            continue;
        }

        it = it->consumer()->memOperands()->replaceProducer(intersect, dom, it, freeList);
    }

    // Replace any use from a memory Phi.
    for (MUseIterator it(orig->usesBegin()); it != orig->usesEnd(); ) {
        MDefinition *consumer = it->consumer();
        if (consumer != dom || !consumer->isPhi() || consumer->toPhi()->isMemory() ||
            !IsDominatedUse(block, consumer))
        {
            it++;
            continue;
        }

        it = consumer->replaceOperand(it, dom);
    }
}

MemoryOperandList *
MBasicBlock::getEntryMemoryOperands()
{
    return memOperands_;
}

void
MBasicBlock::setEntryMemoryOperands(MemoryOperandList *operands)
{
    memOperands_ = operands;
}

void
MBasicBlock::addAliasSetPhi(MPhi *phi)
{
    bool updatePredecessors = phis_.empty();
    addPhi(phi);

    if (!updatePredecessors)
        return;

    for (size_t i = 0; i < numPredecessors(); i++)
        getPredecessor(i)->setSuccessorWithPhis(this, i);
}

/*
void
MBasicBlock::setAliasSetStore(uint32_t aliasSetId, MDefinition *store)
{
    slots_[aliasSetId] = store;
}

MDefinition *
MBasicBlock::getAliasSetStore(uint32_t aliasSetId)
{
    return slots_[aliasSetId];
}
*/

MDefinition *
MDefinition::dependency() const {
    MDefinition *def = NULL;
    for (const MUse *it = memOperands_.begin(); it < memOperands_.end(); it++) {

        if (def) {
            // As we do not renumber the inserted phi, we work around that by
            // checking if the producers are phis.
            if (it->producer()->isPhi() && !def->isPhi() &&
                it->producer()->block()->id() == def->block()->id())
            {
                continue;
            }

            if (it->producer()->id() < def->id())
                continue;
        }

        def = it->producer();
    }
    return def;
}

/*
MUseIterator
MDefinition::replaceAliasSetDependency(MUseIterator use, MDefinition *def)
{
    JS_ASSERT(def != NULL);

    uint32_t aliasSetId = use->index();
    MDefinition *prev = use->producer();

    JS_ASSERT(prev == getAliasSetDependency(aliasSetId));
    JS_ASSERT(use->consumer() == this);

    if (prev == def)
        return use;

    MUseIterator result(prev->memUses_.removeAt(use));

    // Set the operand by reusing the current index in the list of memory
    // operands.
    MUse *it = memOperands_.begin();
    for (; ; it++) {
        JS_ASSERT(it != memOperands_.end());
        if (it->index() == aliasSetId)
            break;
    }
    it->set(def, this, aliasSetId);
    def->memUses_.pushFront(it);

    return result;
}
*/

bool
MDefinition::setAliasSetDependency(uint32_t aliasSetId, MDefinition *mutator)
{
    MDefinition *prev = getAliasSetDependency(aliasSetId);
    if (prev != NULL) {
        if (prev == mutator)
            return true;

        // Set the operand by reusing the current index in the list of memory
        // operands.
        MUse *it = memOperands_.begin();
        for (; ; it++) {
            JS_ASSERT(it != memOperands_.end());
            if (it->index() == aliasSetId)
                break;
        }
        JS_ASSERT(it->producer() == prev);
        it->set(mutator, this, aliasSetId);
        mutator->memUses_.pushFront(it);

        return true;
    }

    // We need to be careful with vector of MUse as resizing them implies that
    // we need to remove them from their original location and add them back
    // after they are copied into a larger array.
    uint32_t index = memOperands_.length();
    bool performingRealloc = !memOperands_.canAppendWithoutRealloc(1);

    // Remove all MUses from all use lists, in case realloc() moves.
    if (performingRealloc) {
        for (uint32_t i = 0; i < index; i++) {
            MUse *use = &memOperands_[i];
            use->producer()->memUses_.remove(use);
        }
    }

    // Create the last MUse.
    if (!memOperands_.append(MUse(mutator, this, aliasSetId)))
        return false;

    // Add the back in case of realloc
    if (performingRealloc) {
        for (uint32_t i = 0; i < index; i++) {
            MUse *use = &memOperands_[i];
            use->producer()->memUses_.pushFront(use);
        }
    }

    // Add the last MUse in the list of uses of the mutator.
    mutator->memUses_.pushFront(&memOperands_[index]);

    return true;
}

MDefinition *
MDefinition::getAliasSetDependency(uint32_t aliasSetId)
{
    for (MUse *it = memOperands_.begin(); it != memOperands_.end(); it++) {
        if (it->index() == aliasSetId)
            return it->producer();
    }

    return NULL;
}

// Add the result of the current block into the destination block. Add Phi if
// another block already added a different input to the destination block.
static bool
InheritEntryAliasSet(MIRGraph &graph, AliasSet set, /*uint32_t aliasSetId,*/
                     MBasicBlock *current, MBasicBlock *dest,
                     MDefinition *initial, MDefinition *added,
                     MemoryUseList *freeList)
{
    MDefinition *last = dest->getAliasSetStore(aliasSetId);
    size_t currentId = current->id();

    size_t predIndex = 0;
    size_t visitedPred = 0;
    for (size_t p = 0; p < dest->numPredecessors(); p++) {
        MBasicBlock *pred = dest->getPredecessor(p);
        if (pred == current) {
            predIndex = p;
            continue;
        }

        visitedPred += pred->id() < currentId;
    }

    if (!visitedPred) {
        // dest->setAliasSetStore(aliasSetId, added);
        dest->getEntryMemoryOperands()->setProducer(aliasSetId, added, freeList);
        return true;
    }

    if (added == last)
        return true;

    if (!last->isPhi() || last->block() != dest) {
        MPhi *phi = MPhi::New(aliasSetId);
        if (!phi)
            return false;
        phi->setResultType(MIRType_None);
        phi->setMemory();
        phi->reserveLength(dest->numPredecessors());
        for (size_t p = 0; p < dest->numPredecessors(); p++) {
            MBasicBlock *pred = dest->getPredecessor(p);
            if (pred->id() < currentId)
                phi->addInput(last);
            else if (pred == current)
                phi->addInput(added);
            else
                phi->addInput(initial);
        }
        dest->addAliasSetPhi(phi);
        dest->setAliasSetStore(aliasSetId, phi);

        // If the block has already been visited, then we need to replace all
        // dominated uses by the phi.  This case happen for loop back-edges.  We
        // also need to update the inherited entries of the successors of
        // dominated blocks to handle outer loops.
        if (current->id() >= dest->id()) {
            ReplaceDominatedMemoryUsesWith(aliasSetId, last, phi, dest);

            for (ReversePostorderIterator block(graph.rpoBegin(dest)); *block != current; block++) {
                if (!dest->dominates(*block))
                    continue;

                for (size_t s = 0; s < block->numSuccessors(); s++) {
                    MBasicBlock *succ = block->getSuccessor(s);
                    InheritEntryAliasSet(graph, aliasSetId, *block, succ, initial, phi);
                }
            }
        }

        return true;
    }

    MPhi *phi = last->toPhi();
    phi->replaceOperand(predIndex, added);
    return true;
}

static bool
MergeProducers(MIRGraph &graph, MemoryOperandList &stores,
               MBasicBlock *current, MBasicBlock *succ,
               MemoryUseList *freeList)
{
    MemoryOperandList *sStores = succ->getEntryMemoryOperands();

    // The successor has not been visited yet.  Just copy the current alias
    // set into the block entry.
    if (sStores->empty()) {
        sStores->copyDependencies(stores);
        return;
    }

    // Store the result of the merge between the successor entry and the
    // current predecessor.
    MemoryOperandList result;
    bool hasNewPhi = false;

    size_t currentId = current->id();
    for (MemoryOperandList::iterator i = stores.begin; i != stores.end(); i++) {
        MDefinition *added = i->producer();

        for (MemoryOperand *j = sStores.begin; j != sStores.end(); j++) {
            AliasSet intersect = i->intersect() & j->intersect();
            if (intersect == AliasSet::None())
                continue;

            MDefinition *curr = j->producer();
            if (curr == added) {
                result.setProducer(curr, intersect, freeList);
                continue;
            }

            // When the current value of the block and the new value to be
            // inserted differ, we have to introduce a phi to account for the
            // disjunction between the 2 operands.
            //
            // If the current value is already a Phi node, then we can reuse it
            // once. When we reuse a Phi, we mutate one of its operand. As an
            // alias set can span on multiple memory area, We need this remember
            // if a phi has been mutated, such as we can duplicate the phi if we
            // need to split its alias set.
            if (!curr->isPhi() || curr->block() != succ || curr->toPhi()->isMutated()) {
                MPhi *phi = MPhi::New(uint32_t(-1));
                if (!phi)
                    return false;
                phi->setResultType(MIRType_None);
                phi->setMemory();
                phi->reserveLength(succ->numPredecessors());

                // Initialize the new Phi with either the data of the previously
                // mutated Phi or with the value which was present before the
                // phi node.
                if (curr->isMutated()) {
                    JS_ASSERT(curr->isPhi() && curr->block() == succ);
                    for (size_t p = 0; p < succ->numPredecessors(); p++)
                        phi->addInput(curr->getOperand(p));
                } else {
                    for (size_t p = 0; p < succ->numPredecessors(); p++)
                        phi->addInput(curr);
                }

                // Add the newly created Phi node into the basic block of the successor.
                succ->addAliasSetPhi(phi);
                curr = phi;
                hasNewPhi = true;
            }

            MPhi *phi = curr->toPhi();
            JS_ASSERT(curr->block() == succ && !phi->isMutated());

            // Find the location of the current block in the list of
            // predecessors of its successor.  This is essential when we
            // need to update a Phi.
            size_t predIndex = 0;
            for (size_t p = 0; p < succ->numPredecessors(); p++) {
                MBasicBlock *pred = succ->getPredecessor(p);
                if (pred == current) {
                    predIndex = p;
                    break;
                }
            }

            phi->replaceOperand(predIndex, added);
            phi->setMutated();
            result.setProducer(phi, intersect, freeList);

            MDefinition *merge = phi;
        }
    }

    // Replace the current list by the computed results.
    MemoryOperandList previous;
    sStores->moveListInto(previous);
    result.moveListInto(*sStores);

    // If we have introduced new Phi instructions in blocks which have already
    // been processed, then we need update all dominated instructions, as well
    // as all successors of dominated blocks.
    if (!hasNewPhi || succ->id() > current->id()) {

        // Iterate over the result, as the result is necessary more fragmented
        // than the original one. We have no way to remove fragmentation while
        // merging, as we necessary have to introduce Phi nodes to account for
        // the origin.
        for (MemoryOperandList::iterator op = sStores->begin(); op != sStores->end(); op++) {
            MDefinition *prev = previous.getUniqProducer(op->intersect());
            JS_ASSERT(prev != NULL);
            if (prev == op->producer())
                continue;
            ReplaceDominatedMemoryUses(op->intersect(), prev, op->producer(), succ);
        }

        // TODO: do some alpha renaming.

        // Update references of the original value inside the successors of the
        // visited blocks.  Such as we do not leave a reference which does not
        // take the newly created phis into account.
        for (ReversePostorderIterator block2(graph.rpoBegin(dest)); *block2 != current; block2++) {
            if (!dest->dominates(*block2))
                continue;

            for (size_t s = 0; s < block2->numSuccessors(); s++) {
                MBasicBlock *succ2 = block2->getSuccessor(s);
                MemoryOperandList *sStores2 = succ2->getEntryMemoryOperands();

                // For each differences, try to update the entry information if
                // it has not changed.
                //
                // Note: This assume that there is no way for a dominated block
                // to get the same producer back if it has been changed in the
                // middle. This property hold has long as we do not move a value
                // from one alias set to another. If we start doing so, then we
                // might want to create a fake MIR instruction to hold the entry
                // memory operand of each basic block, in which case this
                // operation would be implicitly handled by
                // ReplaceDominatedMemoryUses done before these loops.
                MemoryOperandList::iterator op;
                for (op = sStores->begin(); op != sStores->end(); op++) {
                    MDefinition *prev = previous.getUniqProducer(op->intersect());
                    JS_ASSERT(prev != NULL);
                    if (prev == op->producer())
                        continue;

                    AliasSet intersect = sStores2->findMatchingSubset(op->intersect(), prev);
                    sStores2->setProducer(intersect, op->producer(), freeList);
                }
            }
        }
    }

    // TODO: Collect the previous memory use entries and add them to the free list.
}

// This pass annotates every load instruction with the last store instruction
// on which it depends. The algorithm is optimistic in that it ignores explicit
// dependencies and only considers loads and stores.
//
// Loads inside loops only have an implicit dependency on a store before the
// loop header if no instruction inside the loop body aliases it. To calculate
// this efficiently, we maintain a list of maybe-invariant loads and the combined
// alias set for all stores inside the loop. When we see the loop's backedge, this
// information is used to mark every load we wrongly assumed to be loop invaraint as
// having an implicit dependency on the last instruction of the loop header, so that
// it's never moved before the loop header.
//
// The algorithm depends on the invariant that both control instructions and effectful
// instructions (stores) are never hoisted.
bool
AliasAnalysis::analyze()
{
    MemoryUseList freeList;
    Vector<MemoryOperandList, SystemAllocPolicy> blocksOperands;
    if (!blocksOperands.reserve(graph_.numBlocks()))
        return false;

    const size_t numAliasSets = AliasSet::NumCategories;

    // This vector is copied from the basic block alias sets at the beginning of
    // the block visit and merged into the successors of the basic block once we
    // reach the end of it.
    MemoryOperandList stores;

    // Re-use the stack used to build the MIR Graph and their Phi nodes as a
    // model of memory manipulated within each alias set.  Initialize all basic
    // blocks with the first instruction.
    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++)
        block->setEntryMemoryOperands(&blocksOperands[block->id()]);

    // Type analysis may have inserted new instructions. Since this pass depends
    // on the instruction number ordering, all instructions are renumbered.
    // We start with 1 because some passes use 0 to denote failure.
    uint32_t newId = 1;

    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Alias Analysis (main loop)"))
            return false;

        // Load the previous stores from the basic block slots.
        MemoryOperandList *entry = block->getEntryMemoryOperands();
        if (entry->empty()) {
            // This block has not been initialized, so this is an entry block.
            JS_ASSERT(block->numPredecessors())

            // Thus we should take the first instruction of this block as assume
            // it alias everything.
            MDefinition *firstIns = block->begin();
            entry->setProducer(AliasSet::Store(AliasSet::Any), firstIns, freeList);
        }
        stores.copyDependencies(*entry);

        // Iterate over the definitions of the block and update the array with
        // the latest store for each alias set.
        for (MDefinitionIterator def(*block); def; def++) {
            def->setId(newId++);

            AliasSet set = def->getAliasSet();
            if (set.isNone())
                continue;

            // Mark loads & stores dependent on the previous stores.  All the
            // stores on the same alias set would form a chain.
            def->memOperands()->extractDependenciesSubset(stores, set, def, freeList);

            if (set.isStore()) {
                // Update the working list of operands with the current store.
                stores.setProducer(set, def, freeList);

                if (IonSpewEnabled(IonSpew_Alias)) {
                    fprintf(IonSpewFile, "Processing store ");
                    def->printName(IonSpewFile);
                    fprintf(IonSpewFile, " (flags %x)\n", set.flags());
                }
            }
        }

        // Write the current memory status back into the succesors of the
        // current basic blocks.  If needed, we will add a memory Phi node to
        // merge the memory dependency in case we had multiple stores from
        // different branches.
        for (size_t s = 0; s < block->numSuccessors(); s++) {
            MBasicBlock *succ = block->getSuccessor(s);
            MergeProducers(graph_, stores, *block, succ, firstIns);
            /*
            for (size_t i = 0; i < numAliasSets; i++)
                InheritEntryAliasSet(graph_, i, *block, succ, firstIns, stores[i]);
            */
        }
    }

    return true;
}

void
ion::RemoveMemoryPhis(MIRGraph &graph)
{
    for (ReversePostorderIterator block(graph.rpoBegin()); block != graph.rpoEnd(); block++) {
        for (MPhiIterator it = block->phisBegin(); it != block->phisEnd(); ) {
            if (it->isMemory())
                it = block->discardPhiAt(it);
            else
                it++;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// Might alias functions for MIR Nodes
///////////////////////////////////////////////////////////////////////////////

bool
MDefinition::mightAlias(MDefinition *store)
{
    // Return whether this load may depend on the specified store, given
    // that the alias sets intersect. This may be refined to exclude
    // possible aliasing in cases where alias set flags are too imprecise.
    JS_ASSERT(!isEffectful() && store->isEffectful());
    JS_ASSERT(getAliasSet().flags() & store->getAliasSet().flags());
    return true;
}

bool
MLoadFixedSlot::mightAlias(MDefinition *store)
{
    if (store->isStoreFixedSlot() && store->toStoreFixedSlot()->slot() != slot())
        return false;
    return true;
}

bool
MLoadSlot::mightAlias(MDefinition *store)
{
    if (store->isStoreSlot() && store->toStoreSlot()->slot() != slot())
        return false;
    return true;
}

bool
MGetPropertyPolymorphic::mightAlias(MDefinition *store)
{
    // Allow hoisting this instruction if the store does not write to a
    // slot read by this instruction.

    if (!store->isStoreFixedSlot() && !store->isStoreSlot())
        return true;

    for (size_t i = 0; i < numShapes(); i++) {
        Shape *shape = this->shape(i);
        if (shape->slot() < shape->numFixedSlots()) {
            // Fixed slot.
            uint32_t slot = shape->slot();
            if (store->isStoreFixedSlot() && store->toStoreFixedSlot()->slot() != slot)
                continue;
            if (store->isStoreSlot())
                continue;
        } else {
            // Dynamic slot.
            uint32_t slot = shape->slot() - shape->numFixedSlots();
            if (store->isStoreSlot() && store->toStoreSlot()->slot() != slot)
                continue;
            if (store->isStoreFixedSlot())
                continue;
        }

        return true;
    }

    return false;
}
