#include "TemporalSCCsTestAccess.h"

#include <algorithm>
#include <array>
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
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <random>
#include <set>
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
    const auto name = "temporal_incremental_test_" + std::to_string(
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
            require(output.good(),"Cannot write incremental test graph");
        }
        {
            std::ofstream output(directory + "/graph_SCCs.txt");
            output << "1\n" << vertices.size() << '\n';
            for (VertexID v : vertices) output << v << ' ';
            output << '\n';
            require(output.good(),"Cannot write incremental test SCC file");
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
    std::vector<std::vector<std::pair<ui,ui>>> outgoing(chosen.size());
    for (const auto& edge : fixture.events) {
        if (edge.time < window.left || edge.time > window.right) continue;
        const auto source = std::find(chosen.begin(),chosen.end(),edge.source);
        const auto target = std::find(chosen.begin(),chosen.end(),edge.target);
        if (source != chosen.end() && target != chosen.end())
            outgoing[source - chosen.begin()].push_back(
                {static_cast<ui>(target - chosen.begin()),edge.time});
    }
    constexpr ui infinity = std::numeric_limits<ui>::max();
    for (ui source = 0; source < chosen.size(); ++source) {
        std::vector<ul> arrival(chosen.size(),std::numeric_limits<ul>::max());
        std::vector<unsigned char> queued(chosen.size(),0);
        std::queue<ui> pending;
        arrival[source] = window.left;
        pending.push(source);
        queued[source] = 1;
        while (!pending.empty()) {
            const ui u = pending.front();
            pending.pop();
            queued[u] = 0;
            for (const auto& edge : outgoing[u]) {
                if ((u != source && edge.second <= arrival[u]) || edge.second >= arrival[edge.first]) continue;
                arrival[edge.first] = edge.second;
                if (!queued[edge.first]) {
                    queued[edge.first] = 1;
                    pending.push(edge.first);
                }
            }
        }
        if (std::find(arrival.begin(),arrival.end(),std::numeric_limits<ul>::max()) != arrival.end()) return false;
    }
    return true;
}

ul oracleCount(const Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
               const Groups& groups = {}) {
    const auto& window = fixture.graph->getWindow();
    std::vector<unsigned char> selected(fixture.graph->getVertexCount(),0),active(selected.size(),0);
    for (VertexID v : vertices) selected[v] = 1;
    for (const auto& edge : fixture.events)
        if (edge.source != edge.target && edge.time >= window.left && edge.time <= window.right &&
            selected[edge.source] && selected[edge.target])
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
           const Groups& groups = {}) {
    const auto expected = oracleCount(fixture,vertices,k,groups);
    auto ordinaryVertices = vertices;
    if (vertices.size() > 32) {
        std::vector<unsigned char> included(fixture.vertices.size(),0), active(fixture.vertices.size(),0);
        for (VertexID v : vertices) included[v] = 1;
        const auto& window = fixture.graph->getWindow();
        for (const auto& edge : fixture.events)
            if (edge.source != edge.target && edge.time >= window.left && edge.time <= window.right &&
                included[edge.source] && included[edge.target]) active[edge.source] = active[edge.target] = 1;
        ordinaryVertices.erase(std::remove_if(ordinaryVertices.begin(),ordinaryVertices.end(),
            [&](VertexID v) { return active[v] == 0; }),ordinaryVertices.end());
    }
    for (bool optimized : {false,true}) {
        const auto actual = enumerationCount(fixture,optimized ? vertices : ordinaryVertices,k,groups,optimized);
        require(actual == expected,std::string(optimized ? "V1" : "Ordinary") + " enumeration mismatch n=" +
                std::to_string(vertices.size()) + " k=" + std::to_string(k) + ": " +
                std::to_string(actual) + " != " + std::to_string(expected));
    }
    return expected;
}

void verifyEachSubset(Fixture& fixture,ui k) {
    std::vector<VertexID> chosen;
    const auto choose = [&](auto&& self,std::size_t start) -> void {
        if (chosen.size() == k) {
            Groups groups;
            for (VertexID v : chosen) groups.push_back({v});
            require(enumerationCount(fixture,fixture.vertices,k,groups,true) ==
                    static_cast<ul>(temporalOracle(fixture,chosen)),
                    "An individually forced subset was omitted or counted twice");
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

std::vector<ul> oracleTimeLabels(const Fixture& fixture,const std::vector<VertexID>& chosen,
                               ui root,bool reverse) {
    const auto& window = fixture.graph->getWindow();
    constexpr ul infinity = std::numeric_limits<ul>::max();
    std::vector<ul> labels(chosen.size(),infinity);
    labels[root] = reverse ? static_cast<ul>(window.right)+1 : window.left;
    bool changed;
    do {
        changed = false;
        for (const auto& edge : fixture.events) {
            if (edge.time < window.left || edge.time > window.right) continue;
            const auto source = std::find(chosen.begin(),chosen.end(),edge.source);
            const auto target = std::find(chosen.begin(),chosen.end(),edge.target);
            if (source == chosen.end() || target == chosen.end()) continue;
            const auto u = static_cast<ui>(source - chosen.begin());
            const auto v = static_cast<ui>(target - chosen.begin());
            if (!reverse && (u == root || labels[u] < edge.time) && labels[v] > edge.time) {
                labels[v] = edge.time;
                changed = true;
            } else if (reverse && labels[v] != infinity && labels[v] > edge.time &&
                       (labels[u] == infinity || labels[u] < edge.time)) {
                labels[u] = edge.time;
                changed = true;
            }
        }
    } while (changed);
    return labels;
}

void checkAllPrefixes(Fixture& fixture,const std::vector<VertexID>& vertices) {
    fixture.graph->prepareSubsetIndex(vertices);
    const auto index = fixture.graph->buildEnumerationIndex(vertices,{});
    constexpr ul infinity = std::numeric_limits<ul>::max();
    std::vector<ui> chosen;
    const auto choose = [&](auto&& self,ui start) -> void {
        if (!chosen.empty()) {
            const auto prefix = fixture.graph->buildPrefixLabelsGeneral(index,chosen);
            require(prefix.size == chosen.size(),"The prefix state must preserve its local point count");
            std::vector<VertexID> original;
            for (ui v : chosen) original.push_back(vertices[v]);
            bool strong = true;
            for (ui root = 0; root < chosen.size(); ++root) {
                const auto earliest = oracleTimeLabels(fixture,original,root,false);
                const auto latest = oracleTimeLabels(fixture,original,root,true);
                ui missing = 0;
                for (ui v = 0; v < chosen.size(); ++v) {
                    require(prefix.earliest[root * TemporalSCCsTestAccess::kMaxEnumerationSize + v] == earliest[v] &&
                            prefix.latest[v * TemporalSCCsTestAccess::kMaxEnumerationSize + root] == latest[v],
                            "Prefix EA/LD values differ from independent event relaxation");
                    if (earliest[v] == infinity) missing |= ui{1} << v;
                }
                require(prefix.missing[root] == missing,
                        "Prefix missing targets must use local bit positions and exclude reflexive reachability");
                if (missing != 0) strong = false;
            }
            require(prefix.stronglyConnected == strong && strong == temporalOracle(fixture,original),
                    "The prefix strong-connectivity flag differs from the independent temporal oracle");
            for (ui v = 0; v < vertices.size(); ++v) {
                if (std::find(chosen.begin(),chosen.end(),v) != chosen.end()) continue;
                auto extension = original;
                extension.push_back(vertices[v]);
                auto local = chosen;
                local.push_back(v);
                const auto expected = temporalOracle(fixture,extension);
                require(fixture.graph->isTemporalExtension(index,chosen,prefix,v) == expected &&
                        fixture.graph->isTemporalSCCPacked(index,local) == expected,
                        "A prefix extension differs from independent temporal BFS or original packed validation");
            }
        }
        if (chosen.size() == TemporalSCCsTestAccess::kMaxEnumerationSize - 1) return;
        for (ui v = start; v < vertices.size(); ++v) {
            chosen.push_back(v);
            self(self,v + 1);
            chosen.pop_back();
        }
    };
    choose(choose,0);
}

void testRepairablePrefixes() {
    const std::vector<VertexID> cycle = {0,4,1,3,2};
    std::vector<Event> edges;
    for (ui t=0;t<5;++t)for (std::size_t i = 0; i < cycle.size(); ++i)
        edges.push_back({cycle[i],cycle[(i + 1) % cycle.size()],t});
    Fixture directed(6,6,std::move(edges));
    require(!temporalOracle(directed,{0,4,1,3}) &&
            temporalOracle(directed,{0,4,1,3,2}),
            "The cycle fixture must require a final vertex to repair its failed prefix");
    require(compare(directed,directed.vertices,5,{{0}}) == 1 &&
            compare(directed,directed.vertices,4) == 0,
            "A non-SCC prefix must be allowed to grow into its complete directed cycle");
    auto order = std::vector<VertexID>{4,0,5,1,3,2};
    compare(directed,order,5,{{0},{2}});
    checkAllPrefixes(directed,order);

    Fixture repaired(4,6,{{0,1,2},{1,2,1},{2,0,3},
        {0,3,3},{3,0,4},{1,3,3},{3,1,4},{2,3,3},{3,2,4}});
    require(!temporalOracle(repaired,{0,1,2}) && temporalOracle(repaired,{0,1,2,3}) &&
            compare(repaired,repaired.vertices,4) == 1,
            "A temporally invalid prefix may become valid through the newly added vertex");
    verifyEachSubset(repaired,3);
    checkAllPrefixes(repaired,repaired.vertices);
}

void testIncompatibleExtension() {
    Fixture fixture(4,6,{{0,1,3},{1,0,3},{0,2,3},{2,0,3},{1,2,3},{2,1,3},{3,0,1},{0,3,2}});
    require(temporalOracle(fixture,{0,1,2}) && !temporalOracle(fixture,{0,1,2,3}),
            "The valid prefix must lose strong temporal reachability after its incompatible extension");
    require(compare(fixture,fixture.vertices,3) == 1 && compare(fixture,fixture.vertices,4) == 0,
            "An already valid prefix cannot certify an incompatible new vertex");
    compare(fixture,{3,2,0,1},4);
    verifyEachSubset(fixture,3);
    checkAllPrefixes(fixture,{3,2,0,1});
}

void testFalseBidirectionalCoverage() {
    Fixture fixture(3,7,{{0,2,5},{2,1,1},{1,2,1},{2,0,5}});
    require(!temporalOracle(fixture,{0,1,2}) && compare(fixture,fixture.vertices,3) == 0,
            "Two-way new-point coverage must not certify an impossible 5-then-1 path");
    checkAllPrefixes(fixture,fixture.vertices);
    Fixture equal(3,7,{{0,2,3},{2,1,3},{1,2,3},{2,0,3}});
    require(compare(equal,equal.vertices,3) == 0,
            "An EA==LD join must be rejected for strict temporal paths");
    checkAllPrefixes(equal,{1,0,2});
}

void testEqualTimesAndWindows() {
    Fixture fixture(6,8,{{4,5,0},{2,3,0},{0,1,0},{5,0,0},{3,4,0},{1,2,0},
        {0,1,0},{0,0,0},{1,1,2},{0,1,7},{1,2,7},{2,3,7},{3,4,7},{4,5,7},{5,0,7}});
    require(compare(fixture,fixture.vertices,6) == 0 &&
            compare(fixture,fixture.vertices,5) == 0,
            "Two timestamps cannot cover a six-point cycle under strict temporal paths");
    checkAllPrefixes(fixture,fixture.vertices);
    for (const auto& window : std::vector<std::pair<ui,ui>>{{1,6},{7,7},{0,0},{0,7}}) {
        fixture.graph->buildWindow(window.first,window.second,fixture.vertices);
        require(compare(fixture,fixture.vertices,6) == 0,
                "Window changes must reset the incremental time index and reachable state");
        if (window.first == 7) checkAllPrefixes(fixture,{5,3,1,4,2,0});
    }
    Fixture invalid(4,8,{{0,1,7},{1,2,1},{2,3,5},{3,0,3}});
    require(compare(invalid,invalid.vertices,4) == 0,
            "A static directed cycle is insufficient when its times cannot be ordered");
}

void testCompleteGraphAndOwnership() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 12; ++u)
        for (VertexID v = 0; v < 12; ++v)
            if (u != v) edges.push_back({u,v,static_cast<ui>((u + 3 * v) % 7)});
    Fixture fixture(12,7,std::move(edges));
    ul combinations = 12;
    for (ui k = 2; k <= 6; ++k) {
        combinations = combinations * (13 - k) / k;
        require(compare(fixture,fixture.vertices,k) == combinations,
                "Every complete-graph k-subset must be emitted exactly once");
        compare(fixture,fixture.vertices,k,{{0,1},{1,2},{2,3}});
    }
    require(compare(fixture,fixture.vertices,4,{{}}) == 0 &&
            compare(fixture,fixture.vertices,4,{{99}}) == 0,
            "Empty or unavailable required groups must reject every completion");
    compare(fixture,fixture.vertices,4,{{0,0,1},{1,2},{2,3},{3}});
    Groups groups(64,std::vector<VertexID>{0});
    groups.back() = {5};
    require(compare(fixture,fixture.vertices,4,groups) == 45,
            "The highest AOP bit must force its distinct point in a 64-group mask");
    groups.push_back({7});
    require(compare(fixture,fixture.vertices,4,groups) == 9,
            "More than 64 retained AOP groups must preserve every required hit");
    groups.back().clear();
    compare(fixture,fixture.vertices,4,groups);
}

Groups distinctAnchoredGroups(ui vertices,ui count) {
    Groups groups;
    std::vector<VertexID> chosen;
    for (ui size = 2; size <= 4 && groups.size() < count; ++size) {
        const auto generate = [&](auto&& self,VertexID start) -> void {
            if (groups.size() == count) return;
            if (chosen.size() == size) {
                if (chosen.front() <= 1) groups.push_back(chosen);
                return;
            }
            const ui need = size - static_cast<ui>(chosen.size());
            for (VertexID v = start; v < vertices && vertices - v >= need; ++v) {
                chosen.push_back(v);
                self(self,v + 1);
                chosen.pop_back();
                if (groups.size() == count) return;
            }
        };
        generate(generate,0);
    }
    require(groups.size() == count,"Not enough distinct anchored ownership groups");
    return groups;
}

Groups independentExclusionGroups(ui vertices,ui count) {
    Groups groups;
    std::vector<VertexID> omitted;
    const auto generate = [&](auto&& self,VertexID start) -> void {
        if (groups.size() == count) return;
        if (omitted.size() == 4) {
            std::vector<VertexID> group;
            for (VertexID v = 0; v < vertices; ++v)
                if (std::find(omitted.begin(),omitted.end(),v) == omitted.end()) group.push_back(v);
            groups.push_back(std::move(group));
            return;
        }
        const ui need = 4 - static_cast<ui>(omitted.size());
        for (VertexID v = start; v < vertices && vertices - v >= need; ++v) {
            omitted.push_back(v);
            self(self,v + 1);
            omitted.pop_back();
            if (groups.size() == count) return;
        }
    };
    generate(generate,0);
    require(groups.size() == count,"Not enough distinct four-subset exclusions");
    return groups;
}

void requireRetainedDistinctGroups(Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
                                  const Groups& groups) {
    require(std::set<std::vector<VertexID>>(groups.begin(),groups.end()).size() == groups.size(),
            "Multiword ownership fixtures must use genuinely distinct groups");
    fixture.graph->prepareSubsetIndex(vertices);
    const auto normalized = fixture.graph->normalizeRequiredGroups(vertices,groups,k);
    require(normalized && normalized->size() == groups.size(),
            "Every multiword fixture group must survive normalization");
}

void testIndependentMultiwordOwnership() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 12; ++u)
        for (VertexID v = 0; v < 12; ++v)
            if (u != v) edges.push_back({u,v,3});
    Fixture fixture(12,7,std::move(edges));
    auto order = fixture.vertices;
    std::mt19937 random(20271108);
    for (ui count : {63u,64u,65u,127u,128u,129u,257u}) {
        const auto groups = independentExclusionGroups(12,count);
        requireRetainedDistinctGroups(fixture,fixture.vertices,4,groups);
        require(compare(fixture,fixture.vertices,4,groups) == 495 - count,
                "Every distinct complement constraint must exclude exactly its own four-point answer");
        std::shuffle(order.begin(),order.end(),random);
        require(compare(fixture,order,4,groups) == 495 - count,
                "Ownership word boundaries must be independent of candidate order");
    }
    const auto groups = independentExclusionGroups(12,257);
    fixture.graph->buildWindow(3,3,fixture.vertices);
    require(compare(fixture,order,4,groups) == 238,"Equal-time ownership query lost its independent exclusions");
    fixture.graph->buildWindow(4,6,fixture.vertices);
    require(compare(fixture,order,4,groups) == 0,"An empty window cannot retain previous multiword answers");
    fixture.graph->buildWindow(0,6,fixture.vertices);
    require(compare(fixture,order,4,groups) == 238,"Restored windows must rebuild multiword ownership state");
}

void testSmallMultiwordOwnershipGroups() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 12; ++u)
        for (VertexID v = 0; v < 12; ++v)
            if (u != v) edges.push_back({u,v,3});
    Fixture fixture(12,7,std::move(edges));
    auto order = fixture.vertices;
    std::mt19937 random(20271109);
    for (ui count : {63u,64u,65u,127u,128u,129u,257u}) {
        auto groups = distinctAnchoredGroups(12,count - 1);
        const auto before = groups;
        groups.push_back({10,11});
        for (ui k = 2; k <= 6; ++k) {
            std::vector<VertexID> witness;
            for (VertexID v = 0; v < k; ++v) witness.push_back(v);
            require(meetsGroups(witness,before) && !meetsGroups(witness,groups) && temporalOracle(fixture,witness),
                    "The highest ownership bit must reject an otherwise legal witness");
            requireRetainedDistinctGroups(fixture,fixture.vertices,k,groups);
            std::shuffle(order.begin(),order.end(),random);
            const auto actual = compare(fixture,order,k,groups);
            require(actual < oracleCount(fixture,order,k,before),
                    "A decisive high ownership bit must reduce the actual answer count");
        }
    }
    auto disjoint = distinctAnchoredGroups(12,253);
    for (VertexID v : {2u,4u,6u,8u}) disjoint.push_back({v,v + 1});
    requireRetainedDistinctGroups(fixture,fixture.vertices,4,disjoint);
    require(compare(fixture,fixture.vertices,4,disjoint) == 0,
            "Five pairwise-disjoint ownership groups cannot fit in four answer positions");
}

void testMultiwordCenterCompletions() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 20; ++u)
        for (VertexID v = 0; v < 20; ++v)
            if (u != v) { edges.push_back({u,v,1});edges.push_back({u,v,3});edges.push_back({u,v,5}); }
    Fixture fixture(20,7,std::move(edges));
    fixture.graph->prepareSubsetIndex(fixture.vertices);
    const auto index = fixture.graph->buildEnumerationIndex(fixture.vertices,{});
    const std::vector<ul> candidates{(ul{1} << 20) - 2};
    require(fixture.graph->hasEnumerationCenter(index,{0},candidates) &&
            TemporalSCCsTestAccess::countKCombinationsCapped(19,3,64) == 64,
            "The complete-graph fixture must enter center-certified combination completion");
    for (ui count : {65u,129u,257u}) {
        const auto groups = independentExclusionGroups(20,count);
        requireRetainedDistinctGroups(fixture,fixture.vertices,4,groups);
        ui missingAtRoot = 0;
        for (const auto& group : groups)
            if (std::find(group.begin(),group.end(),0) == group.end()) ++missingAtRoot;
        require(missingAtRoot == count,"All center completion constraints must remain missing after root zero");
        require(compare(fixture,fixture.vertices,4,groups) == 4845 - count,
                "Multiword center completion must preserve each independent excluded answer");
    }
}

void testMultiwordNeighborAndPrefix() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 3; ++u)
        for (VertexID v = 0; v < 3; ++v)
            if (u != v) edges.push_back({u,v,3});
    for (VertexID v = 3; v < 12; ++v) {
        edges.push_back({2,v,v % 2 == 0 ? 4u : 1u});
        edges.push_back({v,2,v % 2 == 0 ? 2u : 5u});
    }
    Fixture fixture(12,7,std::move(edges));
    fixture.graph->prepareSubsetIndex(fixture.vertices);
    const auto index = fixture.graph->buildEnumerationIndex(fixture.vertices,{});
    const std::vector<ul> candidates{(ul{1} << 12) - 2};
    require(!fixture.graph->hasEnumerationCenter(index,{0},candidates),
            "The sparse multiword fixture must exercise the ordinary neighbor path");
    require(temporalOracle(fixture,{0,1,2,10}) && !temporalOracle(fixture,{0,1,2,11}),
            "Shared-prefix tail choices must contain both a valid and an invalid temporal extension");
    for (ui count : {65u,129u,257u}) {
        auto groups = distinctAnchoredGroups(12,count - 1);
        groups.push_back({10,11});
        requireRetainedDistinctGroups(fixture,fixture.vertices,4,groups);
        require(compare(fixture,fixture.vertices,4,groups) != 0,
                "Multiword ownership must preserve the valid ordinary-neighbor completion");
        auto order = fixture.vertices;
        std::reverse(order.begin(),order.end());
        compare(fixture,order,4,groups);
    }
}

void testSparseMultiwordOwnershipSmoke() {
    std::vector<Event> edges;
    for (VertexID v = 1; v < 6; ++v) {
        for(ui t=1;t<=5;++t) { edges.push_back({v - 1,v,t});
        edges.push_back({v,v - 1,t}); }
    }
    Fixture fixture(130,7,std::move(edges));
    const auto groups = distinctAnchoredGroups(130,257);
    auto order = fixture.vertices;
    std::reverse(order.begin(),order.end());
    requireRetainedDistinctGroups(fixture,order,6,groups);
    require(oracleCount(fixture,order,6,groups) == 1 &&
            enumerationCount(fixture,order,6,groups,true) == 1,
            "Sparse multiword V1 must enumerate its sole connected completion across candidate words");
}

void testLargeSparseEnumerationV1() {
    for (ui count : {4100u,4200u}) {
        const std::vector<VertexID> active{0,63,64,2048,4096,count - 1};
        std::vector<Event> edges;
        for (std::size_t i = 1; i < active.size(); ++i) {
            for(ui t=1;t<=5;++t) { edges.push_back({active[i - 1],active[i],t});
            edges.push_back({active[i],active[i - 1],t}); }
        }
        Fixture fixture(count,7,std::move(edges));
        auto groups = distinctAnchoredGroups(130,257);
        for (auto& group : groups) {
            for (auto& v : group) {
                if (v == 1) v = 63;
                else if (v == 63) v = 1;
            }
            std::sort(group.begin(),group.end());
        }
        auto reverse = fixture.vertices;
        std::reverse(reverse.begin(),reverse.end());
        for (ui k = 4; k <= 6; ++k) {
            const ul expected = oracleCount(fixture,fixture.vertices,k);
            require(expected == 7 - k,"Large sparse oracle must find only connected path intervals");
            require(enumerationCount(fixture,fixture.vertices,k,{},true) == expected &&
                    enumerationCount(fixture,reverse,k,{},true) == expected,
                    "Large sparse V1 must preserve every answer under reversed local indices");
            requireRetainedDistinctGroups(fixture,reverse,k,groups);
            require(oracleCount(fixture,reverse,k,groups) == 1 &&
                    enumerationCount(fixture,reverse,k,groups,true) == 1,
                    "Large sparse V1 must preserve all 257 ownership groups across candidate words");
        }
    }
}

void checkCompressedEnumerationIndexV1(Fixture& fixture,const std::vector<VertexID>& vertices) {
    fixture.graph->prepareSubsetIndex(vertices);
    const auto index = fixture.graph->buildEnumerationIndex(vertices,{});
    constexpr ui noPair = std::numeric_limits<ui>::max();
    std::vector<ui> local(fixture.graph->getVertexCount(),noPair);
    for (ui i = 0; i < vertices.size(); ++i) local[vertices[i]] = i;
    std::map<std::pair<ui,ui>,std::vector<ui>> expected;
    const auto& window = fixture.graph->getWindow();
    for (const auto& edge : fixture.events)
        if (edge.source != edge.target && edge.time >= window.left && edge.time <= window.right &&
            local[edge.source] != noPair && local[edge.target] != noPair)
            expected[{local[edge.source],local[edge.target]}].push_back(edge.time);
    for (auto& item : expected) {
        auto& times = item.second;
        std::sort(times.begin(),times.end());
        times.erase(std::unique(times.begin(),times.end()),times.end());
    }
    require(!index.pairOffsets.empty() && index.pairIndex.size() == expected.size() &&
            index.pairTimes.size() == expected.size(),
            "Large pair indices must store only distinct existing directed pairs");
    const auto checkPair = [&](ui source,ui target) {
        const auto pair = fixture.graph->findEnumerationPair(index,source,target);
        const auto found = expected.find({source,target});
        if (found == expected.end())
            require(pair == noPair,"Compressed pair lookup must return noPair for every absent edge");
        else
            require(pair < index.pairTimes.size() && index.pairTimes[pair] == found->second,
                    "Compressed pair lookup must preserve sorted unique timestamps of the exact directed pair");
    };
    for (const auto& item : expected) checkPair(item.first.first,item.first.second);
    const std::vector<ui> boundaries{0,1,62,63,64,65,127,128,2047,2048,4095,4096,index.size - 1};
    for (ui source : boundaries)
        for (ui target : boundaries) checkPair(source,target);
    std::mt19937 random(20271110);
    for (ui sample = 0; sample < 1500; ++sample) checkPair(random() % index.size,random() % index.size);

    const std::vector<VertexID> active{0,63,64,2048,4096,fixture.graph->getVertexCount() - 1};
    std::vector<ul> candidates(index.words,0);
    for (VertexID v : {VertexID{0},VertexID{64}})
        candidates[local[v] / 64] |= ul{1} << (local[v] % 64);
    require(fixture.graph->hasEnumerationCenter(index,{local[63]},candidates)==
                (fixture.graph->getWindow().left<fixture.graph->getWindow().right),
            "Center verification must resolve actual pairs through the compressed index");
    candidates[local[1] / 64] |= ul{1} << (local[1] % 64);
    require(!fixture.graph->hasEnumerationCenter(index,{local[63]},candidates),
            "An isolated candidate must invalidate a center certificate in the large index");

    std::vector<ui> chosen;
    std::vector<VertexID> original;
    const auto prefixes = [&](auto&& self,ui start) -> void {
        if (!chosen.empty()) {
            const auto prefix = fixture.graph->buildPrefixLabelsGeneral(index,chosen);
            for (ui root = 0; root < chosen.size(); ++root) {
                const auto earliest = oracleTimeLabels(fixture,original,root,false);
                const auto latest = oracleTimeLabels(fixture,original,root,true);
                for (ui v = 0; v < chosen.size(); ++v)
                    require(prefix.earliest[root * TemporalSCCsTestAccess::kMaxEnumerationSize + v] == earliest[v] &&
                            prefix.latest[v * TemporalSCCsTestAccess::kMaxEnumerationSize + root] == latest[v],
                            "Compressed prefix labels must match independent event relaxation");
            }
            if (chosen.size() >= 2)
                require(fixture.graph->isTemporalSCCPacked(index,chosen) == temporalOracle(fixture,original),
                        "Packed validation must preserve induced reachability in the compressed index");
            auto extensions = active;
            extensions.push_back(1);
            for (VertexID v : extensions) {
                if (std::find(original.begin(),original.end(),v) != original.end()) continue;
                auto full = original;
                full.push_back(v);
                auto packed = chosen;
                packed.push_back(local[v]);
                const bool expectedAnswer = temporalOracle(fixture,full);
                require(fixture.graph->isTemporalExtension(index,chosen,prefix,local[v]) == expectedAnswer &&
                        fixture.graph->isTemporalSCCPacked(index,packed) == expectedAnswer,
                        "Compressed prefix extension and packed validation must match the independent oracle");
            }
        }
        if (chosen.size() == 5) return;
        for (ui v = start; v < active.size(); ++v) {
            chosen.push_back(local[active[v]]);
            original.push_back(active[v]);
            self(self,v + 1);
            original.pop_back();
            chosen.pop_back();
        }
    };
    prefixes(prefixes,0);
}

void testCompressedEnumerationIndexV1() {
    const ui count = 4200;
    const std::vector<VertexID> active{0,63,64,2048,4096,count - 1};
    std::vector<Event> edges;
    for (std::size_t i = 1; i < active.size(); ++i) {
        for(ui t:{1u,3u,5u}){edges.push_back({active[i - 1],active[i],t});
        edges.push_back({active[i],active[i - 1],t});}
    }
    edges.push_back({0,64,3});
    edges.push_back({64,0,3});
    for (ui time : {1u,1u,3u,5u,5u}) edges.push_back({0,63,time});
    edges.push_back({0,0,2});
    Fixture fixture(count,7,std::move(edges));
    checkCompressedEnumerationIndexV1(fixture,fixture.vertices);
    auto reverse = fixture.vertices;
    std::reverse(reverse.begin(),reverse.end());
    checkCompressedEnumerationIndexV1(fixture,reverse);
    fixture.graph->buildWindow(3,3,fixture.vertices);
    checkCompressedEnumerationIndexV1(fixture,reverse);
}

void testCandidateWordBoundaries() {
    const std::vector<VertexID> path = {0,63,64,127,128,129};
    std::vector<Event> edges;
    for (std::size_t i = 1; i < path.size(); ++i) {
        for(ui t=0;t<5;++t){edges.push_back({path[i - 1],path[i],t});
        edges.push_back({path[i],path[i - 1],t});}
    }
    Fixture fixture(130,6,std::move(edges));
    for (ui k = 2; k <= 6; ++k)
        require(compare(fixture,fixture.vertices,k) == 7 - k,
                "Sparse answers must survive all candidate bit-vector words");
    require(compare(fixture,fixture.vertices,4,{{0,63},{128,129}}) == 1,
            "Both separated ownership groups must remain enforceable across words");
    auto order = fixture.vertices;
    std::reverse(order.begin(),order.end());
    compare(fixture,order,4,{{0,63},{128,129}});
    compare(fixture,path,6);
}

void testRandomGraphs() {
    std::mt19937 random(20271028);
    for (ui trial = 0; trial < 96; ++trial) {
        const ui count = 4 + random() % 6;
        std::vector<Event> edges;
        for (VertexID u = 0; u < count; ++u)
            for (VertexID v = 0; v < count; ++v)
                if (random() % 100 < 12 + trial % 8 * 9) {
                    edges.push_back({u,v,static_cast<ui>(random() % 7)});
                    if (random() % 7 == 0) edges.push_back(edges.back());
                }
        Fixture fixture(count,7,std::move(edges));
        auto order = fixture.vertices;
        std::shuffle(order.begin(),order.end(),random);
        for (ui k = 2; k <= 6; ++k) {
            compare(fixture,order,k);
            compare(fixture,order,k,{{order[0],order[1]},{order[1],order.back()}});
        }
        if (trial % 8 == 0) {
            fixture.graph->buildWindow(2,5,fixture.vertices);
            compare(fixture,order,4,{{order[0]},{order.back()}});
            verifyEachSubset(fixture,3);
        }
        if (trial % 12 == 0) checkAllPrefixes(fixture,order);
    }
}

long double combinations(ui n,ui k) {
    if (n < k) return 0;
    long double count = 1;
    for (ui i = 1; i <= std::min(k,n - k); ++i)
        count = count * (n - i + 1) / i;
    return count;
}

void compareQueries(Fixture& fixture,ui first,ui last,ui k) {
    for (bool optimized : {false,true}) {
        OutputCapture capture;
        const auto answers = optimized ? fixture.graph->enumerateDec(first,last,k)
                                       : fixture.graph->enumerateCom(first,last,k);
        const auto expected = oracleCount(fixture,fixture.vertices,k);
        const auto actual = fixture.graph->getAnswerCount();
        require(answers == actual && actual == expected,
                "Baseline and V1 queries must reset counts and match the induced temporal oracle");
        require(capture.output.str().empty(),"Full queries must not print decomposition diagnostics");
    }
}

void testFullQueriesAndSearch() {
    std::vector<Event> edges;
    for (VertexID v = 0; v < 34; ++v) {
        edges.push_back({v,v + 1,v % 4});
        edges.push_back({v + 1,v,v % 4});
    }
    for (VertexID u = 0; u < 3; ++u)
        for (VertexID v = 0; v < 3; ++v)
            if (u != v) edges.push_back({u,v,1});
    Fixture recursive(35,4,std::move(edges));
    compareQueries(recursive,0,3,3);
    compareQueries(recursive,1,2,3);
    compareQueries(recursive,0,3,3);
    const Groups required = {{0,1},{1,2}};
    auto order = recursive.vertices;
    std::reverse(order.begin(),order.end());
    const auto expected = oracleCount(recursive,order,3,required);
    {
        const auto before = recursive.graph->getAnswerCount();
        recursive.graph->decompose(order,3,required);
        require(recursive.graph->getAnswerCount() - before == expected,
                "Direct search must preserve AOP answer counts");
    }
    Fixture separated(10,5,{{0,1,1},{1,2,1},{2,3,1},{3,0,1},
        {4,5,0},{5,6,1},{6,7,2},{7,4,3},{8,9,1},{9,8,1},{3,4,4}});
    compareQueries(separated,0,4,4);
    compareQueries(separated,2,4,4);
    compareQueries(separated,1,1,4);
}

void testPivotQueryAnswers() {
    std::mt19937 random(20271105);
    for (ui trial = 0; trial < 24; ++trial) {
        const ui count = 6 + random() % 5;
        std::vector<Event> edges;
        for (VertexID u = 0; u < count; ++u)
            for (VertexID v = 0; v < count; ++v)
                if (u != v && random() % 100 < 12 + trial % 7 * 10)
                    edges.push_back({u,v,static_cast<ui>(random() % 7)});
        if (trial % 4 == 0)
            for (VertexID u = 0; u < count; ++u)
                edges.push_back({u,(u + 1) % count,3});
        Fixture fixture(count,7,std::move(edges));
        for (ui k = 2; k <= 6; ++k)
            compareQueries(fixture,0,6,k);
        if (trial % 3 == 0) compareQueries(fixture,2,5,4);
    }
    for (ui k : {ui{2},ui{4},ui{5},ui{6}}) {
        const ui count = k == 2 ? 92 : k == 4 ? 20 : 16;
        std::vector<Event> edges;
        for (VertexID v = 1; v < count; ++v) {
            const ui time = random() % 7;
            edges.push_back({v - 1,v,time});
            edges.push_back({v,v - 1,time});
            if (v > 2 && random() % 3 == 0) {
                edges.push_back({v - 3,v,static_cast<ui>(random() % 7)});
                edges.push_back({v,v - 3,static_cast<ui>(random() % 7)});
            }
        }
        Fixture fixture(count,7,std::move(edges));
        fixture.graph->setDecompositionAlpha(0);
        require(!fixture.graph->shouldEnumerateDirectly(fixture.vertices,k),
            "Large pivot query fixture must exceed the alpha-zero size threshold");
        compareQueries(fixture,0,6,k);
    }
}

long double referencePivotScore(Fixture& fixture,const std::vector<VertexID>& subset,ui k,
                                std::size_t& largest) {
    largest = 0;
    long double score = 0;
    for (const auto& component : fixture.graph->findWindowStaticSCCs(subset,fixture.graph->getWindow(),k - 1)) {
        largest = std::max(largest,component.size());
        score += combinations(static_cast<ui>(component.size()),k);
    }
    return score;
}

void checkPivotIndexRestored(Fixture& fixture,const std::vector<VertexID>& vertices,ui k) {
    const auto groups = fixture.graph->normalizeRequiredGroups(vertices,{{vertices.front()},{vertices.back()}},k);
    require(groups.has_value(),"Pivot scoring must restore the complete parent candidate index");
    for (VertexID v : fixture.vertices)
        if (std::find(vertices.begin(),vertices.end(),v) == vertices.end()) {
            require(!fixture.graph->normalizeRequiredGroups(vertices,{{v}},k),
                    "Pivot scoring must exclude vertices outside its indexed parent");
            break;
        }
}

void testPivotSubsetScoringV1() {
    std::mt19937 random(20271106);
    for (ui trial = 0; trial < 40; ++trial) {
        const ui count = 7 + random() % 5;
        std::vector<Event> edges;
        for (VertexID u = 0; u < count; ++u)
            for (VertexID v = 0; v < count; ++v)
                if (random() % 100 < 10 + trial % 8 * 11) {
                    edges.push_back({u,v,static_cast<ui>(random() % 7)});
                    if (random() % 4 == 0) edges.push_back(edges.back());
                }
        Fixture fixture(count,7,std::move(edges));
        auto vertices = fixture.vertices;
        std::shuffle(vertices.begin(),vertices.end(),random);
        vertices.pop_back();
        VertexID degreePivot, neighborPivot;
        auto index = fixture.graph->buildPivotIndex(vertices,degreePivot,neighborPivot);
        require(index.vertices == vertices && index.outgoing.size() == vertices.size() &&
                std::find(vertices.begin(),vertices.end(),degreePivot) != vertices.end() &&
                std::find(vertices.begin(),vertices.end(),neighborPivot) != vertices.end(),
                "Pivot index representatives and adjacency must use the indexed parent set");
        for (ui k = 2; k <= 6; ++k) {
            for (ui sample = 0; sample < 14; ++sample) {
                std::vector<VertexID> subset;
                for (VertexID v : vertices)
                    if (sample == 0 || (sample > 1 && random() % 2 != 0)) subset.push_back(v);
                std::size_t expectedLargest = 0, actualLargest = 0;
                const long double expected = referencePivotScore(fixture,subset,k,expectedLargest);
                fixture.graph->prepareSubsetIndex({fixture.vertices.back()});
                const auto actual = fixture.graph->scorePivotSubset(subset,k,index,actualLargest);
                require(actual == expected && actualLargest == expectedLargest,
                        "Local pivot SCC score and largest component must match the independent SCC interface");
                checkPivotIndexRestored(fixture,vertices,k);
                for (long double cutoff : {0.0L, expected / 2, expected}) {
                    actualLargest = vertices.size() + 1;
                    const auto partial = fixture.graph->scorePivotSubset(subset,k,index,actualLargest,cutoff);
                    if (expected > cutoff)
                        require(partial > cutoff && partial <= expected,
                                "A truncated pivot score must be an exceeded-cutoff lower bound");
                    else
                        require(partial == expected && actualLargest == expectedLargest,
                                "A completed cutoff score must remain exact");
                    checkPivotIndexRestored(fixture,vertices,k);
                }
            }
        }
    }
}

void testPrefixTimePaths() {
    const std::vector<Event> base = {{0,1,6},{1,2,2},{1,2,6},{2,3,2},{2,3,6},
        {3,0,6},{0,4,2},{4,1,2},{3,4,4},{4,0,4}};
    for (ui skipBetter = 0; skipBetter < 2; ++skipBetter) {
        auto events = base;
        if (skipBetter != 0) events.push_back({0,2,1});
        for (ui dual = 0; dual < 2; ++dual) {
            auto directed = events;
            if (dual != 0)
                for (auto& edge : directed) {
                    std::swap(edge.source,edge.target);
                    edge.time = 7 - edge.time;
                }
            Fixture fixture(6,8,std::move(directed));
            fixture.graph->prepareSubsetIndex(fixture.vertices);
            const auto index = fixture.graph->buildEnumerationIndex(fixture.vertices,{});
            const auto prefix = fixture.graph->buildPrefixLabelsGeneral(index,{0,1,2,3,4});
            if (dual == 0) {
                require(prefix.earliest[1] == 6,
                        "Equal-time edges through vertex four must not improve arrival at vertex one");
                require(prefix.earliest[2] == (skipBetter != 0 ? 1 : std::numeric_limits<ul>::max()) && prefix.earliest[3] == (skipBetter != 0 ? 2 : std::numeric_limits<ul>::max()),
                        "Strict paths must preserve an already earlier route without same-time propagation");
            }
            checkAllPrefixes(fixture,{4,0,2,1,3,5});
            compare(fixture,fixture.vertices,4,{{0},{4}});
            compare(fixture,fixture.vertices,5,{{4}});
            fixture.graph->buildWindow(2,5,fixture.vertices);
            checkAllPrefixes(fixture,{3,1,4,2,0,5});
        }
    }
}

void checkSevenPointPrefix(Fixture& fixture,const std::vector<VertexID>& vertices,
                           const std::vector<VertexID>& original) {
    fixture.graph->prepareSubsetIndex(vertices);
    const auto index = fixture.graph->buildEnumerationIndex(vertices,{});
    const auto local = [&](VertexID v) {
        const auto found = std::find(vertices.begin(),vertices.end(),v);
        require(found != vertices.end(),"Prefix fixture vertex must belong to its index");
        return static_cast<ui>(found - vertices.begin());
    };
    std::vector<ui> chosen;
    for (VertexID v : original) chosen.push_back(local(v));
    require(chosen.size() == 7,"Boundary prefix must exercise the seventh matrix row");
    const auto prefix = fixture.graph->buildPrefixLabelsGeneral(index,chosen);
    for (ui root = 0; root < chosen.size(); ++root) {
        const auto earliest = oracleTimeLabels(fixture,original,root,false);
        const auto latest = oracleTimeLabels(fixture,original,root,true);
        for (ui v = 0; v < chosen.size(); ++v)
            require(prefix.earliest[root * TemporalSCCsTestAccess::kMaxEnumerationSize + v] == earliest[v] &&
                    prefix.latest[v * TemporalSCCsTestAccess::kMaxEnumerationSize + root] == latest[v],
                    "Seven-point EA/LD must match independent event relaxation under the current window");
    }
    for (VertexID v : vertices) {
        if (std::find(original.begin(),original.end(),v) != original.end()) continue;
        auto full = original;
        full.push_back(v);
        auto packed = chosen;
        packed.push_back(local(v));
        const bool expected = temporalOracle(fixture,full);
        require(fixture.graph->isTemporalExtension(index,chosen,prefix,local(v)) == expected &&
                fixture.graph->isTemporalSCCPacked(index,packed) == expected,
                "The eighth point must pass exact temporal validation, including bit 63 of the packed matrix");
    }
}

void testEightPointPackedValidation() {
    std::vector<Event> complete;
    for (VertexID u = 0; u < 8; ++u)
        for (VertexID v = 0; v < 8; ++v)
            if (u != v) complete.push_back({u,v,(5 * u + 3 * v) % 7 + 1});
    Fixture dense(8,9,std::move(complete));
    const std::vector<VertexID> order{7,2,5,0,6,1,4,3};
    dense.graph->prepareSubsetIndex(order);
    const auto index = dense.graph->buildEnumerationIndex(order,{});
    require(index.pairTimes.size() == 56,"Eight-point complete fixture must occupy all 56 stream slots");
    const std::vector<ui> all{0,1,2,3,4,5,6,7};
    require(dense.graph->isTemporalSCCPacked(index,all) && temporalOracle(dense,order),
            "All eight rows, including bit 63, must become fully reachable");
    require(compare(dense,order,7) == 8 &&
            compare(dense,order,8) == 1,
            "Complete eight-point graph must emit every seven/eight-point subset exactly once");
    checkSevenPointPrefix(dense,order,{6,2,0,5,1,4,3});

    std::vector<Event> equal, reversed;
    for (VertexID u = 0; u < 8; ++u) {
        equal.push_back({7 - u,(8 - u) % 8,3});
        reversed.push_back({u,(u + 1) % 8,8 - u});
    }
    Fixture sameTime(8,10,std::move(equal));
    require(compare(sameTime,order,8) == 0 &&
            compare(sameTime,order,7) == 0,
            "An eight-edge same-time cycle cannot contain a strict temporal answer");
    checkSevenPointPrefix(sameTime,order,{6,2,0,5,1,4,3});
    Fixture invalid(8,10,std::move(reversed));
    require(!temporalOracle(invalid,order) && compare(invalid,order,8) == 0,
            "Eight statically connected points cannot bypass decreasing event times");
    checkSevenPointPrefix(invalid,order,{6,2,0,5,1,4,3});
}

void testEightPointPrefixesAndOwnership() {
    std::vector<Event> edges;
    for(ui t=0;t<7;++t)for (VertexID u = 0; u < 7; ++u) edges.push_back({u,(u + 1) % 7,t+1});
    edges.insert(edges.end(),{{6,7,8},{7,6,0},{6,8,0},{8,6,8}});
    Fixture sparse(9,10,std::move(edges));
    const std::vector<VertexID> order{8,3,6,1,7,0,5,2,4};
    require(temporalOracle(sparse,{0,1,2,3,4,5,6,7}) &&
            !temporalOracle(sparse,{0,1,2,3,4,5,6,8}),
            "The seventh-prefix boundary must have both valid and invalid eighth-point extensions");
    for (const auto& window : std::vector<std::pair<ui,ui>>{{0,9},{3,3},{1,2},{0,9}}) {
        sparse.graph->buildWindow(window.first,window.second,sparse.vertices);
        for (ui k : {7u,8u}) {
            compare(sparse,order,k);
            compare(sparse,order,k,{{0,3},{5,7},{7,8}});
        }
        checkSevenPointPrefix(sparse,order,{6,2,0,5,1,4,3});
    }

    std::vector<Event> complete;
    for (VertexID u = 0; u < 12; ++u)
        for (VertexID v = 0; v < 12; ++v)
            if (u != v) complete.push_back({u,v,3});
    Fixture manyGroups(12,7,std::move(complete));
    auto groups = distinctAnchoredGroups(12,64);
    groups.push_back({10,11});
    auto reverse = manyGroups.vertices;
    std::reverse(reverse.begin(),reverse.end());
    for (ui k : {7u,8u}) {
        requireRetainedDistinctGroups(manyGroups,reverse,k,groups);
        require(compare(manyGroups,reverse,k,groups) > 0,
                "Seven/eight-point enumeration must preserve multiword ownership and center completions");
        std::vector<VertexID> witness;
        for (VertexID v = 0; v < k; ++v) witness.push_back(v);
        require(!meetsGroups(witness,groups),"The 65th ownership bit must exclude a temporal witness");
    }

    std::mt19937 random(20271030);
    for (ui trial = 0; trial < 12; ++trial) {
        std::vector<Event> randomEdges;
        for (VertexID u = 0; u < 9; ++u)
            for (VertexID v = 0; v < 9; ++v)
                if (u != v && random() % 100 < 25 + 6 * trial)
                    randomEdges.push_back({u,v,static_cast<ui>(random() % 7)});
        Fixture fixture(9,7,std::move(randomEdges));
        auto shuffled = fixture.vertices;
        std::shuffle(shuffled.begin(),shuffled.end(),random);
        for (ui k : {7u,8u})
            compare(fixture,shuffled,k,{{shuffled[0],shuffled[1]},{shuffled[7],shuffled[8]}});
        checkSevenPointPrefix(fixture,shuffled,{6,2,0,5,1,4,3});
    }
}

void testPrefixArguments() {
    Fixture fixture(8,4,{{0,1,1},{1,0,1},{1,2,2},{2,0,2}});
    fixture.graph->prepareSubsetIndex(fixture.vertices);
    const auto index = fixture.graph->buildEnumerationIndex(fixture.vertices,{});
    for (const auto& chosen : std::vector<std::vector<ui>>{{},fixture.vertices,{0,1,1},{0,0,2}}) {
        bool invalid = false;
        try { fixture.graph->buildPrefixLabelsGeneral(index,chosen); }
        catch (const std::invalid_argument&) { invalid = true; }
        require(invalid,"Empty, oversized, or duplicate prefixes must be rejected before matrix access");
    }
    bool outside = false;
    try { fixture.graph->buildPrefixLabelsGeneral(index,{0,1,index.size}); }
    catch (const std::out_of_range&) { outside = true; }
    require(outside,"Prefix vertices must belong to the indexed candidate set");
    require(fixture.graph->buildPrefixLabelsGeneral(index,{0}).size == 1 &&
            fixture.graph->buildPrefixLabelsGeneral(index,{0,1,2,3,4,5,6}).size == 7,
            "Prefix construction must preserve the supported one-to-seven point range");
}

void testFallbackAndArguments() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 10; ++u) {
        for(ui t=0;t<9;++t){edges.push_back({u,(u + 1) % 10,t});
        edges.push_back({(u + 1) % 10,u,t});}
    }
    Fixture fixture(10,10,std::move(edges));
    require(compare(fixture,fixture.vertices,9,{{0},{6}}) == 8 &&
            compare(fixture,fixture.vertices,10) == 1 && compare(fixture,fixture.vertices,11) == 0,
            "k beyond eight must preserve the original fallback behavior");
    bool invalid = false;
    try { fixture.graph->enumerateOptimized(fixture.vertices,1,{}); }
    catch (const std::invalid_argument&) { invalid = true; }
    require(invalid,"Incremental enumeration must reject k below two");
}

}

int main() {
    try {
        {
            OutputCapture capture;
            testRepairablePrefixes();
            testIncompatibleExtension();
            testFalseBidirectionalCoverage();
            testEqualTimesAndWindows();
            testCompleteGraphAndOwnership();
            testIndependentMultiwordOwnership();
            testSmallMultiwordOwnershipGroups();
            testMultiwordCenterCompletions();
            testMultiwordNeighborAndPrefix();
            testSparseMultiwordOwnershipSmoke();
            testLargeSparseEnumerationV1();
            testCompressedEnumerationIndexV1();
            testCandidateWordBoundaries();
            testRandomGraphs();
            testFullQueriesAndSearch();
            testPivotQueryAnswers();
            testPivotSubsetScoringV1();
            testPrefixTimePaths();
            testEightPointPackedValidation();
            testEightPointPrefixesAndOwnership();
            testPrefixArguments();
            testFallbackAndArguments();
        }
        std::cout << "incremental_enumeration_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "incremental_enumeration_test failed: " << error.what() << '\n';
        return 1;
    }
}
