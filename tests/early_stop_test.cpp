#include "TemporalSCCsTestAccess.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

void require(bool value,const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

struct OutputCapture {
    std::ostringstream output;
    std::streambuf* previous;

    OutputCapture() : previous(std::cout.rdbuf(output.rdbuf())) {}
    ~OutputCapture() { std::cout.rdbuf(previous); }
};

std::string createDirectory() {
    static ui sequence = 0;
    const auto name = "temporal_early_stop_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) +
        "_" + std::to_string(++sequence);
#if defined(__MINGW32__) && __GNUC__ < 9
    char path[MAX_PATH];
    const auto length = GetTempPathA(MAX_PATH,path);
    require(length > 0 && length < MAX_PATH,"Cannot locate test temporary directory");
    const auto directory = std::string(path) + name;
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
    ui times;

    Fixture(ui count,ui timeCount,std::vector<Event> input,
            std::vector<VertexID> candidates = {})
        : directory(createDirectory()),events(std::move(input)),
          vertices(std::move(candidates)),times(timeCount) {
        if (vertices.empty())
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
            output << "1\n" << vertices.size() << '\n';
            for (VertexID v : vertices) output << v << ' ';
            output << '\n';
            require(output.good(),"Cannot write test SCCs");
        }
        graph = std::make_unique<TemporalSCCsTestAccess>(directory + "/","graph");
        graph->buildWindow(0,times - 1,vertices);
    }

    ~Fixture() {
        graph.reset();
        removeDirectory(directory);
    }
};

Groups materializeCovers(TemporalSCCsTestAccess& graph,const std::vector<VertexID>& vertices,ui k) {
    if (k < 2) throw std::invalid_argument("k must be at least 2");
    if (vertices.size() < k) return {};
    auto plan=graph.buildSplitPlan(vertices,k);
    if (!plan.decompose) return {vertices};
    Groups covers{std::move(plan.forward.R)};
    auto remaining=std::move(plan.forward.U);
    std::size_t cursor=0;
    while (!remaining.empty()) {
        if (!plan.reverse.empty()) {
            require(cursor<plan.reverse.size(),"Cached reverse plan must be complete");
            auto& cover=plan.reverse[cursor++];
            covers.push_back(std::move(cover.covered));
            remaining=std::move(cover.remaining);
        } else {
            const auto seed=graph.selectReversePivot(vertices,remaining);
            auto cover=graph.buildReverseCover(seed,vertices,plan.arrival,remaining);
            covers.push_back(std::move(cover.first));remaining=std::move(cover.second);
        }
    }
    require(plan.reverse.empty() || cursor==plan.reverse.size(),"Cached reverse plan has unused entries");
    return covers;
}

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
    std::vector<Event> events;
    for (const auto& edge : fixture.events) {
        if (edge.time < window.left || edge.time > window.right) continue;
        if (std::find(chosen.begin(),chosen.end(),edge.source) == chosen.end() ||
            std::find(chosen.begin(),chosen.end(),edge.target) == chosen.end()) continue;
        events.push_back(edge);
    }
    for (VertexID source : chosen) {
        std::vector<unsigned char> reached(fixture.graph->getVertexCount(),0);
        reached[source] = 1;
        for (std::size_t first = 0; first < events.size();) {
            std::size_t last = first + 1;
            while (last < events.size() && events[last].time == events[first].time) ++last;
            const auto before = reached;
            for (std::size_t i = first; i < last; ++i) {
                const auto& edge = events[i];
                if (before[edge.source]) reached[edge.target] = 1;
            }
            first = last;
        }
        for (VertexID v : chosen)
            if (!reached[v]) return false;
    }
    return true;
}

ul oracleCount(const Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
               const Groups& groups = {}) {
    ul count = 0;
    std::vector<VertexID> chosen;
    const auto visit = [&](auto&& self,std::size_t start) -> void {
        if (chosen.size() == k) {
            if (meetsGroups(chosen,groups) && temporalOracle(fixture,chosen)) ++count;
            return;
        }
        const auto need = k - chosen.size();
        for (std::size_t i = start; i < vertices.size() && need <= vertices.size() - i; ++i) {
            chosen.push_back(vertices[i]);
            self(self,i + 1);
            chosen.pop_back();
        }
    };
    visit(visit,0);
    return count;
}

struct SearchResult {
    ul count;
};

SearchResult searchResult(Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
                          const Groups& groups) {
    const ul before = fixture.graph->getAnswerCount();
    OutputCapture output;
    fixture.graph->decompose(vertices,k,groups);
    require(output.output.str().empty(),"Independent search must not print decomposition diagnostics");
    return {fixture.graph->getAnswerCount() - before};
}

void compareSearch(Fixture& fixture,const std::vector<VertexID>& vertices,ui k,
                   const Groups& groups = {}) {
    const auto expected = oracleCount(fixture,vertices,k,groups);
    const auto result = searchResult(fixture,vertices,k,groups);
    require(result.count == expected,
            "V1 differs from the independent temporal oracle");
}
void testCombinationEstimates() {
    require(TemporalSCCsTestAccess::countKCombinationsCapped(20,4,5000) == 4845,
            "A fitting capped combination count must remain exact");
    require(TemporalSCCsTestAccess::countKCombinationsCapped(4,5,100) == 0,
            "An undersized combination count must be zero");
    require(TemporalSCCsTestAccess::countKCombinationsCapped(1000000,6,4097) == 4097,
            "A huge combination count must saturate without overflowing");
    require(TemporalSCCsTestAccess::countKCombinationsCapped(145057,4,4097) == 4097,
            "The reported uint64 overflow case must be safe for threshold decisions");
    require(TemporalSCCsTestAccess::countKCombinationsCapped(20,0,10) == 1 &&
            TemporalSCCsTestAccess::countKCombinationsCapped(20,0,0) == 0,
            "Empty completions must obey the requested cap");
    const auto estimate = TemporalSCCsTestAccess::estimateKCombinations(1642130,4);
    const long double expected = 302982338003484625103380.0L;
    require(std::isfinite(estimate) && std::fabs(estimate / expected - 1.0L) < 1e-12L,
            "Large split-score estimates must not wrap or lose their scale");
}

void testLargeEmptyEnumeration() {
    Fixture fixture(145057,1,{});
    const Groups impossible = {{145058}};
    for (bool optimized : {false,true}) {
        const ul before = fixture.graph->getAnswerCount();
        OutputCapture enumerationOutput;
        if (optimized)
            fixture.graph->enumerateOptimized(fixture.vertices,4,impossible);
        else
            fixture.graph->enumerateCombinations(fixture.vertices,4,impossible);
        require(fixture.graph->getAnswerCount() == before,
                "Large empty enumeration must reject impossible ownership without emitting answers");
        require(enumerationOutput.output.str().empty(),"Independent enumeration must remain silent");
    }
    require(searchResult(fixture,fixture.vertices,4,impossible).count == 0,
            "V1 must reject impossible ownership before estimating a large branch");
}

ul smallCombinations(std::size_t n,ui k) {
    if (n < k) return 0;
    ul count = 1;
    for (ui i = 1; i <= k; ++i) count = count * (n - k + i) / i;
    return count;
}

ul scoreGroupsOracle(const Fixture& fixture,const Groups& groups,ui k) {
    ul score = 0;
    const auto& window = fixture.graph->getWindow();
    for (const auto& group : groups) {
        const auto n = group.size();
        std::vector<std::vector<unsigned char>> reach(n,std::vector<unsigned char>(n,0));
        for (std::size_t i = 0; i < n; ++i) reach[i][i] = 1;
        for (const auto& edge : fixture.events) {
            if (edge.time < window.left || edge.time > window.right) continue;
            const auto source = std::find(group.begin(),group.end(),edge.source);
            const auto target = std::find(group.begin(),group.end(),edge.target);
            if (source != group.end() && target != group.end())
                reach[source - group.begin()][target - group.begin()] = 1;
        }
        for (std::size_t via = 0; via < n; ++via)
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j)
                    reach[i][j] = reach[i][j] || (reach[i][via] && reach[via][j]);
        std::vector<unsigned char> assigned(n,0);
        for (std::size_t i = 0; i < n; ++i) {
            if (assigned[i]) continue;
            std::size_t size = 0;
            for (std::size_t j = 0; j < n; ++j)
                if (reach[i][j] && reach[j][i]) { assigned[j] = 1; ++size; }
            score += smallCombinations(size,k);
        }
    }
    return score;
}

long double verifyPivotScore(Fixture& fixture,const std::vector<VertexID>& vertices,ui k) {
    const auto previousCount=fixture.graph->getAnswerCount();
    if (vertices.size()<k) return 0;
    const auto first=fixture.graph->buildSplitPlan(vertices,k);
    const auto repeated=fixture.graph->buildSplitPlan(vertices,k);
    require(first.decompose==repeated.decompose && (!first.decompose ||
        (first.pivot==repeated.pivot && first.arrival==repeated.arrival &&
         first.forward.R==repeated.forward.R && first.forward.U==repeated.forward.U)),
        "Repeated bounded planning must preserve its selected complete plan");
    require(fixture.graph->getAnswerCount()==previousCount,"Split planning must not count answers");
    const auto covers=materializeCovers(*fixture.graph,vertices,k);
    const auto score=scoreGroupsOracle(fixture,covers,k);
    if(first.decompose) require(score<1.1L*smallCombinations(vertices.size(),k),
        "An accepted plan must be below the complete split threshold");
    return static_cast<long double>(score);
}

void testEqualAndZeroSplitCost() {
    std::vector<Event> bidirectional, directed;
    for (VertexID v = 0; v < 4; ++v) {
        directed.push_back({v,(v + 1) % 4,1});
        bidirectional.push_back({v,(v + 1) % 4,1});
        bidirectional.push_back({(v + 1) % 4,v,1});
    }
    Fixture boundary(4,3,std::move(bidirectional));
    boundary.graph->setDecompositionAlpha(0);
    const Groups rootZeroCovers{{0,1,3},{1,2,3}};
    require(smallCombinations(4,2) == 6 && scoreGroupsOracle(boundary,rootZeroCovers,2) == 6 &&
        oracleCount(boundary,boundary.vertices,2) == 4,
        "The boundary fixture must have exact unit decomposition gain and four valid pairs");
    require(boundary.graph->buildSplitPlan(boundary.vertices,2).decompose,
        "An equal-cost complete split must be allowed by the original S < 1.1B rule");
    const auto equalCovers = materializeCovers(*boundary.graph,boundary.vertices,2);
    require(equalCovers.size() == 2 && scoreGroupsOracle(boundary,equalCovers,2) == 6,
        "An accepted equal-cost plan must contain both complete covers at the independently known cost");
    boundary.graph->enumerateDec(0,2,2);
    require(boundary.graph->getAnswerCount() == 4,
        "An equal-cost split must preserve every strict temporal answer");

    Fixture zeroCost(4,3,std::move(directed));
    zeroCost.graph->setDecompositionAlpha(0);
    require(oracleCount(zeroCost,zeroCost.vertices,3) == 0,
        "The zero-cost fixture must reject same-time multihop temporal answers");
    require(zeroCost.graph->buildSplitPlan(zeroCost.vertices,3).decompose,
        "A zero-cost complete split must satisfy the original fixed stopping rule");
    const auto zeroCovers = materializeCovers(*zeroCost.graph,zeroCost.vertices,3);
    require(zeroCovers.size() > 1 && scoreGroupsOracle(zeroCost,zeroCovers,3) == 0,
        "Zero decomposition cost must come from all complete cover SCCs");
    zeroCost.graph->enumerateDec(0,2,3);
    require(zeroCost.graph->getAnswerCount() == 0,
        "A zero-cost decomposition must preserve the independent strict temporal oracle");
}

void testNoSplitFallback() {
    std::vector<Event> edges;
    for (VertexID u = 0; u < 20; ++u)
        for (VertexID v = 0; v < 20; ++v)
            if (u != v) edges.push_back({u,v,static_cast<ui>((u + 3 * v) % 5)});
    Fixture complete(20,5,std::move(edges));
    require(verifyPivotScore(complete,complete.vertices,4) == 4845,
            "A no-split complete graph must return the equal parent cost");
    require(materializeCovers(*complete.graph,complete.vertices,4).size() == 1,
            "The equality fixture must exercise the no-split fallback");
    const auto result = searchResult(complete,complete.vertices,4,{});
    require(result.count == 4845,
            "An unsplittable graph must safely fall back with every complete-graph answer");
    verifyPivotScore(complete,{2,7},4);
    compareSearch(complete,complete.vertices,4,{{0},{19}});
}

void testUnprofitableSplitStops() {
    constexpr ui count = 20,times = 20;
    std::mt19937 random(784);
    std::vector<Event> edges;
    for (VertexID u = 0; u < count; ++u) {
        edges.push_back({u,(u + 1) % count,static_cast<ui>(random() % times)});
        for (VertexID v = 0; v < count; ++v)
            if (u != v && random() % 100 < 12)
                edges.push_back({u,v,static_cast<ui>(random() % times)});
    }
    for (VertexID u = 0; u < 4; ++u)
        for (VertexID v = 0; v < 4; ++v)
            if (u != v) edges.push_back({u,v,3});
    Fixture fixture(count,times,std::move(edges));
    require(!fixture.graph->shouldEnumerateDirectly(fixture.vertices,4),
            "The unprofitable fixture must exceed the k+alpha size threshold");
    fixture.graph->prepareSubsetIndex(fixture.vertices);
    const VertexID pivot = 10;
    std::vector<ui> arrival;
    TemporalSCCsTestAccess::SplitResult forward;
    fixture.graph->computeEarliestArrival(pivot,fixture.vertices,arrival,forward,true);
    require(!forward.U.empty(),"The unprofitable trial must contain uncovered vertices");
    Groups trialCovers{forward.R};
    auto remaining = forward.U;
    while (!remaining.empty()) {
        const auto seed = fixture.graph->selectReversePivot(fixture.vertices,remaining);
        auto cover = fixture.graph->buildReverseCover(seed,fixture.vertices,arrival,remaining);
        trialCovers.push_back(std::move(cover.first));
        remaining = std::move(cover.second);
    }
    require(scoreGroupsOracle(fixture,trialCovers,4) >=
            1.1L * smallCombinations(count,4),
            "The full overlapping trial must exceed the direct enumeration threshold");
    const auto splitCost = verifyPivotScore(fixture,fixture.vertices,4);
    require(smallCombinations(count,4) == 4845 && splitCost == 4845 &&
            !fixture.graph->buildSplitPlan(fixture.vertices,4).decompose,
            "An unprofitable complete plan must be rejected before publication");
    const auto groups = materializeCovers(*fixture.graph,fixture.vertices,4);
    require(groups.size() == 1,"Rejected trial plans must materialize only the unchanged parent");
    const auto expected = oracleCount(fixture,fixture.vertices,4);
    require(expected == 4,
            "The unprofitable split must retain its four strict temporal answers");
    const auto result = searchResult(fixture,fixture.vertices,4,{});
    require(result.count == expected,
            "V1 must stop the unprofitable split and retain every temporal answer");
    compareSearch(fixture,fixture.vertices,4,{{0,1},{2,3}});
    compareSearch(fixture,fixture.vertices,4,{{0}});
    compareSearch(fixture,fixture.vertices,4,{{4}});
}

void testProfitableSplitContinues() {
    std::vector<Event> edges;
    for (VertexID v = 0; v < 34; ++v) {
        edges.push_back({v,v + 1,v % 4});
        edges.push_back({v + 1,v,v % 4});
    }
    for (VertexID u = 0; u < 3; ++u)
        for (VertexID v = 0; v < 3; ++v)
            if (u != v) edges.push_back({u,v,1});
    Fixture fixture(35,4,std::move(edges));
    const auto expected = oracleCount(fixture,fixture.vertices,3);
    fixture.graph->enumerateCom(0,3,3);
    require(fixture.graph->getAnswerCount() == expected,
        "The profitable fixture enumerateCom must match the independent strict temporal oracle");
    const auto score = verifyPivotScore(fixture,fixture.vertices,3);
    require(fixture.graph->buildSplitPlan(fixture.vertices,3).decompose &&
        score > 0 && score < smallCombinations(35,3),
        "A profitable complete plan must reduce the independently computed direct enumeration cost");
    fixture.graph->enumerateDec(0,3,3);
    require(fixture.graph->getAnswerCount() == expected,
        "Recursive V1 answers must match the independent strict temporal oracle and enumerateCom");
    compareSearch(fixture,fixture.vertices,3,{{0},{1}});
}
void testAlphaEnumerationBoundary() {
    std::vector<Event> edges;
    for (VertexID v = 0; v < 7; ++v) {
        edges.push_back({v,v + 1,v % 3});
        edges.push_back({v + 1,v,v % 3});
    }
    for (VertexID u = 0; u < 3; ++u)
        for (VertexID v = 0; v < 3; ++v)
            if (u != v) edges.push_back({u,v,1});
    Fixture fixture(8,3,std::move(edges));
    fixture.graph->setDecompositionAlpha(0);
    require(!fixture.graph->shouldEnumerateDirectly(fixture.vertices,3),
            "Alpha zero must not use the old small-combination stopping budget");
    const auto recursive = searchResult(fixture,fixture.vertices,3,{});
    fixture.graph->setDecompositionAlpha(6);
    require(smallCombinations(8,3) == 56 &&
            fixture.graph->shouldEnumerateDirectly(fixture.vertices,3) &&
            materializeCovers(*fixture.graph,fixture.vertices,3).size() > 1,
            "The small fixture must be splittable while fitting the k+alpha size threshold");
    const auto expected = oracleCount(fixture,fixture.vertices,3);
    const auto result = searchResult(fixture,fixture.vertices,3,{});
    require(expected > 0 && result.count == expected && recursive.count == expected,
            "Both alpha-driven direct enumeration and recursive search must preserve every answer");
}

void testAlphaQueryIsolation() {
    std::vector<Event> edges;
    for (VertexID v = 0; v < 6; ++v) {
        edges.push_back({v,v + 1,1});
        edges.push_back({v + 1,v,1});
    }
    edges.push_back({0,2,1});
    edges.push_back({2,0,1});
    Fixture first(7,4,edges),second(7,4,edges);
    require(first.graph->getDecompositionAlpha() == 10 && second.graph->getDecompositionAlpha() == 10,
            "Graph instances must start with the documented alpha default");
    second.graph->setDecompositionAlpha(12);
    const auto expected = oracleCount(first,first.vertices,3);
    require(expected == 1,"The alpha fixture must contain exactly one strict temporal triple");
    for (ui alpha : {0u,2u,4u,6u,8u,10u,12u}) {
        first.graph->setDecompositionAlpha(alpha);
        first.graph->enumerateDec(0,3,3);
        require(first.graph->getAnswerCount() == expected && first.graph->getDecompositionAlpha() == alpha,
                "Every alpha must preserve the independent temporal answer count and its setting");
        first.graph->enumerateCom(0,3,3);
        require(first.graph->getAnswerCount() == expected && first.graph->getDecompositionAlpha() == alpha,
                "Baseline queries must not modify the V1 alpha setting");
        first.graph->enumerateDec(2,3,3);
        require(first.graph->getAnswerCount() == 0 && first.graph->getDecompositionAlpha() == alpha,
                "A narrower empty window must reset answers without resetting alpha");
        first.graph->enumerateDec(0,3,3);
        require(first.graph->getAnswerCount() == expected && second.graph->getDecompositionAlpha() == 12,
                "Query changes must not leak answers or alpha settings across graph instances");
    }
}

void testRecursiveSearchAndQueries() {
    constexpr ui count = 44, times = 6;
    std::mt19937 random(20261102);
    std::vector<Event> edges;
    for (VertexID v = 0; v < count; ++v)
        edges.push_back({v,(v + 1) % count,static_cast<ui>(random() % times)});
    for (ui i = 0; i < count * 2; ++i) {
        const auto u = static_cast<VertexID>(random() % count);
        const auto v = static_cast<VertexID>(random() % count);
        if (u != v) edges.push_back({u,v,static_cast<ui>(random() % times)});
    }
    edges.push_back({0,1,2});
    edges.push_back({1,2,2});
    edges.push_back({2,0,2});
    Fixture fixture(count,times,std::move(edges));
    auto order = fixture.vertices;
    std::reverse(order.begin(),order.end());
    for (const auto& vertices : {fixture.vertices,order})
        for (const Groups groups : {Groups{},Groups{{0}},Groups{{0,1},{2,3}},Groups{{99}}})
            compareSearch(fixture,vertices,3,groups);
    const auto expected = oracleCount(fixture,fixture.vertices,3);
    for (bool plain : {false,true,false,true}) {
        OutputCapture queryOutput;
        const auto answers = plain ? fixture.graph->enumerateCom(0,times - 1,3)
                                   : fixture.graph->enumerateDec(0,times - 1,3);
        require(answers == expected && fixture.graph->getAnswerCount() == expected,
                "Interleaved V1/enumerateCom queries must reset counts and preserve all answers");
        require(queryOutput.output.str().empty(),"V1/enumerateCom queries must remain silent");
    }
    {
        OutputCapture queryOutput;
        fixture.graph->enumerateDec(2,4,3);
        require(fixture.graph->getAnswerCount() == oracleCount(fixture,fixture.vertices,3),
                "V1 must rebuild the queried time window before enumerating");
        fixture.graph->enumerateDec(0,times - 1,3);
        require(fixture.graph->getAnswerCount() == expected,
                "A V1 query must recover the full window after a narrower query");
        require(queryOutput.output.str().empty(),"Window-changing V1 queries must remain silent");
    }
    fixture.graph->buildWindow(2,4,fixture.vertices);
    compareSearch(fixture,order,3,{{0,1},{2,3}});
}

void testQueryArguments() {
    Fixture fixture(4,3,{{0,1,1},{1,2,1},{2,0,1}});
    bool reversed = false, beyondWindow = false, badQueryK = false, badSearchK = false;
    try { fixture.graph->enumerateDec(2,1,3); }
    catch (const std::out_of_range&) { reversed = true; }
    try { fixture.graph->enumerateDec(0,3,3); }
    catch (const std::out_of_range&) { beyondWindow = true; }
    try { fixture.graph->enumerateDec(0,2,1); }
    catch (const std::invalid_argument&) { badQueryK = true; }
    try { fixture.graph->decompose(fixture.vertices,1,{}); }
    catch (const std::invalid_argument&) { badSearchK = true; }
    require(reversed && beyondWindow && badQueryK && badSearchK,
            "V1 must reject reversed/out-of-range windows and k below two");
}

void testRandomSearch() {
    std::mt19937 random(20261103);
    for (ui trial = 0; trial < 8; ++trial) {
        const ui count = 6 + random() % 4;
        std::vector<Event> edges;
        for (VertexID u = 0; u < count; ++u)
            for (VertexID v = 0; v < count; ++v)
                if (u != v && random() % 100 < 35 + trial * 3)
                    edges.push_back({u,v,static_cast<ui>(random() % 5)});
        Fixture fixture(count,5,std::move(edges));
        auto vertices = fixture.vertices;
        std::shuffle(vertices.begin(),vertices.end(),random);
        for (ui k : {ui{2},ui{4},ui{6}}) {
            verifyPivotScore(fixture,vertices,k);
            compareSearch(fixture,vertices,k,{{vertices[0],vertices[2]},{vertices.back()}});
        }
    }
}

std::vector<ui> windowComponentSizes(const Fixture& fixture,ui k) {
    const auto components = fixture.graph->findWindowStaticSCCs(
        fixture.vertices,fixture.graph->getWindow(),k - 1);
    std::vector<ui> sizes;
    for (const auto& component : components) sizes.push_back(static_cast<ui>(component.size()));
    std::sort(sizes.begin(),sizes.end());
    return sizes;
}

ul checkBaseline(Fixture& fixture,ui first,ui last,ui k,std::vector<ui> expectedSizes) {
    OutputCapture capture;
    const auto answers = fixture.graph->enumerateCom(first,last,k);
    const auto actual = fixture.graph->getAnswerCount();
    require(answers == actual && actual == oracleCount(fixture,fixture.vertices,k),
            "Baseline query differs from the independent induced temporal oracle");
    require(capture.output.str().empty(),"Baseline queries must remain silent");
    std::sort(expectedSizes.begin(),expectedSizes.end());
    require(windowComponentSizes(fixture,k) == expectedSizes,
            "Baseline must preserve the queried window's static SCCs");
    return actual;
}

void testBaselineQueries() {
    std::vector<Event> firstCycle;
    for (ui time = 0; time < 4; ++time)
        for (VertexID u = 0; u < 4; ++u)
            firstCycle.push_back({u,(u + 1) % 4,time});
    firstCycle.insert(firstCycle.end(),{
        {4,5,0},{5,6,1},{6,7,2},{7,4,3},
        {8,9,1},{9,8,1},{3,4,2}
    });
    Fixture fixture(10,5,std::move(firstCycle));
    require(temporalOracle(fixture,{0,1,2,3}) &&
            !temporalOracle(fixture,{4,5,6,7}) &&
            !temporalOracle(fixture,{0,1,2}),
            "Baseline fixture must distinguish strict paths, time order and external intermediates");
    require(checkBaseline(fixture,0,3,4,{4,4}) == 1,
            "Only the repeated four-cycle is a valid four-point answer");
    require(checkBaseline(fixture,2,3,4,{4}) == 0,
            "A static four-cycle with only two timestamp layers must have no strict four-point answer");
    require(checkBaseline(fixture,1,1,4,{4}) == 0 &&
            checkBaseline(fixture,0,3,4,{4,4}) == 1,
            "Baseline must restore a widened window and reset previous answer counts");
    require(checkBaseline(fixture,0,3,3,{4,4}) == 0,
            "Enumerated triples must not borrow paths through an unchosen fourth point");

    std::vector<Event> edges;
    for (ui time = 0; time < 7; ++time)
        for (VertexID u = 0; u < 8; ++u) {
            edges.push_back({u,(u + 1) % 8,time});
            edges.push_back({(u + 1) % 8,u,time});
        }
    Fixture largeK(8,8,std::move(edges));
    require(checkBaseline(largeK,0,6,7,{8}) == 8 &&
            checkBaseline(largeK,0,6,8,{8}) == 1 &&
            checkBaseline(largeK,1,1,8,{8}) == 0 &&
            checkBaseline(largeK,0,6,9,{}) == 0,
            "The plain enumerateCom must support k beyond packed validation's eight-point limit");

    bool reversed = false, beyondWindow = false, badK = false;
    try { largeK.graph->enumerateCom(2,1,4); }
    catch (const std::out_of_range&) { reversed = true; }
    try { largeK.graph->enumerateCom(0,8,4); }
    catch (const std::out_of_range&) { beyondWindow = true; }
    try { largeK.graph->enumerateCom(0,2,1); }
    catch (const std::invalid_argument&) { badK = true; }
    require(reversed && beyondWindow && badK,
            "Baseline must reject reversed/out-of-range windows and k below two");
}

}

int main() {
    try {
        {
            OutputCapture capture;
            testCombinationEstimates();
            testLargeEmptyEnumeration();
            testEqualAndZeroSplitCost();
            testNoSplitFallback();
            testUnprofitableSplitStops();
            testProfitableSplitContinues();
            testAlphaEnumerationBoundary();
            testAlphaQueryIsolation();
            testRecursiveSearchAndQueries();
            testQueryArguments();
            testRandomSearch();
            testBaselineQueries();
        }
        std::cout << "early_stop_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "early_stop_test failed: " << error.what() << '\n';
        return 1;
    }
}
