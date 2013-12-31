/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ion/ValueNumbering.h"

#include "ion/CompileInfo.h"
#include "ion/Ion.h"
#include "ion/IonBuilder.h"
#include "ion/IonSpewer.h"

using namespace js;
using namespace js::ion;

ScalarReplacement::ScalarReplacement(MIRGenerator *mir, MIRGraph &graph, AliasAnalysisCache &aac)
  : mir(mir),
    graph_(graph),
    aac_(aac)
{ }

///////////////////////////////////////////////////////////////////////////////
// Value context functions for MIR Nodes
///////////////////////////////////////////////////////////////////////////////

bool
ScalarReplacement::contextAreIndentical(MDefinition *lhs, MDefinition *rhs) const
{
    if (lhs == rhs)
        return true;

    if (lhs->objectContext() != rhs->objectContext())
        return false;

    if (lhs->indexContext() != rhs->indexContext())
        return false;

    // Context are identical, mark one of the two as the representant of the
    // context, such as we can avoid doing these comparisons later.
    if (lhs->block()->id() < rhs->block()->id())
        rhs->getMemoryDefinition()->context = lhs;
    else
        lhs->getMemoryDefinition()->context = rhs;

    return true;
}

MDefinition *
ScalarReplacement::repContext(MDefinition *def) const
{
    MemoryDefinition *memDef = def->getMemoryDefinition();
    MDefinition *context = memDef->context;

    // Before comparing any context, we should ensure that we are comparing the
    // representant and not 2 identical context which have a different
    // representant.
    if (!context)
        return nullptr;

    MDefinition *repContext = context->getMemoryDefinition()->context;
    if (context == repContext)
        return context;

    do {
        context = repContext;
        repContext = context->getMemoryDefinition()->context;
    } while (context != repContext);

    // Update the representant.
    memDef->context = context;
    return context;
}

void
ScalarReplacement::computeValueContext(MDefinition *def)
{
    MemoryDefinition *memDef = def->getMemoryDefinition();

    memDef->context = nullptr;
    if (!def->isPhi()) {
        if (def->hasValueContext())
            memDef->context = def;
        return;
    }

    MPhi *phi = def->toPhi();
    MDefinition *context = phi->getOperand(0)->getMemoryDefinition()->context;
    for (size_t i = 0; i < phi->numOperands(); i++) {

        MDefinition *op = phi->getOperand(i);

        // Assume reverse post-order, which means that we have not yet visited
        // any backedge. If we wanted to be precise we should queue the Phi, but
        // for now we can assume that the variable seen at the beginning of the
        // loop is likely flowing into the same variable.  Otherwise this would
        // be interepreted as doing some spilling in the backedge, which will
        // raise the expectations.
        if (phi->block()->id() < op->block()->id())
            continue;

        MDefinition *opContext = repContext(op);
        if (!opContext)
            continue;

        JS_ASSERT(opContext->hasValueContext());
        if (!context) {
            context = opContext;
            continue;
        }

        if (contextAreIndentical(context, opContext))
            continue;

        // Inconsistent context implies no context.
        return;
    }

    memDef->context = context;
}

///////////////////////////////////////////////////////////////////////////////
// Compute expectation based for similar value context.
///////////////////////////////////////////////////////////////////////////////

bool
ScalarReplacement::computeBlocksLikelyhood(MIRGraph &graph) const
{
    static const double loopMultiplier = 32;

    startBlock->setLikelyhood(1);

    // OSR is an unlikely entry point which is useful when we want to enter
    // quickly, but this is not the main entry point.
    MBasicBlock *osrBlock = graph.osrBlock();
    if (osrBlock)
        osrBlock->setLikelyhood(0);

    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Attach Likelyhood"))
            return false;

        MBasicBlock *idom = block->immediateDominator();
        double likelyhood = 0;
        if (idom == block && block->numPredecessors() > 1) {
            for (size_t i = 0; i < block->numPredecessors(); i++)
                likelyhood += block->getPredecessors(i)->getLikelyhood();
        } else if (block->loopDepth() < idom->loopDepth()) {
            JS_ASSERT(idom->isLoopHeader());
            likelyhood = idom->immediateDominator()->getLikelyhood();
        } else {
            likelyhood = idom->getLikelyhood();
        }

        if (block->isLoopHeader())
            likelyhood *= loopMultiplier;

        block->setLikelyhood(likelyhood);
    }

    return true;
}

void
ScalarReplacement::approxExpectation(MDefinition *def) const
{
    static const double StoreFactor = 1;
    static const double LoadFactor = 1;
    static const double Remove = -1;
    static const double Add = 1;

    MDefinition *context = repContext(def);
    if (!context)
        return;

    // Assume a traversal done in reverse-post-order, as we prefer the context
    // coming from the block with the lowest id first.
    MemoryDefinition *memContext = context->getMemoryDefinition();
    double expectation = 0;
    if (context == def)
        memContext->expectation = 0;

    // Note that this expectation is an approximation as it does not account for
    // consecutive reads.
    MemoryUseList &uses = orig->memUses();
    for (MemoryUseList::iterator it = uses.begin(); it != uses.end(); it++) {
        MDefinition *consumer = it->consumer();
        double blockCost = consumer->block()->getLikelyhood();

        if (repContext(consumer) == context) {
            // A Phi with the same context does not reduce the cost as a load.
            if (consumer->isPhi())
                continue;

            // Any use with the same context implies that we can remove the load
            // and replace it by a register/stack-slot instead.
            expectation += Remove * LoadFactor * blockCost;
        } else
            // Any use with a different context implies that we need to spill
            // the register/stack-slot before executing this instruction.
            expectation += Add * StoreFactor * blockCost;
    }

    for (MemoryOperandList::iterator it = memOperands().begin(); it != memOperands().end(); it++) {
        MDefinition *producer = it->producer();
        double blockCost = producer->block()->getLikelyhood();

        if (repContext(producer) == context) {
            // A Phi with the same context does not reduce the cost as a store.
            if (consumer->isPhi())
                continue;

            // Any operand with the same context implies that we can remove a store
            // and replace it by a register/stack-slot instead.
            expectation += Remove * StoreFactor * blockCost;
        } else
            // Any operand with a different context implies that we need to load
            // the register/stack-slot before executing this instruction.
            expectation += Add * LoadFactor * blockCost;
    }

    memContext->expectation += expectation;
}

bool
ScalarReplacement::analyzeExpectations(MIRGraph &graph) const
{
    if (!computeBlockLikelyhood())
        return false;

    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Compute Value Context"))
            return false;

        for (MPhiIterator phi = block->phisBegin(); phi != block->phisEnd(); phi++) {
            if (!phi->getMemoryDefinition())
                continue;
            computeValueContext(phi);
        }

        for (MDefinitionIterator def(*block); def; def++) {
            if (!def->getMemoryDefinition())
                continue;
            computeValueContext(def);
        }
    }

    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Approximate Expectation"))            return false;

        for (MPhiIterator phi = block->phisBegin(); phi != block->phisEnd(); phi++) {
            if (!phi->getMemoryDefinition())
                continue;
            approxExpectation(phi);
        }

        for (MDefinitionIterator def(*block); def; def++) {
            if (!def->getMemoryDefinition())
                continue;
            approxExpectation(def);
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Transformation primitives
///////////////////////////////////////////////////////////////////////////////

struct BlockSink {
    BlockSink(MBasicBlock *block, MDefinition *store, MResumePoint *previous)
      : block(block), store(store), previous(previous)
    { }

    // Basic block in which we sink the store.
    MBasicBlock *block;

    // Store to be sink in the previous basic block.
    MInstruction *store;

    // Last resume point patched with the side effect, in case we can share the
    // list of side effects.
    MResumePoint *previous;
};

bool
ScalarReplacement::sinkStore(MInstruction *store)
{
    // Queue of blocks with their pending store which needs to be sinked.
    Vector<BlockSink, 2, SystemAllocPolicy> queue;

    // Mark the store as Recovering.
    MDefinition *context = repContext(store);
    MemoryDefinition *memContext = repContext(store)->getMemoryDefinition();
    JS_ASSERT(memContext->expectation < 0);
    store->setRecovering();

    // Add the current store and its block to the queue.
    queue.infallibleAppend(BlockSink(store->block(), store, nullptr));

    // While the queue is not empty.
    while (!queue.empty()) {
        // Pop the last block.
        BlockSink bs = queue.popCopy();
        JS_ASSERT(bs.store->block() == bs.block || bs.store->block()->dominates(bs.block));

        // Used to check if any of the following instruction is aliasing this
        // instruction without having to look into the list of uses.
        AliasSet storeSet = store->getAliasSet(aac_.sc());
        MResumePoint *previousRp = bs.previous;

        // For each instruction following the store
        MInstructionIterator ins = bs.block->begin(bs.store);
        ins++;
        MInstructionIterator insEnd = bs.block->end();
        for (; ins != insEnd; ) {
            if (!ins->getMemoryDefinition()) {
                // If this instruction does not alias any memory, then avoid
                // checking if it alias or not.
                ;
            } else if (context == repContext(*ins)) {
                // If the instruction alias this store with the same context.

                // If this is a store.  Stop sinking this store, and continue to
                // sink it in the remaining blocks.
                if (ins->mightStore())
                    goto store_cannot_be_pushed_futher;

                // Otherwise this is a read. Replace all its uses by the stored value.
                MDefinition *content = bs.store->memoryContent();
                JS_ASSERT(content->block() == bs.block || content->block()->dominates(bs.block));
                ins->replaceAllUsesWith(content);
                ins->memOperands().clear(aac_);
                ins->memUses().moveListInto(store->memUses());
                ins = bs.block->discardAt(ins);

                // Continue to the next instruction.
                continue;
            } else if (!storeSet.isEmptyIntersect(ins->getAliasSet(aac_.sc()))) {
                // If the instruction alias this store with a different context.

                // Clone the store above the current instruction, and inherit
                // all dominated uses of the previous store instruction.
                MDefinition *clone = store->cloneAt(*ins);
                MemoryDefinition *cloneMem = new MemoryDefinition();
                clone->setMemoryDefinition(cloneMem);
                cloneMem->context = context;
                store->memUses().moveDominatedListInto(&cloneMem->uses, bs.block);
                cloneMem->operands.extractDependenciesSubset(store->memOperands(), storeSet, clone, aac_);

                // Continue to the next block.
                goto store_cannot_be_pushed_futher;
            }

            // if the instruction has a resume point.
            if (MResumePoint *rp = ins->resumePoint()) {
                // Add this store as a side effect which need to be resumed.
                if (!rp->addSideEffect(def, previousRp))
                    return false;
                previousRp = rp;
            }

            ins++;
        }

        // We reached the end of the basic block.
        // For each successor.
        size_t numSucc = bs.block->numSuccessors();
        for (size_t s = 0; s < numSucss; s++) {
            MBasicBlock *succ = bs.block->getSuccessor(s);

            // If this successor is dominated by the block of the store, in
            // which case there is no need to add any Phi instruction.
            if (store->block()->dominates(succ)) {

                // Update the entry resume point of the successor.
                MResumePoint *rp = succ->entryResumePoint();
                if (!rp->addSideEffect(store, previousRp))
                    return false;

                // Add to the queue that we want to continue to sink the same
                // store in this block.
                if (!queue.append(BlockSink(succ, store, rp)))
                    return false;

                // Continue to the next successor.
                continue;
            }

            // If the successor has multiple predecessors.
            size_t numPred = succ->numPredecessors();
            JS_ASSERT(numPred > 1);

            // Ensure that the object & index context are dominating this Phi. (no need to add a Phi)
            JS_ASSERT_IF(store->objectContext(), store->objectContext()->block()->dominates(succ));
            JS_ASSERT_IF(store->indexContext(), store->indexContext()->block()->dominates(succ));

            // Add a Phi to collect all stored values from all predecessors.
            MPhi *phi = MPhi::New(uint32_t(-1));
            phi->reserveLength(numPred);

            // :TODO: Handle different type of memory content.  Should we
            // locally reuse apply types ?!
            phi->setResultType(store->memoryContent()->type());

            // The successor must have a memory Phi which represents the memory
            // content of the alias set, otherwise the block would be dominated
            // by the block of the store instruction.
            MPhi *memPhi = nullptr;
            for (MPhiIterator phiIt = succ->phisBegin(); phiIt != succ->phisEnd(); phiIt++) {
                if (!phi->isMemory() || context != repContext(*phiIt))
                    continue;

                memPhi = *phiIt;
                break;
            }
            JS_ASSERT(memPhi);

            // Add Loads to all predecessors except the block from which we are
            // coming from, and register their values inside the Phi.
            for (size_t p = 0; p < numPred; p++) {
                MDefinition *predAlias = memPhi->getOperand(p);

                // If the previous aliasing instruction from the predecessor p
                // has the same context, fill the operand p of the phi with the
                // memory content of the aliasing instruction.
                if (context == repContext(predAlias)) {
                    phi->addInput(predAlias->memoryContent());
                    continue;
                }

                // Add a load before the control instruction of the predecessor.
                MBasicBlock *pred = succ->getPredecessor(p);
                MDefinition *clonedLoad = bs.store->cloneLoadAt(*pred->end());

                // Create a new memory operand for this Load.
                clone->replaceProducer(storeSet, predAlias, clonedLoad, aac_);

                // Use its return value of the Load input of the Phi.
                phi->addInput(clonedLoad);
            }

            // Add a store at the beginning of the block.
            MDefinition *clone = store->cloneStoreAt(*succ->begin());

            // Move the uses of memory Phi of the successor to the cloned store
            // operation.
            clone->setMemoryDefinition(new MemoryDefinition());
            store->memUses()->moveListInto(clone->memUses());

            // Add the clone as the only use of the memory phi since the clone
            // has the same alias set as the memory phi.
            clone->memOperands()->replaceProducer(storeSet, memPhi, clone, aac_);

            // Mark the store as recovering.
            clone->setRecovering();

            // Add this store as a side effect of the basic block resume point.
            MResumePoint *rp = succ->entryResumePoint();
            if (!rp->addSideEffect(clone, nullptr))
                return false;

            // Queue this new store and block into the queue of basic block
            // which have to be processed.
            if (!queue.append(BlockSink(succ, clone, rp)))
                return false;
        }

      store_cannot_be_pushed_futher:
        ;
    }
}

MDefinition *
MStoreFixedSlot::cloneLoadAt(MInstruction *at)
{
    MLoadFixedSlot *clone = MLoadFixedSlot::New(object(), slot());
    at->block()->insertBefore(at, clone);
    return clone;
}

MDefinition *
MStoreFixedSlot::cloneStoreAt(MInstruction *at)
{
    MStoreFixedSlot *clone = MStoreFixedSlot::New(object(), slot(), value(), needsBarrier());
    at->block()->insertBefore(at, clone);
    return clone;
}
