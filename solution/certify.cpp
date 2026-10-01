// Certifies the lowest penalty of a survey. Written from the survey README alone, apart from
// planner.cpp and sharing no code or method with it (that is a forward pass over whole masts; this
// is a bounded search over single radios, rebuilt once for every suffix of the line).
//
//     g++ -O2 -std=c++17 -o certify certify.cpp && ./certify SURVEY_DIR     prints {"optimum": N}
//
// Radios are taken one at a time in line order (km, then mast, then radio id). S(k) is the lowest
// penalty of radios k..n-1 alone: legal channels, and only the pairs and mast rules among them.
// S(n) = 0, and S(k) >= S(k+1) because dropping radio k from a plan of k..n-1 leaves a legal plan
// of k+1..n-1 that costs no more. S(k) comes from an exact search over radios k..n-1 whose state is
// the channels of the placed radios that still have a pair or a mast mate to come. After placing
// radio j a state of cost g is dropped only when g + X + S(j+1) > ceiling, where X sums, over the
// radios still to come, the cheapest legal channel each has against the placed ones. Every
// completion pays at least X on the pairs into the placed radios and at least S(j+1) among the rest,
// so no plan at or under the ceiling is ever dropped. The ceiling starts at S(k+1) + 3 and doubles
// its margin until a plan survives to the end; the cheapest survivor is then S(k) exactly.
// S(0) is the survey's optimum.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

struct Radio { long id, site; double km; };

static std::vector<std::vector<std::string>> rows(const std::string& path) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot read %s\n", path.c_str()); exit(1); }
    std::vector<std::vector<std::string>> out;
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) cells.push_back(c);
        out.push_back(cells);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: certify SURVEY_DIR\n"); return 2; }
    std::string d = argv[1];
    int C;
    {
        std::ifstream f(d + "/licence.txt");
        std::string w;
        f >> w >> C;
        if (w != "channels" || C < 1 || C > 8) { fprintf(stderr, "bad licence\n"); return 1; }
    }
    std::vector<Radio> rs;
    for (auto& r : rows(d + "/radios.csv")) rs.push_back({std::stol(r[0]), std::stol(r[1]), std::stod(r[2])});
    std::sort(rs.begin(), rs.end(), [](const Radio& a, const Radio& b) {
        if (a.km != b.km) return a.km < b.km;
        if (a.site != b.site) return a.site < b.site;
        return a.id < b.id;
    });
    const int n = rs.size();
    std::map<long, int> at;
    for (int i = 0; i < n; i++) at[rs[i].id] = i;

    std::vector<int> legal(n, (1 << C) - 1);  // bit c set: channel c may be used
    for (auto& r : rows(d + "/barred.csv")) legal[at.at(std::stol(r[0]))] &= ~(1 << std::stoi(r[1]));

    // cost[i][j] for i < j: penalty by channel distance (0 = same, 1 = adjacent); mate[i][j]: same mast
    std::map<std::pair<int, int>, std::pair<long, long>> pen;
    for (auto& r : rows(d + "/pairs.csv")) {
        int a = at.at(std::stol(r[0])), b = at.at(std::stol(r[1]));
        if (a > b) std::swap(a, b);
        auto& p = pen[{a, b}];
        p.first += std::stol(r[2]);
        p.second += std::stol(r[3]);
    }
    std::vector<std::vector<std::pair<int, std::pair<long, long>>>> back(n);  // earlier pair partners of j
    std::vector<std::vector<int>> mates_back(n);                              // earlier radios on j's mast
    std::vector<int> last(n);  // last radio index that touches i (pair or mast mate), or i itself
    for (int i = 0; i < n; i++) last[i] = i;
    long total = 0;  // every penalty at once: no plan costs more
    for (auto& [ab, p] : pen) {
        total += p.first + p.second;
        back[ab.second].push_back({ab.first, p});
        last[ab.first] = std::max(last[ab.first], ab.second);
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (rs[j].site == rs[i].site) { mates_back[j].push_back(i); last[i] = std::max(last[i], j); }

    // open[j]: radios placed before j that still have something at j or later, in index order
    std::vector<std::vector<int>> open(n + 1);
    for (int j = 0; j <= n; j++)
        for (int i = 0; i < j; i++)
            if (last[i] >= j) open[j].push_back(i);
    for (auto& o : open)
        if (o.size() > 21) { fprintf(stderr, "frontier of %zu radios does not fit a 64-bit key\n", o.size()); return 1; }

    const long INF = 1L << 60;
    // reach_end[j]: the last radio that has a pair or a mast mate before j
    std::vector<int> reach_end(n + 1, -1);
    for (int m = 0; m < n; m++) {
        int lo = m;
        for (auto& bp : back[m]) lo = std::min(lo, bp.first);
        for (int q : mates_back[m]) lo = std::min(lo, q);
        for (int j = lo + 1; j <= m; j++) reach_end[j] = std::max(reach_end[j], m);
    }
    std::vector<long> S(n + 1, 0);
    long work = 0;
    size_t peak = 0;
    for (int k = n - 1; k >= 0; k--) {
        long t = 3;
        for (;;) {
            long ceil = S[k + 1] + t;
            // states: key holds 3 bits per open radio >= k, in the order of open[j] filtered to >= k
            std::vector<std::pair<uint64_t, long>> cur{{0, 0}}, nxt;
            std::vector<int> pos(n, -1);
            std::vector<int> ch(n);
            for (int j = k; j < n && !cur.empty(); j++) {
                std::vector<int> in, out;
                for (int i : open[j]) if (i >= k) in.push_back(i);
                for (int i : open[j + 1]) if (i >= k) out.push_back(i);
                // radios still to come with a pair or a mast mate among the radios placed so far (k..j),
                // each with those links
                struct Ahead { int legal; std::vector<int> mates; std::vector<std::pair<int, std::pair<long, long>>> pairs; };
                std::vector<Ahead> ahead;
                for (int m = j + 1; m < n && m <= reach_end[j + 1]; m++) {
                    Ahead a{legal[m], {}, {}};
                    for (auto& bp : back[m]) if (bp.first >= k && bp.first <= j) a.pairs.push_back(bp);
                    for (int q : mates_back[m]) if (q >= k && q <= j) a.mates.push_back(q);
                    if (!a.pairs.empty() || !a.mates.empty()) ahead.push_back(a);
                }
                nxt.clear();
                for (auto& [key, g] : cur) {
                    for (size_t s = 0; s < in.size(); s++) ch[in[s]] = (key >> (3 * s)) & 7;
                    for (int c = 0; c < C; c++) {
                        if (!(legal[j] >> c & 1)) continue;
                        bool ok = true;
                        for (int m : mates_back[j]) if (m >= k && std::abs(ch[m] - c) < 2) { ok = false; break; }
                        if (!ok) continue;
                        long h = g;
                        for (auto& [i, p] : back[j]) {
                            if (i < k) continue;
                            int dd = std::abs(ch[i] - c);
                            h += dd == 0 ? p.first : (dd == 1 ? p.second : 0);
                        }
                        if (h + S[j + 1] > ceil) continue;
                        ch[j] = c;
                        // each radio to come pays at least its cheapest channel against the radios placed
                        long cross = 0;
                        for (auto& a : ahead) {
                            long x[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // channel e is x[e + 1]
                            for (auto& [i, p] : a.pairs) {
                                x[ch[i] + 1] += p.first;
                                x[ch[i]] += p.second;
                                x[ch[i] + 2] += p.second;
                            }
                            int ok = a.legal;
                            for (int q : a.mates) ok &= ~(7 << ch[q] >> 1);  // ch[q]-1 .. ch[q]+1
                            long best = INF;
                            for (int e = 0; e < C; e++) if (ok >> e & 1) best = std::min(best, x[e + 1]);
                            cross += best;
                            if (h + cross + S[j + 1] > ceil) break;
                        }
                        if (h + cross + S[j + 1] > ceil) continue;
                        uint64_t nk = 0;
                        for (size_t s = 0; s < out.size(); s++) nk |= (uint64_t)ch[out[s]] << (3 * s);
                        nxt.push_back({nk, h});
                    }
                }
                std::sort(nxt.begin(), nxt.end());
                size_t w = 0;
                for (size_t r = 0; r < nxt.size(); r++)
                    if (w == 0 || nxt[r].first != nxt[w - 1].first) nxt[w++] = nxt[r];
                nxt.resize(w);
                work += w;
                peak = std::max(peak, w);
                std::swap(cur, nxt);
            }
            if (!cur.empty()) { S[k] = cur[0].second; if (k % 50 == 0) fprintf(stderr, "radios from %d on: %ld\n", k, S[k]); break; }
            if (t > total) { fprintf(stderr, "no legal plan for radios %d..%d\n", k, n - 1); return 1; }
            t *= 2;
        }
    }
    fprintf(stderr, "radios %d, states kept %ld, widest step %zu\n", n, work, peak);
    printf("{\"optimum\": %ld}\n", S[0]);
    return 0;
}
