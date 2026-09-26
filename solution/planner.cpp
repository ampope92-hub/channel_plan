// Channel plan for the corridor radio network.
//
// usage: planner <survey-dir> <plan-file>
//
// Every radio at a site shares the site's mast, so the radios of one site are planned together:
// a site's options are the channel tuples that keep its own radios two channels apart and stay
// off the channels barred for each radio. Interference only reaches a few kilometres, so once the
// sites are taken in order along the line, only the radios that still have a listed pair with a
// site further on can affect what comes next. Their channels are the whole state of a dynamic
// programme over the line, exact while the state fits a fixed table and bounded after that.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static vector<vector<string>> read_csv(const string& path) {
    ifstream in(path);
    if (!in) {
        fprintf(stderr, "cannot open %s\n", path.c_str());
        exit(1);
    }
    vector<vector<string>> rows;
    string line;
    bool header = true;
    while (getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) { header = false; continue; }
        if (line.empty()) continue;
        vector<string> f;
        string cell;
        stringstream ss(line);
        while (getline(ss, cell, ',')) f.push_back(cell);
        rows.push_back(f);
    }
    return rows;
}

// "155.3" -> 1553 (tenths of a km); the survey writes one decimal place
static long tenths(const string& s) {
    long whole = 0, frac = 0;
    size_t i = 0;
    for (; i < s.size() && s[i] != '.'; i++) whole = whole * 10 + (s[i] - '0');
    if (i < s.size()) frac = s[i + 1] - '0';
    return whole * 10 + frac;
}

struct Site {
    long id = 0, km = 0;
    vector<int> radios;             // radio indices, in radio id order
    vector<vector<int>> patterns;   // one channel per radio
};

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: planner <survey-dir> <plan-file>\n");
        return 2;
    }
    string dir = argv[1];

    int channels = 0;
    {
        ifstream in(dir + "/licence.txt");
        string word;
        in >> word >> channels;
        if (word != "channels" || channels <= 0) {
            fprintf(stderr, "bad licence.txt\n");
            return 1;
        }
    }

    auto radio_rows = read_csv(dir + "/radios.csv");
    int n = radio_rows.size();
    vector<long> radio_id(n);
    map<long, int> radio_index;
    map<long, int> site_index;
    vector<Site> sites;
    vector<int> site_of(n);
    for (int i = 0; i < n; i++) {
        radio_id[i] = stol(radio_rows[i][0]);
        long sid = stol(radio_rows[i][1]);
        radio_index[radio_id[i]] = i;
        if (!site_index.count(sid)) {
            site_index[sid] = sites.size();
            Site s;
            s.id = sid;
            s.km = tenths(radio_rows[i][2]);
            sites.push_back(s);
        }
        site_of[i] = site_index[sid];
    }

    vector<vector<char>> allowed(n, vector<char>(channels, 1));
    for (auto& r : read_csv(dir + "/barred.csv")) {
        int c = stoi(r[1]);
        if (c >= 0 && c < channels) allowed[radio_index.at(stol(r[0]))][c] = 0;
    }

    // order the sites along the line
    int S = sites.size();
    vector<int> order(S);
    for (int s = 0; s < S; s++) order[s] = s;
    sort(order.begin(), order.end(), [&](int a, int b) {
        if (sites[a].km != sites[b].km) return sites[a].km < sites[b].km;
        return sites[a].id < sites[b].id;
    });
    vector<int> rank(S);
    for (int k = 0; k < S; k++) rank[order[k]] = k;
    vector<Site> line(S);
    for (int k = 0; k < S; k++) line[k] = sites[order[k]];
    for (int i = 0; i < n; i++) site_of[i] = rank[site_of[i]];
    for (auto& s : line) s.radios.clear();
    // radios.csv is in radio id order, so each site's list comes out sorted too
    for (int i = 0; i < n; i++) line[site_of[i]].radios.push_back(i);

    // a site's options: one channel per radio, two apart within the site, none barred
    for (auto& s : line) {
        int t = s.radios.size();
        vector<int> ch(t, 0);
        while (true) {
            bool ok = true;
            for (int a = 0; a < t && ok; a++) {
                if (!allowed[s.radios[a]][ch[a]]) ok = false;
                for (int b = a + 1; b < t && ok; b++)
                    if (abs(ch[a] - ch[b]) < 2) ok = false;
            }
            if (ok) s.patterns.push_back(ch);
            int a = 0;
            while (a < t && ++ch[a] == channels) ch[a++] = 0;
            if (a == t) break;
        }
        if (s.patterns.empty()) {
            fprintf(stderr, "site %ld has no legal channel set\n", s.id);
            return 1;
        }
    }

    // listed pairs, grouped by the two sites they join, and for every radio the furthest site
    // along the line it has a pair with (its own site if none)
    struct Pair { int a, b; long co, adj; };
    map<pair<int, int>, vector<Pair>> between;  // key: (earlier site, later site)
    vector<int> reach_to(n);
    for (int i = 0; i < n; i++) reach_to[i] = site_of[i];
    for (auto& r : read_csv(dir + "/pairs.csv")) {
        int a = radio_index.at(stol(r[0])), b = radio_index.at(stol(r[1]));
        long co = stol(r[2]), adj = stol(r[3]);
        int sa = site_of[a], sb = site_of[b];
        if (sa == sb) continue;  // radios on one mast are held apart by the site rule instead
        if (sa > sb) { swap(a, b); swap(sa, sb); }
        between[{sa, sb}].push_back({a, b, co, adj});
        reach_to[a] = max(reach_to[a], sb);
    }
    auto pos_in_site = [&](int radio) {
        auto& v = line[site_of[radio]].radios;
        return int(find(v.begin(), v.end(), radio) - v.begin());
    };

    // What the line ahead can still see of a site is the channels of its radios that have a pair
    // further on. A view lists the distinct choices on those radios: of[p] is the view index of
    // the site's option p, rep[x] one option showing view x.
    struct View { int site = -1; vector<int> of, rep; };
    auto view_of = [&](int k, int after) {
        View v;
        v.site = k;
        const Site& s = line[k];
        map<vector<int>, int> seen;
        for (auto& p : s.patterns) {
            vector<int> key;
            for (size_t a = 0; a < s.radios.size(); a++)
                if (reach_to[s.radios[a]] > after) key.push_back(p[a]);
            auto it = seen.find(key);
            if (it == seen.end()) {
                it = seen.emplace(key, (int)v.rep.size()).first;
                v.rep.push_back(&p - &s.patterns[0]);
            }
            v.of.push_back(it->second);
        }
        return v;
    };
    auto live = [&](int k, int after) {
        for (int r : line[k].radios)
            if (reach_to[r] > after) return true;
        return false;
    };

    // Dynamic programme over the sites in line order. The state after site k is the view of every
    // site up to k that still has a radio with a pair after k, packed as a mixed-radix key. While
    // the keys fit EXACT_STATES slots every state is kept and the programme is exact; past that a
    // state goes to a hashed slot of a table of BEAM_STATES and the cheaper of two colliding states
    // stays, so memory is bounded on any survey and a legal plan always comes out.
    const long long INF = (long long)4e18;
    const uint64_t EXACT_STATES = uint64_t(1) << 22, BEAM_STATES = uint64_t(1) << 18;
    vector<View> frontier;
    vector<uint64_t> keys(1, 0);
    vector<long long> cost(1, 0);
    vector<vector<uint32_t>> back(S);
    vector<vector<uint32_t>> pick(S);
    bool exact = true;

    for (int k = 0; k < S; k++) {
        const Site& here = line[k];
        int P = here.patterns.size();
        int F = frontier.size();

        // interference between each frontier site and this one, for every pair of choices
        vector<vector<long long>> table(F);
        for (int f = 0; f < F; f++) {
            auto it = between.find({frontier[f].site, k});
            if (it == between.end()) continue;
            const Site& there = line[frontier[f].site];
            int X = frontier[f].rep.size();
            table[f].assign(X * P, 0);
            for (auto& pr : it->second) {
                int ia = pos_in_site(pr.a), ib = pos_in_site(pr.b);
                for (int x = 0; x < X; x++)
                    for (int y = 0; y < P; y++) {
                        int d = abs(there.patterns[frontier[f].rep[x]][ia] - here.patterns[y][ib]);
                        table[f][x * P + y] += d == 0 ? pr.co : (d == 1 ? pr.adj : 0);
                    }
            }
        }

        // the frontier after this site; a site whose radios have nothing ahead leaves it
        vector<View> next;
        vector<int> from;  // for each member of next, its place in frontier (-1 for this site)
        for (int f = 0; f < F; f++)
            if (live(frontier[f].site, k)) { next.push_back(view_of(frontier[f].site, k)); from.push_back(f); }
        if (live(k, k)) { next.push_back(view_of(k, k)); from.push_back(-1); }
        // a key must fit 62 bits; on a survey where it would not, the earliest sites are let go
        // (their remaining pairs are then not steered, the plan stays legal)
        while (true) {
            long double size = 1;
            for (auto& v : next) size *= v.rep.size();
            if (size < 4e18L) break;
            next.erase(next.begin());
            from.erase(from.begin());
            exact = false;
        }
        uint64_t next_size = 1;
        vector<vector<uint64_t>> kept_add(F);
        vector<uint64_t> here_add(P, 0);
        for (size_t i = 0; i < next.size(); i++) {
            if (from[i] >= 0) {
                const View& old = frontier[from[i]];
                kept_add[from[i]].resize(old.rep.size());
                for (size_t x = 0; x < old.rep.size(); x++)
                    kept_add[from[i]][x] = next[i].of[old.rep[x]] * next_size;
            } else {
                for (int y = 0; y < P; y++) here_add[y] = next[i].of[y] * next_size;
            }
            next_size *= next[i].rep.size();
        }
        bool dense = next_size <= EXACT_STATES;
        if (!dense) exact = false;
        uint64_t slots = dense ? next_size : BEAM_STATES;
        vector<long long> slot_cost(slots, INF);
        vector<uint64_t> slot_key(dense ? 0 : slots);
        vector<uint32_t> slot_back(slots);
        vector<uint32_t> slot_pick(slots);

        vector<int> digit(F);
        vector<long long> add(P);
        for (size_t s = 0; s < keys.size(); s++) {
            uint64_t rest = keys[s], kept = 0;
            for (int f = 0; f < F; f++) {
                int X = frontier[f].rep.size();
                digit[f] = rest % X;
                rest /= X;
                if (!kept_add[f].empty()) kept += kept_add[f][digit[f]];
            }
            for (int y = 0; y < P; y++) add[y] = cost[s];
            for (int f = 0; f < F; f++)
                if (!table[f].empty()) {
                    const long long* row = &table[f][digit[f] * P];
                    for (int y = 0; y < P; y++) add[y] += row[y];
                }
            for (int y = 0; y < P; y++) {
                uint64_t key = kept + here_add[y], slot = key;
                if (!dense) {
                    uint64_t h = key + 0x9e3779b97f4a7c15ULL;
                    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
                    slot = (h ^ (h >> 31)) & (slots - 1);
                }
                if (add[y] >= slot_cost[slot]) continue;  // the cheaper state in the slot stays
                if (!dense) slot_key[slot] = key;
                slot_cost[slot] = add[y];
                slot_back[slot] = s;
                slot_pick[slot] = y;
            }
        }

        keys.clear();
        cost.clear();
        for (uint64_t slot = 0; slot < slots; slot++)
            if (slot_cost[slot] < INF) {
                keys.push_back(dense ? slot : slot_key[slot]);
                cost.push_back(slot_cost[slot]);
                back[k].push_back(slot_back[slot]);
                pick[k].push_back(slot_pick[slot]);
            }
        frontier = next;
    }

    // the frontier is empty after the last site, so there is one final state
    vector<int> choice(S, -1);
    size_t idx = 0;
    for (int k = S - 1; k >= 0; k--) {
        choice[k] = pick[k][idx];
        idx = back[k][idx];
    }


    vector<int> channel(n, -1);
    for (int k = 0; k < S; k++)
        for (size_t a = 0; a < line[k].radios.size(); a++)
            channel[line[k].radios[a]] = line[k].patterns[choice[k]][a];

    long long best = 0;
    for (auto& [sites_joined, list] : between)
        for (auto& pr : list) {
            int d = abs(channel[pr.a] - channel[pr.b]);
            best += d == 0 ? pr.co : (d == 1 ? pr.adj : 0);
        }

    FILE* out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    fprintf(out, "radio,channel\n");
    for (int i = 0; i < n; i++) fprintf(out, "%ld,%d\n", radio_id[i], channel[i]);
    fclose(out);
    fprintf(stderr, "planned %d radios at %d sites, total penalty %lld (%s)\n", n, S, best,
            exact ? "exact" : "bounded search");
    return 0;
}
