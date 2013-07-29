/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>

#include "ion/MIR.h"
#include "ion/AliasAnalysis.h"
#include "ion/MIRGraph.h"
#include "ion/Ion.h"
#include "ion/IonBuilder.h"
#include "ion/IonSpewer.h"

using namespace js;
using namespace js::ion;

using mozilla::Array;

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

AliasAnalysis::AliasAnalysis(MIRGenerator *mir, MIRGraph &graph)
  : mir(mir),
    graph_(graph),
    loop_(NULL)
{
}

static void
IonSpewDependency(MDefinition *load, MDefinition *store, const char *verb, const char *reason)
{
    if (!IonSpewEnabled(IonSpew_Alias))
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
InheritEntryAliasSet(uint32_t aliasSetId, MBasicBlock *current, MBasicBlock *dest,
                     MDefinition *initial, MDefinition *added)
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
        dest->setAliasSetStore(aliasSetId, added);
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
        // dominated uses by the phi.  This case happen for loop back-edges.
        if (current->id() >= dest->id())
            ReplaceDominatedMemoryUsesWith(aliasSetId, last, phi, dest);

        return true;
    }

    MPhi *phi = last->toPhi();
    phi->replaceOperand(predIndex, added);
    return true;
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
    const size_t numAliasSets = AliasSet::NumCategories;

    // This vector is copied from the basic block alias sets at the beginning
    // and merged into the successors of the basic block once we reach the end
    // of it.
    Array<MDefinition *, AliasSet::NumCategories> stores;

    // Re-use the stack used to build the MIR Graph and their Phi nodes as a
    // model of memory manipulated within each alias set.  Initialize all basic
    // blocks with the first instruction.
    MDefinition *firstIns = *graph_.begin()->begin();
    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (!block->initNumAliasSets(numAliasSets, firstIns))
            return false;
    }

    // Type analysis may have inserted new instructions. Since this pass depends
    // on the instruction number ordering, all instructions are renumbered.
    // We start with 1 because some passes use 0 to denote failure.
    uint32_t newId = 1;

    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Alias Analysis (main loop)"))
            return false;

        // Load the previous stores from the basic block slots.
        for (size_t i = 0; i < numAliasSets; i++)
            stores[i] = block->getAliasSetStore(i);

        // Iterate over the definitions of the block and update the array with
        // the latest store for each alias set.
        for (MDefinitionIterator def(*block); def; def++) {
            def->setId(newId++);

            AliasSet set = def->getAliasSet();
            if (set.isNone())
                continue;

            if (set.isStore()) {
                // Chain the stores by marking them as dependent on the previous
                // stores.
                for (AliasSetIterator iter(set); iter; iter++) {
                    IonSpewDependency(*def, stores[*iter], "depends", "");
                    def->setAliasSetDependency(*iter, stores[*iter]);
                    stores[*iter] = *def;
                }

                if (IonSpewEnabled(IonSpew_Alias)) {
                    fprintf(IonSpewFile, "Processing store ");
                    def->printName(IonSpewFile);
                    fprintf(IonSpewFile, " (flags %x)\n", set.flags());
                }
            } else {
                // Mark load as dependent on the previous stores.
                for (AliasSetIterator iter(set); iter; iter++) {
                    IonSpewDependency(*def, stores[*iter], "depends", "");
                    def->setAliasSetDependency(*iter, stores[*iter]);
                }

            }
        }

        // Write the current memory status back into the succesors of the
        // current basic blocks.  If needed, we will add a memory Phi node to
        // merge the memory dependency in case we had multiple stores from
        // different branches.
        for (size_t s = 0; s < block->numSuccessors(); s++) {
            MBasicBlock *succ = block->getSuccessor(s);
            for (size_t i = 0; i < numAliasSets; i++)
                InheritEntryAliasSet(i, *block, succ, firstIns, stores[i]);
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
