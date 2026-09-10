// Reference solver 2 of 3: ALT (A* + Landmarks + Triangle inequality).
//
// Cheap preprocessing -- 2 * L full single-source runs -- then unidirectional
// A* guided by landmark distances. The bound comes from the triangle
// inequality rather than from the coordinate embedding, so this works on the
// instances that carry no coordinates too.
//
// For a directed graph both directions are needed per landmark:
//   to_L[i][v]   = d(v -> L_i)   (single-source on the reverse graph)
//   from_L[i][v] = d(L_i -> v)   (single-source on the forward graph)
// and for any u, t:
//   d(u,t) >= to_L[i][u]   - to_L[i][t]
//   d(u,t) >= from_L[i][t] - from_L[i][u]

#include "common.hpp"

static const int LANDMARKS = 16;

struct ALT {
    int L = 0;
    vector<vector<int64_t>> to_L, from_L;
    Dist dist;
    Heap heap;

    static void sssp(const Graph& g, int32_t src, bool reverse,
                     vector<int64_t>& out) {
        const vector<int64_t>& off = reverse ? g.roff : g.foff;
        const vector<int32_t>& to = reverse ? g.rto : g.fto;
        const vector<int32_t>& w = reverse ? g.rw : g.fw;
        out.assign((size_t)g.V, INF);
        Heap h;
        out[src] = 0;
        h.push(0, src);
        while (!h.empty()) {
            auto [d, u] = h.pop();
            if (d != out[u]) continue;
            for (int64_t j = off[u]; j < off[u + 1]; ++j) {
                int32_t v = to[j];
                int64_t nd = d + w[j];
                if (nd < out[v]) { out[v] = nd; h.push(nd, v); }
            }
        }
    }

    void prepare(const Graph& g) {
        dist.init(g.V);
        L = std::min(LANDMARKS, g.V);
        to_L.resize(L);
        from_L.resize(L);

        // Farthest-point landmark selection: each new landmark is the node
        // furthest from the ones already chosen. Cheap, and much better than
        // picking at random.
        vector<int64_t> mind((size_t)g.V, INF), tmp;
        int32_t cur = 0;
        for (int i = 0; i < L; ++i) {
            sssp(g, cur, true, to_L[i]);     // distances INTO the landmark
            sssp(g, cur, false, from_L[i]);  // distances OUT of it
            int32_t best = 0;
            int64_t bestd = -1;
            for (int32_t v = 0; v < g.V; ++v) {
                int64_t reach = from_L[i][v];
                if (reach < INF) mind[v] = std::min(mind[v], reach);
                int64_t m = (mind[v] >= INF) ? -1 : mind[v];
                if (m > bestd) { bestd = m; best = v; }
            }
            cur = best;
        }
    }

    inline int64_t potential(int32_t u, int32_t t) const {
        int64_t best = 0;
        for (int i = 0; i < L; ++i) {
            const int64_t tu = to_L[i][u], tt = to_L[i][t];
            if (tu < INF && tt < INF) best = std::max(best, tu - tt);
            const int64_t fu = from_L[i][u], ft = from_L[i][t];
            if (fu < INF && ft < INF) best = std::max(best, ft - fu);
        }
        return best;
    }

    int64_t query(const Graph& g, int32_t s, int32_t t) {
        dist.begin();
        heap.clear();
        dist.set(s, 0);
        heap.push(potential(s, t), s);

        while (!heap.empty()) {
            auto [f, u] = heap.pop();
            int64_t d = dist.at(u);
            if (d >= INF || f != d + potential(u, t)) continue;   // stale
            if (u == t) return d;
            for (int64_t j = g.foff[u]; j < g.foff[u + 1]; ++j) {
                int32_t v = g.fto[j];
                int64_t nd = d + g.fw[j];
                if (nd < dist.at(v)) {
                    dist.set(v, nd);
                    heap.push(nd + potential(v, t), v);
                }
            }
        }
        return -1;
    }
};

int main(int argc, char** argv) { return run_main<ALT>(argc, argv); }
