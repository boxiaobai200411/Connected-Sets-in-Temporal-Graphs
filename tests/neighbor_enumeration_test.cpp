#include "TemporalSCCsTestAccess.h"

#include <algorithm>
#include <chrono>
#if defined(__MINGW32__) && __GNUC__ < 9
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <filesystem>
#endif
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Groups = std::vector<std::vector<VertexID>>;

struct Event {
    VertexID source, target;
    ui time;
};

void require(bool condition,const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct OutputCapture {
    std::ostringstream output;
    std::streambuf* previous;

    OutputCapture() : previous(std::cout.rdbuf(output.rdbuf())) {}
    ~OutputCapture() { std::cout.rdbuf(previous); }
};

std::string createDirectory() {
    static ui sequence = 0;
    const auto name = "temporal_neighbor_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) +
        "_" + std::to_string(++sequence);
#if defined(__MINGW32__) && __GNUC__ < 9
    char base[MAX_PATH];
    const auto length = GetTempPathA(MAX_PATH,base);
    require(length > 0 && length < MAX_PATH,"Cannot locate temporary directory");
    const auto directory = std::string(base) + name;
    require(CreateDirectoryA(directory.c_str(),nullptr) != 0,"Cannot create test directory");
    return directory;
#else
    const auto directory = std::filesystem::temp_directory_path() / name;
    require(std::filesystem::create_directory(directory),"Cannot create test directory");
    return directory.generic_string();
#endif
}

void removeDirectory(const std::string& directory) {
#if defined(__MINGW32__) && __GNUC__ < 9
    DeleteFileA((directory + "/graph.txt").c_str());
    DeleteFileA((directory + "/graph_SCCs.txt").c_str());
    RemoveDirectoryA(directory.c_str());
#else
    std::error_code error;
    std::filesystem::remove_all(directory,error);
#endif
}

struct Fixture {
    std::string directory;
    std::vector<Event> events;
    std::vector<VertexID> vertices;
    std::unique_ptr<TemporalSCCsTestAccess> graph;

    Fixture(ui count,ui times,std::vector<Event> input)
        : directory(createDirectory()),events(std::move(input)) {
        for (VertexID v = 0; v < count; ++v) vertices.push_back(v);
        std::stable_sort(events.begin(),events.end(),
            [](const Event& a,const Event& b) { return a.time < b.time; });
        {
            std::ofstream output(directory + "/graph.txt");
            output << count << ' ' << events.size() << ' ' << times << '\n';
            for (const auto& edge : events)
                output << edge.source << ' ' << edge.target << ' ' << edge.time << '\n';
            require(output.good(),"Cannot write test graph");
        }
        {
            std::ofstream output(directory + "/graph_SCCs.txt");
            output << "1\n" << count << '\n';
            for (VertexID v : vertices) output << v << ' ';
            output << '\n';
            require(output.good(),"Cannot write test SCC");
        }
        graph = std::make_unique<TemporalSCCsTestAccess>(directory + "/","graph");
        graph->buildWindow(0,times - 1,vertices);
    }

    ~Fixture() {
        graph.reset();
        removeDirectory(directory);
    }
};

bool meetsGroups(const std::vector<VertexID>& chosen,const Groups& groups) {
    for (const auto& group : groups) {
        bool hit = false;
        for (VertexID v : group)
            if (std::find(chosen.begin(),chosen.end(),v) != chosen.end()) hit = true;
        if (!hit) return false;
    }
    return true;
}

bool temporalOracle(const Fixture& fixture,const std::vector<VertexID>& chosen) {
    const auto& window = fixture.graph->getWindow();
    std::vector<Event> local;
    for (const auto& edge : fixture.events) {
        if (edge.time < window.left || edge.time > window.right) continue;
        const auto source = std::find(chosen.begin(),chosen.end(),edge.source);
        const auto target = std::find(chosen.begin(),chosen.end(),edge.target);
        if (source != chosen.end() && target != chosen.end())
            local.push_back({static_cast<VertexID>(source - chosen.begin()),
                             static_cast<VertexID>(target - chosen.begin()),edge.time});
    }
    for (std::size_t source = 0; source < chosen.size(); ++source) {
        std::vector<unsigned char> reached(chosen.size(),0);
        reached[source] = 1;
        for (std::size_t first = 0; first < local.size();) {
            std::size_t last = first + 1;
            while (last < local.size() && local[last].time == local[first].time) ++last;
            const auto before = reached;
            for (std::size_t i = first; i < last; ++i) {
                const auto& edge = local[i];
                if (before[edge.source]) reached[edge.target] = 1;
            }
            first = last;
        }
        if (std::find(reached.begin(),reached.end(),0) != reached.end()) return false;
    }
    return true;
}

ul oracleCount(const Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
               const Groups& groups) {
    const auto& window = fixture.graph->getWindow();
    std::vector<unsigned char> selected(fixture.graph->getVertexCount(),0),active(selected.size(),0);
    for (VertexID v : vertices) selected[v] = 1;
    for (const auto& edge : fixture.events)
        if (edge.time >= window.left && edge.time <= window.right &&
            edge.source != edge.target && selected[edge.source] && selected[edge.target])
            active[edge.source] = active[edge.target] = 1;
    std::vector<VertexID> candidates,chosen;
    for (VertexID v : vertices)
        if (active[v]) candidates.push_back(v);
    ul count = 0;
    const auto choose = [&](auto&& self,std::size_t start) -> void {
        if (chosen.size() == k) {
            if (meetsGroups(chosen,groups) && temporalOracle(fixture,chosen)) ++count;
            return;
        }
        const auto need = k - chosen.size();
        for (std::size_t i = start; i < candidates.size() && need <= candidates.size() - i; ++i) {
            chosen.push_back(candidates[i]);
            self(self,i + 1);
            chosen.pop_back();
        }
    };
    choose(choose,0);
    return count;
}

ul enumerationCount(Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
                    const Groups& groups,bool optimized) {
    const auto before = fixture.graph->getAnswerCount();
    if (optimized) fixture.graph->enumerateOptimized(vertices,k,groups);
    else fixture.graph->enumerateCombinations(vertices,k,groups);
    return fixture.graph->getAnswerCount() - before;
}

ul compare(Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
           const Groups& groups = {},bool original = true) {
    const auto expected = oracleCount(fixture,vertices,k,groups);
    const auto actual = enumerationCount(fixture,vertices,k,groups,true);
    const auto context = "n=" + std::to_string(vertices.size()) + " k=" + std::to_string(k);
    require(actual == expected,"Neighbor enumeration mismatch (" + context + "): " +
            std::to_string(actual) + " != " + std::to_string(expected));
    if (original)
        require(enumerationCount(fixture,vertices,k,groups,false) == expected,
                "Original enumeration disagrees with independent oracle (" + context + ")");
    return actual;
}

void verifyEachSubset(Fixture& fixture,ui k) {
    std::vector<VertexID> chosen;
    const auto choose = [&](auto&& self,std::size_t start) -> void {
        if (chosen.size() == k) {
            Groups required;
            for (VertexID v : chosen) required.push_back({v});
            require(enumerationCount(fixture,fixture.vertices,k,required,true) ==
                    static_cast<ul>(temporalOracle(fixture,chosen)),
                    "A forced individual subset was omitted or counted repeatedly");
            return;
        }
        const auto need = k - chosen.size();
        for (std::size_t i = start; i < fixture.vertices.size() && need <= fixture.vertices.size() - i; ++i) {
            chosen.push_back(fixture.vertices[i]);
            self(self,i + 1);
            chosen.pop_back();
        }
    };
    choose(choose,0);
}

void testUniqueNeighbors() {
    std::vector<Event> cycleEvents{{4,5,4},{5,4,4}};
    for(ui t=0;t<4;++t)for(ui u=0;u<4;++u)cycleEvents.push_back({u,(u+1)%4,t});
    Fixture fixture(6,5,std::move(cycleEvents));
    require(compare(fixture,fixture.vertices,2) == 1 &&
            compare(fixture,fixture.vertices,3) == 0 &&
            compare(fixture,fixture.vertices,4) == 1,
            "Unique in/out neighbors must force the full four-cycle");
    compare(fixture,fixture.vertices,4,{{0},{2}});
    compare(fixture,fixture.vertices,4,{{0},{4}});
    auto order = std::vector<VertexID>{3,0,4,2,1,5};
    compare(fixture,order,4);
}

void testMultipleNeighborsAndOwnership() {
    std::vector<Event> edges;
    for (VertexID v = 1; v < 6; ++v) {
        edges.push_back({0,v,2});
        edges.push_back({v,0,1});
    }
    for(ui u=1;u<=3;++u)for(ui v=1;v<=3;++v)if(u!=v)edges.push_back({u,v,2});
    Fixture fixture(6,4,std::move(edges));
    require(compare(fixture,fixture.vertices,3) == 11 &&
            compare(fixture,fixture.vertices,4) == 10,
            "The star and leaf triangle must preserve all overlapping answers");
    require(compare(fixture,fixture.vertices,4,{{1,2},{2,3}}) == 8,
            "Overlapping ownership groups must allow one vertex to satisfy both");
    compare(fixture,fixture.vertices,4,{{1,1,2},{2,3},{99,5}});
    compare(fixture,fixture.vertices,4,{{2},{2},{2,4}});
    compare(fixture,fixture.vertices,4,{{99}});
    compare(fixture,fixture.vertices,4,{{}});
    verifyEachSubset(fixture,3);
    verifyEachSubset(fixture,4);
}

void testMultipleNeighborsWithoutCenter() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 6; ++u) {
        for(ui t=0;t<5;++t) {edges.push_back({u,(u + 1) % 6,t});
        edges.push_back({(u + 1) % 6,u,t});}
    }
    Fixture fixture(6,6,std::move(edges));
    for (ui k = 2; k < 6; ++k)
        require(compare(fixture,fixture.vertices,k) == 6,
                "A bidirectional cycle must enumerate every connected interval exactly once");
    compare(fixture,fixture.vertices,6);
    compare(fixture,fixture.vertices,4,{{1,3},{3,5}});
    verifyEachSubset(fixture,4);
}

void testBridgingLocalCycles() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 5; ++u) {
        for(ui t=0;t<5;++t) {edges.push_back({u,u + 1,t});
        edges.push_back({u + 1,u,t});}
    }
    Fixture fixture(6,6,std::move(edges));
    require(compare(fixture,fixture.vertices,4) == 3 &&
            compare(fixture,fixture.vertices,5) == 2 &&
            compare(fixture,fixture.vertices,6,{{0},{1},{4},{5}}) == 1,
            "Satisfied local cycles still need both bridge vertices");
    compare(fixture,fixture.vertices,5,{{0},{1},{4}});
    auto order = std::vector<VertexID>{0,1,4,5,2,3};
    compare(fixture,order,6);
    verifyEachSubset(fixture,4);
}

void testNonIncreasingForcedOrder() {
    const std::vector<VertexID> cycle = {0,5,2,4,1,3};
    std::vector<Event> edges;
    for(ui t=0;t<5;++t)for (std::size_t i = 0; i < cycle.size(); ++i)
        edges.push_back({cycle[i],cycle[(i + 1) % cycle.size()],t});
    Fixture fixture(7,6,std::move(edges));
    require(compare(fixture,fixture.vertices,6,{{0}}) == 1 &&
            compare(fixture,fixture.vertices,5,{{0}}) == 0,
            "Forced neighbor insertion must permit non-increasing local indices");
    auto order = fixture.vertices;
    std::reverse(order.begin(),order.end());
    compare(fixture,order,6,{{0},{5}});
}

void testTemporalOrderAndWindows() {
    Fixture simultaneous(4,5,{{0,1,0},{1,2,0},{2,3,0},{3,0,0},{0,1,0}});
    require(compare(simultaneous,simultaneous.vertices,4) == 0,
            "A same-time cycle including time zero must not form a strict temporal answer");
    simultaneous.graph->buildWindow(1,4,simultaneous.vertices);
    require(compare(simultaneous,simultaneous.vertices,4) == 0,
            "Rebuilding the window must discard old neighbor and time information");
    simultaneous.graph->buildWindow(0,0,simultaneous.vertices);
    compare(simultaneous,simultaneous.vertices,4);
    Fixture invalid(4,5,{{0,1,1},{1,2,4},{2,3,2},{3,0,3}});
    require(compare(invalid,invalid.vertices,4) == 0,
            "A static directed cycle with incompatible timestamps must fail temporal validation");
}

void testWordBoundaries() {
    const std::vector<VertexID> path = {0,63,64,127,128,129};
    std::vector<Event> edges;
    for (std::size_t i = 1; i < path.size(); ++i) {
        for(ui t=0;t<5;++t){edges.push_back({path[i - 1],path[i],t});
        edges.push_back({path[i],path[i - 1],t});}
    }
    Fixture fixture(130,6,std::move(edges));
    require(compare(fixture,fixture.vertices,2,{},false) == 5 &&
            compare(fixture,fixture.vertices,4,{},false) == 3 &&
            compare(fixture,fixture.vertices,6,{},false) == 1,
            "Sparse answers must survive multiple candidate bit-vector words");
    require(compare(fixture,fixture.vertices,4,{{0,63},{128,129}},false) == 1,
            "Ownership groups on separated bit-vector words must both be hit");
    auto order = fixture.vertices;
    std::reverse(order.begin(),order.end());
    compare(fixture,order,4,{{0,63},{128,129}},false);
}

void testRandomGraphs() {
    std::mt19937 random(20270202);
    for (ui trial = 0; trial < 40; ++trial) {
        const ui count = 4 + random() % 6;
        std::vector<Event> edges;
        for (VertexID u = 0; u < count; ++u)
            for (VertexID v = 0; v < count; ++v)
                if (u != v && random() % 100 < 15 + trial % 7 * 10) {
                    edges.push_back({u,v,static_cast<ui>(random() % 5)});
                    if (random() % 5 == 0) edges.push_back(edges.back());
                }
        Fixture fixture(count,5,std::move(edges));
        auto order = fixture.vertices;
        std::shuffle(order.begin(),order.end(),random);
        for (ui k = 2; k <= 6; ++k) {
            compare(fixture,order,k);
            compare(fixture,order,k,{{order[0],order[1]},{order[1],order.back()}});
            if (trial % 5 == 0) compare(fixture,order,k,{{order[0]},{count + 1}});
        }
    }
}

void testFallbackAndArguments() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 10; ++u) {
        for(ui t=0;t<9;++t){edges.push_back({u,(u + 1) % 10,t});
        edges.push_back({(u + 1) % 10,u,t});}
    }
    Fixture fixture(10,10,std::move(edges));
    require(compare(fixture,fixture.vertices,9) == 10 &&
            compare(fixture,fixture.vertices,9,{{0},{6}}) == 8 &&
            compare(fixture,fixture.vertices,10) == 1 &&
            compare(fixture,fixture.vertices,11) == 0,
            "k beyond eight must use the original enumeration behavior");
    bool invalid = false;
    try { fixture.graph->enumerateOptimized(fixture.vertices,1,{}); }
    catch (const std::invalid_argument&) { invalid = true; }
    require(invalid,"Neighbor enumeration must reject k below two");
}

void testQueryAndBaseline() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 6; ++u) {
        for(ui t=0;t<3;++t){edges.push_back({u,(u + 1) % 6,t});
        edges.push_back({(u + 1) % 6,u,t});}
    }
    edges.push_back({6,7,0});
    edges.push_back({7,8,2});
    edges.push_back({8,9,1});
    edges.push_back({9,6,3});
    edges.push_back({5,6,4});
    Fixture fixture(10,5,std::move(edges));
    for (ui window : {ui{0},ui{1},ui{2},ui{0}}) {
        const ui first = window;
        const ui last = window == 1 ? 1 : 4;
        for (bool plain : {false,true}) {
            OutputCapture capture;
            const auto answers = plain ? fixture.graph->enumerateCom(first,last,4)
                                       : fixture.graph->enumerateDec(first,last,4);
            const auto expected = oracleCount(fixture,fixture.vertices,4,{});
            require(answers == expected && fixture.graph->getAnswerCount() == expected &&
                    expected == (window == 0 ? 6 : 0),
                    "V1/enumerateCom queries must match the temporal oracle and reset count/window state");
            require(capture.output.str().empty(),"Algorithm queries must leave output to main");
        }
    }
}

}

int main() {
    try {
        {
            OutputCapture capture;
            testUniqueNeighbors();
            testMultipleNeighborsAndOwnership();
            testMultipleNeighborsWithoutCenter();
            testBridgingLocalCycles();
            testNonIncreasingForcedOrder();
            testTemporalOrderAndWindows();
            testWordBoundaries();
            testRandomGraphs();
            testFallbackAndArguments();
            testQueryAndBaseline();
        }
        std::cout << "neighbor_enumeration_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "neighbor_enumeration_test failed: " << error.what() << '\n';
        return 1;
    }
}
