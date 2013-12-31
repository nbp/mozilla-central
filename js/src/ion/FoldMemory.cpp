/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ion/FoldMemory.h"

#include "mozilla/FloatingPoint.h"

#include "ion/ValueNumbering.h"

#include "ion/CompileInfo.h"
#include "ion/Ion.h"
#include "ion/IonBuilder.h"
#include "ion/IonSpewer.h"

using namespace js;
using namespace js::ion;

ScalarReplacement::ScalarReplacement(MIRGenerator *mir, MIRGraph &graph)
  : mir(mir),
    graph_(graph),
    aac_(graph.aliasAnalysisCache())
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

bool
ScalarReplacement::linkValueContext(MDefinition *memOperand, MDefinition **context) const
{
    MDefinition *opContext = repContext(memOperand);
    if (!opContext)
        return true;

    if (!*context) {
        *context = opContext;
        return true;
    }

    JS_ASSERT(opContext->hasValueContext());
    if (contextAreIndentical(*context, opContext)) {
        if (opContext->block()->id() <= (*context)->block()->id())
            *context = opContext;
        return true;
    }

    // Inconsistent context implies no context.
    *context = nullptr;
    return false;
}

void
ScalarReplacement::updateValueContext(MDefinition *memOperand, MDefinition *context) const
{
    MDefinition *opContext = repContext(memOperand);
    if (!opContext)
        return;

    JS_ASSERT(contextAreIndentical(context, opContext));
    if (opContext->block()->id() <= context->block()->id())
        return;

    opContext->getMemoryDefinition()->context = context;
}

void
ScalarReplacement::computeValueContext(MDefinition *def)
{
    MemoryDefinition *memDef = def->getMemoryDefinition();

    MDefinition *context = nullptr;
    memDef->expectation = js_NaN;

    if (def->isPhi()) {
        MPhi *phi = def->toPhi();
        context = phi->getOperand(0)->getMemoryDefinition()->context;
        for (size_t i = 0; i < phi->numOperands(); i++) {
            MDefinition *op = phi->getOperand(i);

            // Assume reverse post-order, which means that we have not yet
            // visited any backedge. If we wanted to be precise we should queue
            // the Phi, but for now we can assume that the variable seen at the
            // beginning of the loop is likely flowing into the same variable.
            // Otherwise this would be interepreted as doing some spilling in
            // the backedge, which will raise the expectations.
            if (phi->block()->id() < op->block()->id())
                continue;

            if (!linkValueContext(op, &context))
                break;
        }
    } else if (def->hasValueContext()) {
        context = def;
        memDef->expectation = 0.0;
        MemoryOperandList &operands = def->memOperands();
        for (MemoryOperandList::iterator it = operands.begin(); it != operands.end(); it++) {
            if (!linkValueContext(it->producer(), &context))
                break;
        }

    }

    // Update the memory definition context.
    memDef->context = context;

    // If the current context has been updated and the operands is not from
    // the same block, ensure that all operands have the same value context.
    if (context && context->block()->id() != def->block()->id()) {
        if (def->isPhi()) {
            MPhi *phi = def->toPhi();
            for (size_t i = 0; i < phi->numOperands(); i++) {
                MDefinition *op = phi->getOperand(i);
                if (phi->block()->id() < op->block()->id())
                    continue;
                updateValueContext(op, context);
            }
        } else {
            MemoryOperandList &operands = def->memOperands();
            for (MemoryOperandList::iterator it = operands.begin(); it != operands.end(); it++)
                updateValueContext(it->producer(), context);
        }
    }
}

bool
ScalarReplacement::computeValueContext()
{
    // Attach to every instruction with an alias set the corresponding value
    // context which is used to quickly identify if an instruction is weakly
    // aliasing, in which case we do not know if this can be the same memory
    // region, or if it is strly aliasling, in which case we can fold loads and
    // stores together.
    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Compute Value Context"))
            return false;

        for (MPhiIterator phi = block->phisBegin(); phi != block->phisEnd(); phi++) {
            if (!phi->getMemoryDefinition())
                continue;
            computeValueContext(*phi);
        }

        for (MInstructionIterator def = block->begin(); def != block->end(); def++) {
            if (!def->getMemoryDefinition())
                continue;
            computeValueContext(*def);
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Compute expectation based for similar value context.
///////////////////////////////////////////////////////////////////////////////

bool
ScalarReplacement::computeBlocksLikelyhood() const
{
    static const double loopMultiplier = 32;

    graph_.entryBlock()->setLikelyhood(1);

    // OSR is an unlikely entry point which is useful when we want to enter
    // quickly, but this is not the main entry point.
    MBasicBlock *osrBlock = graph_.osrBlock();
    if (osrBlock)
        osrBlock->setLikelyhood(0);

    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Attach Likelyhood"))
            return false;

        MBasicBlock *idom = block->immediateDominator();
        double likelyhood = 0;

        if (idom == *block && block->numPredecessors() > 1) {
            for (size_t i = 0; i < block->numPredecessors(); i++) {
                // Apparently the dominator computation is not always able to
                // recover that the loop is dominated by the preheader block.
                // So we just sum over all predecessors which have a lower id.
                if (block->getPredecessor(i)->id() < block->id())
                    likelyhood += block->getPredecessor(i)->getLikelyhood();
            }
        } else if (block->loopDepth() < idom->loopDepth()) {
            // If the loop depth is wrong, then our likelyhood computation is
            // also wrong, but this is a performance issue and not a critical
            // issue.
            //JS_ASSERT(idom->isLoopHeader());
            JS_ASSERT(idom->immediateDominator()->id() < block->id());
            likelyhood = idom->immediateDominator()->getLikelyhood();
        } else {
            JS_ASSERT(idom->id() < block->id() ||
                      *block == graph_.entryBlock() || *block == osrBlock);
            likelyhood = idom->getLikelyhood();
        }

        if (block->isLoopHeader())
            likelyhood *= loopMultiplier;

        block->setLikelyhood(likelyhood);
    }

    return true;
}

// Register an estimation of the benefit that scalar replacement optimization
// might bring. The reason of this computation is that the outcome depends on
// the likelyhood of branches containing escaping calls.
//
// example:            in converted to:
//    var a = {};          var a = {};
//    a.b = 1;             var _a_b = 1;
//    if (...) {           if (...) {
//                             a.b = _a_b;
//      foo(a);                foo(a)
//                             _a_b = a.b;
//    }                    }
//    print(a.b)           print(_a_b)
//
// This transformation is fine, in all cases, but it is not anymore if we add an
// extra branch before the print.  In which case it depends on the likelyhood of
// each branch.
//
// example:            in converted to:
//    var a = {};          var a = {};
//    a.b = 1;             var _a_b = 1;
//    if (...) {           if (...) {
//                             a.b = _a_b;
//      foo(a);                foo(a)
//                             _a_b = a.b;
//    }                    }
//    if (...) {           if (...) {
//                             a.b = _a_b;
//      bar(a);                bar(a)
//                             _a_b = a.b;
//    }                    }
//    print(a.b)           print(_a_b)
//
// In this example, based on the likelyhood of the branches, the scalar
// replacement might cause us to do more stores and loads than the original code
// where it might have been unnecessary, if the value was just written on the
// object in the first place.
void
ScalarReplacement::computeExpectations(MDefinition *def) const
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
    JS_ASSERT_IF(context == def, memContext->expectation == 0);
    JS_ASSERT_IF(context != def, mozilla::IsNaN(def->getMemoryDefinition()->expectation) ||
                 def->getMemoryDefinition()->expectation == 0);
    if (context == def)
        memContext->expectation = 0;

    // Note that this expectation is an approximation as it does not account for
    // consecutive reads.
    MemoryUseList &uses = def->memUses();
    for (MemoryUseList::iterator it = uses.begin(); it != uses.end(); it++) {
        MDefinition *consumer = it->consumer();
        double blockCost = consumer->block()->getLikelyhood();

        if (repContext(consumer) == context) {
            // A Phi does not count as a load.
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

    MemoryOperandList &operands = def->memOperands();
    for (MemoryOperandList::iterator it = operands.begin(); it != operands.end(); it++) {
        MDefinition *producer = it->producer();
        double blockCost = producer->block()->getLikelyhood();

        if (repContext(producer) == context) {
            // A Phi does not count as a store.
            if (producer->isPhi())
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
ScalarReplacement::analyzeExpectations()
{
    // Compute expectations to decide where we should apply the scalar
    // replacement modifications.
    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Approximate Expectation"))
            return false;

        for (MPhiIterator phi = block->phisBegin(); phi != block->phisEnd(); phi++) {
            if (!phi->getMemoryDefinition())
                continue;
            computeExpectations(*phi);
        }

        for (MInstructionIterator def = block->begin(); def != block->end(); def++) {
            if (!def->getMemoryDefinition())
                continue;
            computeExpectations(*def);
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Transformation primitives
///////////////////////////////////////////////////////////////////////////////

void
MemoryUseList::moveDominatedUsesInto(MemoryUseList &list, MDefinition *newProducer,
                                     MDefinition *oldProducer)
{
    JS_ASSERT_IF(!empty(), begin()->producer() == oldProducer);
    MBasicBlock *newBlock = newProducer->block();
    for (MemoryUseList::iterator it = begin(); it != end(); ) {
        if (!newBlock->dominates(it->consumer()->block())) {
            it++;
            continue;
        }

        MemoryUse *use = *it;
        it++;
        this->Parent::remove(use);
        use->set(newProducer, use->consumer(), use->set());
#ifdef CRAZY_DEBUG
        use->ownerUList = &newProducer->memUses();
#endif
        newProducer->memUses().pushBack(use);
    }

    for (MUseIterator use = oldProducer->usesBegin(); use != oldProducer->usesBegin(); ) {
        MNode *consumer = use->consumer();
        if (consumer->isDefinition() &&
            consumer->toDefinition()->isPhi() &&
            consumer->toDefinition()->toPhi()->isMemory() &&
            newBlock->dominates(consumer->block()->getPredecessor(use->index())))
        {
            use = consumer->toDefinition()->toPhi()->replaceOperand(use, newProducer);
        } else {
            use++;
        }
    }
}

struct BlockSink {
    BlockSink(MBasicBlock *block, MInstruction *store, MResumePoint *previous)
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
    mozilla::DebugOnly<MemoryDefinition *> memContext = context->getMemoryDefinition();
    JS_ASSERT(memContext->expectation < 0);
    store->setRecovering();

    // Add the current store and its block to the queue.
    queue.infallibleAppend(BlockSink(store->block(), store, nullptr));

    // While the queue is not empty.
    while (!queue.empty()) {
        // Pop the last block.
        BlockSink bs = queue.popCopy();
        JS_ASSERT(bs.store->block() == bs.block || bs.store->block()->dominates(bs.block));
        JS_ASSERT(repContext(bs.store) == context);

        // Ensure that the block of the context index/object is well dominating
        // the block in which we are sinking the store instruction.
        JS_ASSERT_IF(bs.store->objectContext(), bs.store->objectContext()->block()->dominates(bs.block));
        JS_ASSERT_IF(bs.store->indexContext(), bs.store->indexContext()->block()->dominates(bs.block));

        // Used to check if any of the following instruction is aliasing this
        // instruction without having to look into the list of uses.
        AliasSet storeSet = bs.store->getAliasSet(aac_.sc());
        MResumePoint *previousRp = bs.previous;

        // For each instruction following the store
        MInstructionIterator ins = bs.block->begin();
        if (bs.store->block() == bs.block)
            ins = bs.block->begin(bs.store);
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
                ins->memUses().moveDominatedUsesInto(bs.store->memUses(), bs.store, *ins);
                ins = bs.block->discardAt(ins);

                // Continue to the next instruction.
                continue;
            } else if (!storeSet.isEmptyIntersect(ins->getAliasSet(aac_.sc()))) {
                // If the instruction alias this store with a different context.

                // Clone the store above the current instruction, and inherit
                // all dominated uses of the previous store instruction.
                MInstruction *clone = bs.store->cloneStoreAt(*ins);
                MemoryDefinition *cloneMem = new MemoryDefinition();
                clone->setMemoryDefinition(cloneMem);
                cloneMem->context = context;
                cloneMem->operands.extractDependenciesSubset(store->memOperands(), storeSet, clone, aac_);
                bs.store->memUses().moveDominatedUsesInto(cloneMem->uses, clone, bs.store);

                // Mark this clone as being a in the work list, to prevent a
                // second call of sinkStore on it.
                clone->setInWorklist();
                JS_ASSERT(repContext(clone) == context);

                // Continue to the next block.
                goto store_cannot_be_pushed_futher;
            }

            // if the instruction has a resume point.
            if (MResumePoint *rp = ins->resumePoint()) {
                // Add this store as a side effect which need to be resumed.
                if (!rp->addSideEffect(bs.store, previousRp))
                    return false;
                previousRp = rp;
            }

            ins++;
        }

        if (false) {
          store_cannot_be_pushed_futher:
            continue;
        }

        // We reached the end of the basic block.
        size_t numSucc = bs.block->numSuccessors();

        // Check that we can sink the store in all successors before
        // continuing. If the value context is not dominating one of the
        // successors, this means that we cannot continue, as the value context
        // might not be defined in other predecessors.
        bool canSinkInAllSuccessors = true;

        // For each successor.
        for (size_t s = 0; s < numSucc; s++) {
            MBasicBlock *succ = bs.block->getSuccessor(s);

            // If this successor is dominated by the block of the store, then
            // there is no need to add any Phi instruction.
            if (bs.store->block()->dominates(succ)) {

                // Update the entry resume point of the successor.
                MResumePoint *rp = succ->entryResumePoint();
                if (!rp->addSideEffect(bs.store, previousRp))
                    return false;

                // Add to the queue that we want to continue to sink the same
                // store in this successor.
                if (!queue.append(BlockSink(succ, bs.store, rp)))
                    return false;

                // Continue to the next successor.
                continue;
            }

            // If the successor has multiple predecessors.
            size_t numPred = succ->numPredecessors();
            JS_ASSERT(numPred > 1);

            // if the value context is not dominating this successor, this means
            // that we need to insert a store at the end of the current basic
            // block, as we have no garantee to be able to represent the same
            // value in the following block.
            //
            // Another approach would be to introduce a new basic block between
            // the current block and this successors such as we can only spill
            // on the path towards this successor and not towards all of them.
            MDefinition *objContext = bs.store->objectContext();
            MDefinition *idxContext = bs.store->indexContext();
            if ((objContext && !objContext->block()->dominates(succ)) ||
                (idxContext && !idxContext->block()->dominates(succ)))
            {
                canSinkInAllSuccessors = false;
                continue;
            }

            // Find the memory Phi which has the same memory context in the
            // successor.
            MPhi *memPhi = nullptr;
            for (MPhiIterator phiIt = succ->phisBegin(); phiIt != succ->phisEnd(); phiIt++) {
                if (!phiIt->isMemory() || context != repContext(*phiIt))
                    continue;

                memPhi = *phiIt;
                break;
            }

            // A Phi should be added by the alias analysis phase, but the memory
            // context might not be the same, even if there is one for the same
            // alias set. If we cannot find the same memory context in the phis
            // of the successors, this means that it might not be as efficient
            // to sink the store in this successor.
            if (!memPhi) {
                canSinkInAllSuccessors = false;
                continue;
            }

            // The Memory Phi is marked as mutated to indicate that the current
            // successor has been traversed successfully and that one store has
            // already be added into this successor.
            if (memPhi->isMutated())
                continue;
            memPhi->setMutated();

            // Add a Phi to collect all stored values from all predecessors.
            MPhi *contentPhi = MPhi::New(uint32_t(-1));
            contentPhi->reserveLength(numPred);

            // :TODO: Handle different type of memory content.  Should we
            // locally reuse apply types ?!
            contentPhi->setResultType(bs.store->memoryContent()->type());

            // Add Loads to all predecessors except the block from which we are
            // coming from, and register their values inside the Phi.
            for (size_t p = 0; p < numPred; p++) {
                MDefinition *predAlias = memPhi->getOperand(p);

                // If the previous aliasing instruction from the predecessor p
                // has the same context, fill the operand p of the phi with the
                // memory content of the aliasing instruction.
                if (context == repContext(predAlias)) {
                    contentPhi->addInput(predAlias->memoryContent());
                    continue;
                }

                // Add a load before the control instruction of the predecessor.
                MBasicBlock *pred = succ->getPredecessor(p);
                MInstruction *clonedLoad = bs.store->cloneLoadAt(*pred->end());
                MemoryDefinition *cloneMem = new MemoryDefinition();
                clonedLoad->setMemoryDefinition(cloneMem);
                cloneMem->context = context;

                // :TODO: ReplaceConsumer

                // Create a new memory operand for this Load.
                //memPhi->replaceProducer(storeSet, predAlias, clonedLoad, aac_);

                // Use its return value of the Load input of the Phi.
                contentPhi->addInput(clonedLoad);
            }

            // Add the Phi of the memory content at the beginning of the basic
            // block.
            succ->addPhi(contentPhi);

            // Add a store at the beginning of the block.
            MInstruction *clone = bs.store->cloneStoreAt(*succ->begin(), contentPhi);

            // Move the uses of memory Phi of the successor to the cloned store
            // operation.
            MemoryDefinition *cloneMem = new MemoryDefinition();
            clone->setMemoryDefinition(cloneMem);
            cloneMem->context = context;

            // :FIXME: We cannot copy the list, as we might have multiple
            // successors.
            bs.store->memUses().moveDominatedUsesInto(clone->memUses(), clone, bs.store);

            // Add the clone as the only use of the memory phi since the clone
            // has the same alias set as the memory phi.
            clone->memOperands().replaceProducer(storeSet, memPhi, clone, aac_);

            // Mark the store as recovering.
            clone->setRecovering();

            // Mark the clone as visited it is added into a successor block.
            clone->setInWorklist();

            // Add this store as a side effect of the basic block resume point.
            MResumePoint *rp = succ->entryResumePoint();
            if (!rp->addSideEffect(clone, nullptr))
                return false;

            // Queue this new store and block into the queue of basic block
            // which have to be processed.
            JS_ASSERT(repContext(clone) == context);
            if (!queue.append(BlockSink(succ, clone, rp)))
                return false;
        }

        if (!canSinkInAllSuccessors) {
            // The value context does not dominate one of the successors, we
            // need to copy the store at the end of this basic block such as we
            // keep the semantic identical for all blocks.
            MInstruction *clone = bs.store->cloneStoreAt(bs.block->lastIns());
            MemoryDefinition *cloneMem = new MemoryDefinition();
            clone->setMemoryDefinition(cloneMem);
            cloneMem->context = context;
            cloneMem->operands.extractDependenciesSubset(bs.store->memOperands(), storeSet, clone, aac_);
            bs.store->memUses().moveDominatedUsesInto(cloneMem->uses, clone, bs.store);

            // Mark this clone as being a in the work list, to prevent a second
            // call of sinkStore on it.
            clone->setInWorklist();
            JS_ASSERT(repContext(clone) == context);
            continue;
        }
    }

    return true;
}

bool
ScalarReplacement::sinkAllStores()
{
    if (!computeValueContext())
        return false;

    if (!computeBlocksLikelyhood())
        return false;

    if (!analyzeExpectations())
        return false;

    for (ReversePostorderIterator block(graph_.rpoBegin()); block != graph_.rpoEnd(); block++) {
        if (mir->shouldCancel("Sink Stores"))
            return false;

        MInstructionIterator end = block->end();
        for (MInstructionIterator ins(block->begin()); ins != end; ins++) {
            if (!ins->getMemoryDefinition())
                continue;
            if (!ins->mightStore())
                continue;
            if (ins->isInWorklist()) {
                ins->setNotInWorklist();
                continue;
            }

            MDefinition *context = repContext(*ins);
            if (!context)
                continue;

            if (!ins->hasValueContext())
                continue;

            // Note: 0 is not an uncommon case, accepting zero here might cause
            // us to be greedy, and transform extra operations.  Rejecting the
            // equality here implies that we are trusting the source code for
            // being optimal.
            JS_ASSERT(!mozilla::IsNaN(context->getMemoryDefinition()->expectation));
            if (context->getMemoryDefinition()->expectation >= 0.0)
                continue;

            if (!sinkStore(*ins))
                return false;
        }
    }

    return true;
}

MInstruction *
MStoreFixedSlot::cloneLoadAt(MInstruction *at) const
{
    MLoadFixedSlot *clone = MLoadFixedSlot::New(object(), slot());
    at->block()->insertBefore(at, clone);
    return clone;
}

MInstruction *
MStoreFixedSlot::cloneStoreAt(MInstruction *at, MDefinition *content) const
{
    MStoreFixedSlot *clone = nullptr;
    if (!content)
        content = value();

    if (needsBarrier())
        clone = MStoreFixedSlot::NewBarriered(object(), slot(), content);
    else
        clone = MStoreFixedSlot::New(object(), slot(), content);

    at->block()->insertBefore(at, clone);
    return clone;
}
