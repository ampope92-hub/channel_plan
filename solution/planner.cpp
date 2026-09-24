// Channel plan for the corridor radio network.
//
// usage: planner <survey-dir> <plan-file>
//
// Every radio at a site shares the site's mast, so the radios of one site are planned together:
// a site's options are the channel tuples that keep its own radios two channels apart and stay
// off the channels barred for each radio. Interference only reaches a few kilometres, so once the
// sites are taken in order along the line, only the sites that still have a listed pair with a
// site further on can affect what comes next. That set is small and it is the whole state of an
// exact dynamic programme over the line.

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

    // listed pairs, grouped by the two sites they join
    struct Pair { int a, b; long co, adj; };
    map<pair<int, int>, vector<Pair>> between;  // key: (earlier site, later site)
    vector<int> reach_to(S);
    for (int k = 0; k < S; k++) reach_to[k] = k;
    for (auto& r : read_csv(dir + "/pairs.csv")) {
        int a = radio_index.at(stol(r[0])), b = radio_index.at(stol(r[1]));
        long co = stol(r[2]), adj = stol(r[3]);
        int sa = site_of[a], sb = site_of[b];
        if (sa == sb) continue;  // radios on one mast are held apart by the site rule instead
        if (sa > sb) { swap(a, b); swap(sa, sb); }
        between[{sa, sb}].push_back({a, b, co, adj});
        reach_to[sa] = max(reach_to[sa], sb);
    }
    auto pos_in_site = [&](int radio) {
        auto& v = line[site_of[radio]].radios;
        return int(find(v.begin(), v.end(), radio) - v.begin());
    };

    // Dynamic programme over the sites in line order. The state after site k is the choice made
    // at every site up to k that still has a listed pair with a site after k (the frontier).
    const long long INF = (long long)4e18;
    vector<int> frontier;        // site indices
    vector<long long> cost(1, 0);
    vector<vector<uint32_t>> back_state(S);
    vector<vector<uint16_t>> back_choice(S);
    vector<vector<int>> frontier_after(S);

    for (int k = 0; k < S; k++) {
        const Site& here = line[k];
        int P = here.patterns.size();
        int F = frontier.size();

        // interference between each frontier site and this one, for every pair of choices
        vector<vector<long long>> table(F);
        for (int f = 0; f < F; f++) {
            auto it = between.find({frontier[f], k});
            if (it == between.end()) continue;
            const Site& there = line[frontier[f]];
            table[f].assign(there.patterns.size() * P, 0);
            for (auto& pr : it->second) {
                int ia = pos_in_site(pr.a), ib = pos_in_site(pr.b);
                for (size_t x = 0; x < there.patterns.size(); x++)
                    for (int y = 0; y < P; y++) {
                        int d = abs(there.patterns[x][ia] - here.patterns[y][ib]);
                        table[f][x * P + y] += d == 0 ? pr.co : (d == 1 ? pr.adj : 0);
                    }
            }
        }

        // which sites stay in the frontier, and their place in the new index
        vector<int> next;
        vector<long long> keep_mult(F, 0);
        long long here_mult = 0, next_size = 1;
        for (int f = 0; f < F; f++)
            if (reach_to[frontier[f]] > k) {
                keep_mult[f] = next_size;
                next_size *= line[frontier[f]].patterns.size();
                next.push_back(frontier[f]);
            }
        if (reach_to[k] > k) {
            here_mult = next_size;
            next_size *= P;
            next.push_back(k);
        }

        vector<long long> nxt(next_size, INF);
        back_state[k].assign(next_size, 0);
        back_choice[k].assign(next_size, 0);

        vector<int> digit(F, 0);
        for (size_t state = 0; state < cost.size(); state++) {
            if (state > 0) {  // odometer over the frontier, first site fastest
                int f = 0;
                while (++digit[f] == (int)line[frontier[f]].patterns.size()) digit[f++] = 0;
            }
            long long base = cost[state];
            if (base >= INF) continue;
            long long kept = 0;
            for (int f = 0; f < F; f++) kept += digit[f] * keep_mult[f];
            for (int y = 0; y < P; y++) {
                long long c = base;
                for (int f = 0; f < F; f++)
                    if (!table[f].empty()) c += table[f][digit[f] * P + y];
                long long idx = kept + y * here_mult;
                if (c < nxt[idx]) {
                    nxt[idx] = c;
                    back_state[k][idx] = state;
                    back_choice[k][idx] = y;
                }
            }
        }
        frontier_after[k] = next;
        frontier = next;
        cost.swap(nxt);
    }

    // the frontier is empty after the last site, so there is one final state
    long long best = cost[0];
    vector<int> choice(S, -1);
    long long idx = 0;
    for (int k = S - 1; k >= 0; k--) {
        choice[k] = back_choice[k][idx];
        long long prev = back_state[k][idx];
        // the previous frontier is the frontier after k-1; decode it and pin those sites
        if (k > 0) {
            auto& fr = frontier_after[k - 1];
            long long rest = prev;
            for (int f : fr) {
                int r = line[f].patterns.size();
                int d = rest % r;
                rest /= r;
                if (choice[f] < 0) choice[f] = d;
            }
        }
        idx = prev;
    }

    vector<int> channel(n, -1);
    for (int k = 0; k < S; k++)
        for (size_t a = 0; a < line[k].radios.size(); a++)
            channel[line[k].radios[a]] = line[k].patterns[choice[k]][a];

    FILE* out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    fprintf(out, "radio,channel\n");
    for (int i = 0; i < n; i++) fprintf(out, "%ld,%d\n", radio_id[i], channel[i]);
    fclose(out);
    fprintf(stderr, "planned %d radios at %d sites, total penalty %lld\n", n, S, best);
    return 0;
}
