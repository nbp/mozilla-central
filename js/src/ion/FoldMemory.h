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

    // A lift location indicates the definition which we need to look at and the
    // closest block in which we need to insert a copy of the instruction such
    // as we do not add extra overhead in other paths.
    struct LiftLocation
    {
        MDefinition *def;
        MBlock *dest;
    };

  public:
    ScalarReplacement(MIRGenerator *mir, MIRGraph &graph, AliasAnalysisCache &aac);

  private:
    ///////////////////////////////////////////////////////////////////////////
    // Analyze the memory operations.
    ///////////////////////////////////////////////////////////////////////////

    // Check if 2 definitions have the same value context (object & index).
    bool contextAreIndentical(MDefinition *lhs, MDefinition *rhs) const;

    // To be able to fold memory accesses across Phi nodes, we need to annotate
    // Phi nodes with the object & index used at run-time.  If there is a unique
    // common object & index definition, then we can garantee that there is at
    // least one optimizable path.
    void computeValueContext(MDefinition *def);

    // Find the definition which represent a value context, in order to simplify
    // comparisons.
    MDefinition *repContext(MDefinition *def) const;

    // Annotate each basic block with the likelyhood of visiting this basic
    // block.
    bool computeBlocksLikelyhood(MIRGraph &graph) const;

    // Before doing any transformation, we need to ensure that it might be
    // faster if we have good profiling informations.
    void approxExpectation(MDefinition *def) const;

    bool analyzeExpectations(MIRGraph &graph) const;

  private:
    ///////////////////////////////////////////////////////////////////////////
    // Scalar Replacement
    ///////////////////////////////////////////////////////////////////////////

    // Move a store to the end of its basic block or until the next operation
    // which might observe the state of the store.
    bool sinkStore(MDefinition *store);    

};

} // namespace ion
} // namespace js

#endif /* ion_ScalarReplacement_h */
