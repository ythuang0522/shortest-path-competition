// Reference solver 1 of 3: bidirectional Dijkstra on a CSR graph.
//
// The "did the obvious engineering" baseline: flat adjacency, a reused
// distance array with a generation counter instead of a per-query clear, and
// a search from both ends. No preprocessing at all.

#include "common.hpp"

struct BiDijkstra {
    Dist df, db;
    Heap hf, hb;

    void prepare(const Graph& g) {
        df.init(g.V);
        db.init(g.V);
    }

    int64_t query(const Graph& g, int32_t s, int32_t t) {
        df.begin(); db.begin();
        hf.clear(); hb.clear();
        df.set(s, 0); hf.push(0, s);
        db.set(t, 0); hb.push(0, t);

        int64_t mu = INF;
        while (!hf.empty() && !hb.empty()) {
            // The classic stopping rule for plain bidirectional Dijkstra: once
            // the two frontiers together already cost as much as the best
            // path found, nothing unexplored can beat it.
            if (hf.top_key() + hb.top_key() >= mu) break;

            // Alternate by frontier size rather than by turn. On a graph with
            // a heavy-tailed in-degree, one backward step from a hub can
            // expand tens of thousands of nodes, and strict round-robin walks
            // straight into it.
            bool forward = hf.h.size() <= hb.h.size();
            if (forward) {
                auto [d, u] = hf.pop();
                if (d != df.at(u)) continue;
                for (int64_t j = g.foff[u]; j < g.foff[u + 1]; ++j) {
                    int32_t v = g.fto[j];
                    int64_t nd = d + g.fw[j];
                    if (nd < df.at(v)) {
                        df.set(v, nd);
                        hf.push(nd, v);
                        if (db.seen(v)) mu = std::min(mu, nd + db.d[v]);
                    }
                }
            } else {
                auto [d, u] = hb.pop();
                if (d != db.at(u)) continue;
                for (int64_t j = g.roff[u]; j < g.roff[u + 1]; ++j) {
                    int32_t v = g.rto[j];
                    int64_t nd = d + g.rw[j];
                    if (nd < db.at(v)) {
                        db.set(v, nd);
                        hb.push(nd, v);
                        if (df.seen(v)) mu = std::min(mu, nd + df.d[v]);
                    }
                }
            }
        }
        return mu >= INF ? -1 : mu;
    }
};

int main(int argc, char** argv) { return run_main<BiDijkstra>(argc, argv); }
