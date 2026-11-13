#include "temporal_test_support.h"

#include <array>
#include <numeric>
#include <random>

namespace {

using Prefix=TemporalSCCsTestAccess::PrefixLabels;
constexpr ul noTime=std::numeric_limits<ul>::max();

std::vector<ul> oracleLabels(const Fixture& fixture,const std::vector<VertexID>& vertices,
    ui root,bool reverse) {
    std::vector<ul> labels(vertices.size(),noTime);
    const auto& window=fixture.graph->getWindow();
    labels[root]=reverse?static_cast<ul>(window.right)+1:window.left;
    bool changed;
    do {
        changed=false;
        for(const auto& edge:fixture.events) {
            if(edge.time<window.left || edge.time>window.right)continue;
            auto a=std::find(vertices.begin(),vertices.end(),edge.source);
            auto b=std::find(vertices.begin(),vertices.end(),edge.target);
            if(a==vertices.end() || b==vertices.end())continue;
            const ui u=static_cast<ui>(a-vertices.begin()),v=static_cast<ui>(b-vertices.begin());
            if(!reverse) {
                if((u==root || labels[u]<edge.time) && labels[v]>edge.time){labels[v]=edge.time;changed=true;}
            } else if(labels[v]!=noTime && edge.time<labels[v] &&
                (labels[u]==noTime || edge.time>labels[u])){labels[u]=edge.time;changed=true;}
        }
    }while(changed);
    return labels;
}

bool samePrefix(const Prefix& a,const Prefix& b) {
    return a.size==b.size && a.stronglyConnected==b.stronglyConnected &&
        a.earliest==b.earliest && a.latest==b.latest && a.missing==b.missing &&
        a.allArrivals==b.allArrivals && a.allDepartures==b.allDepartures;
}

void checkLabels(Fixture& f,const std::vector<VertexID>& vertices,
    const std::vector<ui>& chosen,const Prefix& labels) {
    std::vector<VertexID> original;
    for(ui v:chosen)original.push_back(vertices[v]);
    require(labels.size==chosen.size(),"Incremental prefix has the wrong size");
    std::array<ul,8> allArrivals{},allDepartures;
    allDepartures.fill(noTime);
    bool strong=true;
    for(ui source=0;source<chosen.size();++source) {
        const auto expected=oracleLabels(f,original,source,false);
        ui missing=0;
        for(ui target=0;target<chosen.size();++target) {
            require(labels.earliest[source*8+target]==expected[target],"Incremental EA differs from independent events");
            if(expected[target]==noTime){missing|=1u<<target;strong=false;}
            allArrivals[target]=std::max(allArrivals[target],expected[target]);
        }
        require(labels.missing[source]==missing,"Incremental prefix lost its missing old targets");
    }
    for(ui target=0;target<chosen.size();++target) {
        const auto expected=oracleLabels(f,original,target,true);
        for(ui source=0;source<chosen.size();++source) {
            require(labels.latest[source*8+target]==expected[source],"Incremental LD differs from independent events");
            allDepartures[source]=std::min(allDepartures[source],expected[source]);
        }
    }
    require(labels.stronglyConnected==strong,"Incremental strong flag differs from independent reachability");
    if(strong)for(ui v=0;v<chosen.size();++v)
        require(labels.allArrivals[v]==allArrivals[v] && labels.allDepartures[v]==allDepartures[v],
            "Incremental summary thresholds use the wrong minimum or maximum");
}

void exerciseOrder(Fixture& f,std::vector<VertexID> vertices,std::mt19937& random) {
    f.graph->prepareSubsetIndex(vertices);
    const auto index=f.graph->buildEnumerationIndex(vertices,{});
    std::vector<ui> order(vertices.size());std::iota(order.begin(),order.end(),0);
    std::shuffle(order.begin(),order.end(),random);
    std::vector<ui> chosen{order[0]};
    auto parent=f.graph->buildPrefixLabels(index,chosen);
    checkLabels(f,vertices,chosen,parent);
    for(ui depth=1;depth<7;++depth) {
        std::array<ui,64> pairs;pairs.fill(std::numeric_limits<ui>::max());
        for(ui a=0;a<chosen.size();++a)for(ui b=0;b<chosen.size();++b)
            pairs[a*8+b]=f.graph->findEnumerationPair(index,chosen[a],chosen[b]);
        const auto snapshot=parent;
        const auto pairSnapshot=pairs;
        for(ui branch: {depth,depth+1}) {
            const ui added=order[branch];
            const auto next=f.graph->extendPrefixLabels(index,chosen,parent,pairs,added);
            require(samePrefix(snapshot,parent) && pairs==pairSnapshot,"An extension modified its reusable parent");
            auto extended=chosen;extended.push_back(added);
            checkLabels(f,vertices,extended,next);
            std::vector<VertexID> original;
            for(ui v:extended)original.push_back(vertices[v]);
            for(ui tail:order)if(std::find(extended.begin(),extended.end(),tail)==extended.end()) {
                auto answer=original;answer.push_back(vertices[tail]);
                require(f.graph->isTemporalExtension(index,extended,next,tail)==temporalOracle(f,answer),
                    "Incrementally built labels changed the exact tail decision");
            }
        }
        parent=f.graph->extendPrefixLabels(index,chosen,parent,pairs,order[depth]);
        chosen.push_back(order[depth]);
    }
}

void testOldPairImprovement() {
    std::vector<Event> events;
    for(ui a=0;a<4;++a)for(ui b=0;b<4;++b)if(a!=b) {
        events.push_back({a,b,3});events.push_back({a,b,4});
    }
    events.insert(events.end(),{{0,4,1},{0,4,6},{4,1,2},{4,1,7}});
    Fixture f(5,8,std::move(events));
    f.graph->prepareSubsetIndex(f.vertices);
    const auto index=f.graph->buildEnumerationIndex(f.vertices,{});
    const std::vector<ui> chosen{0,1,2,3};
    const auto parent=f.graph->buildPrefixLabels(index,chosen);
    std::array<ui,64> pairs;pairs.fill(std::numeric_limits<ui>::max());
    for(ui a=0;a<4;++a)for(ui b=0;b<4;++b)pairs[a*8+b]=f.graph->findEnumerationPair(index,a,b);
    const auto next=f.graph->extendPrefixLabels(index,chosen,parent,pairs,4);
    require(parent.earliest[1]==3 && parent.latest[1]==4 &&
        next.earliest[1]==2 && next.latest[1]==6,
        "New vertices must improve old-old EA and LD, not merely append one row and column");
    checkLabels(f,f.vertices,{0,1,2,3,4},next);
}

}

int main() {
    try {
        OutputCapture capture;
        testOldPairImprovement();
        std::mt19937 random(2026110142);
        for(ui trial=0;trial<24;++trial) {
            std::vector<Event> events;
            for(ui a=0;a<9;++a)for(ui b=0;b<9;++b)if(a!=b && random()%100<20+trial%5*15)
                for(ui j=0;j<3;++j)events.push_back({a,b,static_cast<ui>(random()%9)});
            Fixture f(9,10,std::move(events));
            for(auto range:{std::pair<ui,ui>{0,9},{2,6},{4,4}}) {
                f.graph->buildWindow(range.first,range.second,f.vertices);
                auto vertices=f.vertices;std::shuffle(vertices.begin(),vertices.end(),random);
                exerciseOrder(f,std::move(vertices),random);
            }
        }
        std::cerr<<"prefix_labels_test passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"prefix_labels_test failed: "<<error.what()<<'\n';return 1;}
}
