/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_ScalarReplacement_h
#define ion_ScalarReplacement_h

#include "ion/CompileInfo.h"
#include "ion/MIR.h"
#include "ion/MIRGraph.h"

namespace js {
namespace ion {

class ScalarReplacement
{
  protected:
    MIRGenerator *mir;
    MIRGraph &graph_;
    AliasAnalysisCache &aac_;

  public:
    ScalarReplacement(MIRGenerator *mir, MIRGraph &graph);

  private:
    ///////////////////////////////////////////////////////////////////////////
    // Analyze the memory operations.
    ///////////////////////////////////////////////////////////////////////////

    // Check if 2 definitions have the same value context (object & index).
    bool contextAreIndentical(MDefinition *lhs, MDefinition *rhs) const;
    bool linkValueContext(MDefinition *memOperand, MDefinition **context) const;
    void updateValueContext(MDefinition *memOperand, MDefinition *context) const;

    // Find the definition which represent a value context, in order to simplify
    // comparisons.
    MDefinition *repContext(MDefinition *def) const;

    // To be able to fold memory accesses across Phi nodes, we need to annotate
    // Phi nodes with the object & index used at run-time.  If there is a unique
    // common object & index definition, then we can garantee that there is at
    // least one optimizable path.
    void computeValueContext(MDefinition *def);
    bool computeValueContext();

    // Annotate each basic block with the likelyhood of visiting this basic
    // block.
    bool computeBlocksLikelyhood() const;

    // Before doing any transformation, we need to ensure that it might be
    // faster if we have good profiling informations.
    void computeExpectations(MDefinition *def) const;

    bool analyzeExpectations();

  private:
    ///////////////////////////////////////////////////////////////////////////
    // Scalar Replacement
    ///////////////////////////////////////////////////////////////////////////

    // Move a store instruction closer to any of the escaping calls are
    // reads. While moving this instruction down, it is also registered as a
    // side-effect in all resume points which are crossed.  If a join block is
    // encountered loads are added in other incoming branches.
    bool sinkStore(MInstruction *store);

  public:
    bool sinkAllStores();
};

} // namespace ion
} // namespace js

#endif /* ion_ScalarReplacement_h */
