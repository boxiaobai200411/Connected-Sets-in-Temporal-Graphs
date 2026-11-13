#include "temporal_test_support.h"

namespace {

std::vector<Event> completeEvents(ui size,ui time) {
    std::vector<Event> edges;
    for (VertexID u = 0; u < size; ++u)
        for (VertexID v = 0; v < size; ++v)
            if (u != v) edges.push_back({u,v,time});
    return edges;
}

ul compareEnumeration(Fixture& fixture,ui k,const Groups& groups = {}) {
    const ul expected = oracleCount(fixture,fixture.vertices,k,groups);
    const ul before = fixture.graph->getAnswerCount();
    fixture.graph->enumerateOptimized(fixture.vertices,k,groups);
    const ul count = fixture.graph->getAnswerCount() - before;
    require(count == expected,"Optimized enumeration differs from the independent oracle");
    return count;
}

void testPackedAndTriples() {
    Fixture packed(4,9,completeEvents(4,4));
    require(compareEnumeration(packed,4) == 1,"A complete four-point set must be accepted");
    require(compareEnumeration(packed,4) == 1 && packed.graph->getAnswerCount() == 2,
            "Standalone enumeration must accumulate answers");
    require(compareEnumeration(packed,4,{{99}}) == 0,"Impossible ownership produced an answer");
    Fixture triple(3,9,completeEvents(3,4));
    require(compareEnumeration(triple,3) == 1,"A complete triple must be accepted");
    Fixture invalid(4,9,{{0,1,4},{1,0,4},{0,2,7},{2,0,7},{0,3,8},{3,0,8}});
    require(compareEnumeration(invalid,4) == 0,"An invalid four-point set was accepted");
}

void testSharedPrefixes() {
    auto edges = completeEvents(3,4);
    edges.insert(edges.end(),{{0,3,5},{3,0,3},{0,4,7},{4,0,7}});
    Fixture strong(5,9,std::move(edges));
    const Groups forced{{0},{1},{2},{3,4}};
    require(compareEnumeration(strong,4,forced) == 1,
            "Shared prefixes must distinguish valid and invalid time connections");
    std::vector<Event> weakEdges{{1,0,3},{2,4,1},{4,0,1}};
    for (ui t = 0; t < 4; ++t)
        for (ui u = 0; u < 4; ++u) weakEdges.push_back({u,(u + 1) % 4,t});
    Fixture weak(5,9,std::move(weakEdges));
    require(compareEnumeration(weak,4,forced) == 1,"An extension must be allowed to repair a weak prefix");
    weak.graph->prepareSubsetIndex(weak.vertices);
    const auto index = weak.graph->buildEnumerationIndex(weak.vertices,{});
    const ul before = weak.graph->getAnswerCount();
    const auto prefix = weak.graph->buildPrefixLabels(index,{0,1,2});
    require(weak.graph->isTemporalExtension(index,{0,1,2},prefix,3) &&
            weak.graph->isTemporalSCCPacked(index,{0,1,2,3}),"Prefix and packed validators disagree");
    require(weak.graph->getAnswerCount() == before,"A standalone validator changed the answer count");
}

void testCenterPairsAndFallback() {
    auto edges = completeEvents(20,2);
    const auto late = completeEvents(20,6);
    edges.insert(edges.end(),late.begin(),late.end());
    Fixture center(20,9,std::move(edges));
    require(compareEnumeration(center,4,{{0}}) == 969,"Center completion lost combinations");
    Fixture pairs(4,9,{{0,1,1},{1,0,7},{1,2,4},{2,1,4},{2,3,5}});
    require(compareEnumeration(pairs,2) == 2,"Pair enumeration lost direct answers");
    Fixture fallback(9,9,completeEvents(9,4));
    require(compareEnumeration(fallback,9) == 1,"The general fallback lost a nine-point answer");
}

void testQueryReset() {
    Fixture fixture(4,9,completeEvents(4,4));
    for (ui pass = 0; pass < 2; ++pass) {
        require(fixture.graph->enumerateCom(0,8,4) == 1,"Com query failed to reset its count");
        require(fixture.graph->enumerateRG(0,8,4) == 1,"RG query failed to reset its count");
        require(fixture.graph->enumerateDec(0,8,4) == 1,"Dec query failed to reset its count");
        require(fixture.graph->getAnswerCount() == 1,"Returned and stored counts differ");
        require(fixture.graph->enumerateCom(0,0,4) == 0,"Empty Com window retained answers");
        require(fixture.graph->enumerateRG(0,0,4) == 0,"Empty RG window retained answers");
        require(fixture.graph->enumerateDec(0,0,4) == 0,"Empty Dec window retained answers");
    }
}

}

int main() try {
    OutputCapture capture;
    testPackedAndTriples();
    testSharedPrefixes();
    testCenterPairsAndFallback();
    testQueryReset();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
