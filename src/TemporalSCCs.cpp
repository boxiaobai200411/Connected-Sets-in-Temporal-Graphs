#include "TemporalSCCs.h"

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <limits>
#include <numeric>
#include <memory>
#include <iterator>
#include <queue>
#include <array>

using namespace std;

TemporalSCCs::TemporalSCCs(const std::string& dataset_path,const std::string& dataid){
    std::string base = dataset_path;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    processDataset(base,dataid);
    loadGlobalSCCs(base,dataid);
}

void TemporalSCCs::processDataset(const std::string& data_path,const std::string& dataid) {
    std::string dataset_path= data_path+dataid+".txt";
    std::ifstream input(dataset_path);
    if (!input) throw std::runtime_error("Cannot open dataset: " + dataset_path);

    long long n, m, times;
    constexpr auto max_ui = std::numeric_limits<ui>::max();
    if (!(input >> n >> m >> times) || n < 0 || m < 0 || times < 0 ||
        n > max_ui || m > max_ui || times > max_ui) {
        throw std::runtime_error("Invalid dataset header: " + dataset_path);
    }
    const auto vertex_count = static_cast<ui>(n);
    const auto edge_count = static_cast<ui>(m);
    const auto time_count = static_cast<ui>(times);

    auto edges = std::make_unique<Edge[]>(edge_count);
    auto time_offsets = std::make_unique<ui[]>(static_cast<size_t>(time_count) + 1);
    auto neighbors = std::make_unique<TemporalNeighbor[]>(edge_count);
    auto vertex_offsets = std::make_unique<ui[]>(static_cast<size_t>(vertex_count) + 1);
    auto reverse_neighbors = std::make_unique<TemporalNeighbor[]>(edge_count);
    auto reverse_offsets = std::make_unique<ui[]>(static_cast<size_t>(vertex_count) + 1);

    ui previous_time = 0;
    for (ui i = 0; i < edge_count; ++i) {
        long long u, v, t;
        if (!(input >> u >> v >> t) || u < 0 || v < 0 || t < 0 ||
            u >= n || v >= n || t >= times) {
            throw std::runtime_error("Invalid edge record " + std::to_string(static_cast<ul>(i) + 1));
        }
        const auto time = static_cast<ui>(t);
        if (i > 0 && time < previous_time) {
            throw std::runtime_error("Dataset edges must be sorted by time");
        }
        previous_time = time;
        edges[i] = {static_cast<VertexID>(u), static_cast<VertexID>(v)};
        ++time_offsets[static_cast<size_t>(time) + 1];
        ++vertex_offsets[static_cast<size_t>(u) + 1];
        ++reverse_offsets[static_cast<size_t>(v) + 1];
    }
    std::string extra;
    if (input >> extra) throw std::runtime_error("More edge records than declared in header");
    if (input.bad()) throw std::runtime_error("Failed to read dataset: " + dataset_path);

    for (size_t t = 0; t < time_count; ++t) time_offsets[t + 1] += time_offsets[t];
    for (size_t u = 0; u < vertex_count; ++u) vertex_offsets[u + 1] += vertex_offsets[u];
    for (size_t u = 0; u < vertex_count; ++u) reverse_offsets[u + 1] += reverse_offsets[u];

    auto cursor = std::make_unique<ui[]>(vertex_count);
    std::copy_n(vertex_offsets.get(), vertex_count, cursor.get());
    auto reverse_cursor = std::make_unique<ui[]>(vertex_count);
    std::copy_n(reverse_offsets.get(), vertex_count, reverse_cursor.get());

    for (ui t = 0; t < time_count; ++t) {
        for (ui i = time_offsets[t]; i < time_offsets[static_cast<size_t>(t) + 1]; ++i) {
            const auto& edge = edges[i];
            neighbors[cursor[edge.source]++] = {edge.target, t};
            reverse_neighbors[reverse_cursor[edge.target]++] = {edge.source, t};
        }
    }

    vertex_count_ = vertex_count;
    time_count_ = time_count;
    neighbors_ = std::move(neighbors);
    offsets_ = std::move(vertex_offsets);
    reverse_neighbors_ = std::move(reverse_neighbors);
    reverse_offsets_ = std::move(reverse_offsets);
}

std::vector<std::vector<VertexID>> TemporalSCCs::createGlobalSCCs() const {
    constexpr ui unseen = std::numeric_limits<ui>::max();
    std::vector<ui> discovery(vertex_count_,unseen),low(vertex_count_);
    std::vector<unsigned char> onStack(vertex_count_,0);
    std::vector<VertexID> active;
    std::vector<std::pair<VertexID,ui>> dfs;
    std::vector<std::vector<VertexID>> components;
    ui nextIndex = 0;

    for (VertexID root = 0; root < vertex_count_; ++root) {
        if (discovery[root] != unseen) continue;
        discovery[root] = low[root] = nextIndex++;
        onStack[root] = 1;
        active.push_back(root);
        dfs.emplace_back(root,offsets_[root]);
        while (!dfs.empty()) {
            const VertexID u = dfs.back().first;
            ui& next = dfs.back().second;
            if (next < offsets_[u + 1]) {
                const VertexID v = neighbors_[next++].vertex;
                if (discovery[v] == unseen) {
                    discovery[v] = low[v] = nextIndex++;
                    onStack[v] = 1;
                    active.push_back(v);
                    dfs.emplace_back(v,offsets_[v]);
                } else if (onStack[v]) {
                    low[u] = std::min(low[u],discovery[v]);
                }
                continue;
            }
            if (low[u] == discovery[u]) {
                std::size_t begin = active.size() - 1;
                while (active[begin] != u) --begin;
                if (active.size() - begin >= kMinSCCSize) {
                    components.emplace_back(active.begin() + begin,active.end());
                    std::sort(components.back().begin(),components.back().end());
                }
                for (std::size_t i = begin; i < active.size(); ++i) onStack[active[i]] = 0;
                active.resize(begin);
            }
            dfs.pop_back();
            if (!dfs.empty()) {
                const VertexID parent = dfs.back().first;
                low[parent] = std::min(low[parent],low[u]);
            }
        }
    }
    std::sort(components.begin(),components.end(),[](const auto& a,const auto& b) {
        return a.front() < b.front();
    });
    return components;
}

void TemporalSCCs::loadGlobalSCCs(const std::string& data_path,const std::string& dataid) {
    const std::string path = data_path + dataid + "_SCCs.txt";
    errno = 0;
    std::ifstream input(path);
    if (!input) {
        if (errno != ENOENT) throw std::runtime_error("Cannot open SCC file: " + path);
        auto components = createGlobalSCCs();
        const std::string temporary = path + ".tmp";
        std::ofstream output(temporary);
        if (!output) throw std::runtime_error("Cannot create SCC file: " + temporary);
        output << components.size() << '\n';
        for (const auto& component : components) {
            output << component.size();
            for (VertexID v : component) output << ' ' << v;
            output << '\n';
        }
        output.close();
        if (!output) {
            std::remove(temporary.c_str());
            throw std::runtime_error("Failed to write SCC file: " + path);
        }
        if (std::rename(temporary.c_str(),path.c_str()) != 0) {
            std::remove(temporary.c_str());
            throw std::runtime_error("Cannot save SCC file: " + path);
        }
        globalSCCs = std::move(components);
        return;
    }

    long long count;
    if (!(input >> count) || count < 0 || count > vertex_count_)
        throw std::runtime_error("Invalid SCC header: " + path);
    std::vector<std::vector<VertexID>> components;
    std::vector<unsigned char> seen(vertex_count_,0);
    std::size_t total = 0;
    for (long long i = 0; i < count; ++i) {
        long long size;
        if (!(input >> size) || size <= 0 || static_cast<ul>(size) > vertex_count_ - total)
            throw std::runtime_error("Invalid SCC size: " + path);
        std::vector<VertexID> component;
        component.reserve(static_cast<std::size_t>(size));
        for (long long j = 0; j < size; ++j) {
            long long v;
            if (!(input >> v) || v < 0 || v >= vertex_count_ || seen[static_cast<VertexID>(v)])
                throw std::runtime_error("Invalid or duplicate SCC vertex: " + path);
            seen[static_cast<VertexID>(v)] = 1;
            component.push_back(static_cast<VertexID>(v));
        }
        total += component.size();
        components.push_back(std::move(component));
    }
    std::string extra;
    if (input >> extra) throw std::runtime_error("Unexpected SCC data: " + path);
    if (input.bad()) throw std::runtime_error("Failed to read SCC file: " + path);
    globalSCCs = std::move(components);
}

std::vector<std::vector<VertexID>> TemporalSCCs::findWindowStaticSCCs(
    const std::vector<VertexID>& vertices, const Window& window, ui k) const {

    const size_t n = window.outgoing.size();
    std::vector<std::vector<VertexID>> components;

    std::vector<unsigned char> state(n, 0);
    size_t selectedCount = 0;
    for (VertexID u : vertices) {
        if (u >= n) throw std::out_of_range("Invalid SCC vertex ID");

        if (state[u] == 0 && !window.outgoing[u].empty()) {
            state[u] = 1;
            ++selectedCount;
        }
    }
    if (selectedCount < kMinSCCSize || selectedCount <= k) return components;

    auto discovery = std::unique_ptr<ui[]>(new ui[n]);
    auto low = std::unique_ptr<ui[]>(new ui[n]);
    std::vector<VertexID> active;
    std::vector<std::pair<VertexID, size_t>> dfs;
    active.reserve(selectedCount);
    dfs.reserve(selectedCount);
    ui nextIndex = 0;

    for (VertexID root : vertices) {
        if (state[root] != 1) continue;
        discovery[root] = low[root] = nextIndex++;
        state[root] = 2;
        active.push_back(root);
        dfs.emplace_back(root, 0);
        while (!dfs.empty()) {
            const VertexID u = dfs.back().first;
            size_t& next = dfs.back().second;
            const auto& neighbors = window.outgoing[u];
            if (next < neighbors.size()) {
                const VertexID v = neighbors[next++].first;

                if (v >= n || state[v] == 0 || state[v] == 3) continue;
                if (state[v] == 1) {
                    discovery[v] = low[v] = nextIndex++;
                    state[v] = 2;
                    active.push_back(v);
                    dfs.emplace_back(v, 0);
                } else {
                    low[u] = std::min(low[u], discovery[v]);
                }
                continue;
            }

            if (low[u] == discovery[u]) {
                if (active.back() == u) {

                    active.pop_back();
                    state[u] = 3;
                } else {
                    size_t begin = active.size() - 1;
                    while (active[begin] != u) --begin;
                    const size_t componentSize = active.size() - begin;

                    if (componentSize > k) {
                        components.emplace_back();
                        auto& component = components.back();
                        component.reserve(componentSize);
                        for (size_t i = active.size(); i > begin; --i)
                            component.push_back(active[i - 1]);
                    }
                    for (size_t i = begin; i < active.size(); ++i)
                        state[active[i]] = 3;
                    active.resize(begin);
                }
            }
            dfs.pop_back();
            if (!dfs.empty()) {
                const VertexID parent = dfs.back().first;
                low[parent] = std::min(low[parent], low[u]);
            }
        }
    }
    return components;
}

void TemporalSCCs::prepareSubsetIndex(const std::vector<VertexID>& vertices){
    if (subsetStamp_.size() != vertex_count_) {
        subsetStamp_.assign(vertex_count_, 0);
        subsetIndex_.resize(vertex_count_);
        subsetEpoch_ = 0;
    }

    if (++subsetEpoch_ == 0) {
        std::fill(subsetStamp_.begin(), subsetStamp_.end(), 0);
        subsetEpoch_ = 1;
    }
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const VertexID v = vertices[i];
        subsetStamp_[v] = subsetEpoch_;
        subsetIndex_[v] = static_cast<ui>(i);
    }
}

void TemporalSCCs::buildWindow(ui t1, ui t2,vector<VertexID> candidates) {
    if (t1 > t2 || t2 >= time_count_)
        throw std::out_of_range("Invalid window");

    std::vector<bool> selected(vertex_count_, false);
    for (VertexID u : candidates) selected[u] = true;

    Window next;
    next.left = t1;
    next.right = t2;
    next.outgoing.resize(vertex_count_);
    next.incoming.resize(vertex_count_);
    for (VertexID u : candidates) {
        ui count = 0;
        const auto* neighbors = getVertexNeighbors(u, t1, t2, count);
        for (ui i = 0; i < count; ++i) {
            const auto& neighbor = neighbors[i];
            if (selected[neighbor.vertex]) next.outgoing[u].emplace_back(neighbor.vertex, neighbor.time);

        }

        neighbors = getVertexInNeighbors(u, t1, t2, count);
        for (ui i = 0; i < count; ++i) {
            const auto& neighbor = neighbors[i];
            if (selected[neighbor.vertex]) next.incoming[u].emplace_back(neighbor.vertex, neighbor.time);
        }

    }
    window_ = std::move(next);
    window_ready_ = true;
}

ul TemporalSCCs::countKCombinationsCapped(ul n,ui k,ul cap) {
    if (n < k || cap == 0) return 0;
    const ul r = std::min<ul>(k,n - k);
    ul combinations = 1;
    for (ul i = 1; i <= r; ++i) {
        ul factor = n - r + i;
        ul divisor = i;
        const ul common = std::gcd(factor,divisor);
        factor /= common;
        divisor /= common;
        combinations /= divisor;
        if (combinations > cap / factor) return cap;
        combinations *= factor;
        if (combinations >= cap) return cap;
    }
    return combinations;
}

long double TemporalSCCs::estimateKCombinations(ul n,ui k) {
    if (n < k) return 0;
    const ul r = std::min<ul>(k,n - k);
    long double combinations = 1;
    for (ul i = 1; i <= r; ++i)
        combinations = combinations * (n - r + i) / i;
    return combinations;
}

std::vector<std::vector<VertexID>> TemporalSCCs::prepareQuery(ui left,ui right,ui k) {
    if (left > right || right >= time_count_)
        throw std::out_of_range("Invalid window");
    if (k < 2)
        throw std::invalid_argument("k must be at least 2");
    answerCount_ = 0;
    const auto candidates = flatten(globalSCCs);
    buildWindow(left,right,candidates);
    auto components = findWindowStaticSCCs(candidates,window_,k - 1);
    buildWindow(left,right,flatten(components));
    return components;
}

ul TemporalSCCs::runQuery(ui left,ui right,ui k,QueryMethod method) {
    const auto components = prepareQuery(left,right,k);
    for (const auto& component : components) {
        if (method == QueryMethod::EnumerateDec)
            decompose(component,k,{});
        else
            enumerateCombinations(component,k,{});
    }
    return answerCount_;
}

std::optional<std::vector<std::vector<VertexID>>> TemporalSCCs::normalizeRequiredGroups(const std::vector<VertexID>& vertices,const std::vector<std::vector<VertexID>>& requiredGroups,ui k) {
    ui n=vertices.size();
    if (n<k) return nullopt;

    std::vector<std::vector<VertexID>> normalized;
    normalized.reserve(requiredGroups.size());
    std::vector<std::size_t> seen(n, requiredGroups.size());

    const ui maxAvoidable = n - k;
    for (std::size_t g = 0; g < requiredGroups.size(); ++g) {
        const auto& source = requiredGroups[g];
        std::vector<VertexID> group;
        group.reserve(std::min((ui)source.size(), n));

        for (VertexID v : source) {
            if (v >= subsetStamp_.size() || subsetStamp_[v] != subsetEpoch_) {
                continue;
            }
            const ui i = subsetIndex_[v];
            if (seen[i] == g)
                continue;
            seen[i] = g;
            group.push_back(v);
            if (group.size() > maxAvoidable)
                break;
        }

        if (group.empty())
            return std::nullopt;

        if (group.size() > maxAvoidable)
            continue;
        normalized.push_back(std::move(group));
    }
    return normalized;
}

bool TemporalSCCs::shouldEnumerateDirectly(const std::vector<VertexID>& vertices, ui k){
    if (k < 2)
        throw std::invalid_argument("k must be at least 2");
    const ul n = vertices.size();
    if (n <= k)
        return true;
    return n - k <= decompositionAlpha_;
}

void TemporalSCCs::enumerateCombinations(const std::vector<VertexID>& vertices,ui k,const std::vector<std::vector<VertexID>>& requiredGroups){
    if (k < 2)
        throw std::invalid_argument("k must be at least 2");

    const std::size_t n = vertices.size();
    const std::size_t K = k;

    if (n < K)
        return;

    if (!window_ready_)
        throw std::logic_error("Call buildWindow first");

    prepareSubsetIndex(vertices);

    const std::size_t groupCount = requiredGroups.size();
    std::vector<std::vector<std::size_t>> groupsAtVertex(n);
    std::vector<std::size_t> lastIndex(groupCount, n);
    std::vector<std::size_t> groupOrder(groupCount);

    for (std::size_t g = 0; g < groupCount; ++g) {
        groupOrder[g] = g;

        for (VertexID v : requiredGroups[g]) {
            if (v >= vertex_count_ ||
                subsetStamp_[v] != subsetEpoch_)
                continue;

            const std::size_t i = subsetIndex_[v];
            auto& list = groupsAtVertex[i];

            if (!list.empty() && list.back() == g)
                continue;

            list.push_back(g);
            if (lastIndex[g] == n || i > lastIndex[g])
                lastIndex[g] = i;
        }

        if (lastIndex[g] == n)
            return;
    }

    std::sort(
        groupOrder.begin(), groupOrder.end(),
        [&](std::size_t a, std::size_t b) {
            return lastIndex[a] < lastIndex[b];
        }
    );

    std::vector<std::size_t> hitCount(groupCount, 0);
    std::size_t missingGroups = groupCount;

    std::vector<VertexID> chosen;
    chosen.reserve(K);
    std::vector<std::size_t> selectedPosition(n, K);

    constexpr ul TIME_INF = std::numeric_limits<ul>::max();
    std::vector<ul> timeLabel(K);
    std::vector<std::size_t> cursor(K);
    std::vector<unsigned char> queued(K);
    std::queue<std::size_t> que;

    const auto isTemporalSCC = [&]() {
        for (std::size_t source = 0; source < K; ++source) {
            while (!que.empty())
                que.pop();

            std::fill(timeLabel.begin(), timeLabel.end(), TIME_INF);
            std::fill(queued.begin(), queued.end(), 0);

            for (std::size_t i = 0; i < K; ++i)
                cursor[i] = window_.outgoing[chosen[i]].size();

            timeLabel[source] = window_.left;
            std::size_t reachedCount = 1;
            queued[source] = 1;
            que.push(source);

            while (!que.empty() && reachedCount < K) {
                const std::size_t i = que.front();
                que.pop();
                queued[i] = 0;

                const auto& edges = window_.outgoing[chosen[i]];
                auto& pos = cursor[i];

                while (pos != 0 && reachedCount < K) {
                    const auto& [v, t] = edges[pos - 1];

                    if (i != source && t <= timeLabel[i])
                        break;

                    --pos;

                    if (subsetStamp_[v] != subsetEpoch_)
                        continue;

                    const std::size_t j =
                        selectedPosition[subsetIndex_[v]];

                    if (j == K || t >= timeLabel[j])
                        continue;

                    if (timeLabel[j] == TIME_INF)
                        ++reachedCount;

                    timeLabel[j] = t;

                    if (!queued[j]) {
                        queued[j] = 1;
                        que.push(j);
                    }
                }
            }

            if (reachedCount != K)
                return false;
        }

        return true;
    };

    if (n == K) {
        chosen = vertices;
        for (std::size_t i = 0; i < K; ++i)
            selectedPosition[i] = i;

        if (isTemporalSCC()) {
            if (answerCount_ == std::numeric_limits<ul>::max())
                throw std::overflow_error("Answer count exceeds uint64_t");
            ++answerCount_;

        }
        return;
    }

    const auto dfs = [&](auto&& self,
                         std::size_t start,
                         std::size_t firstMissing) -> void {
        const std::size_t depth = chosen.size();

        if (depth == K) {
            if (missingGroups == 0 && (isTemporalSCC())) {
                if (answerCount_ == std::numeric_limits<ul>::max())
                    throw std::overflow_error("Answer count exceeds uint64_t");
                ++answerCount_;

            }
            return;
        }

        const std::size_t need = K - depth;

        if (n - start < need)
            return;

        while (firstMissing < groupCount &&
               hitCount[groupOrder[firstMissing]] != 0)
            ++firstMissing;

        std::size_t lastCandidate = n - need;

        if (firstMissing < groupCount) {
            lastCandidate = std::min(
                lastCandidate,
                lastIndex[groupOrder[firstMissing]]
            );
        }

        for (std::size_t i = start; i <= lastCandidate; ++i) {
            chosen.push_back(vertices[i]);
            selectedPosition[i] = depth;

            for (std::size_t g : groupsAtVertex[i]) {
                if (hitCount[g]++ == 0)
                    --missingGroups;
            }

            self(self, i + 1, firstMissing);

            for (std::size_t g : groupsAtVertex[i]) {
                if (--hitCount[g] == 0)
                    ++missingGroups;
            }

            selectedPosition[i] = K;
            chosen.pop_back();
        }
    };

    dfs(dfs, 0, 0);
}

TemporalSCCs::EnumerationIndex TemporalSCCs::buildEnumerationIndex(
    const std::vector<VertexID>& vertices,const std::vector<std::vector<VertexID>>& groups) {
    EnumerationIndex index;
    index.size = static_cast<ui>(vertices.size());
    index.words = (index.size + 63) / 64;
    constexpr ui noPair = std::numeric_limits<ui>::max();

    const bool compressed = index.size > 4096;
    if (compressed)
        index.pairOffsets.resize(static_cast<std::size_t>(index.size) * index.words);
    else
        index.pairIndex.assign(static_cast<std::size_t>(index.size) * index.size,noPair);
    index.outgoing.assign(index.size,std::vector<ul>(index.words,0));
    index.incoming.assign(index.size,std::vector<ul>(index.words,0));
    index.required.assign(groups.size(),std::vector<ul>(index.words,0));
    index.belongs.assign(index.size,0);
    if (groups.size() > 64)
        index.belongsWords.assign(index.size,std::vector<ul>((groups.size() + 63) / 64,0));

    std::vector<ui> rowPairs, targets;
    if (compressed) rowPairs.assign(index.size,noPair);
    for (ui u = 0; u < index.size; ++u) {
        for (const auto& [target,t] : window_.outgoing[vertices[u]]) {
            if (subsetStamp_[target] != subsetEpoch_) continue;
            const ui v = subsetIndex_[target];
            if (u == v) continue;
            auto& pair = compressed ? rowPairs[v] : index.pairIndex[static_cast<std::size_t>(u) * index.size + v];
            if (pair == noPair) {
                if (index.pairTimes.size() >= noPair)
                    throw std::overflow_error("Enumeration pair count exceeds uint32_t");
                pair = static_cast<ui>(index.pairTimes.size());
                index.pairTimes.emplace_back();
                if (compressed) targets.push_back(v);
                index.outgoing[u][v / 64] |= ul{1} << (v % 64);
                index.incoming[v][u / 64] |= ul{1} << (u % 64);
            }
            auto& times = index.pairTimes[pair];

            if (times.empty() || times.back() != t) times.push_back(t);
        }
        if (compressed) {

            ui offset = static_cast<ui>(index.pairIndex.size());
            for (ui word = 0; word < index.words; ++word) {
                index.pairOffsets[static_cast<std::size_t>(u) * index.words + word] = offset;
                offset += static_cast<ui>(__builtin_popcountll(index.outgoing[u][word]));
            }
            std::sort(targets.begin(),targets.end());
            for (ui v : targets) {
                index.pairIndex.push_back(rowPairs[v]);
                rowPairs[v] = noPair;
            }
            targets.clear();
        }
    }
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (VertexID v : groups[g]) {
            const ui i = subsetIndex_[v];
            index.required[g][i / 64] |= ul{1} << (i % 64);
            if (g < 64) index.belongs[i] |= ul{1} << g;
            if (!index.belongsWords.empty())
                index.belongsWords[i][g / 64] |= ul{1} << (g % 64);
        }
    }
    return index;
}

ui TemporalSCCs::findEnumerationPair(const EnumerationIndex& index,ui source,ui target) const {
    if (index.pairOffsets.empty())
        return index.pairIndex[static_cast<std::size_t>(source) * index.size + target];
    const ui word = target / 64;
    const ul bit = ul{1} << (target % 64);
    const ul neighbors = index.outgoing[source][word];
    if ((neighbors & bit) == 0) return std::numeric_limits<ui>::max();
    const ui offset = index.pairOffsets[static_cast<std::size_t>(source) * index.words + word];
    return index.pairIndex[offset + static_cast<ui>(__builtin_popcountll(neighbors & (bit - 1)))];
}

bool TemporalSCCs::collectEnumerationRequirements(const EnumerationIndex& index,
const std::vector<ui>& chosen,const std::vector<ul>& candidates,ul missing,ui slots,
    std::array<const ul*,kMaxEnumerationRequirements>& required,ui& count) const {
    count = 0;
    while (missing != 0) {
        const ui g = static_cast<ui>(__builtin_ctzll(missing));
        missing &= missing - 1;
        required[count++] = index.required[g].data();
    }

    for (ui u : chosen) {
        bool hasOutgoing = false, hasIncoming = false;
        for (ui v : chosen) {
            const ul bit = ul{1} << (v % 64);
            hasOutgoing = hasOutgoing || (index.outgoing[u][v / 64] & bit) != 0;
            hasIncoming = hasIncoming || (index.incoming[u][v / 64] & bit) != 0;
        }
        if (!hasOutgoing) required[count++] = index.outgoing[u].data();
        if (!hasIncoming) required[count++] = index.incoming[u].data();
    }

    return checkEnumerationRequirements(index,candidates,slots,required.data(),count);
}

bool TemporalSCCs::checkEnumerationRequirements(const EnumerationIndex& index,
    const std::vector<ul>& candidates,ui slots,const ul* const* required,ui count) const {
    std::array<const ul*,kMaxEnumerationSize + 1> disjoint{};
    ui disjointCount = 0;
    for (ui g = 0; g < count; ++g) {
        bool possible = false;
        for (ui word = 0; word < index.words; ++word)
            if ((candidates[word] & required[g][word]) != 0) {
                possible = true;
                break;
            }
        if (!possible) return false;

        bool separate = true;
        for (ui previous = 0; previous < disjointCount && separate; ++previous) {
            for (ui word = 0; word < index.words; ++word) {
                if ((candidates[word] & required[g][word] & disjoint[previous][word]) != 0) {
                    separate = false;
                    break;
                }
            }
        }
        if (separate) {
            if (++disjointCount > slots) return false;
            disjoint[disjointCount - 1] = required[g];
        }
    }
    return true;
}

bool TemporalSCCs::isTemporalSCCPacked(const EnumerationIndex& index,const std::vector<ui>& chosen) const {
    const ui k = static_cast<ui>(chosen.size());
    if (k < 2 || k > kMaxEnumerationSize)
        throw std::invalid_argument("Packed validation requires 2 <= k <= 8");

    constexpr ui noPair = std::numeric_limits<ui>::max();
    std::array<ui,kMaxEnumerationSize * kMaxEnumerationSize> pairs;
    pairs.fill(noPair);
    const ui all = (ui{1} << k) - 1;
    std::array<ui,kMaxEnumerationSize> outgoing{}, incoming{};
    for (ui i = 0; i < k; ++i) {
        for (ui j = 0; j < k; ++j) {
            if (i == j) continue;
            const ui pair = findEnumerationPair(index,chosen[i],chosen[j]);
            pairs[i * kMaxEnumerationSize + j] = pair;
            if (pair == noPair) continue;
            outgoing[i] |= ui{1} << j;
            incoming[j] |= ui{1} << i;
        }
        if (outgoing[i] == 0)
            return false;
    }
    for (ui i = 0; i < k; ++i)
        if (incoming[i] == 0) return false;

    const auto reachesAll = [&](const std::array<ui,kMaxEnumerationSize>& adjacency) {
        ui reached = 1, previous;
        do {
            previous = reached;
            for (ui i = 0; i < k; ++i)
                if (reached & (ui{1} << i)) reached |= adjacency[i];
        } while (reached != previous);
        return reached == all;
    };

    if (!reachesAll(outgoing) || !reachesAll(incoming))
        return false;

    if (k >= 4) {
        constexpr ul noTime = std::numeric_limits<ul>::max();
        std::array<ul,kMaxEnumerationSize> arrivals;
        arrivals.fill(noTime);
        arrivals[0] = window_.left;
        relaxArrivals(index,pairs,k,0,0,arrivals.data());
        for (ui vertex = 0; vertex < k; ++vertex)
            if (arrivals[vertex] == noTime) return false;
    }

    struct Stream {
        const std::vector<ui>* times;
        std::size_t position;
        ui source;
        ui target;
        ui time;
    };
    std::array<Stream,kMaxEnumerationSize * (kMaxEnumerationSize - 1)> heap;
    ui streamCount = 0;
    for (ui i = 0; i < k; ++i) {
        for (ui j = 0; j < k; ++j) {
            if (i == j) continue;
            const ui pair = pairs[i * kMaxEnumerationSize + j];
            if (pair == noPair) continue;
            const auto& times = index.pairTimes[pair];
            heap[streamCount++] = {&times,times.size() - 1,i,j,times.back()};
        }
    }
    const auto earlier = [](const Stream& a,const Stream& b) {
        return a.time < b.time;
    };
    std::make_heap(heap.begin(),heap.begin() + streamCount,earlier);

    ul identity = 0, column0 = 0;

    for (ui i = 0; i < k; ++i) {
        identity |= ul{1} << (8 * i + i);
        column0 |= ul{1} << (8 * i);
    }
    const ul full = static_cast<ul>(all) * column0;
    ul future = identity, snapshot = identity;

    ui currentTime = 0;
    bool firstTime = true;

    while (streamCount != 0) {
        std::pop_heap(heap.begin(),heap.begin() + streamCount,earlier);
        auto stream = heap[--streamCount];
        if (firstTime || stream.time != currentTime) {
            currentTime = stream.time;
            snapshot = future;
            firstTime = false;
        }

        const ul futureTargets = (snapshot >> (8 * stream.target)) & 0xff;
        future |= futureTargets << (8 * stream.source);
        if (future == full)
            return true;

        if (stream.position != 0) {
            --stream.position;
            stream.time = (*stream.times)[stream.position];
            heap[streamCount++] = stream;
            std::push_heap(heap.begin(),heap.begin() + streamCount,earlier);
        }
    }
    return false;
}

bool TemporalSCCs::hasEnumerationCenter(const EnumerationIndex& index,const std::vector<ui>& chosen,const std::vector<ul>& candidates) const {
    constexpr ui noPair = std::numeric_limits<ui>::max();
    for (ui center : chosen) {
        ui latestIncoming = 0;
        ui earliestOutgoing = std::numeric_limits<ui>::max();
        const auto compatible = [&](ui vertex) {
            if (vertex == center) return true;
            const ui inPair = findEnumerationPair(index,vertex,center);
            const ui outPair = findEnumerationPair(index,center,vertex);
            if (inPair == noPair || outPair == noPair)
                return false;
            latestIncoming = std::max(latestIncoming,index.pairTimes[inPair].front());
            earliestOutgoing = std::min(earliestOutgoing,index.pairTimes[outPair].back());
            return latestIncoming < earliestOutgoing;
        };

        bool valid = true;
        for (ui vertex : chosen) {
            if (!compatible(vertex)) {
                valid = false;
                break;
            }
        }
        for (ui word = 0; valid && word < index.words; ++word) {
            ul bits = candidates[word];
            while (bits != 0) {
                const ui bit = static_cast<ui>(__builtin_ctzll(bits));
                bits &= bits - 1;
                if (!compatible(word * 64 + bit)) {
                    valid = false;
                    break;
                }
            }
        }
        if (valid)
            return true;
    }
    return false;
}

VertexID TemporalSCCs::selectPivot(const std::vector<VertexID>& vertices) {

    ui n=vertices.size();
    vector<int> VerticesX0(n,0);
    vector<int> VerticesU0(n,0);
    vector<pair<int,int>> verticesInfo(n);
    vector<int> vector_lv;
    vector_lv.reserve(n);

    for (ui i=0;i<n;i++){
        VertexID v=vertices[i];
        int dv=INF;
        int lv=-1;
        for (auto& [u,t] : window_.outgoing[v]) {
            if (subsetStamp_[u]!=subsetEpoch_) continue;
            dv=t;
            break;
        }

        for (auto it = window_.incoming[v].rbegin(); it != window_.incoming[v].rend(); ++it) {
            auto& [u, t] = *it;
            if (subsetStamp_[u]!=subsetEpoch_) continue;
            lv=t;
            break;
        }

        vector_lv.push_back(lv);
        verticesInfo[i]={dv,lv};
    }

    sort(vector_lv.begin(),vector_lv.end());
    std::vector<std::size_t> seenX(n, n);
    for (ui i=0;i<n;i++) {
        VertexID p=vertices[i];
        auto [dp,lp]=verticesInfo[i];
        VerticesU0[i] = std::lower_bound(vector_lv.begin(), vector_lv.end(), dp) - vector_lv.begin();
        if (lp < dp) --VerticesU0[i];
        for (const auto& [v, t] : window_.outgoing[p]) {
            if (subsetStamp_[v] != subsetEpoch_) continue;
            const std::size_t j = subsetIndex_[v];
            if (t <= verticesInfo[j].first && seenX[j] != i) {
                seenX[j] = i;
                ++VerticesX0[i];
            }
        }
    }

    ul max_score=0;
    VertexID selectPoint=vertices.front();
    for (int i=0;i<n;i++) {
        ul score=(ul)VerticesX0[i]*VerticesU0[i];
        if (score>max_score) {
            max_score=score;
            selectPoint=vertices[i];
        }
    }
    return selectPoint;
}

void TemporalSCCs::computeEarliestArrival(const VertexID &selectedVertex, const std::vector<VertexID> &vertices, std::vector<ui> &arrival, SplitResult &split,bool flag) {

    ui n=vertices.size();
    arrival.assign(n,std::numeric_limits<ui>::max());
    const auto& adjacency = flag ? window_.outgoing : window_.incoming;

    std::vector<ui> cursor(n, 0);
    if (flag) {
        for (ui i=0;i<n;i++) {
            cursor[i]=adjacency[vertices[i]].size();
        }
    }

    std::vector<unsigned char> reached(n, 0);
    std::vector<unsigned char> queued(n, 0);
    std::queue<VertexID> que;

    const auto enqueue = [&](std::size_t i) {
        if (queued[i])
            return;
        queued[i] = 1;
        que.push(i);
    };

    VertexID source=subsetIndex_[selectedVertex];
    ui reachedCount=1;
    reached[source]=1;

    arrival[source]= flag ? window_.left : window_.right;
    enqueue(source);

    const auto relax = [&](VertexID v, ui t) {
        if (subsetStamp_[v] != subsetEpoch_)
            return;

        const ui j = subsetIndex_[v];
        const bool firstVisit = !reached[j];

        if (firstVisit || (flag ? t < arrival[j] : t > arrival[j])) {
            if (firstVisit) {
                reached[j] = 1;
                ++reachedCount;
            }
            arrival[j] = t;
            enqueue(j);
        }
    };

    while (!que.empty()) {
        const ui i = que.front();
        que.pop();
        queued[i] = 0;

        const auto& edges = adjacency[vertices[i]];
        auto& pos = cursor[i];

        if (flag) {

            while (pos != 0) {
                const auto& [v, t] = edges[pos - 1];
                if (i != source && t <= arrival[i])
                    break;
                --pos;
                relax(v, t);
            }
        } else {

            while (pos < edges.size()) {
                const auto& [v, t] = edges[pos];
                if (i != source && t >= arrival[i])
                    break;
                ++pos;
                relax(v, t);
            }
        }
    }

    split.R.reserve(reachedCount);
    split.U.reserve(n - reachedCount);

    for (std::size_t i = 0; i < n; ++i) {
        if (reached[i])
            split.R.push_back(vertices[i]);
        else
            split.U.push_back(vertices[i]);
    }
}

void TemporalSCCs::buildOverlapCover(const std::vector<VertexID> &vertices, std::vector<ui> &arrival, SplitResult &split, bool flag) {

    ui n=vertices.size();
    if (split.U.empty()) return;

    std::vector<unsigned char> isU(n, 0);
    std::vector<unsigned char> inQ(n, 0);
    std::queue<std::size_t> que;

    split.Q.reserve(n);

    const auto enqueue = [&](ui i) {
        if (inQ[i])
            return;

        inQ[i] = 1;
        split.Q.push_back(vertices[i]);
        que.push(i);
    };

    for (VertexID u : split.U) {
        const ui i = subsetIndex_[u];
        isU[i] = 1;
        enqueue(i);
    }

    while (!que.empty()) {
        const VertexID i = que.front();
        que.pop();
        const VertexID v = vertices[i];

        if (flag) {

            for (const auto& [u, t] : window_.incoming[v]) {
                if (!isU[i] && t >= arrival[i])
                    break;
                if (subsetStamp_[u] != subsetEpoch_)
                    continue;
                enqueue(subsetIndex_[u]);
            }
        } else {

            const auto& edges = window_.outgoing[v];
            for (auto it = edges.rbegin();it != edges.rend();++it) {
                const auto& [u, t] = *it;
                if (!isU[i] && t <= arrival[i])
                    break;
                if (subsetStamp_[u] != subsetEpoch_)
                    continue;
                enqueue(subsetIndex_[u]);
            }
        }
    }
}

VertexID TemporalSCCs::selectReversePivot(const std::vector<VertexID>& vertices,
    const std::vector<VertexID>& Ux,ReversePivotCache& cache) {
    if (Ux.empty()) throw std::invalid_argument("Cannot select a pivot from an empty set");
    if (!window_ready_) throw std::logic_error("Call buildWindow first");

    prepareSubsetIndex(vertices);
    if (Ux.size() == 1) {
        const VertexID seed = Ux.front();
        if (seed >= vertex_count_ || subsetStamp_[seed] != subsetEpoch_)
            throw std::out_of_range("Invalid reverse cover seed");
        return seed;
    }
    const std::size_t n = vertices.size();
    if (cache.degrees.empty()) {
        cache.degrees.assign(n,0);
        for (std::size_t i = 0; i < n; ++i) {
            for (const auto& edge : window_.outgoing[vertices[i]])
                if (subsetStamp_[edge.first] == subsetEpoch_) ++cache.degrees[i];
            for (const auto& edge : window_.incoming[vertices[i]])
                if (subsetStamp_[edge.first] == subsetEpoch_) ++cache.degrees[i];
        }
    }
    ul maximumDegree = 0;
    std::vector<VertexID> tied;
    for (VertexID v : Ux) {
        if (v >= vertex_count_ || subsetStamp_[v] != subsetEpoch_)
            throw std::out_of_range("Invalid reverse cover seed");
        const ul degree = cache.degrees[subsetIndex_[v]];
        if (tied.empty() || degree > maximumDegree) {
            maximumDegree = degree;
            tied.clear();
            tied.push_back(v);
        } else if (degree == maximumDegree) {
            tied.push_back(v);
        }
    }

    if (tied.size() == 1) return tied.front();

    if (cache.departure.empty()) {
        const long long noDeparture = static_cast<long long>(std::numeric_limits<ui>::max()) + 1;
        cache.departure.assign(n,noDeparture);
        cache.latest.assign(n,-1);
        cache.scores.resize(n);
        cache.scoreReady.assign(n,0);
        cache.seen.assign(n,n);
        for (std::size_t i = 0; i < n; ++i) {
            for (const auto& [v,t] : window_.outgoing[vertices[i]]) {
                if (subsetStamp_[v] != subsetEpoch_) continue;
                cache.departure[i] = t;
                break;
            }
            const auto& incoming = window_.incoming[vertices[i]];
            for (auto edge = incoming.rbegin(); edge != incoming.rend(); ++edge) {
                if (subsetStamp_[edge->first] != subsetEpoch_) continue;
                cache.latest[i] = edge->second;
                break;
            }
        }
        cache.sortedLatest = cache.latest;
        std::sort(cache.sortedLatest.begin(),cache.sortedLatest.end());
    }
    VertexID selected = tied.front();
    ul bestScore = 0;
    for (VertexID p : tied) {
        const std::size_t i = subsetIndex_[p];
        if (!cache.scoreReady[i]) {
            ul unreachable = static_cast<ul>(std::lower_bound(cache.sortedLatest.begin(),cache.sortedLatest.end(),
                cache.departure[i]) - cache.sortedLatest.begin());
            if (cache.latest[i] < cache.departure[i]) --unreachable;
            ul earlyNeighbors = 0;
            for (const auto& [v,t] : window_.outgoing[p]) {
                if (subsetStamp_[v] != subsetEpoch_) continue;
                const std::size_t j = subsetIndex_[v];
                if (t <= cache.departure[j] && cache.seen[j] != i) {
                    cache.seen[j] = i;
                    ++earlyNeighbors;
                }
            }
            cache.scores[i] = earlyNeighbors * unreachable;
            cache.scoreReady[i] = 1;
        }
        const ul score = cache.scores[i];
        if (score > bestScore) {
            bestScore = score;
            selected = p;
        }
    }
    return selected;
}

std::pair<std::vector<VertexID>,std::vector<VertexID>> TemporalSCCs::buildReverseCover(const VertexID& selectedVertex, const std::vector<VertexID>& vertices,std::vector<ui>& arrival, const std::vector<VertexID>& Ux) {
    if (arrival.size() != vertices.size())
        throw std::invalid_argument("Arrival labels must match the candidate set");

    prepareSubsetIndex(vertices);
    if (selectedVertex >= vertex_count_ || subsetStamp_[selectedVertex] != subsetEpoch_)
        throw std::out_of_range("Invalid reverse cover seed");

    SplitResult split;
    split.U.push_back(selectedVertex);
    buildOverlapCover(vertices,arrival,split,true);

    std::vector<unsigned char> reached(vertices.size(),0);
    for (VertexID v : split.Q) reached[subsetIndex_[v]] = 1;
    std::vector<VertexID> U2;
    U2.reserve(Ux.size());
    for (VertexID v : Ux) {
        if (!reached[subsetIndex_[v]]) U2.push_back(v);
    }
    return {std::move(split.Q),std::move(U2)};
}

TemporalSCCs::PivotIndex TemporalSCCs::buildPivotIndex(
    const std::vector<VertexID>& vertices,VertexID& degreePivot,VertexID& neighborPivot) {
    if (vertices.empty()) throw std::invalid_argument("Cannot index an empty pivot set");
    if (!window_ready_) throw std::logic_error("Call buildWindow first");
    prepareSubsetIndex(vertices);
    const std::size_t n = vertices.size();
    PivotIndex index;
    index.vertices = vertices;
    index.outgoing.resize(n);
    index.state.resize(n);
    index.discovery.resize(n);
    index.low.resize(n);
    index.active.reserve(n);
    index.dfs.reserve(n);
    std::vector<std::size_t> seenOutgoing(n,n), seenNeighbor(n,n);
    ul maxDegree = 0;
    ui maxNeighbors = 0;
    degreePivot = neighborPivot = vertices.front();
    for (std::size_t i = 0; i < n; ++i) {
        const VertexID v = vertices[i];
        ul degree = 0;
        ui neighbors = 0;
        for (const auto& [u,t] : window_.outgoing[v]) {
            if (subsetStamp_[u] != subsetEpoch_) continue;
            ++degree;
            const ui j = subsetIndex_[u];
            if (seenOutgoing[j] != i) {
                seenOutgoing[j] = i;
                index.outgoing[i].push_back(j);
            }
            if (seenNeighbor[j] != i) {
                seenNeighbor[j] = i;
                ++neighbors;
            }
        }
        for (const auto& [u,t] : window_.incoming[v]) {
            if (subsetStamp_[u] != subsetEpoch_) continue;
            ++degree;
            const ui j = subsetIndex_[u];
            if (seenNeighbor[j] != i) {
                seenNeighbor[j] = i;
                ++neighbors;
            }
        }
        if (degree > maxDegree) {
            maxDegree = degree;
            degreePivot = v;
        }
        if (neighbors > maxNeighbors) {
            maxNeighbors = neighbors;
            neighborPivot = v;
        }
    }
    return index;
}

long double TemporalSCCs::scorePivotSubset(const std::vector<VertexID>& subset,ui k,
    PivotIndex& index,std::size_t& largest,long double cutoff) {
    if (k < 2) throw std::invalid_argument("k must be at least 2");
    prepareSubsetIndex(index.vertices);
    largest = 0;
    if (subset.size() < k) return 0;
    std::fill(index.state.begin(),index.state.end(),0);
    index.active.clear();
    index.dfs.clear();
    for (VertexID v : subset) {
        if (v >= subsetStamp_.size() || subsetStamp_[v] != subsetEpoch_)
            throw std::out_of_range("Pivot subset must belong to the indexed set");
        index.state[subsetIndex_[v]] = 1;
    }

    ui nextIndex = 0;
    long double cost = 0;
    for (VertexID v : subset) {
        const ui root = subsetIndex_[v];
        if (index.state[root] != 1) continue;
        index.discovery[root] = index.low[root] = nextIndex++;
        index.state[root] = 2;
        index.active.push_back(root);
        index.dfs.emplace_back(root,0);
        while (!index.dfs.empty()) {
            const ui u = index.dfs.back().first;
            auto& position = index.dfs.back().second;
            const auto& edges = index.outgoing[u];
            if (position < edges.size()) {
                const ui target = edges[position++];
                if (index.state[target] == 0 || index.state[target] == 3) continue;
                if (index.state[target] == 1) {
                    index.discovery[target] = index.low[target] = nextIndex++;
                    index.state[target] = 2;
                    index.active.push_back(target);
                    index.dfs.emplace_back(target,0);
                } else {
                    index.low[u] = std::min(index.low[u],index.discovery[target]);
                }
                continue;
            }
            if (index.low[u] == index.discovery[u]) {
                std::size_t size = 0;
                ui member;
                do {
                    member = index.active.back();
                    index.active.pop_back();
                    index.state[member] = 3;
                    ++size;
                } while (member != u);
                if (size >= k) {
                    cost += estimateKCombinations(size,k);
                    largest = std::max(largest,size);
                    if (cost > cutoff) return cost;
                }
            }
            index.dfs.pop_back();
            if (!index.dfs.empty()) {
                const ui parent = index.dfs.back().first;
                index.low[parent] = std::min(index.low[parent],index.low[u]);
            }
        }
    }
    return cost;
}

bool TemporalSCCs::collectEnumerationRequirementsMultiword(const EnumerationIndex& index,
    const std::vector<ui>& chosen,const std::vector<ul>& candidates,const std::vector<ul>& missing,
    ui slots,std::vector<const ul*>& required,ui& ownershipCount) const {
    required.clear();
    for (std::size_t word = 0; word < missing.size(); ++word) {
        ul bits = missing[word];
        while (bits != 0) {
            const ui bit = static_cast<ui>(__builtin_ctzll(bits));
            bits &= bits - 1;
            required.push_back(index.required[word * 64 + bit].data());
        }
    }
    ownershipCount = static_cast<ui>(required.size());
    for (ui u : chosen) {
        bool hasOutgoing = false, hasIncoming = false;
        for (ui v : chosen) {
            const ul bit = ul{1} << (v % 64);
            hasOutgoing = hasOutgoing || (index.outgoing[u][v / 64] & bit) != 0;
            hasIncoming = hasIncoming || (index.incoming[u][v / 64] & bit) != 0;
        }
        if (!hasOutgoing) required.push_back(index.outgoing[u].data());
        if (!hasIncoming) required.push_back(index.incoming[u].data());
    }
    return checkEnumerationRequirements(index,candidates,slots,required.data(),static_cast<ui>(required.size()));
}

void TemporalSCCs::enumerateCenterCompletions(const EnumerationIndex& index,std::vector<ui>& chosen,const std::vector<ul>& candidates,
    const std::vector<ul>& missing,ui slots) {
    if (slots == 0 || chosen.size() + slots > kMaxEnumerationSize || index.belongsWords.empty())
        throw std::invalid_argument("Invalid multiword center completion");
    const std::size_t words = missing.size();
    std::vector<ui> completionVertices;
    for (ui word = 0; word < index.words; ++word) {
        ul bits = candidates[word];
        while (bits != 0) {
            const ui bit = static_cast<ui>(__builtin_ctzll(bits));
            bits &= bits - 1;
            completionVertices.push_back(word * 64 + bit);
        }
    }
    const ui available = static_cast<ui>(completionVertices.size());
    std::vector<ul> suffixGroups((static_cast<std::size_t>(available) + 1) * words,0);
    for (ui i = available; i != 0; --i)
        for (std::size_t word = 0; word < words; ++word)
            suffixGroups[(i - 1) * words + word] = suffixGroups[i * words + word] |
                index.belongsWords[completionVertices[i - 1]][word];
    std::array<std::vector<ul>,kMaxEnumerationSize + 1> remainingPools;
    for (ui depth = 0; depth <= slots; ++depth) remainingPools[depth].resize(words);
    remainingPools[0] = missing;
    const auto complete = [&](auto&& self,ui start,ui need,ui depth) -> void {
        if (available - start < need) return;
        const auto& remaining = remainingPools[depth];
        for (std::size_t word = 0; word < words; ++word)
            if ((remaining[word] & ~suffixGroups[start * words + word]) != 0) return;
        if (need == 1) {
            for (ui i = start; i < available; ++i) {
                const ui v = completionVertices[i];
                bool covers = true;
                for (std::size_t word = 0; word < words; ++word)
                    if ((remaining[word] & ~index.belongsWords[v][word]) != 0) {
                        covers = false;
                        break;
                    }
                if (!covers) continue;
                chosen.push_back(v);
                ++answerCount_;

                chosen.pop_back();
            }
            return;
        }
        const ui last = available - need;
        for (ui i = start; i <= last; ++i) {
            for (std::size_t word = 0; word < words; ++word)
                if ((remaining[word] & ~suffixGroups[i * words + word]) != 0) return;
            const ui v = completionVertices[i];
            auto& next = remainingPools[depth + 1];
            for (std::size_t word = 0; word < words; ++word)
                next[word] = remaining[word] & ~index.belongsWords[v][word];
            chosen.push_back(v);
            self(self,i + 1,need - 1,depth + 1);
            chosen.pop_back();
        }
    };
    complete(complete,0,slots,0);
}

TemporalSCCs::PrefixLabels TemporalSCCs::buildPrefixLabelsGeneral(
    const EnumerationIndex& index,const std::vector<ui>& chosen) const {
    const ui size = static_cast<ui>(chosen.size());
    if (size == 0 || size >= kMaxEnumerationSize)
        throw std::invalid_argument("Prefix validation requires 1 <= size <= 7");
    if (!window_ready_) throw std::logic_error("Call buildWindow first");

    constexpr ui noPair = std::numeric_limits<ui>::max();
    constexpr ul noTime = std::numeric_limits<ul>::max();
    PrefixLabels prefix;
    prefix.size = size;
    prefix.earliest.fill(noTime);
    prefix.latest.fill(noTime);
    prefix.allArrivals.fill(noTime);
    prefix.allDepartures.fill(noTime);
    std::array<ui,kMaxEnumerationSize * kMaxEnumerationSize> pairs;
    pairs.fill(noPair);
    for (ui i = 0; i < size; ++i) {
        if (chosen[i] >= index.size)
            throw std::out_of_range("Invalid prefix vertex");
        for (ui j = 0; j < i; ++j)
            if (chosen[i] == chosen[j])
                throw std::invalid_argument("Duplicate prefix vertex");
    }
    for (ui i = 0; i < size; ++i)
        for (ui j = 0; j < size; ++j)
            pairs[i * kMaxEnumerationSize + j] = findEnumerationPair(index,chosen[i],chosen[j]);

    for (ui source = 0; source < size; ++source) {
        std::array<ul,kMaxEnumerationSize> label;
        label.fill(noTime);
        label[source] = window_.left;
        ui settled = 0;
        for (ui step = 0; step < size; ++step) {
            ui u = size;
            ul time = noTime;
            for (ui i = 0; i < size; ++i)
                if ((settled & (ui{1} << i)) == 0 && label[i] < time) {
                    u = i;
                    time = label[i];
                }
            if (u == size) break;
            settled |= ui{1} << u;
            for (ui v = 0; v < size; ++v) {
                const ui pair = pairs[u * kMaxEnumerationSize + v];
                if ((settled & (ui{1} << v)) != 0 || pair == noPair) continue;
                const auto& times = index.pairTimes[pair];
                if (u != source && times.back() <= time) continue;
                const ui arrival = u == source || times.front() > time ? times.front() :
                    *std::upper_bound(times.begin(),times.end(),static_cast<ui>(time));
                if (arrival < label[v]) label[v] = arrival;
            }
        }
        for (ui v = 0; v < size; ++v) {
            prefix.earliest[source * kMaxEnumerationSize + v] = label[v];
            if (label[v] == noTime) prefix.missing[source] |= ui{1} << v;
        }
    }

    for (ui target = 0; target < size; ++target) {
        std::array<ul,kMaxEnumerationSize> label;
        label.fill(noTime);
        label[target] = static_cast<ul>(window_.right) + 1;
        ui settled = 0;
        for (ui step = 0; step < size; ++step) {
            ui v = size;
            ul time = 0;
            for (ui i = 0; i < size; ++i)
                if ((settled & (ui{1} << i)) == 0 && label[i] != noTime &&
                    (v == size || label[i] > time)) {
                    v = i;
                    time = label[i];
                }
            if (v == size) break;
            settled |= ui{1} << v;
            for (ui u = 0; u < size; ++u) {
                const ui pair = pairs[u * kMaxEnumerationSize + v];
                if ((settled & (ui{1} << u)) != 0 || pair == noPair) continue;
                const auto& times = index.pairTimes[pair];
                if (times.front() >= time) continue;
                const ui departure = times.back() < time ? times.back() :
                    *std::prev(std::lower_bound(times.begin(),times.end(),static_cast<ui>(time)));
                if (label[u] == noTime || departure > label[u]) label[u] = departure;
            }
        }
        for (ui u = 0; u < size; ++u)
            prefix.latest[u * kMaxEnumerationSize + target] = label[u];
    }

    prefix.stronglyConnected = true;
    for (ui u = 0; u < size; ++u)
        if (prefix.missing[u] != 0) prefix.stronglyConnected = false;

    if (prefix.stronglyConnected) {
        for (ui u = 0; u < size; ++u) {
            ul arrival = 0, departure = noTime;
            for (ui v = 0; v < size; ++v) {
                arrival = std::max(arrival,prefix.earliest[v * kMaxEnumerationSize + u]);
                departure = std::min(departure,prefix.latest[u * kMaxEnumerationSize + v]);
            }
            prefix.allArrivals[u] = arrival;
            prefix.allDepartures[u] = departure;
        }
    }
    return prefix;
}

void TemporalSCCs::relaxArrivals(const EnumerationIndex& index,
    const std::array<ui,kMaxEnumerationSize * kMaxEnumerationSize>& pairs,
    ui size,ui source,ui seed,ul* arrival) const {
    constexpr ui noPair = std::numeric_limits<ui>::max();
    constexpr ul noTime = std::numeric_limits<ul>::max();
    if (arrival[seed] == noTime) return;
    ui active = ui{1} << seed, settled = 0;
    while (active != 0) {
        ui u = size;
        ul time = noTime;
        ui bits = active;
        while (bits != 0) {
            const ui v = static_cast<ui>(__builtin_ctz(bits));
            bits &= bits - 1;
            if (arrival[v] < time) {
                u = v;
                time = arrival[v];
            }
        }
        active &= ~(ui{1} << u);
        settled |= ui{1} << u;
        for (ui v = 0; v < size; ++v) {
            if ((settled & (ui{1} << v)) != 0 || time >= arrival[v]) continue;
            const ui pair = pairs[u * kMaxEnumerationSize + v];
            if (pair == noPair) continue;
            const auto& times = index.pairTimes[pair];
            if (u != source && times.back() <= time) continue;
            const ui next = u == source || times.front() > time ? times.front() :
                *std::upper_bound(times.begin(),times.end(),static_cast<ui>(time));
            if (next < arrival[v]) {
                arrival[v] = next;
                active |= ui{1} << v;
            }
        }
    }
}

void TemporalSCCs::extendPrefixState(const EnumerationIndex& index,
    const std::vector<ui>& chosen,const PrefixState& parent,ui vertex,
    PrefixState& next) const {
    const ui size = parent.size;
    if (size >= kMaxEnumerationSize || chosen.size() != size || vertex >= index.size)
        throw std::invalid_argument("Invalid layered temporal extension");
    constexpr ui noPair = std::numeric_limits<ui>::max();
    constexpr ul noTime = std::numeric_limits<ul>::max();
    next = parent;
    if (size == 0) {
        next.pairs.fill(noPair);
        next.earliest.fill(noTime);
    }
    next.size = size + 1;
    next.earliest[size * kMaxEnumerationSize + size] = window_.left;
    next.reachable |= ul{1} << (8 * size + size);
    for (ui i = 0; i < size; ++i) {
        next.pairs[i * kMaxEnumerationSize + size] = findEnumerationPair(index,chosen[i],vertex);
        next.pairs[size * kMaxEnumerationSize + i] = findEnumerationPair(index,vertex,chosen[i]);
        next.earliest[i * kMaxEnumerationSize + size] = noTime;
        next.earliest[size * kMaxEnumerationSize + i] = noTime;
    }

    for (ui source = 0; source < size; ++source) {
        auto* arrival = next.earliest.data() + source * kMaxEnumerationSize;
        ul first = noTime, lastKnown = 0;
        for (ui a = 0; a < size; ++a) {
            lastKnown = std::max(lastKnown,arrival[a]);
            const ui pair = next.pairs[a * kMaxEnumerationSize + size];
            if (pair == noPair || arrival[a] == noTime || arrival[a] >= first) continue;
            const auto& times = index.pairTimes[pair];
            if ((a != source && times.back() <= arrival[a]) || times.front() >= first) continue;
            const ui time = a == source || times.front() > arrival[a] ? times.front() :
                *std::upper_bound(times.begin(),times.end(),static_cast<ui>(arrival[a]));
            first = std::min(first,static_cast<ul>(time));
        }
        arrival[size] = first;
        if (first == noTime) continue;
        next.reachable |= ul{1} << (8 * source + size);

        if (first < lastKnown)
            relaxArrivals(index,next.pairs,next.size,source,size,arrival);

        if (lastKnown == noTime)
            for (ui v = 0; v < size; ++v)
                if (arrival[v] != noTime) next.reachable |= ul{1} << (8 * source + v);
    }
    auto* arrival = next.earliest.data() + size * kMaxEnumerationSize;
    relaxArrivals(index,next.pairs,next.size,size,size,arrival);
    for (ui v = 0; v < next.size; ++v)
        if (arrival[v] != noTime) next.reachable |= ul{1} << (8 * size + v);

    const ul all = (ul{1} << next.size) - 1;
    ul full = 0;
    for (ui i = 0; i < next.size; ++i) full |= all << (8 * i);
    next.stronglyConnected = next.reachable == full;
}

std::vector<std::vector<ui>> TemporalSCCs::buildMutualReachabilityGraph(const std::vector<VertexID>& vertices) {
    if (!window_ready_)
        throw std::logic_error("Call buildWindow first");
    for (VertexID v : vertices) {
        if (v >= vertex_count_)
            throw std::out_of_range("Invalid mutual reachability vertex ID");
    }

    const ui n = vertices.size();
    prepareSubsetIndex(vertices);
    std::vector<std::vector<bool>> reachable(n,std::vector<bool>(n,false));
    std::vector<ui> arrival;
    for (ui i = 0; i < n; ++i) {
        SplitResult split;
        computeEarliestArrival(vertices[i],vertices,arrival,split,true);
        for (VertexID v : split.R) reachable[i][subsetIndex_[v]] = true;
    }

    std::vector<std::vector<ui>> mutualGraph(n);
    for (ui i = 0; i < n; ++i) {
        for (ui j = i + 1; j < n; ++j) {
            if (reachable[i][j] && reachable[j][i]) {
                mutualGraph[i].push_back(j);
                mutualGraph[j].push_back(i);
            }
        }
    }
    return mutualGraph;
}

ul TemporalSCCs::enumerateCom(ui t1,ui t2,ui k) {
    return runQuery(t1,t2,k,QueryMethod::EnumerateCom);
}

ul TemporalSCCs::enumerateDec(ui t1,ui t2,ui k) {
    return runQuery(t1,t2,k,QueryMethod::EnumerateDec);
}

TemporalSCCs::SplitPlan TemporalSCCs::buildSplitPlan(const std::vector<VertexID>& vertices,ui k) {
    if (vertices.empty()) throw std::invalid_argument("Cannot select a pivot from an empty set");
    if (k < 2) throw std::invalid_argument("k must be at least 2");
    VertexID degreePivot, neighborPivot;
    auto index = buildPivotIndex(vertices,degreePivot,neighborPivot);
    const std::size_t n = vertices.size();
    const long double directCost = estimateKCombinations(n,k);
    SplitPlan result;
    ReversePivotCache reverseScores;
    const long double threshold = directCost * 1.1L;

    const std::size_t planLimit = std::min<std::size_t>(262144,std::max<std::size_t>(4096,n*4));
    const std::size_t candidateLimit = k == 3 ? 2 : (n < 128 ? 3 : 6);
    std::vector<VertexID> candidates;
    const auto addCandidate = [&](VertexID v) {
        if (candidates.size() < candidateLimit &&
            std::find(candidates.begin(),candidates.end(),v) == candidates.end())
            candidates.push_back(v);
    };
    addCandidate(degreePivot);
    addCandidate(selectPivot(vertices));
    addCandidate(neighborPivot);
    addCandidate(vertices.front());
    addCandidate(vertices[n / 2]);
    addCandidate(vertices.back());

    struct Trial {
        VertexID pivot;
        SplitResult split;
        std::vector<ui> arrival;
        long double forwardCost = 0, proxy = 0;
        std::size_t largest = 0;
        std::vector<ReverseCover> covers;
        std::size_t stored = 0;
        bool reusable = true;
    };
    std::vector<Trial> trials;
    trials.reserve(candidates.size());
    std::vector<std::size_t> ranked;
    for (VertexID candidate : candidates) {
        trials.push_back({candidate,{},{}});
        auto& trial = trials.back();
        prepareSubsetIndex(vertices);
        computeEarliestArrival(candidate,vertices,trial.arrival,trial.split,true);
        if (trial.split.U.empty()) continue;
        trial.forwardCost = scorePivotSubset(trial.split.R,k,index,trial.largest);
        trial.stored=trial.split.R.size();
        trial.reusable=trial.stored<=planLimit;

        trial.proxy = trial.forwardCost + estimateKCombinations(trial.split.U.size(),k);
        ranked.push_back(trials.size() - 1);
    }

    std::size_t selected = 0;
    long double bestCost = threshold;
    std::size_t bestLargest = n;
    bool foundSplit = false;
    if (!ranked.empty()) {
        const std::size_t representative = ranked.front();
        std::stable_sort(ranked.begin(),ranked.end(),[&](std::size_t a,std::size_t b) {
            if (trials[a].proxy != trials[b].proxy) return trials[a].proxy < trials[b].proxy;
            const auto largestA = std::max(trials[a].largest,trials[a].split.U.size());
            const auto largestB = std::max(trials[b].largest,trials[b].split.U.size());
            if (largestA != largestB) return largestA < largestB;
            return a < b;
        });
        std::vector<std::size_t> finalists{representative};
        for (std::size_t candidate : ranked)
            if (candidate != representative) finalists.push_back(candidate);
        for (std::size_t position = 0; position < finalists.size(); ++position) {
            const std::size_t candidate = finalists[position];
            const auto& trial = trials[candidate];

            if (foundSplit && (trial.forwardCost > bestCost ||
                (trial.forwardCost == bestCost && (trial.largest > bestLargest ||
                (trial.largest == bestLargest && candidate > selected)))))
                continue;
            long double cost = trial.forwardCost;
            std::size_t largest = trial.largest;
            auto Ux = trial.split.U;
            while (!Ux.empty() && cost <= bestCost) {
                const VertexID seed = selectReversePivot(vertices,Ux,reverseScores);

                auto cover = buildReverseCover(seed,vertices,trials[candidate].arrival,Ux);
                std::size_t componentLargest = 0;
                cost += scorePivotSubset(cover.first,k,index,componentLargest,bestCost - cost);
                largest = std::max(largest,componentLargest);
                auto& saved = trials[candidate];
                if (saved.reusable && cost<=bestCost) {
                    const std::size_t extra = cover.first.size()+cover.second.size();
                    if (extra>planLimit-saved.stored) {

                        saved.covers.clear();saved.stored=0;saved.reusable=false;
                    } else {
                        saved.stored+=extra;
                        saved.covers.push_back({seed,std::move(cover.first),cover.second});
                    }
                }
                Ux = std::move(cover.second);
            }
            if (Ux.empty() && cost<=bestCost && (!foundSplit || cost < bestCost ||
                (cost == bestCost && (largest < bestLargest ||
                (largest == bestLargest && candidate < selected))))) {
                if (foundSplit && selected!=candidate) {
                    trials[selected].covers.clear();trials[selected].stored=0;
                }
                foundSplit = true;
                selected = candidate;
                bestCost = cost;
                bestLargest = largest;
            }
            if (!foundSplit || selected!=candidate) {
                trials[candidate].covers.clear();trials[candidate].stored=0;
            }
        }
    }
    prepareSubsetIndex(vertices);
    if (!foundSplit || (std::isfinite(bestCost) && std::isfinite(directCost) && bestCost >= threshold))
        return result;
    auto& winner = trials[selected];
    if (winner.split.U.empty()) return result;
    result.decompose=true;
    result.pivot=winner.pivot;
    result.forward=std::move(winner.split);
    result.arrival=std::move(winner.arrival);
    if (winner.reusable) result.reverse=std::move(winner.covers);
    return result;
}

void TemporalSCCs::decompose(const std::vector<VertexID>& vertices, ui k,
    const std::vector<std::vector<VertexID>>& requiredHits) {
    if (k < 2)
        throw std::invalid_argument("k must be at least 2");
    if (vertices.size() < k)
        return;

    prepareSubsetIndex(vertices);
    auto normalized = normalizeRequiredGroups(vertices,requiredHits,k);
    if (!normalized) return;
    const auto& groups = *normalized;

    const bool directly = shouldEnumerateDirectly(vertices,k);
    if (directly) {
        enumerateOptimized(vertices,k,groups);
        return;
    }

    auto plan=buildSplitPlan(vertices,k);
    if (!plan.decompose) {
        enumerateOptimized(vertices,k,groups);
        return;
    }

    auto U=std::move(plan.forward.R);
    auto Ux=std::move(plan.forward.U);
    std::size_t cursor=0;
    ReversePivotCache reverseScores;
    auto branchGroups = groups;
    while (true) {

        auto components = findWindowStaticSCCs(U,window_,k - 1);
        for (const auto& scc : components) {
            decompose(scc,k,branchGroups);
        }
        if (Ux.empty()) break;

        prepareSubsetIndex(U);
        std::vector<VertexID> outside;
        outside.reserve(vertices.size() - U.size());
        for (VertexID v : vertices) {
            if (subsetStamp_[v] != subsetEpoch_) outside.push_back(v);
        }
        branchGroups.push_back(std::move(outside));

        std::pair<std::vector<VertexID>,std::vector<VertexID>> cover;
        if (!plan.reverse.empty()) {
            if (cursor>=plan.reverse.size())
                throw std::logic_error("Incomplete reverse-cover plan");
            auto& saved=plan.reverse[cursor++];
            cover={std::move(saved.covered),std::move(saved.remaining)};
        } else {
            const VertexID seed = selectReversePivot(vertices,Ux,reverseScores);
            cover=buildReverseCover(seed,vertices,plan.arrival,Ux);
        }
        auto& [U1,U2] = cover;
        U = std::move(U1);
        Ux = std::move(U2);
    }
    if (!plan.reverse.empty() && cursor!=plan.reverse.size())
        throw std::logic_error("Unused reverse-cover plan suffix");
}

void TemporalSCCs::enumerateWithPrefixCache(const std::vector<VertexID>& vertices,ui k,
    const std::vector<std::vector<VertexID>>& requiredGroups) {
    if (k < 2) throw std::invalid_argument("k must be at least 2");
    const std::size_t n = vertices.size();
    if (n < k) return;
    if (!window_ready_) throw std::logic_error("Call buildWindow first");

    if (k > kMaxEnumerationSize) {

        enumerateCombinations(vertices,k,requiredGroups);
        return;
    }
    prepareSubsetIndex(vertices);

    auto normalized = normalizeRequiredGroups(vertices,requiredGroups,k);

    if (!normalized) {
        return;
    }

    const auto index = buildEnumerationIndex(vertices,*normalized);
    if(n==k) {
        std::vector<ui> chosen;
        for(ui i=0;i<k;++i) chosen.push_back(i);
        if(isTemporalSCCPacked(index,chosen)) {
            ++answerCount_;

        }
        return;
    }

    std::unique_ptr<PrefixCache> reuse;
    if(k>=5) reuse=std::make_unique<PrefixCache>();
    const bool manyGroups = index.required.size() > 64;
    const std::size_t groupWords = manyGroups ? index.belongsWords.front().size() : 0;
    const ul allGroups = manyGroups ? 0 : index.required.size() == 64 ?
        std::numeric_limits<ul>::max() : (ul{1} << index.required.size()) - 1;

    std::vector<ui> chosen;
    chosen.reserve(k);
    std::array<std::vector<ul>,kMaxEnumerationSize + 1> pools;
    std::array<std::vector<ul>,kMaxEnumerationSize + 1> neighborPools;
    std::array<std::vector<ul>,kMaxEnumerationSize + 1> frontiers;
    std::array<std::vector<ul>,kMaxEnumerationSize + 1> ownershipPools;
    std::array<std::vector<const ul*>,kMaxEnumerationSize + 1> requirementPools;
    std::array<std::vector<std::vector<ul>>,kMaxEnumerationSize + 1> componentPools;
    for (auto& pool : pools) pool.resize(index.words,0);
    for (auto& pool : neighborPools) pool.resize(index.words,0);
    for (auto& frontier : frontiers) frontier.resize(index.words,0);
    std::fill(pools[0].begin(),pools[0].end(),std::numeric_limits<ul>::max());
    if (n % 64 != 0) pools[0].back() = (ul{1} << (n % 64)) - 1;
    if (manyGroups) {
        for (auto& pool : ownershipPools) pool.resize(groupWords,0);
        for (auto& pool : requirementPools) pool.reserve(index.required.size() + 2 * k);
        std::fill(ownershipPools[0].begin(),ownershipPools[0].end(),std::numeric_limits<ul>::max());
        if (index.required.size() % 64 != 0)
            ownershipPools[0].back() = (ul{1} << (index.required.size() % 64)) - 1;
    }

    const auto emitAnswer = [&]() {
        ++answerCount_;

    };

    const auto dfs = [&](auto&& self,ul hitMask,ui availableCount) -> void {
        const ui depth = static_cast<ui>(chosen.size());
        if(reuse) {reuse->labels[depth].reset();reuse->eligible[depth]=false;}

        const ui need = k - depth;

        const auto& available = pools[depth];
        const ul missing = allGroups & ~hitMask;
        if (availableCount < need) return;

        std::array<const ul*,kMaxEnumerationRequirements> smallRequirements{};
        const ul* const* required = smallRequirements.data();
        ui requiredCount = 0;
        ui ownershipCount = 0;

        bool possible;
        if (manyGroups) {
            auto& requirements = requirementPools[depth];
            possible = collectEnumerationRequirementsMultiword(index,chosen,available,ownershipPools[depth],
                need,requirements,ownershipCount);
            required = requirements.data();
            requiredCount = static_cast<ui>(requirements.size());
        } else {
            ownershipCount = static_cast<ui>(__builtin_popcountll(missing));
            possible = collectEnumerationRequirements(index,chosen,available,missing,need,smallRequirements,requiredCount);
        }
        if (!possible) return;
        if(depth>=2 && need>=2) {
            const auto cuts=buildComponentCuts(index,chosen,true);
            const ui total=cuts.incomingCount+cuts.outgoingCount;
            if(total) {
                auto& bitmaps=componentPools[depth];bitmaps.resize(total);
                auto& augmented=requirementPools[depth];
                if(!manyGroups) augmented.assign(smallRequirements.begin(),smallRequirements.begin()+requiredCount);
                for(ui cut=0;cut<total;++cut) {
                    bitmaps[cut].resize(index.words);
                    const bool incoming=cut<cuts.incomingCount;
                    const ui members=incoming?cuts.incoming[cut]:cuts.outgoing[cut-cuts.incomingCount];
                    for(ui word=0;word<index.words;++word) {
                        ui left=members;ul bits=0;
                        while(left) {const ui a=static_cast<ui>(__builtin_ctz(left));left&=left-1;
                            bits|=incoming?index.incoming[chosen[a]][word]:index.outgoing[chosen[a]][word];}
                        bitmaps[cut][word]=bits;
                    }
                    augmented.push_back(bitmaps[cut].data());
                }
                required=augmented.data();requiredCount=static_cast<ui>(augmented.size());
                if(!checkEnumerationRequirements(index,available,need,required,requiredCount)) return;
            }
        }

        const bool hasCenter = need >= 2 && !chosen.empty() &&
            countKCombinationsCapped(availableCount,need,64) >= 64 && hasEnumerationCenter(index,chosen,available);
        if (hasCenter) {

            if (manyGroups) {
                enumerateCenterCompletions(index,chosen,available,ownershipPools[depth],need);
                return;
            }
            std::vector<ui> completionVertices;
            completionVertices.reserve(availableCount);
            for (ui word = 0; word < index.words; ++word) {
                ul bits = available[word];
                while (bits != 0) {
                    const ui bit = static_cast<ui>(__builtin_ctzll(bits));
                    bits &= bits - 1;
                    completionVertices.push_back(word * 64 + bit);
                }
            }

            std::vector<ul> suffixGroups(availableCount + 1,0);
            for (ui i = availableCount; i != 0; --i)
                suffixGroups[i - 1] = suffixGroups[i] | index.belongs[completionVertices[i - 1]];

            const auto complete = [&](auto&& combine,ui start,ui slots,ul remainingGroups) -> void {
                if (availableCount - start < slots || (remainingGroups & ~suffixGroups[start]) != 0)
                    return;
                if (slots == 1) {
                    for (ui i = start; i < availableCount; ++i) {
                        const ui v = completionVertices[i];
                        if ((remainingGroups & ~index.belongs[v]) != 0)
                            continue;
                        chosen.push_back(v);
                        emitAnswer();
                        chosen.pop_back();
                    }
                    return;
                }

                const ui last = availableCount - slots;
                for (ui i = start; i <= last; ++i) {
                    if ((remainingGroups & ~suffixGroups[i]) != 0)
                        break;
                    const ui v = completionVertices[i];
                    chosen.push_back(v);
                    combine(combine,i + 1,slots - 1,remainingGroups & ~index.belongs[v]);
                    chosen.pop_back();
                }
            };
            complete(complete,0,need,missing);
            return;
        }

        if (need == 1) {
            const auto cuts=buildComponentCuts(index,chosen);
            auto& lastCandidates = neighborPools[depth];
            ui lastCount = 0;
            for (ui word = 0; word < index.words; ++word) {
                ul last = available[word];
                for (ui g = 0; g < requiredCount && last != 0; ++g)
                    last &= required[g][word];
                for(ui cut=0;cut<cuts.incomingCount && last;++cut) {
                    ui members=cuts.incoming[cut];ul covered=0;
                    while(members) {const ui a=static_cast<ui>(__builtin_ctz(members));members&=members-1;
                        covered|=index.incoming[chosen[a]][word];}
                    last&=covered;
                }
                for(ui cut=0;cut<cuts.outgoingCount && last;++cut) {
                    ui members=cuts.outgoing[cut];ul covered=0;
                    while(members) {const ui a=static_cast<ui>(__builtin_ctz(members));members&=members-1;
                        covered|=index.outgoing[chosen[a]][word];}
                    last&=covered;
                }
                ul outgoing = 0, incoming = 0;
                for (ui u : chosen) {
                    outgoing |= index.outgoing[u][word];
                    incoming |= index.incoming[u][word];
                }
                lastCandidates[word] = last & outgoing & incoming;
                lastCount += static_cast<ui>(__builtin_popcountll(lastCandidates[word]));
            }
            if (lastCount == 0) return;
            if (k == 3) {
                const ui ab=findEnumerationPair(index,chosen[0],chosen[1]);
                const ui ba=findEnumerationPair(index,chosen[1],chosen[0]);
                for(ui word=0;word<index.words;++word) {
                    ul bits=lastCandidates[word];
                    while(bits) {
                        const ui bit=static_cast<ui>(__builtin_ctzll(bits));bits&=bits-1;
                        const ui tail=word*64+bit;
                        if(!isTemporalTriple(index,chosen,ab,ba,tail)) continue;
                        chosen.push_back(tail);emitAnswer();chosen.pop_back();
                    }
                }
                return;
            }
            std::optional<PrefixLabels> prefix;
            if (k > 2 && lastCount > 1) {
                if(reuse && depth>0 && reuse->eligible[depth-1]) {
                    const ui added=chosen.back();chosen.pop_back();
                    if(!reuse->labels[depth-1]) {
                        reuse->labels[depth-1]=buildPrefixLabels(index,chosen);
                        for(ui a=0;a<depth-1;++a)for(ui b=0;b<depth-1;++b)
                            reuse->pairs[depth-1][a*8+b]=findEnumerationPair(index,chosen[a],chosen[b]);
                    }
                    prefix=extendPrefixLabels(index,chosen,*reuse->labels[depth-1],reuse->pairs[depth-1],added);
                    chosen.push_back(added);
                } else prefix = buildPrefixLabels(index,chosen);
            }
            for (ui word = 0; word < index.words; ++word) {
                ul bits = lastCandidates[word];
                while (bits != 0) {
                    const ui bit = static_cast<ui>(__builtin_ctzll(bits));
                    bits &= bits - 1;
                    const ui v = word * 64 + bit;

                    bool valid = k == 2;
                    if (prefix) {
                        valid = isTemporalExtension(index,chosen,*prefix,v);
                    }
                    chosen.push_back(v);
                    if (k > 2 && !prefix) {
                        valid = isTemporalSCCPacked(index,chosen);
                    }
                    if (valid) {
                        emitAnswer();
                    }
                    chosen.pop_back();
                }
            }
            return;
        }

        auto& neighbors = neighborPools[depth];
        neighbors = chosen.empty() ? available : frontiers[depth];
        ui neighborCount = 0;
        for (ul word : neighbors) neighborCount += static_cast<ui>(__builtin_popcountll(word));
        if (neighborCount == 0) {
            return;
        }
        const ul* tightest = nullptr;

        for (ui g = 0; g < requiredCount && neighborCount > 1; ++g) {
            ui count = 0;
            bool adjacent = true;
            for (ui word = 0; word < index.words; ++word) {
                const ul hits = available[word] & required[g][word];
                if (g < ownershipCount && (hits & ~neighbors[word]) != 0) {adjacent=false;break;}
                count += static_cast<ui>(__builtin_popcountll(hits));
                if (count >= neighborCount) break;
            }
            if (adjacent && count < neighborCount) {
                neighborCount = count;
                tightest = required[g];
            }
        }
        if (neighborCount == 0) {
            return;
        }
        if (tightest != nullptr)
            for (ui word = 0; word < index.words; ++word)
                neighbors[word] = available[word] & tightest[word];

        if(reuse && need==2 && depth>=3 && neighborCount>=4 && ownershipCount<=2) {
            reuse->eligible[depth]=true;
        }
        auto& next = pools[depth + 1];
        next = available;
        ui remaining = availableCount;
        for (ui word = 0; word < index.words; ++word) {
            ul bits = neighbors[word];
            while (bits != 0) {
                const ui bit = static_cast<ui>(__builtin_ctzll(bits));
                bits &= bits - 1;
                next[word] &= ~(ul{1} << bit);
                if (--remaining < need - 1) return;
                const ui v = word * 64 + bit;

                auto& nextFrontier = frontiers[depth + 1];
                for (ui i = 0; i < index.words; ++i)
                    nextFrontier[i] = (frontiers[depth][i] | index.outgoing[v][i] | index.incoming[v][i]) & next[i];
                if (manyGroups) {
                    auto& nextOwnership = ownershipPools[depth + 1];
                    for (std::size_t i = 0; i < groupWords; ++i)
                        nextOwnership[i] = ownershipPools[depth][i] & ~index.belongsWords[v][i];
                }
                chosen.push_back(v);
                self(self,hitMask | index.belongs[v],remaining);
                chosen.pop_back();
            }
        }
    };
    dfs(dfs,0,static_cast<ui>(n));
}

TemporalSCCs::ComponentCuts TemporalSCCs::buildComponentCuts(
    const EnumerationIndex& index,const std::vector<ui>& chosen,bool whole) const {
    ComponentCuts result;
    const ui n=static_cast<ui>(chosen.size());
    if(n<2 || (!whole && n<3)) return result;
    const ui all=(ui{1}<<n)-1;
    std::array<ui,8> adjacency{},reach{};
    for(ui i=0;i<n;++i) {
        for(ui j=0;j<n;++j)
            if(index.outgoing[chosen[i]][chosen[j]/64]&(ul{1}<<(chosen[j]%64))) adjacency[i]|=ui{1}<<j;
        reach[i]=adjacency[i]|(ui{1}<<i);
    }
    for(ui middle=0;middle<n;++middle)
        for(ui i=0;i<n;++i) if(reach[i]&(ui{1}<<middle)) reach[i]|=reach[middle];
    ui unassigned=all;
    while(unassigned) {
        const ui seed=static_cast<ui>(__builtin_ctz(unassigned));
        ui component=0;
        for(ui j=0;j<n;++j)
            if((reach[seed]&(ui{1}<<j)) && (reach[j]&(ui{1}<<seed))) component|=ui{1}<<j;
        unassigned&=~component;
        if((!whole && component==all) || (component&(component-1))==0) continue;
        bool source=true,sink=true;
        for(ui j=0;j<n;++j) {
            if(component&(ui{1}<<j)) {if(adjacency[j]&~component) sink=false;}
            else if(adjacency[j]&component) source=false;
        }
        if(source) result.incoming[result.incomingCount++]=component;
        if(sink) result.outgoing[result.outgoingCount++]=component;
    }
    return result;
}

bool TemporalSCCs::isTemporalTriple(const EnumerationIndex& index,
    const std::vector<ui>& chosen,ui ab,ui ba,ui x) const {
    constexpr ui noPair=std::numeric_limits<ui>::max();
    const ui ax=findEnumerationPair(index,chosen[0],x);
    const ui bx=findEnumerationPair(index,chosen[1],x);
    const ui xa=findEnumerationPair(index,x,chosen[0]);
    const ui xb=findEnumerationPair(index,x,chosen[1]);
    if(ab==noPair && (ax==noPair || xb==noPair ||
        index.pairTimes[ax].front()>=index.pairTimes[xb].back())) return false;
    if(ba==noPair && (bx==noPair || xa==noPair ||
        index.pairTimes[bx].front()>=index.pairTimes[xa].back())) return false;
    if(ax==noPair && (ab==noPair || bx==noPair ||
        index.pairTimes[ab].front()>=index.pairTimes[bx].back())) return false;
    if(bx==noPair && (ba==noPair || ax==noPair ||
        index.pairTimes[ba].front()>=index.pairTimes[ax].back())) return false;
    if(xa==noPair && (xb==noPair || ba==noPair ||
        index.pairTimes[xb].front()>=index.pairTimes[ba].back())) return false;
    if(xb==noPair && (xa==noPair || ab==noPair ||
        index.pairTimes[xa].front()>=index.pairTimes[ab].back())) return false;
    return true;
}

TemporalSCCs::PrefixLabels TemporalSCCs::buildPrefixLabels(
    const EnumerationIndex& index,const std::vector<ui>& chosen) const {
    const ui size=static_cast<ui>(chosen.size());
    if (size>3) return buildPrefixLabelsGeneral(index,chosen);
    constexpr ui noPair=std::numeric_limits<ui>::max();
    constexpr ul noTime=std::numeric_limits<ul>::max();
    PrefixLabels p;
    p.size=size;
    p.earliest.fill(noTime); p.latest.fill(noTime);
    p.allArrivals.fill(noTime); p.allDepartures.fill(noTime);
    std::array<ui,9> pairs;
    pairs.fill(noPair);
    for(ui a=0;a<size;++a) for(ui b=0;b<size;++b)
        if(a!=b) pairs[a*3+b]=findEnumerationPair(index,chosen[a],chosen[b]);
    p.stronglyConnected=true;
    for(ui a=0;a<size;++a) for(ui b=0;b<size;++b) {
        ul ea=noTime, ld=noTime;
        if(a==b) { ea=window_.left; ld=static_cast<ul>(window_.right)+1; }
        else {
            const ui pair=pairs[a*3+b];
            if(pair!=noPair) { ea=index.pairTimes[pair].front(); ld=index.pairTimes[pair].back(); }
            if(size==3) {
                const ui c=3-a-b;
                const ui ab=pairs[a*3+c],bc=pairs[c*3+b];
                if(ab!=noPair && bc!=noPair) {
                    const auto& first=index.pairTimes[ab];
                    const auto& second=index.pairTimes[bc];
                    if(second.back()>first.front()) {
                        const ul t=second.front()>first.front()?second.front():
                            *std::upper_bound(second.begin(),second.end(),first.front());
                        ea=std::min(ea,t);
                    }
                    if(first.front()<second.back()) {
                        const ul t=first.back()<second.back()?first.back():
                            *std::prev(std::lower_bound(first.begin(),first.end(),second.back()));
                        if(ld==noTime || t>ld) ld=t;
                    }
                }
            }
        }
        p.earliest[a*8+b]=ea; p.latest[a*8+b]=ld;
        if(ea==noTime) { p.missing[a]|=ui{1}<<b; p.stronglyConnected=false; }
    }
    if(p.stronglyConnected) for(ui a=0;a<size;++a) {
        ul ea=0,ld=noTime;
        for(ui b=0;b<size;++b) { ea=std::max(ea,p.earliest[b*8+a]); ld=std::min(ld,p.latest[a*8+b]); }
        p.allArrivals[a]=ea; p.allDepartures[a]=ld;
    }
    return p;
}

bool TemporalSCCs::isTemporalExtension(const EnumerationIndex& index,
    const std::vector<ui>& chosen,const PrefixLabels& prefix,ui vertex) const {
    const ui size=prefix.size;
    constexpr ui noPair=std::numeric_limits<ui>::max();
    constexpr ul noTime=std::numeric_limits<ul>::max();
    std::array<ui,8> incoming{},outgoing{};
    if(!prefix.stronglyConnected) {
        for(ui a=0;a<size;++a) {
            incoming[a]=findEnumerationPair(index,chosen[a],vertex);
            outgoing[a]=findEnumerationPair(index,vertex,chosen[a]);
        }
        std::array<ul,8> thresholds;
        thresholds.fill(noTime);
        for(ui u=0;u<size;++u) {
            ul earliest=noTime;
            for(ui a=0;a<size;++a) {
                const ui pair=incoming[a];
                const ul limit=prefix.earliest[u*8+a];
                if(pair==noPair || limit==noTime)continue;
                const auto& times=index.pairTimes[pair];
                if(a!=u && times.back()<=limit)continue;
                if(prefix.missing[u]==0) { earliest=0;break; }
                const ul arrival=a==u || times.front()>limit?times.front():
                    *std::upper_bound(times.begin(),times.end(),static_cast<ui>(limit));
                earliest=std::min(earliest,arrival);
            }
            if(earliest==noTime)return false;
            ui missing=prefix.missing[u];
            while(missing) {
                const ui v=static_cast<ui>(__builtin_ctz(missing));missing&=missing-1;
                thresholds[v]=thresholds[v]==noTime?earliest:std::max(thresholds[v],earliest);
            }
        }
        for(ui v=0;v<size;++v) {
            bool covered=false;
            for(ui b=0;b<size;++b) {
                const ui pair=outgoing[b];
                const ul limit=prefix.latest[b*8+v];
                if(pair==noPair || limit==noTime)continue;
                const auto& times=index.pairTimes[pair];
                if(times.front()>=limit || (thresholds[v]!=noTime && times.back()<=thresholds[v]))continue;
                if(thresholds[v]==noTime || times.front()>thresholds[v] || times.back()<limit ||
                    *std::upper_bound(times.begin(),times.end(),static_cast<ui>(thresholds[v]))<limit) {
                    covered=true;break;
                }
            }
            if(!covered)return false;
        }
        return true;
    }
    bool allIn=false,allOut=false;
    for(ui a=0;a<size;++a) {
        incoming[a]=findEnumerationPair(index,chosen[a],vertex);
        outgoing[a]=findEnumerationPair(index,vertex,chosen[a]);
        if(incoming[a]!=noPair && (size==1 || index.pairTimes[incoming[a]].back()>prefix.allArrivals[a])) allIn=true;
        if(outgoing[a]!=noPair && index.pairTimes[outgoing[a]].front()<prefix.allDepartures[a]) allOut=true;
    }
    if(allIn && allOut) return true;
    for(ui u=0;u<size && !allIn;++u) {
        bool covered=false;
        for(ui a=0;a<size;++a) if(incoming[a]!=noPair &&
            (a==u || index.pairTimes[incoming[a]].back()>prefix.earliest[u*8+a])) { covered=true; break; }
        if(!covered) return false;
    }
    for(ui v=0;v<size && !allOut;++v) {
        bool covered=false;
        for(ui b=0;b<size;++b) if(outgoing[b]!=noPair &&
            index.pairTimes[outgoing[b]].front()<prefix.latest[b*8+v]) { covered=true; break; }
        if(!covered) return false;
    }
    return true;
}

TemporalSCCs::PrefixLabels TemporalSCCs::extendPrefixLabels(
    const EnumerationIndex& index,const std::vector<ui>& chosen,
    const PrefixLabels& parent,const std::array<ui,64>& parentPairs,ui vertex) const {
    constexpr ui noPair=std::numeric_limits<ui>::max();
    constexpr ul noTime=std::numeric_limits<ul>::max();
    const ui size=parent.size;
    PrefixState previous,next;
    previous.size=size;previous.stronglyConnected=parent.stronglyConnected;
    previous.earliest=parent.earliest;previous.pairs=parentPairs;previous.reachable=0;
    for(ui u=0;u<size;++u)for(ui v=0;v<size;++v)
        if(parent.earliest[u*8+v]!=noTime)previous.reachable|=ul{1}<<(u*8+v);
    extendPrefixState(index,chosen,previous,vertex,next);
    PrefixLabels p;
    p.size=size+1;p.stronglyConnected=next.stronglyConnected;
    p.earliest=next.earliest;p.latest.fill(noTime);
    p.allArrivals.fill(noTime);p.allDepartures.fill(noTime);
    for(ui u=0;u<p.size;++u)p.missing[u]=static_cast<ui>((~(next.reachable>>(8*u)))&((ui{1}<<p.size)-1));
    const auto relax=[&](std::array<ul,8>& label) {
        if(label[size]==noTime)return;
        ui active=ui{1}<<size,settled=0;
        while(active) {
            ui v=p.size;ul time=0;ui bits=active;
            while(bits) {
                const ui u=static_cast<ui>(__builtin_ctz(bits));bits&=bits-1;
                if(v==p.size||label[u]>time){v=u;time=label[u];}
            }
            active&=~(ui{1}<<v);settled|=ui{1}<<v;
            for(ui u=0;u<p.size;++u) {
                if((settled&(ui{1}<<u)) || (label[u]!=noTime&&label[u]>=time))continue;
                const ui pair=next.pairs[u*8+v];
                if(pair==noPair)continue;
                const auto& times=index.pairTimes[pair];
                if(times.front()>=time)continue;
                const ul departure=times.back()<time?times.back():
                    *std::prev(std::lower_bound(times.begin(),times.end(),static_cast<ui>(time)));
                if(label[u]==noTime||departure>label[u]){label[u]=departure;active|=ui{1}<<u;}
            }
        }
    };
    for(ui target=0;target<p.size;++target) {
        std::array<ul,8> label;
        label.fill(noTime);
        if(target==size) {
            label[size]=static_cast<ul>(window_.right)+1;relax(label);
        } else {
            ul best=noTime,minKnown=noTime;bool unknown=false;
            for(ui u=0;u<size;++u) {
                label[u]=parent.latest[u*8+target];
                if(label[u]==noTime)unknown=true;else minKnown=std::min(minKnown,label[u]);
                const ui pair=next.pairs[size*8+u];
                if(pair==noPair||label[u]==noTime)continue;
                const auto& times=index.pairTimes[pair];
                if(times.front()>=label[u])continue;
                const ul time=times.back()<label[u]?times.back():
                    *std::prev(std::lower_bound(times.begin(),times.end(),static_cast<ui>(label[u])));
                if(best==noTime||time>best)best=time;
            }
            label[size]=best;
            if(best!=noTime&&(unknown||best>minKnown))relax(label);
        }
        for(ui u=0;u<p.size;++u)p.latest[u*8+target]=label[u];
    }
    if(p.stronglyConnected)for(ui u=0;u<p.size;++u) {
        ul ea=0,ld=noTime;
        for(ui v=0;v<p.size;++v){ea=std::max(ea,p.earliest[v*8+u]);ld=std::min(ld,p.latest[u*8+v]);}
        p.allArrivals[u]=ea;p.allDepartures[u]=ld;
    }
    return p;
}

void TemporalSCCs::enumerateTriples(const std::vector<VertexID>& vertices,ui k,
    const std::vector<std::vector<VertexID>>& requiredGroups) {
    if(k!=3) {enumerateWithPrefixCache(vertices,k,requiredGroups);return;}
    const ui n=static_cast<ui>(vertices.size());
    if(n<3)return;
    if(!window_ready_)throw std::logic_error("Call buildWindow first");
    prepareSubsetIndex(vertices);
    auto normalized=normalizeRequiredGroups(vertices,requiredGroups,k);
    if(!normalized)return;
    if(normalized->size()>64){enumerateWithPrefixCache(vertices,k,*normalized);return;}
    const auto index=buildEnumerationIndex(vertices,*normalized);
    const ui W=index.words;
    constexpr ui noPair=std::numeric_limits<ui>::max();
    const ul allGroups=index.required.size()==64?~ul{0}:(ul{1}<<index.required.size())-1;
    std::vector<ul> available(W,~ul{0}),roots,bChoices(W),secondPool(W);
    if(n%64)available.back()=(ul{1}<<(n%64))-1;
    roots=available;
    if(!index.required.empty()) {
        ui smallest=n+1;
        for(const auto& group:index.required) {
            ui count=0;for(ul word:group)count+=static_cast<ui>(__builtin_popcountll(word));
            if(count<smallest){smallest=count;roots=group;}
        }
    }
    std::vector<ui> chosen;chosen.reserve(3);
    const auto emit=[&](){
        ++answerCount_;

    };
    ui remaining=n;
    for(ui rootWord=0;rootWord<W;++rootWord) {
        ul rootBits=roots[rootWord];
        while(rootBits) {
            const ui rootBit=static_cast<ui>(__builtin_ctzll(rootBits));rootBits&=rootBits-1;
            const ui a=rootWord*64+rootBit;
            available[rootWord]&=~(ul{1}<<rootBit);
            if(--remaining<2)return;
            const ul missingA=allGroups&~index.belongs[a];
            chosen.push_back(a);
            if(countKCombinationsCapped(remaining,2,64)>=64 && hasEnumerationCenter(index,chosen,available)) {
                std::vector<ui> tails;tails.reserve(remaining);
                for(ui word=0;word<W;++word) {
                    ul bits=available[word];while(bits) {
                        const ui bit=static_cast<ui>(__builtin_ctzll(bits));bits&=bits-1;tails.push_back(word*64+bit);
                    }
                }
                for(ui i=0;i<tails.size();++i)for(ui j=i+1;j<tails.size();++j) {
                    if(missingA&~(index.belongs[tails[i]]|index.belongs[tails[j]]))continue;
                    chosen.push_back(tails[i]);chosen.push_back(tails[j]);emit();chosen.pop_back();chosen.pop_back();
                }
                chosen.pop_back();continue;
            }
            ui outCount=0,inCount=0;
            for(ui word=0;word<W;++word) {
                outCount+=static_cast<ui>(__builtin_popcountll(available[word]&index.outgoing[a][word]));
                inCount+=static_cast<ui>(__builtin_popcountll(available[word]&index.incoming[a][word]));
            }
            if(!outCount || !inCount){chosen.pop_back();continue;}
            const auto* best=outCount<=inCount?&index.outgoing[a]:&index.incoming[a];
            ui bestCount=std::min(outCount,inCount);
            ul groups=missingA;
            while(groups && bestCount>1) {
                const ui group=static_cast<ui>(__builtin_ctzll(groups));groups&=groups-1;
                ui count=0;bool adjacent=true;
                for(ui word=0;word<W;++word) {
                    const ul hits=available[word]&index.required[group][word];
                    if(hits&~(index.outgoing[a][word]|index.incoming[a][word])){adjacent=false;break;}
                    count+=static_cast<ui>(__builtin_popcountll(hits));if(count>=bestCount)break;
                }
                if(adjacent && count<bestCount){bestCount=count;best=&index.required[group];}
            }
            if(!bestCount){chosen.pop_back();continue;}
            for(ui word=0;word<W;++word)bChoices[word]=available[word]&(*best)[word];
            secondPool=available;
            ui secondRemaining=remaining;
            for(ui bWord=0;bWord<W;++bWord) {
                ul bBits=bChoices[bWord];
                while(bBits) {
                    const ui bBit=static_cast<ui>(__builtin_ctzll(bBits));bBits&=bBits-1;
                    const ui b=bWord*64+bBit;
                    secondPool[bWord]&=~(ul{1}<<bBit);
                    if(--secondRemaining<1)break;
                    const ui ab=findEnumerationPair(index,a,b),ba=findEnumerationPair(index,b,a);
                    const ul missing=missingA&~index.belongs[b];
                    chosen.push_back(b);
                    for(ui word=0;word<W;++word) {
                        ul bits=secondPool[word];
                        if(ab==noPair)bits&=index.outgoing[a][word]&index.incoming[b][word];
                        else if(ba==noPair)bits&=index.outgoing[b][word]&index.incoming[a][word];
                        else bits&=(index.outgoing[a][word]|index.outgoing[b][word])&
                            (index.incoming[a][word]|index.incoming[b][word]);
                        ul needed=missing;
                        while(needed && bits) {
                            const ui group=static_cast<ui>(__builtin_ctzll(needed));needed&=needed-1;
                            bits&=index.required[group][word];
                        }
                        while(bits) {
                            const ui bit=static_cast<ui>(__builtin_ctzll(bits));bits&=bits-1;
                            const ui c=word*64+bit;
                            if(!isTemporalTriple(index,chosen,ab,ba,c))continue;
                            chosen.push_back(c);emit();chosen.pop_back();
                        }
                    }
                    chosen.pop_back();
                }
            }
            chosen.pop_back();
        }
    }
}

void TemporalSCCs::enumerateOptimized(const std::vector<VertexID>& vertices,ui k,
    const std::vector<std::vector<VertexID>>& groups) {
    if(k==3) {enumerateTriples(vertices,k,groups);return;}
    enumerateWithPrefixCache(vertices,k,groups);
}
