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

AliasAnalysis::AliasAnalysis(MIRGenerator *mir, MIRGraph &graph)
  : mir(mir),
    graph_(graph),
    loop_(NULL)
{
}

MemoryUsePool &
MIRGraph::memUsesFreeList()
{
    if (!memUsesFreeList_)
        memUsesFreeList_ = new MemoryUsePool();
    return *memUsesFreeList_;
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

MemoryUse *
MemoryUse::New(MDefinition *producer, MDefinition *consumer, const AliasSet &intersect,
               MemoryUsePool &freeList)
{
    MemoryUse *use = freeList.allocate();
    use->set(producer, consumer, intersect);
    return use;
}

void
MemoryOperandList::insertMemoryUse(MemoryUse *use)
{
    pushBack(use);
#ifdef CRAZY_DEBUG
    use->ownerOList = this;
    use->ownerUList = NULL;

    // Assert that after this operation both the producer and the consumer has
    // only one use/operand with this alias set, and that no other alias set use
    // the same couple (producer, consumer), in which case this means that we
    // have a merge issue.
    for (MemoryOperandList::iterator i = begin(); i != end(); i++) {
        MemoryOperandList::iterator j = begin(*i);
        j++;
        for (; j != end(); j++) {
            JS_ASSERT(i->producer() != j->producer());
            JS_ASSERT((i->intersect() & j->intersect()).isNone());
        }
    }
#endif

    // If this memory use is the operand of an instruction then attach at among
    // the memory use list of the producer.
    if (use->hasConsumer()) {
        use->producer()->memUses().pushBack(use);
#ifdef CRAZY_DEBUG
        use->ownerUList = &use->producer()->memUses();

        for (MemoryUseList::iterator i = use->ownerUList->begin(); i != use->ownerUList->end(); i++) {
            MemoryUseList::iterator j = use->ownerUList->begin(*i);
            j++;
            for (; j != use->ownerUList->end(); j++)
                JS_ASSERT(i->consumer() != j->consumer());
        }
#endif
    }
}

// Should probably find a uniq name, and parametrized with the iterator to be used in the next function.
// This function sounds more like unlink MemoryUse.
MemoryUseList::iterator
MemoryUseList::removeAliasingMemoryUse(const AliasSet &set, MemoryUseList::iterator it,
                                       MemoryUsePool &freeList)
{
    // Substract the given alias set from the memory use alias set.
    AliasSet newIntersect = it->intersect().exclude(set);
    if (!newIntersect.isNone()) {
        it->setIntersect(newIntersect);
        it++;
        return it;
    }

    // Remove it from both the list of operands and the list of uses.
    MemoryUse *use = static_cast<MemoryUse *>(*it);
    it = removeAt(it);
    use->consumer()->memOperands().remove(use);

    // Add it to the freeList.
#ifdef CRAZY_DEBUG
    use->ownerOList = NULL;
    use->ownerUList = NULL;
#endif
    freeList.free(use);

    return it;
}

void
MemoryOperandList::removeAliasingMemoryUse(const AliasSet &set, MemoryUsePool &freeList)
{
    for (MemoryOperandList::iterator it = begin(); it != end(); ) {
        JS_ASSERT(it->ownerOList == this);

        // Substract the given alias set from the memory use alias set.
        AliasSet newIntersect = it->intersect().exclude(set);
        if (!newIntersect.isNone()) {
            it->setIntersect(newIntersect);
            it++;
            continue;
        }

        // Remove it from both the list of operands and the list of uses.
        MemoryUse *use = static_cast<MemoryUse *>(*it);
        it = removeAt(it);
        if (use->hasConsumer())
            use->producer()->memUses().remove(use);

        // Add it to the freeList.
#ifdef CRAZY_DEBUG
        use->ownerOList = NULL;
        use->ownerUList = NULL;
#endif
        freeList.free(use);
    }
}

void
MemoryOperandList::extractDependenciesSubset(const MemoryOperandList &operands,
                                             const AliasSet &set,
                                             MDefinition *consumer,
                                             MemoryUsePool &freeList)
{
    for (MemoryOperandList::iterator it = operands.begin(); it != operands.end(); it++) {
        JS_ASSERT(it->ownerOList == &operands);
        AliasSet intersect = it->intersect() & set;
        if (intersect.isNone())
            continue;

        IonSpewDependency(consumer, it->producer(), "depends", "");
        MemoryUse *use = MemoryUse::New(it->producer(), consumer, intersect, freeList);
        insertMemoryUse(use);
    }
}

MemoryUseList::iterator
MemoryOperandList::replaceProducer(const AliasSet &set, MDefinition *producer,
                                   MemoryUseList::iterator it, MemoryUsePool &freeList)
{
    JS_ASSERT(!set.isNone());

    // 1. Remove all included sets.
    MemoryUse *use = static_cast<MemoryUse *>(*it);
    MDefinition *consumer = use->consumer();
    it = use->producer()->memUses().removeAliasingMemoryUse(set, it, freeList);

    // 2. Try to extend a memory use which has the same producer.
    JS_ASSERT(&consumer->memOperands() == this);
    for (MemoryOperandList::iterator op = begin(); op != end(); op++) {
        JS_ASSERT(op->ownerOList == this);
        if (op->producer() == producer) {
            op->setIntersect(op->intersect() | set);
            return it;
        }
    }

    // 3. Cannot extend any, then create a new memory use.
    use = MemoryUse::New(producer, consumer, set, freeList);
    insertMemoryUse(use);
    return it;
}

void
MemoryOperandList::replaceProducer(const AliasSet &set, MDefinition *producer,
                                   MDefinition *consumer, MemoryUsePool &freeList)
{
    JS_ASSERT(!set.isNone());

    // 1. Remove all included sets.
    removeAliasingMemoryUse(set, freeList);

    // 2. Try to extend a memory use which has the same producer.
    for (MemoryOperandList::iterator op = begin(); op != end(); op++) {
        JS_ASSERT(op->ownerOList == this);
        if (op->producer() == producer) {
            op->setIntersect(op->intersect() | set);
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
        JS_ASSERT(it->ownerOList == this);
        if (it->producer() == producer)
            return set & it->intersect();
    }

    return AliasSet::None();
}

MDefinition *
MemoryOperandList::getUniqProducer(const AliasSet &set)
{
    for (MemoryOperandList::iterator it = begin(); it != end(); it++) {
        JS_ASSERT(it->ownerOList == this);
        AliasSet intersect = it->intersect() & set;
        if (intersect.isNone())
            continue;

        // The set must be fully covered by the MemoryOperand alias
        // set. Otherwise we will have multiple producers for the given alias
        // set.
        JS_ASSERT(set.exclude(it->intersect()).isNone());
        return it->producer();
    }

    return NULL;
}

void
MemoryOperandList::clear(MemoryUsePool &freeList)
{
    while (!empty()) {
        MemoryUse *use = static_cast<MemoryUse *>(popFront());
        JS_ASSERT(use->ownerOList == this);
        if (use->hasConsumer())
            use->producer()->memUses().remove(use);
        freeList.free(use);
    }
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

static void
ReplaceDominatedMemoryUses(const AliasSet &set, MDefinition *orig,
                           MDefinition *dom, MBasicBlock *block,
                           MemoryUsePool &freeList)
{
    JS_ASSERT(orig != dom);

    // Replace any memory use.
    MemoryUseList &uses = orig->memUses();
    for (MemoryUseList::iterator it = uses.begin(); it != uses.end(); ) {
        JS_ASSERT(it->ownerUList == &uses);
        JS_ASSERT(it->producer() == orig);
        AliasSet intersect = it->intersect() & set;
        MDefinition *consumer = it->consumer();
        JS_ASSERT(!consumer->isPhi());
        if (intersect.isNone() || consumer == dom ||
            !block->dominates(consumer->block()))
        {
            it++;
            continue;
        }

        it = consumer->memOperands().replaceProducer(intersect, dom, it, freeList);
    }

    // Replace any use from a memory Phi.
    for (MUseIterator it(orig->usesBegin()); it != orig->usesEnd(); ) {
        // The instruction might 
        if (!it->consumer()->isDefinition()) {
            it++;
            continue;
        }

        MDefinition *consumer = it->consumer()->toDefinition();
        if (!consumer->isPhi() || !consumer->toPhi()->isMemory() || consumer == dom ||
            !IsDominatedUse(block, *it))
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

MDefinition *
MDefinition::dependency() const {
    MDefinition *def = NULL;
    if (!mem_)
        return def;

    for (MemoryOperandList::iterator it = memOperands().begin(); it != memOperands().end(); it++) {
        JS_ASSERT(it->ownerOList == &memOperands());
        MDefinition *candidate = it->producer();

        if (def) {
            // As we do not renumber the inserted phi, we work around that by
            // checking if the producers are phis.
            if (candidate->isPhi() && !def->isPhi() &&
                candidate->block()->id() == def->block()->id())
            {
                continue;
            }

            if (candidate->id() < def->id())
                continue;
        }

        def = it->producer();
    }
    return def;
}

static bool
MergeProducers(MIRGraph &graph, MemoryOperandList &stores,
               MBasicBlock *current, MBasicBlock *succ,
               MemoryUsePool &freeList)
{
    MemoryOperandList *succStores = succ->getEntryMemoryOperands();

    // The successor has not been visited yet.  Just copy the current alias
    // set into the block entry.
    if (succStores->empty()) {
        succStores->copyDependencies(stores, freeList);
        return true;
    }

    // Store the result of the merge between the successor entry and the
    // current predecessor.
    MemoryOperandList result;
    size_t nbMutatedPhi = 0;
    bool hasNewPhi = false;

    for (MemoryOperandList::iterator i = stores.begin(); i != stores.end(); i++) {
        JS_ASSERT(i->ownerOList == &stores);
        MDefinition *added = i->producer();

        for (MemoryOperandList::iterator j = succStores->begin(); j != succStores->end(); j++) {
            JS_ASSERT(j->ownerOList == succStores);
            AliasSet intersect = i->intersect() & j->intersect();
            if (intersect.isNone())
                continue;

            MDefinition *curr = j->producer();
            if (curr == added) {
                result.setProducer(intersect, curr, freeList);
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
            if (curr->block() != succ || !curr->isPhi() || curr->toPhi()->isMutated()) {
                MPhi *phi = MPhi::New(uint32_t(-1));
                if (!phi)
                    return false;
                phi->setResultType(MIRType_None);
                phi->setMemory();
                phi->reserveLength(succ->numPredecessors());
                phi->setMemoryDefinition(new MemoryDefinition());

                // Initialize the new Phi with either the data of the previously
                // mutated Phi or with the value which was present before the
                // phi node.
                if (curr->isPhi() && curr->toPhi()->isMutated()) {
                    JS_ASSERT(curr->block() == succ);
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
            nbMutatedPhi++;
            result.setProducer(intersect, phi, freeList);
        }
    }

    // The result is complete, remove all temporary Mutated flags added on Phi
    // nodes as they are only used to prevent the reuse of a Phi which has
    // already been reused.
    if (nbMutatedPhi) {
        mozilla::DebugOnly<size_t> nbResetedPhi = 0;
        for (MemoryOperandList::iterator op = result.begin(); op != result.end(); op++) {
            JS_ASSERT(op->ownerOList == &result);
            MDefinition *producer = op->producer();
            if (!producer->isPhi())
                continue;

            MPhi *phi = producer->toPhi();
            if (phi->isMutated()) {
                phi->setNotMutated();
#ifdef DEBUG
                nbResetedPhi += 1;
#endif
            }
        }

        JS_ASSERT(nbResetedPhi == nbMutatedPhi);
    }


    // Replace the current list by the computed results.
    MemoryOperandList previous;
    succStores->moveListInto(previous);
    result.moveListInto(*succStores);

    // If we have introduced new Phi instructions in blocks which have already
    // processed, then we need update all dominated instructions, as well as all
    // entries of any successors of dominated blocks.
    if (!hasNewPhi || succ->id() <= current->id()) {

        // Iterate over the result, as the result is necessary more fragmented
        // than the original one. We have no way to remove fragmentation while
        // merging, as we necessary have to introduce Phi nodes to account for
        // the origin.
        for (MemoryOperandList::iterator op = succStores->begin(); op != succStores->end(); op++) {
            JS_ASSERT(op->ownerOList == succStores);
            MDefinition *prev = previous.getUniqProducer(op->intersect());
            JS_ASSERT(prev != NULL);
            if (prev == op->producer())
                continue;
            ReplaceDominatedMemoryUses(op->intersect(), prev, op->producer(), succ, freeList);
        }

        // Update references of the original value inside the successors of the
        // visited blocks.  Such as we do not leave a reference which does not
        // take the newly created phis into account.
        ReversePostorderIterator domBlock(graph.rpoBegin(succ));
        for (; *domBlock != current; domBlock++) {

            // All blocks might not be dominated if the origin of the backedge
            // is coming from a branch within the loop.
            if (!succ->dominates(*domBlock))
                continue;

            for (size_t s = 0; s < domBlock->numSuccessors(); s++) {
                MBasicBlock *domSucc = domBlock->getSuccessor(s);
                MemoryOperandList *domSuccStores = domSucc->getEntryMemoryOperands();

                // For each differences, try to update the entry information if
                // it has not changed.
                MemoryOperandList::iterator op = succStores->begin();
                for (; op != succStores->end(); op++) {
                    JS_ASSERT(op->ownerOList == succStores);

                    // We can always find a unique producer in the previous list
                    // because the merged process can only add more
                    // fragmentation by adding Phis with a smaller intersection
                    // subsets.
                    MDefinition *prev = previous.getUniqProducer(op->intersect());
                    JS_ASSERT(prev != NULL);

                    // If there is no modification, then skip the update
                    // process.
                    if (prev == op->producer())
                        continue;

                    // Look for some inherited mutators in the list of producers
                    // of one the successor of the dominated block.  If we find
                    // some inherited property for the updated subset, we will
                    // substiture it by the new value.
                    //
                    // Note: This assume that there is no way for a dominated
                    // block to get the same producer back if it has been
                    // changed in the middle. This property hold has long as we
                    // do not move a value from one alias set to another.
                    //
                    // Side note: If we start doing so, then we might want to
                    // create a fake MIR instruction to hold the entry memory
                    // operand of each basic block, in which case this operation
                    // would be implicitly handled by ReplaceDominatedMemoryUses
                    // done before these loops.
                    AliasSet intersect = domSuccStores->findMatchingSubset(op->intersect(), prev);
                    if (intersect.isNone())
                        continue;

                    domSuccStores->setProducer(intersect, op->producer(), freeList);
                }
            }
        }
    }

    // Collect the previous memory use entries and add them to the free list.
    previous.clear(freeList);

    return true;
}

bool
AliasAnalysis::clear()
{
    MemoryUsePool &freeList = graph_.memUsesFreeList();

    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Alias Analysis (clean)"))
            return false;

        for (MPhiIterator phi = block->phisBegin(); phi != block->phisEnd(); ) {
            if (!phi->isMemory()) {
                phi++;
                continue;
            }

            phi = block->discardPhiAt(phi);
        }

        for (MDefinitionIterator def(*block); def; def++) {
            if (!def->memOperands().empty())
                def->memOperands().clear(freeList);
        }
    }

    return true;
}

// This pass annotates every load instruction with the last store instructions
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
    MemoryUsePool &freeList = graph_.memUsesFreeList();

    // Allocate the sentinel of the list for the entry of each basic
    // blocks. These behaves as the basic block stacks slots except that they
    // are used to represent the state of the memory at the entry of every basic
    // block.
    Vector<MemoryOperandList, 0, SystemAllocPolicy> blocksOperands;
    if (!blocksOperands.growBy(graph_.numBlocks()))
        return false;

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
            JS_ASSERT(block->numPredecessors() == 0);

            // Thus we take the first instruction of this block as assume it
            // alias all inputs.
            MDefinition *firstIns = block->begin()->toDefinition();
            if (!firstIns->getMemoryDefinition())
                firstIns->setMemoryDefinition(new MemoryDefinition());

            AliasSet allInputs = AliasSet::Load(AliasSet::Any);

            entry->setProducer(allInputs, firstIns, freeList);
        }
        stores.copyDependencies(*entry, freeList);

        // Iterate over the definitions of the block and update the array with
        // the latest store for each alias set.
        for (MDefinitionIterator def(*block); def; def++) {
            def->setId(newId++);

            AliasSet set = def->getAliasSet();
            if (set.isNone())
                continue;

            if (!def->getMemoryDefinition())
                def->setMemoryDefinition(new MemoryDefinition());

            bool isStore = set.isStore();

            // The store bit is not part of the alias set.
            set = set & AliasSet::Load(AliasSet::Any);

            // Mark loads & stores dependent on the previous stores.  All the
            // stores on the same alias set would form a chain.
            JS_ASSERT(def->memOperands().empty());
            def->memOperands().extractDependenciesSubset(stores, set, *def, freeList);

            if (isStore) {

                // Update the working list of operands with the current store.
                stores.setProducer(set, *def, freeList);

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
            MergeProducers(graph_, stores, *block, succ, freeList);
        }

        stores.clear(freeList);
    }

    // Remove all the entry blocks, as the vector would be freed.
    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++)
        block->setEntryMemoryOperands(NULL);

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
