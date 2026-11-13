#include "temporal_test_support.h"

#include <random>

namespace {

struct Features {
    ul degree = 0;
    ul score = 0;
};

std::vector<Features> featureOracle(const Fixture& fixture,const std::vector<VertexID>& vertices) {
    const auto n = fixture.vertices.size();
    std::vector<unsigned char> selected(n,0);
    for (VertexID v : vertices) selected[v] = 1;
    std::vector<Features> features(n);
    std::vector<long long> departure(n,std::numeric_limits<int>::max()),incoming(n,-1);
    const auto& window = fixture.graph->getWindow();
    for (const auto& event : fixture.events) {
        if (!selected[event.source] || !selected[event.target] ||
            event.time < window.left || event.time > window.right) continue;
        ++features[event.source].degree;
        ++features[event.target].degree;
        departure[event.source] = std::min(departure[event.source],static_cast<long long>(event.time));
        incoming[event.target] = std::max(incoming[event.target],static_cast<long long>(event.time));
    }
    for (VertexID root : vertices) {
        ul unreachable = 0, earlyNeighbors = 0;
        std::vector<unsigned char> counted(n,0);
        for (VertexID other : vertices)
            if (root != other && incoming[other] < departure[root]) ++unreachable;
        for (const auto& event : fixture.events) {
            if (event.source != root || !selected[event.target] ||
                event.time < window.left || event.time > window.right ||
                event.time > departure[event.target] || counted[event.target]) continue;
            counted[event.target] = 1;
            ++earlyNeighbors;
        }
        features[root].score = unreachable * earlyNeighbors;
    }
    return features;
}

VertexID seedOracle(const Fixture& fixture,const std::vector<VertexID>& vertices,
    const std::vector<VertexID>& remaining) {
    const auto features = featureOracle(fixture,vertices);
    VertexID best = remaining.front();
    for (VertexID v : remaining)
        if (features[v].degree > features[best].degree ||
            (features[v].degree == features[best].degree && features[v].score > features[best].score))
            best = v;
    return best;
}

VertexID checkSeed(Fixture& fixture,const std::vector<VertexID>& vertices,
    const std::vector<VertexID>& remaining) {
    std::vector<ui> arrival(vertices.size(),std::numeric_limits<ui>::max());
    const auto expected = seedOracle(fixture,vertices,remaining);
    const auto actual = fixture.graph->selectReversePivot(vertices,remaining);
    require(actual == expected,"V1 reverse seed differs from independent degree/time-feature ranking");
    require(arrival == std::vector<ui>(vertices.size(),std::numeric_limits<ui>::max()),
        "Reverse selection modified frozen forward arrival labels");
    return actual;
}

void testSpecifiedRanking() {
    Fixture fixture(8,10,{
        {1,0,1},{0,1,7},{5,4,4},{4,5,7},
        {2,3,2},{3,2,2},{6,6,6},
        {1,0,9},{0,1,9},
        {7,1,0},{1,7,0},{7,1,0},{1,7,0}
    });
    const std::vector<VertexID> parent{6,3,5,2,4,1,0};
    fixture.graph->buildWindow(1,7,fixture.vertices);
    const auto features = featureOracle(fixture,parent);
    require(features[1].degree == features[5].degree && features[1].score == 0 && features[5].score == 3,
        "Ranking fixture must isolate a temporal-score tie break over the full parent set");
    require(checkSeed(fixture,parent,{1,5}) == 5 && checkSeed(fixture,parent,{5,1}) == 5,
        "A temporal tie break must select the higher score, even at a later/higher-ID candidate");
    fixture.graph->prepareSubsetIndex({1,7});
    require(checkSeed(fixture,parent,{1,5}) == 5,"Previous subset indexing contaminated seed ranking");
    require(checkSeed(fixture,parent,{5}) == 5,"A single remaining vertex must be selected");
    fixture.graph->buildWindow(1,9,fixture.vertices);
    require(checkSeed(fixture,parent,{5,1}) == 1,
        "Distinct-time events to an existing neighbor must count individually in event degree");
    fixture.graph->buildWindow(0,7,fixture.vertices);
    require(checkSeed(fixture,parent,{1,5}) == 5,
        "Events whose endpoint is outside the parent induced set changed ranking");
    require(checkSeed(fixture,fixture.vertices,{5,1}) == 1,
        "The induced-set isolation fixture must distinguish inclusion of the noisy endpoint");

    Fixture tied(7,10,{{1,0,1},{0,1,7},{5,4,1},{4,5,7},{2,3,2},{3,2,2},{6,6,6}});
    require(checkSeed(tied,tied.vertices,{5,1}) == 5 && checkSeed(tied,tied.vertices,{1,5}) == 1,
        "Equal degree and score must preserve Ux order instead of choosing the smallest vertex ID");
    Fixture self(7,10,{{1,0,1},{0,1,7},{5,4,1},{4,5,7},{5,5,2}});
    require(checkSeed(self,self.vertices,{1,5}) == 5,
        "A self event must contribute once to incoming and once to outgoing event degree");

    bool rejected = false;
    try { fixture.graph->selectReversePivot(parent,{}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected,"V1 reverse selector must reject an empty remaining set");
}

void testRandomRanking() {
    std::mt19937 random(20261105);
    for (ui trial = 0; trial < 32; ++trial) {
        std::vector<Event> events;
        for (VertexID u = 0; u < 12; ++u)
            for (VertexID v = 0; v < 12; ++v)
                if (random() % 100 < 30)
                    for (ui repeat = 1 + random() % 3; repeat != 0; --repeat)
                        events.push_back({u,v,static_cast<ui>(random() % 10)});
        Fixture fixture(12,10,std::move(events));
        auto parent = fixture.vertices;
        std::shuffle(parent.begin(),parent.end(),random);
        parent.resize(5 + random() % 7);
        auto remaining = parent;
        std::shuffle(remaining.begin(),remaining.end(),random);
        remaining.resize(1 + random() % remaining.size());
        for (const auto range : {std::pair<ui,ui>{0,9},{2,6},{4,4}}) {
            fixture.graph->buildWindow(range.first,range.second,fixture.vertices);
            checkSeed(fixture,parent,remaining);
            std::reverse(parent.begin(),parent.end());
            checkSeed(fixture,parent,remaining);
        }
    }
}

std::string checkQuery(Fixture& fixture,ui left,ui right,ui k,ul expected) {
    OutputCapture capture;
    const auto flags = std::cout.flags();
    const auto precision = std::cout.precision();
    fixture.graph->enumerateDec(left,right,k);
    require(fixture.graph->getAnswerCount() == expected,
        "V1 query differs from independent count, k=" + std::to_string(k));
    require(std::cout.flags() == flags && std::cout.precision() == precision,
        "V1 query changed caller output formatting");
    const auto output = capture.output.str();
    require(output.empty(),"V1 queries must leave output to main");
    return output;
}

void testSmallQueryOracle() {
    std::mt19937 random(20261106);
    for (ui trial = 0; trial < 12; ++trial) {
        const ui n = 8 + trial % 2;
        std::vector<Event> events;
        for (VertexID u = 0; u < n; ++u)
            for (VertexID v = 0; v < n; ++v)
                if (u != v && random() % 100 < 35 + trial * 3)
                    for (ui repeat = 1 + random() % 3; repeat != 0; --repeat)
                        events.push_back({u,v,static_cast<ui>(random() % 8)});
        Fixture fixture(n,8,std::move(events));
        for (const auto range : {std::pair<ui,ui>{0,7},{2,5},{3,3},{7,7}}) {
            fixture.graph->buildWindow(range.first,range.second,fixture.vertices);
            for (ui k = 2; k <= 8; ++k) {
                const auto expected = oracleCount(fixture,fixture.vertices,k);
                checkQuery(fixture,range.first,range.second,k,expected);
            }
        }
    }
}

ul choose(ui n,ui k) {
    if (k > n) return 0;
    ul result = 1;
    for (ui i = 1; i <= k; ++i) result = result * (n - k + i) / i;
    return result;
}

std::vector<Event> cliqueRing(ui blocks,ui blockSize) {
    std::vector<Event> events;
    for (VertexID block = 0; block < blocks; ++block) {
        const VertexID first = block * blockSize;
        for (VertexID u = first; u < first + blockSize; ++u)
            for (VertexID v = first; v < first + blockSize; ++v)
                if (u != v) events.push_back({u,v,2});
        events.push_back({first + blockSize - 1,((block + 1) % blocks) * blockSize,3});
    }
    return events;
}

std::vector<VertexID> coverOracle(const Fixture& fixture,const std::vector<VertexID>& vertices,
    const std::vector<ui>& arrival,VertexID seed) {
    std::vector<ui> position(fixture.vertices.size(),vertices.size());
    for (ui i = 0; i < vertices.size(); ++i) position[vertices[i]] = i;
    std::vector<unsigned char> reached(fixture.vertices.size(),0);
    reached[seed] = 1;
    const auto& window = fixture.graph->getWindow();
    bool changed;
    do {
        changed = false;
        for (const auto& event : fixture.events) {
            if (event.time < window.left || event.time > window.right ||
                position[event.source] == vertices.size() || position[event.target] == vertices.size() ||
                !reached[event.target] || reached[event.source]) continue;
            if (event.target != seed && event.time >= arrival[position[event.target]]) continue;
            reached[event.source] = 1;
            changed = true;
        }
    } while (changed);
    std::vector<VertexID> result;
    for (VertexID v : vertices) if (reached[v]) result.push_back(v);
    return result;
}

void checkAcceptedPlan(Fixture& fixture) {
    const auto plan = fixture.graph->buildSplitPlan(fixture.vertices,3);
    require(plan.decompose && !plan.forward.U.empty(),
        "Large fixture must accept a decomposition and exercise reverse selection");
    auto remaining = plan.forward.U;
    ui cursor = 0;
    while (!remaining.empty()) {
        const auto seed = seedOracle(fixture,fixture.vertices,remaining);
        const auto expected = coverOracle(fixture,fixture.vertices,plan.arrival,seed);
        require(fixture.graph->selectReversePivot(fixture.vertices,remaining) == seed,
            "Accepted plan uses a reverse root outside its direct ranking");
        auto arrival = plan.arrival;
        auto cover = fixture.graph->buildReverseCover(seed,fixture.vertices,arrival,remaining);
        std::sort(cover.first.begin(),cover.first.end());
        auto sortedExpected = expected;
        std::sort(sortedExpected.begin(),sortedExpected.end());
        require(cover.first == sortedExpected && arrival == plan.arrival && cover.second.size() < remaining.size(),
            "Reverse cover differs from an independent frozen-label ancestor closure");
        if (!plan.reverse.empty()) {
            require(cursor < plan.reverse.size() && plan.reverse[cursor].seed == seed,
                "Cached V1 plan contains a reverse root outside the direct ranking");
            auto cached = plan.reverse[cursor].covered;
            std::sort(cached.begin(),cached.end());
            require(cached == sortedExpected && plan.reverse[cursor].remaining == cover.second,
                "Cached reverse cover disagrees with recomputation");
        }
        remaining = std::move(cover.second);
        ++cursor;
    }
    require(cursor != 0 && (plan.reverse.empty() || cursor == plan.reverse.size()),
        "An accepted V1 plan did not consume exactly its complete reverse covers");
}

void testRecursiveQueriesAndOwnership() {
    Fixture fixture(128,8,cliqueRing(16,8));
    require(!fixture.graph->shouldEnumerateDirectly(fixture.vertices,3),
        "Large fixture must exceed the direct-enumeration budget");
    const auto components = fixture.graph->findWindowStaticSCCs(fixture.vertices,fixture.graph->getWindow(),2);
    require(components.size() == 1 && components.front().size() == 128,
        "Large fixture must start as one static SCC");
    checkAcceptedPlan(fixture);
    for (ui k = 2; k <= 8; ++k)
        checkQuery(fixture,0,7,k,16 * choose(8,k));

    Groups groups(2);
    for (VertexID v : fixture.vertices) {
        if ((v / 8) % 2 == 0) groups[0].push_back(v);
        if (v % 8 < 4) groups[1].push_back(v);
    }
    for (ui k : {3u,5u,8u}) {
        fixture.graph->buildWindow(0,7,fixture.vertices);
        require(!fixture.graph->shouldEnumerateDirectly(fixture.vertices,k),
            "Inherited groups must not bypass decomposition in this fixture");
        const auto before = fixture.graph->getAnswerCount();
        fixture.graph->decompose(fixture.vertices,k,groups);
        require(fixture.graph->getAnswerCount() - before == 8 * (choose(8,k) - choose(4,k)),
            "Reverse-seed replacement lost or duplicated an inherited-ownership answer");
    }
    checkQuery(fixture,0,0,5,0);
    checkQuery(fixture,2,2,5,16 * choose(8,5));
    checkQuery(fixture,3,7,5,0);
    checkQuery(fixture,0,7,5,16 * choose(8,5));
}

}

int main() {
    try {
        {
            OutputCapture capture;
            testSpecifiedRanking();
            testRandomRanking();
            testSmallQueryOracle();
            testRecursiveQueriesAndOwnership();
        }
        std::cout << "reverse_pivot_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "reverse_pivot_test failed: " << error.what() << '\n';
        return 1;
    }
}
