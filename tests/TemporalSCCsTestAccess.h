#ifndef TCS_ENUM_TEST_ACCESS_H
#define TCS_ENUM_TEST_ACCESS_H

#include "TemporalSCCs.h"

class TemporalSCCsTestAccess : public TemporalSCCs {
public:
    using TemporalSCCs::TemporalSCCs;
    using TemporalSCCs::SplitResult;
    using TemporalSCCs::PrefixLabels;
    using TemporalSCCs::buildEnumerationIndex;
    using TemporalSCCs::buildMutualReachabilityGraph;
    using TemporalSCCs::buildPivotIndex;
    using TemporalSCCs::buildPrefixLabels;
    using TemporalSCCs::buildPrefixLabelsGeneral;
    using TemporalSCCs::buildReverseCover;
    using TemporalSCCs::buildSplitPlan;
    using TemporalSCCs::buildWindow;
    using TemporalSCCs::computeEarliestArrival;
    using TemporalSCCs::countKCombinationsCapped;
    using TemporalSCCs::decompose;
    using TemporalSCCs::enumerateCombinations;
    using TemporalSCCs::enumerateOptimized;
    using TemporalSCCs::estimateKCombinations;
    using TemporalSCCs::extendPrefixLabels;
    using TemporalSCCs::findEnumerationPair;
    using TemporalSCCs::findWindowStaticSCCs;
    using TemporalSCCs::hasEnumerationCenter;
    using TemporalSCCs::isTemporalExtension;
    using TemporalSCCs::isTemporalSCCPacked;
    using TemporalSCCs::kMaxEnumerationSize;
    using TemporalSCCs::normalizeRequiredGroups;
    using TemporalSCCs::prepareSubsetIndex;
    using TemporalSCCs::scorePivotSubset;
    using TemporalSCCs::selectPivot;
    using TemporalSCCs::selectReversePivot;
    using TemporalSCCs::shouldEnumerateDirectly;

    ui getDecompositionAlpha() const { return decompositionAlpha_; }
    const Window& getWindow() const {
        if (!window_ready_) throw std::logic_error("Call buildWindow first");
        return window_;
    }
    VertexID selectReversePivot(const std::vector<VertexID>& vertices,const std::vector<VertexID>& remaining) {
        ReversePivotCache cache;
        return TemporalSCCs::selectReversePivot(vertices,remaining,cache);
    }
};

#endif
