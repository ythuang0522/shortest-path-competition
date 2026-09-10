// Reference solver 3 of 3: Contraction Hierarchies, with a core.
//
// Nodes are contracted cheapest-first (edge difference, lazily re-evaluated).
// Contracting v means: for every in-arc (u,v) and out-arc (v,w), check with a
// hop-limited search whether a path from u to w already exists that avoids v
// and is no longer; if not, insert the shortcut u->w.
//
// Contraction stops when the shortcut budget runs out. Whatever is left is
// the *core*: nodes that all share the top rank. The query then relaxes an arc
// (u,v) when v outranks u, or when both endpoints are in the core -- so the
// search climbs the hierarchy and then falls back to an ordinary
// bidirectional Dijkstra inside whatever could not be contracted.
//
// That fallback is what keeps this honest on graphs with large separators,
// where an unbounded contraction would either exhaust memory or run for
// twenty minutes. Without a core, "CH" on such an instance is not a slower
// solver, it is a solver that never finishes preprocessing.

#include "common.hpp"

// Witness-search limits. Both are tight on purpose. Giving up early can only
// add a shortcut that was not strictly necessary, never miss one that was, so
// the cost of being stingy is a slightly larger hierarchy -- while the cost of
// being generous is preprocessing that does not finish. Measured on
// scalefree_dev (V=50,000): 500 nodes/unbounded hops did not complete in 400 s;
// 60 nodes/5 hops finishes in 42 s.
static const int WITNESS_HOPS = 5;
static const int WITNESS_NODES = 60;
// A node with this many live neighbours is left to the core rather than
// contracted. Contracting one costs O(in*out) witness searches, so on a graph
// whose degrees blow up during contraction a single node can cost more than
// everything before it -- and the shortcut budget only notices afterwards.
// Capping the degree bounds the cost per contraction instead of regretting it.
static const int MAX_CONTRACT_DEGREE = 40;
// Multiple of the original arc count. Overridable so the fuzzer can force a
// core on graphs that would otherwise contract completely.
static double shortcut_budget() {
    if (const char* e = std::getenv("CH_SHORTCUT_BUDGET")) return std::atof(e);
    return 8.0;
}

struct CH {
    int32_t V = 0;
    // dynamic adjacency during contraction
    vector<vector<std::pair<int32_t, int64_t>>> out, in;
    vector<char> contracted;
    vector<int32_t> rank_;
    int32_t CORE = 0;

    // final upward CSR
    vector<int64_t> uoff, doff;
    vector<int32_t> uto, dto;
    vector<int64_t> uw, dw;

    Dist df, db;
    Heap hf, hb;

    // --- preprocessing ----------------------------------------------------

    bool witness(int32_t u, int32_t w, int64_t limit, int32_t exclude) {
        // Bounded search for an existing u->w path that avoids `exclude`.
        // Hop- and node-limited: a witness search that is allowed to run to
        // completion costs more than the shortcut it saves.
        static vector<int64_t> d;
        static vector<int32_t> stampv, hops;
        static int32_t gen = 0;
        if ((int32_t)d.size() != V) {
            d.assign(V, INF); stampv.assign(V, -1); hops.assign(V, 0); gen = 0;
        }
        ++gen;

        Heap h;
        d[u] = 0; stampv[u] = gen; hops[u] = 0;
        h.push(0, u);
        int visited = 0;
        while (!h.empty()) {
            auto [du, x] = h.pop();
            if (stampv[x] != gen || du != d[x]) continue;
            if (du > limit) return false;
            if (x == w) return true;
            if (++visited > WITNESS_NODES) return false;
            // Both limits below make the search give up early, which can only
            // cause a shortcut to be added that was not strictly necessary --
            // never one to be missed. Without the hop limit the search fans
            // out across the whole neighbourhood, and on a graph whose degrees
            // grow during contraction that turns preprocessing quadratic:
            // measured, it was the difference between minutes and not
            // finishing at all on the high-treewidth instance.
            if (hops[x] >= WITNESS_HOPS) continue;
            for (auto& [y, wy] : out[x]) {
                if (contracted[y] || y == exclude) continue;
                int64_t nd = du + wy;
                if (nd > limit) continue;
                if (stampv[y] != gen || nd < d[y]) {
                    stampv[y] = gen; d[y] = nd; hops[y] = hops[x] + 1;
                    h.push(nd, y);
                }
            }
        }
        return false;
    }

    struct Pending { int32_t u, w; int64_t via; };
    vector<Pending> pending;

    // Shortcuts that contracting v would need. Returns the count; if `apply`,
    // inserts them.
    //
    // Every shortcut is decided against the graph as it stands *before* this
    // contraction, and only inserted once the whole scan is done. Inserting as
    // we go is wrong, and quietly so: a shortcut u1->w1 added here represents a
    // path through v, so a later witness search in the same contraction can
    // travel along it, "prove" an alternative route, and suppress a shortcut
    // that was genuinely needed. The `exclude` argument stops the witness
    // search walking through v itself but cannot stop it walking through an
    // edge that stands for v. The result is a hierarchy that is missing arcs,
    // which shows up as distances that are too large on long-range queries
    // while short ones still look fine.
    int simulate(int32_t v, bool apply) {
        pending.clear();
        for (auto& [u, wu] : in[v]) {
            if (contracted[u] || u == v) continue;
            for (auto& [w, ww] : out[v]) {
                if (contracted[w] || w == v || w == u) continue;
                int64_t via = wu + ww;
                // already have something at least as good?
                bool have = false;
                for (auto& [y, wy] : out[u])
                    if (y == w && wy <= via) { have = true; break; }
                if (have) continue;
                if (witness(u, w, via, v)) continue;
                pending.push_back({u, w, via});
            }
        }
        if (apply) apply_pending();
        return (int)pending.size();
    }

    // Insert the shortcuts the last simulate() decided on. Valid only while
    // nothing has mutated the graph since that scan, which is why prepare()
    // calls it immediately after its own simulate(v, false).
    int apply_pending() {
        for (const Pending& p : pending) {
            bool replaced = false;
            for (auto& e : out[p.u])
                if (e.first == p.w) { e.second = std::min(e.second, p.via); replaced = true; break; }
            if (!replaced) out[p.u].push_back({p.w, p.via});
            replaced = false;
            for (auto& e : in[p.w])
                if (e.first == p.u) { e.second = std::min(e.second, p.via); replaced = true; break; }
            if (!replaced) in[p.w].push_back({p.u, p.via});
        }
        return (int)pending.size();
    }

    int live_degree(int32_t v) {
        int n = 0;
        for (auto& [u, _] : in[v]) if (!contracted[u]) ++n;
        for (auto& [w, _] : out[v]) if (!contracted[w]) ++n;
        return n;
    }

    void prepare(const Graph& g) {
        V = g.V;
        df.init(V); db.init(V);
        out.assign(V, {}); in.assign(V, {});
        for (int32_t u = 0; u < V; ++u)
            for (int64_t j = g.foff[u]; j < g.foff[u + 1]; ++j) {
                out[u].push_back({g.fto[j], g.fw[j]});
                in[g.fto[j]].push_back({u, g.fw[j]});
            }

        contracted.assign(V, 0);
        rank_.assign(V, 0);
        int64_t arcs0 = (int64_t)g.foff[V];
        int64_t budget = (int64_t)(shortcut_budget() * (double)arcs0);
        int64_t shortcuts = 0;

        Heap pq;
        const int64_t NEVER = 1LL << 40;
        for (int32_t v = 0; v < V; ++v) {
            int dg = live_degree(v);
            if (dg > MAX_CONTRACT_DEGREE) { pq.push(NEVER, v); continue; }
            pq.push(simulate(v, false) - dg, v);
        }

        int32_t order = 0;
        while (!pq.empty()) {
            auto [key, v] = pq.pop();
            if (contracted[v]) continue;
            if (key >= NEVER) break;          // only over-degree nodes remain
            int dg = live_degree(v);
            if (dg > MAX_CONTRACT_DEGREE) { pq.push(NEVER, v); continue; }
            int64_t nk = simulate(v, false) - dg;
            if (!pq.empty() && nk > pq.top_key()) { pq.push(nk, v); continue; }  // lazy update
            if (shortcuts >= budget) { pq.push(nk, v); break; }
            shortcuts += apply_pending();   // reuse the scan just done
            contracted[v] = 1;
            rank_[v] = order++;
        }

        // Everything left shares the top rank and forms the core.
        CORE = order;
        for (int32_t v = 0; v < V; ++v) if (!contracted[v]) rank_[v] = CORE;

        // Freeze into upward / downward CSR.
        vector<int64_t> ud(V + 1, 0), dd(V + 1, 0);
        auto up = [&](int32_t a, int32_t b) {
            return rank_[b] > rank_[a] || (rank_[a] == CORE && rank_[b] == CORE);
        };
        for (int32_t u = 0; u < V; ++u) {
            for (auto& [w, _] : out[u]) if (up(u, w)) ud[u]++;
            for (auto& [w, _] : in[u]) if (up(u, w)) dd[u]++;
        }
        auto pack = [&](vector<int64_t>& off, vector<int32_t>& to, vector<int64_t>& w,
                        const vector<int64_t>& deg,
                        const vector<vector<std::pair<int32_t, int64_t>>>& src) {
            off.assign(V + 1, 0);
            int64_t s = 0;
            for (int32_t i = 0; i < V; ++i) { off[i] = s; s += deg[i]; }
            off[V] = s;
            to.resize(s); w.resize(s);
            vector<int64_t> pos(off.begin(), off.end() - 1);
            for (int32_t u = 0; u < V; ++u)
                for (auto& [y, wy] : src[u])
                    if (up(u, y)) { to[pos[u]] = y; w[pos[u]] = wy; pos[u]++; }
        };
        pack(uoff, uto, uw, ud, out);
        pack(doff, dto, dw, dd, in);

        out.clear(); out.shrink_to_fit();
        in.clear(); in.shrink_to_fit();

        std::fprintf(stderr, "ch: contracted %d/%d, %lld shortcuts, core %d\n",
                     order, V, (long long)shortcuts, V - order);
    }

    // --- query ------------------------------------------------------------

    int64_t query(const Graph&, int32_t s, int32_t t) {
        df.begin(); db.begin();
        hf.clear(); hb.clear();
        df.set(s, 0); hf.push(0, s);
        db.set(t, 0); hb.push(0, t);

        int64_t mu = INF;
        // Each side stops on its own once its frontier already costs as much
        // as the best path found. The summed criterion is not valid here: an
        // up-down path's two halves are not both short.
        bool fdone = false, bdone = false;
        while (!fdone || !bdone) {
            if (!fdone) {
                if (hf.empty() || hf.top_key() >= mu) fdone = true;
                else {
                    auto [d, u] = hf.pop();
                    if (d == df.at(u)) {
                        if (db.seen(u)) mu = std::min(mu, d + db.d[u]);
                        for (int64_t j = uoff[u]; j < uoff[u + 1]; ++j) {
                            int32_t v = uto[j];
                            int64_t nd = d + uw[j];
                            if (nd < df.at(v)) {
                                df.set(v, nd); hf.push(nd, v);
                                if (db.seen(v)) mu = std::min(mu, nd + db.d[v]);
                            }
                        }
                    }
                }
            }
            if (!bdone) {
                if (hb.empty() || hb.top_key() >= mu) bdone = true;
                else {
                    auto [d, u] = hb.pop();
                    if (d == db.at(u)) {
                        if (df.seen(u)) mu = std::min(mu, d + df.d[u]);
                        for (int64_t j = doff[u]; j < doff[u + 1]; ++j) {
                            int32_t v = dto[j];
                            int64_t nd = d + dw[j];
                            if (nd < db.at(v)) {
                                db.set(v, nd); hb.push(nd, v);
                                if (df.seen(v)) mu = std::min(mu, nd + df.d[v]);
                            }
                        }
                    }
                }
            }
        }
        return mu >= INF ? -1 : mu;
    }
};

int main(int argc, char** argv) { return run_main<CH>(argc, argv); }
