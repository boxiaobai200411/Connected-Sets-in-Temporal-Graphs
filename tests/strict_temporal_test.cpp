#include "temporal_test_support.h"

#include <numeric>
#include <random>

namespace {

void checkTraversalLabels(Fixture& fixture,const std::vector<VertexID>& selected) {
    fixture.graph->prepareSubsetIndex(selected);
    const auto& window=fixture.graph->getWindow();
    constexpr ui infinity=std::numeric_limits<ui>::max();
    for(ui root=0;root<selected.size();++root)for(bool forward:{true,false}) {
        std::vector<ui> expected(selected.size(),forward?infinity:0);
        std::vector<unsigned char> seen(selected.size(),0);
        expected[root]=forward?window.left:window.right;seen[root]=1;
        bool changed;
        do {
            changed=false;
            for(const auto& edge:fixture.events) {
                if(edge.time<window.left || edge.time>window.right)continue;
                const auto from=std::find(selected.begin(),selected.end(),edge.source);
                const auto to=std::find(selected.begin(),selected.end(),edge.target);
                if(from==selected.end() || to==selected.end())continue;
                const ui u=static_cast<ui>(from-selected.begin()),v=static_cast<ui>(to-selected.begin());
                const ui current=forward?u:v,next=forward?v:u;
                if(!seen[current] || (current!=root && (forward?edge.time<=expected[current]:edge.time>=expected[current])))continue;
                if(next!=root && (!seen[next] || (forward?edge.time<expected[next]:edge.time>expected[next]))) {
                    expected[next]=edge.time;seen[next]=1;changed=true;
                }
            }
        }while(changed);
        std::vector<ui> actual;
        TemporalSCCsTestAccess::SplitResult split;
        fixture.graph->computeEarliestArrival(selected[root],selected,actual,split,forward);
        for(ui v=0;v<selected.size();++v)
            if(seen[v])require(actual[v]==expected[v],"Strict traversal label differs from independent events");
        std::vector<VertexID> reachable=split.R;reachable.insert(reachable.end(),split.Q.begin(),split.Q.end());
        for(ui v=0;v<selected.size();++v)
            require((std::find(reachable.begin(),reachable.end(),selected[v])!=reachable.end())==static_cast<bool>(seen[v]),
                "Strict traversal visited set differs from independent events");
    }
}

void verifyDecision(Fixture& fixture,const std::vector<VertexID>& selected,bool expected) {
    require(temporalOracle(fixture,selected)==expected,"Strict regression fixture has wrong independent decision");
    checkTraversalLabels(fixture,selected);
    fixture.graph->prepareSubsetIndex(fixture.vertices);
    const auto index=fixture.graph->buildEnumerationIndex(fixture.vertices,{});
    std::vector<ui> chosen(selected.begin(),selected.end());
    if(chosen.size()<=TemporalSCCsTestAccess::kMaxEnumerationSize) {
        require(fixture.graph->isTemporalSCCPacked(index,chosen)==expected,"Packed strict decision differs");
        if(chosen.size()>1) {
            auto prefixChosen=chosen; const ui tail=prefixChosen.back(); prefixChosen.pop_back();
            const auto prefix=fixture.graph->buildPrefixLabels(index,prefixChosen);
            require(fixture.graph->isTemporalExtension(index,prefixChosen,prefix,tail)==expected,
                "Strict prefix-tail decision differs");
        }
    }
    const ul before=fixture.graph->getAnswerCount();
    fixture.graph->enumerateOptimized(selected,static_cast<ui>(selected.size()),{});
    require(fixture.graph->getAnswerCount()-before==static_cast<ul>(expected),"Strict V1 enumeration decision differs");
    const ul ordinaryBefore=fixture.graph->getAnswerCount();
    fixture.graph->enumerateCombinations(selected,static_cast<ui>(selected.size()),{});
    require(fixture.graph->getAnswerCount()-ordinaryBefore==static_cast<ul>(expected),"Strict ordinary enumeration differs");
}

void testTimestampBoundariesAndEqualEvents() {
    Fixture pair(2,2,{{0,1,0},{1,0,0},{0,1,0}});
    pair.graph->buildWindow(0,0,pair.vertices);
    verifyDecision(pair,{0,1},true);

    Fixture cycle(3,4,{{0,1,2},{1,2,2},{2,0,2}});
    cycle.graph->buildWindow(2,2,cycle.vertices);
    verifyDecision(cycle,{0,1,2},false);
    verifyDecision(cycle,{2,1,0},false);

    std::vector<Event> complete;
    for(ui u=0;u<3;++u)for(ui v=0;v<3;++v)if(u!=v)complete.push_back({u,v,2});
    Fixture direct(3,4,std::move(complete));
    direct.graph->buildWindow(2,2,direct.vertices);
    verifyDecision(direct,{0,1,2},true);

    std::vector<Event> increasing;
    for(ui t=0;t<3;++t)for(ui u=0;u<3;++u)increasing.push_back({u,(u+1)%3,t});
    Fixture strictCycle(3,4,std::move(increasing));
    verifyDecision(strictCycle,{0,1,2},true);
    strictCycle.graph->buildWindow(1,1,strictCycle.vertices);
    verifyDecision(strictCycle,{0,1,2},false);
    strictCycle.graph->buildWindow(0,2,strictCycle.vertices);
    verifyDecision(strictCycle,{0,1,2},true);
}

void testEqualTimestampOrderAndPrefixRepair() {
    const std::vector<Event> events{{0,1,1},{1,2,1},{2,0,1},{0,0,1},{2,0,1}};
    std::mt19937 random(20261106);
    for(ui trial=0;trial<12;++trial) {
        auto shuffled=events;std::shuffle(shuffled.begin(),shuffled.end(),random);
        Fixture fixture(3,3,std::move(shuffled));
        std::vector<VertexID> order{0,1,2};
        do {verifyDecision(fixture,order,false);}while(std::next_permutation(order.begin(),order.end()));
    }
    Fixture repair(3,5,{{0,2,1},{1,2,1},{2,0,2},{2,1,2}});
    verifyDecision(repair,{0,1,2},true);
    Fixture equalRepair(3,5,{{0,2,1},{1,2,1},{2,0,1},{2,1,1}});
    verifyDecision(equalRepair,{0,1,2},false);
}

void testLargeFallbackStrictness() {
    std::vector<Event> cycle,complete;
    for(ui u=0;u<9;++u) {
        cycle.push_back({u,(u+1)%9,1});
        for(ui v=0;v<9;++v)if(u!=v)complete.push_back({u,v,1});
    }
    Fixture invalid(9,3,std::move(cycle));
    verifyDecision(invalid,invalid.vertices,false);
    Fixture valid(9,3,std::move(complete));
    valid.graph->buildWindow(1,1,valid.vertices);
    verifyDecision(valid,valid.vertices,true);
}

}

int main() {
    try {
        OutputCapture capture;
        testTimestampBoundariesAndEqualEvents();
        testEqualTimestampOrderAndPrefixRepair();
        testLargeFallbackStrictness();
        std::cerr<<"Strict temporal regression passed (equal times, strict controls, boundary windows, prefix repair, large-k fallback)\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
