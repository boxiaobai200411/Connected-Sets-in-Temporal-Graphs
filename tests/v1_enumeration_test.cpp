#include "temporal_test_support.h"
#include <random>

void compareV1(Fixture& f,const std::vector<VertexID>& vertices,const Groups& groups,ui k) {
    const ul expected=oracleCount(f,vertices,k,groups),before=f.graph->getAnswerCount();
    f.graph->enumerateOptimized(vertices,k,groups);
    require(f.graph->getAnswerCount()-before==expected,"V1 count differs from independent oracle");
}

int main() {
    try {
        OutputCapture capture;
        std::mt19937 rng(20261101);
        for(ui trial=0;trial<48;++trial) {
            std::vector<Event> events;
            for(ui a=0;a<8;++a)for(ui b=0;b<8;++b)if(a!=b && rng()%100<45)
                for(ui j=0;j<3;++j)events.push_back({a,b,static_cast<ui>(rng()%9)});
            Fixture f(8,10,std::move(events));
            auto order=f.vertices;std::shuffle(order.begin(),order.end(),rng);
            for(auto range:{std::pair<ui,ui>{0,9},{2,6},{4,4}}) {
                f.graph->buildWindow(range.first,range.second,f.vertices);
                for(ui k=2;k<=8;++k) {
                    compareV1(f,order,{{order[0],order[4]},{order[2],order[6]}},k);
                    if(trial%8==0)compareV1(f,order,{},k);
                }
            }
        }
        Fixture cuts(5,9,{{0,1,4},{1,0,4},{1,2,4},{2,3,4},{3,0,4},{2,4,4},{4,2,4}});
        for(ui k=2;k<=5;++k)compareV1(cuts,cuts.vertices,{},k);
        Fixture repair(5,10,{{0,1,1},{1,0,1},{1,2,2},{2,3,3},{3,4,4},{4,0,5}});
        for(ui k=2;k<=5;++k)compareV1(repair,repair.vertices,{},k);
        const std::array<ui,8> active{{0,63,64,129,1024,4095,4096,4204}};
        std::vector<Event> events;
        for(ui a:active)for(ui b:active)if(a!=b) {
            events.push_back({a,b,4});events.push_back({a,b,4});
        }
        Fixture large(4205,10,std::move(events));
        auto order=large.vertices;std::shuffle(order.begin(),order.end(),rng);
        Groups many;
        for(ui mask=0;mask<256;++mask)if(__builtin_popcount(mask)==4) {
            std::vector<VertexID> group;
            for(ui bit=0;bit<8;++bit)if(mask&(1u<<bit))group.push_back(active[bit]);
            many.push_back(std::move(group));
        }
        for(ui k:{3u,4u,5u,8u})compareV1(large,order,many,k);
        large.graph->buildWindow(5,8,large.vertices);
        compareV1(large,order,many,5);
        events.clear();
        for(ui a=0;a<32;++a)for(ui b=0;b<32;++b)
            if(a!=b && rng()%100<12)events.push_back({a,b,static_cast<ui>(rng()%9)});
        Fixture query(32,10,std::move(events));
        for(auto range:{std::pair<ui,ui>{0,9},{2,6},{4,4}}) {
            query.graph->buildWindow(range.first,range.second,query.vertices);
            const ul expected=oracleCount(query,query.vertices,3);
            query.graph->enumerateDec(range.first,range.second,3);
            require(query.graph->getAnswerCount()==expected,"V1 decomposition count differs");
        }
        const ul before=query.graph->getAnswerCount();
        bool invalid=false;
        try{query.graph->enumerateDec(9,2,3);}catch(const std::out_of_range&){invalid=true;}
        require(invalid && query.graph->getAnswerCount()==before,"Invalid V1 query changed state");
        Fixture fallback(9,2,{{0,1,1},{1,2,1},{2,3,1},{3,4,1},{4,5,1},{5,6,1},{6,7,1},{7,8,1},{8,0,1}});
        compareV1(fallback,fallback.vertices,{},9);
        std::cerr<<"V1 enumeration regression passed (independent oracle k2..9, ownership, cycles, repair, windows";
        std::cerr<<")\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
