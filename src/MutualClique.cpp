#include "TemporalSCCs.h"
#include "kCListClique.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace {
template<class TemporalWindow>
class InducedTemporalValidator {
    const TemporalWindow& window;
    ui size;
    std::vector<ui> selectedPosition, chosen, pending;
    std::vector<ul> arrival;
    std::vector<std::size_t> cursor;
    std::vector<unsigned char> queued;

public:
    InducedTemporalValidator(const TemporalWindow& window,ui vertexCount,ui k)
        : window(window),size(k),selectedPosition(vertexCount,k),chosen(k),pending(k),
          arrival(k),cursor(k),queued(k) {}

    bool operator()(const std::vector<ui>& localVertices,const std::vector<VertexID>& component) {
        for (ui i = 0; i < size; ++i) {

            chosen[i] = component.at(localVertices.at(i));
            selectedPosition[chosen[i]] = i;
        }
        constexpr ul infinity = std::numeric_limits<ul>::max();
        bool valid = true;
        for (ui source = 0; source < size && valid; ++source) {
            std::fill(arrival.begin(),arrival.end(),infinity);
            std::fill(queued.begin(),queued.end(),0);
            for (ui i = 0; i < size; ++i) cursor[i] = window.outgoing[chosen[i]].size();
            ui head = 0,tail = 0,pendingCount = 0,reached = 1;
            const auto enqueue = [&](ui vertex) {
                pending[tail] = vertex;
                if (++tail == size) tail = 0;
                ++pendingCount;
                queued[vertex] = 1;
            };
            arrival[source] = window.left;
            enqueue(source);
            while (pendingCount != 0 && reached < size) {
                const ui i = pending[head];
                if (++head == size) head = 0;
                --pendingCount;
                queued[i] = 0;
                const auto& edges = window.outgoing[chosen[i]];
                auto& position = cursor[i];
                while (position != 0 && reached < size) {
                    const auto& edge = edges[position - 1];

                    if (i != source && edge.second <= arrival[i]) break;
                    --position;
                    const ui j = selectedPosition[edge.first];
                    if (j == size || edge.second >= arrival[j]) continue;
                    if (arrival[j] == infinity) ++reached;
                    arrival[j] = edge.second;
                    if (!queued[j]) enqueue(j);
                }
            }
            valid = reached == size;
        }
        for (ui vertex : chosen) selectedPosition[vertex] = size;
        return valid;
    }
};

template<class Validator>
ul enumerateMutualCliques(std::vector<std::vector<ui>> mutualGraph,ui k,Validator&& isAnswer) {
    Graph cliqueGraph;
    cliqueGraph.n = static_cast<ui>(mutualGraph.size());
    ul arcCount = 0;
    for (const auto& row : mutualGraph) {
        arcCount += row.size();
        cliqueGraph.maxD = std::max(cliqueGraph.maxD,static_cast<ui>(row.size()));
    }
    if (arcCount > std::numeric_limits<ui>::max())
        throw std::overflow_error("Mutual graph exceeds CCR's 32-bit CSR edge limit");
    cliqueGraph.m = static_cast<ui>(arcCount);
    cliqueGraph.pIdx.reserve(mutualGraph.size() + 1);
    cliqueGraph.pEdge.reserve(cliqueGraph.m);
    cliqueGraph.pIdx.push_back(0);
    for (const auto& row : mutualGraph) {
        cliqueGraph.pEdge.insert(cliqueGraph.pEdge.end(),row.begin(),row.end());
        cliqueGraph.pIdx.push_back(static_cast<ui>(cliqueGraph.pEdge.size()));
    }

    std::vector<std::vector<ui>>().swap(mutualGraph);
    cliqueGraph.changeToCoreOrder();

    ul answers = 0;
    if (cliqueGraph.n >= k && cliqueGraph.coreNumber >= k - 1) {
        kclistClique enumerator(std::move(cliqueGraph),k,false);

        enumerator.setCliqueCallback([&](const std::vector<ui>& vertices) {
            if (isAnswer(vertices)) {
                if (answers == std::numeric_limits<ul>::max())
                    throw std::overflow_error("Answer count exceeds uint64_t");
                ++answers;
            }
        });
        enumerator.run_v6();
    }
    return answers;
}
}

ul TemporalSCCs::enumerateRG(ui t1,ui t2,ui k) {
    const auto components = prepareQuery(t1,t2,k);
    InducedTemporalValidator validator(window_,vertex_count_,k);
    for (const auto& component : components) {
        auto mutualGraph = buildMutualReachabilityGraph(component);
        const ul count = enumerateMutualCliques(std::move(mutualGraph),k,
            [&](const std::vector<ui>& vertices) { return validator(vertices,component); });
        if (count > std::numeric_limits<ul>::max() - answerCount_)
            throw std::overflow_error("Answer count exceeds uint64_t");
        answerCount_ += count;
    }
    return answerCount_;
}
