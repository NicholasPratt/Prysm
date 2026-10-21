// SPDX-License-Identifier: GPL-3.0-only
// One of the sound's watchers (the plugin window, the FM-1): what it has of the patch. diff() gives what changed
// since, but not a change the watcher made itself and is still waiting to see in the patch (mine()): without that,
// a slider dragged quickly would be pulled back to a value it had a moment ago.
#pragma once
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

class Mirror {
public:
    void reset(const uint8_t* p, size_t n) { have.assign(p, p + n); expect.clear(); }
    bool ready() const { return !have.empty(); }
    void clear() { have.clear(); expect.clear(); }

    void mine(uint32_t off, uint8_t v)         // the watcher set this itself
    {
        if (off >= have.size()) return;
        have[off] = v;
        expect[off] = {v, clock::now()};
    }

    std::vector<std::pair<uint32_t, uint8_t>> diff(const uint8_t* p, size_t n)
    {
        std::vector<std::pair<uint32_t, uint8_t>> out;
        if (n != have.size()) return out;
        const auto now = clock::now();
        for (size_t o = 0; o < n; o++) {
            auto e = expect.find((uint32_t)o);
            if (e != expect.end()) {
                if (p[o] == e->second.first || now - e->second.second > std::chrono::seconds(1))
                    expect.erase(e);           // (arrived, or never will: from now on the patch has it)
                else
                    continue;                  // (still on its way: not a change from elsewhere)
            }
            if (p[o] != have[o]) {
                have[o] = p[o];
                out.emplace_back((uint32_t)o, p[o]);
            }
        }
        return out;
    }

private:
    using clock = std::chrono::steady_clock;
    std::vector<uint8_t> have;
    std::unordered_map<uint32_t, std::pair<uint8_t, clock::time_point>> expect;
};
