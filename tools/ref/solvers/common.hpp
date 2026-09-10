// Shared scaffolding for the reference solvers.
//
// These three solvers stand in for student submissions, so they obey the
// competition rules: C++17, standard library only, single-threaded. They exist
// to answer one question -- does the dataset actually separate different
// levels of solution? -- so they deliberately share this I/O and heap layer.
// Holding the engineering constant means any spread in the results comes from
// the algorithm rather than from who wrote a better parser.

#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using std::vector;

static const int64_t INF = std::numeric_limits<int64_t>::max() / 4;
static const int32_t FLAG_COORDS = 1;
static const int32_t FLAG_DIRECTED = 2;

// ------------------------------------------------------------------ I/O ----

struct Reader {
    vector<char> buf;
    size_t p = 0;

    explicit Reader(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) { std::fprintf(stderr, "cannot open %s: %s\n", path, std::strerror(errno)); std::exit(1); }
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        buf.resize((size_t)n + 1);
        if (n > 0 && std::fread(buf.data(), 1, (size_t)n, f) != (size_t)n) {
            std::fprintf(stderr, "short read on %s\n", path); std::exit(1);
        }
        buf[(size_t)n] = '\0';
        std::fclose(f);
    }

    int64_t integer() {
        while (p < buf.size() && (unsigned char)buf[p] <= ' ') ++p;
        bool neg = false;
        if (buf[p] == '-') { neg = true; ++p; }
        int64_t v = 0;
        while (p < buf.size() && buf[p] >= '0' && buf[p] <= '9')
            v = v * 10 + (buf[p++] - '0');
        return neg ? -v : v;
    }
};

struct Graph {
    int32_t V = 0, E = 0, flags = 0;
    bool directed = false, has_coords = false;

    vector<int64_t> foff, roff;    // CSR, forward and reverse
    vector<int32_t> fto, rto;
    vector<int32_t> fw, rw;
    vector<int64_t> cx, cy;

    void load(const char* path) {
        Reader r(path);
        V = (int32_t)r.integer();
        E = (int32_t)r.integer();
        flags = (int32_t)r.integer();
        directed = (flags & FLAG_DIRECTED) != 0;
        has_coords = (flags & FLAG_COORDS) != 0;

        vector<int32_t> eu((size_t)E), ev((size_t)E), ew((size_t)E);
        vector<int64_t> fdeg((size_t)V + 1, 0), rdeg((size_t)V + 1, 0);
        for (int32_t i = 0; i < E; ++i) {
            eu[i] = (int32_t)r.integer();
            ev[i] = (int32_t)r.integer();
            ew[i] = (int32_t)r.integer();
            fdeg[eu[i]]++; rdeg[ev[i]]++;
            if (!directed) { fdeg[ev[i]]++; rdeg[eu[i]]++; }
        }
        auto build = [&](vector<int64_t>& off, vector<int32_t>& to,
                         vector<int32_t>& w, const vector<int64_t>& deg,
                         bool reverse) {
            off.assign((size_t)V + 1, 0);
            int64_t s = 0;
            for (int32_t i = 0; i < V; ++i) { off[i] = s; s += deg[i]; }
            off[V] = s;
            to.resize((size_t)s); w.resize((size_t)s);
            vector<int64_t> pos(off.begin(), off.end() - 1);
            for (int32_t i = 0; i < E; ++i) {
                int32_t a = reverse ? ev[i] : eu[i], b = reverse ? eu[i] : ev[i];
                to[pos[a]] = b; w[pos[a]] = ew[i]; pos[a]++;
                if (!directed) { to[pos[b]] = a; w[pos[b]] = ew[i]; pos[b]++; }
            }
        };
        build(foff, fto, fw, fdeg, false);
        build(roff, rto, rw, rdeg, true);

        if (has_coords) {
            cx.resize((size_t)V); cy.resize((size_t)V);
            for (int32_t i = 0; i < V; ++i) { cx[i] = r.integer(); cy[i] = r.integer(); }
        }
    }
};

// A distance array with a generation counter instead of a per-query clear.
struct Dist {
    vector<int64_t> d;
    vector<uint32_t> stamp;
    uint32_t gen = 0;

    void init(int32_t V) { d.assign((size_t)V, 0); stamp.assign((size_t)V, 0); gen = 0; }
    void begin() { if (++gen == 0) { std::fill(stamp.begin(), stamp.end(), 0); gen = 1; } }
    inline bool seen(int32_t v) const { return stamp[v] == gen; }
    inline int64_t at(int32_t v) const { return stamp[v] == gen ? d[v] : INF; }
    inline void set(int32_t v, int64_t x) { stamp[v] = gen; d[v] = x; }
};

// Binary min-heap over (key, node). Deliberately plain -- see the header note.
struct Heap {
    vector<std::pair<int64_t, int32_t>> h;
    struct Gt {
        bool operator()(const std::pair<int64_t, int32_t>& a,
                        const std::pair<int64_t, int32_t>& b) const {
            return a.first > b.first;
        }
    };
    void clear() { h.clear(); }
    bool empty() const { return h.empty(); }
    int64_t top_key() const { return h.front().first; }
    void push(int64_t k, int32_t v) { h.push_back({k, v}); std::push_heap(h.begin(), h.end(), Gt{}); }
    std::pair<int64_t, int32_t> pop() {
        std::pop_heap(h.begin(), h.end(), Gt{});
        auto x = h.back();
        h.pop_back();
        return x;
    }
};

// ----------------------------------------------------------------- driver ----

// Every solver is the same program modulo one function, so the CLI, the query
// loop and the output formatting live here.
template <class Solver>
int run_main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <graph_file> <query_file> <output_file>\n", argv[0]);
        return 1;
    }
    Graph g;
    g.load(argv[1]);

    Solver solver;
    solver.prepare(g);

    Reader q(argv[2]);
    int64_t Q = q.integer();

    std::string out;
    out.reserve((size_t)Q * 10);
    char tmp[32];
    for (int64_t i = 0; i < Q; ++i) {
        int32_t s = (int32_t)q.integer(), t = (int32_t)q.integer();
        int64_t d = (s == t) ? 0 : solver.query(g, s, t);
        int n = std::snprintf(tmp, sizeof tmp, "%lld\n", (long long)d);
        out.append(tmp, (size_t)n);
    }

    std::FILE* of = std::fopen(argv[3], "w");
    if (!of) { std::fprintf(stderr, "cannot open output file: %s\n", argv[3]); return 1; }
    std::fwrite(out.data(), 1, out.size(), of);
    std::fclose(of);
    return 0;
}
