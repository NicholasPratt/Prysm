// SPDX-License-Identifier: GPL-3.0-only
#include "fm1link.h"
#include <RtMidi.h>
#include <algorithm>
#include <cctype>
#include <chrono>

using namespace std::chrono_literals;

static bool isFm1(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return name.find("fm-1") != std::string::npos || name.find("prysm") != std::string::npos ||
           name.find("felucca") != std::string::npos;
}

Fm1Link::Fm1Link() { thread = std::thread([this] { run(); }); }

Fm1Link::~Fm1Link()
{
    quit = true;
    wake.notify_all();
    thread.join();
    close();
}

std::string Fm1Link::status()
{
    std::lock_guard<std::mutex> g(lock);
    return state;
}

void Fm1Link::sync(const uint8_t* patch, size_t n, const uint8_t* seqs, size_t sn)
{
    std::lock_guard<std::mutex> g(lock);
    patchSize = n;
    seqsSize = sn;
    if (!synced) return;
    for (auto& c : mirror.diff(patch, n)) pending.push_back(c);
    if (smirror.ready())
        for (auto& c : smirror.diff(seqs, sn)) spending.push_back(c);
    if (!pending.empty() || !spending.empty()) wake.notify_all();
}

void Fm1Link::note(uint8_t st, uint8_t d1, uint8_t d2)
{
    unsigned w = nqW.load(std::memory_order_relaxed);
    if (w - nqR.load(std::memory_order_acquire) >= NQ) return;   // (full: dropped)
    nq[w % NQ][0] = st; nq[w % NQ][1] = d1; nq[w % NQ][2] = d2;
    nqW.store(w + 1, std::memory_order_release);
}

void Fm1Link::store(int slot, const uint8_t* patch, size_t n, uint32_t version)
{
    std::lock_guard<std::mutex> g(lock);
    std::vector<uint8_t> s{(uint8_t)slot, (uint8_t)(n >> 7), (uint8_t)(n & 127), (uint8_t)version};
    s.insert(s.end(), patch, patch + n);
    stores.push_back(std::move(s));
    wake.notify_all();
}

bool Fm1Link::open()
{
    try {
        auto i = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "Prysm");
        auto o = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "Prysm");
        int pi = -1, po = -1;
        for (unsigned k = 0; k < i->getPortCount() && pi < 0; k++) if (isFm1(i->getPortName(k))) pi = (int)k;
        for (unsigned k = 0; k < o->getPortCount() && po < 0; k++) if (isFm1(o->getPortName(k))) po = (int)k;
        if (pi < 0 || po < 0) {
            std::lock_guard<std::mutex> g(lock);
            state = "FM-1: not found (connect it by USB)";
            return false;
        }
        i->ignoreTypes(false, true, true);      // SysEx in; not the clock or active sensing
        i->setCallback(&Fm1Link::midiIn, this);
        i->openPort((unsigned)pi, "Prysm editor");
        o->openPort((unsigned)po, "Prysm editor");
        in = std::move(i);
        out = std::move(o);
        return true;
    } catch (const RtMidiError& e) {
        std::lock_guard<std::mutex> g(lock);
        state = "FM-1: its port is busy (on Windows: switch it off in the DAW's MIDI devices)";
        return false;
    }
}

void Fm1Link::close()
{
    try {
        if (in) in->closePort();
        if (out) out->closePort();
    } catch (...) {}
    in.reset();
    out.reset();
    std::lock_guard<std::mutex> g(lock);
    connected = synced = false;
    mirror.clear();
    smirror.clear();
    pending.clear();
    spending.clear();
}

void Fm1Link::send(char type, const std::vector<uint8_t>& data)
{
    std::vector<unsigned char> m{0xF0, 0x7D, 0x50, (unsigned char)type};
    m.insert(m.end(), data.begin(), data.end());
    m.push_back(0xF7);
    out->sendMessage(&m);
}

int Fm1Link::ask(char type, const std::vector<uint8_t>& data)
{
    for (int tries = 0; tries < 3; tries++) {
        std::unique_lock<std::mutex> g(lock);
        answered = -2;
        awaiting = type;
        g.unlock();
        send(type, data);
        g.lock();
        if (wake.wait_for(g, 400ms, [this] { return answered != -2 || quit; })) {
            awaiting = 0;
            return quit ? -1 : answered;
        }
    }
    std::lock_guard<std::mutex> g(lock);
    awaiting = 0;
    return -1;
}

void Fm1Link::midiIn(double, std::vector<unsigned char>* m, void* self)
{
    static_cast<Fm1Link*>(self)->receive(*m);
}

void Fm1Link::receive(const std::vector<unsigned char>& m)
{
    if (m.size() < 5 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 0x50) return;
    const char t = (char)m[3];
    const unsigned char* b = m.data() + 4;
    const size_t n = m.size() - 5;
    std::unique_lock<std::mutex> g(lock);
    if (t == 'A' && n >= 3 && awaiting && b[0] == (unsigned char)awaiting) {
        answered = b[2];
        wake.notify_all();
    } else if (t == 'E' && n >= 129) {           // the FM-1's sound, 64 bytes at b[0] * 64
        if (dump.size() < (b[0] + 1u) * 64) dump.resize((b[0] + 1u) * 64);
        for (int j = 0; j < 64; j++) dump[b[0] * 64 + j] = (uint8_t)(b[1 + 2 * j] << 4 | b[2 + 2 * j]);
    } else if (t == 'Q' && n >= 129) {          // the FM-1's patterns, 64 bytes at b[0] * 64
        if (sdump.size() < (b[0] + 1u) * 64) sdump.resize((b[0] + 1u) * 64);
        for (int j = 0; j < 64; j++) sdump[b[0] * 64 + j] = (uint8_t)(b[1 + 2 * j] << 4 | b[2 + 2 * j]);
    } else if (t == 'F' && n >= 4) {
        const size_t size = (size_t)b[0] << 7 | b[1];
        if (size != patchSize || dump.size() < size) {
            state = "FM-1: its patch format differs: install the Prysm that goes with this plugin";
            return;
        }
        mirror.reset(dump.data(), size);        // (the project's sound then goes to it: sync)
        dump.clear();
        smirror.clear();                        // (and the project's patterns: an FM-1 that has them)
        if (n >= 7 && ((size_t)b[4] << 7 | b[5]) == seqsSize && sdump.size() >= seqsSize) smirror.reset(sdump.data(), seqsSize);
        sdump.clear();
        synced = true;
        state = "FM-1: connected";
        wake.notify_all();
    } else if (t == 'C' && n >= 4 && synced) {   // turned on the FM-1
        const uint32_t o = (uint32_t)b[0] << 7 | b[1];
        const uint8_t v = (uint8_t)(b[2] << 7 | b[3]);
        mirror.mine(o, v);
        auto cb = onChange;
        g.unlock();
        if (cb) cb(o, v);
    } else if (t == 'G' && n >= 4 && synced && smirror.ready()) {   // its patterns changed on the FM-1
        const uint32_t o = (uint32_t)b[0] << 7 | b[1];
        const uint8_t v = (uint8_t)(b[2] << 7 | b[3]);
        smirror.mine(o, v);
        auto cb = onSeqChange;
        g.unlock();
        if (cb) cb(o, v);
    }
}

void Fm1Link::run()
{
    auto lastTry = std::chrono::steady_clock::now() - 10s, lastCheck = lastTry, asked = lastTry;
    while (!quit) {
        if (!connected) {
            nqR.store(nqW.load(std::memory_order_acquire), std::memory_order_release);   // (no FM-1: no notes for it)
            if (std::chrono::steady_clock::now() - lastTry < 2s) { std::this_thread::sleep_for(50ms); continue; }
            lastTry = std::chrono::steady_clock::now();
            if (!open()) continue;
            { std::lock_guard<std::mutex> g(lock); connected = true; state = "FM-1: asking"; }
            send('H', {});
            asked = lastCheck = std::chrono::steady_clock::now();
            continue;
        }
        if (std::chrono::steady_clock::now() - lastCheck > 3s) {   // still there? (an unplug says nothing)
            lastCheck = std::chrono::steady_clock::now();
            bool there = false;
            try {
                for (unsigned k = 0; k < in->getPortCount() && !there; k++) there = isFm1(in->getPortName(k));
            } catch (...) {}
            if (!there) {
                close();
                std::lock_guard<std::mutex> g(lock);
                state = "FM-1: unplugged; looking for it";
                continue;
            }
        }
        bool busy = false;
        for (unsigned r; (r = nqR.load(std::memory_order_relaxed)) != nqW.load(std::memory_order_acquire);) {
            std::vector<unsigned char> m{nq[r % NQ][0], nq[r % NQ][1], nq[r % NQ][2]};
            if ((m[0] & 0xF0) == 0xC0 || (m[0] & 0xF0) == 0xD0) m.pop_back();
            try { out->sendMessage(&m); } catch (...) {}
            nqR.store(r + 1, std::memory_order_release);
            busy = true;
        }
        std::unique_lock<std::mutex> g(lock);
        if (!stores.empty()) {                  // into a slot: 'P' 64 bytes at a time, then 'W'
            auto s = std::move(stores.front());
            stores.pop_front();
            state = "FM-1: storing";
            g.unlock();
            const size_t n = s.size() - 4;
            bool ok = true;
            for (size_t i = 0; i < n && ok; i += 64) {
                std::vector<uint8_t> d{(uint8_t)(i / 64)};
                for (size_t j = 0; j < 64; j++) {
                    const uint8_t v = i + j < n ? s[4 + i + j] : 0;
                    d.push_back(v >> 4);
                    d.push_back(v & 15);
                }
                ok = ask('P', d) == 0;
            }
            ok = ok && ask('W', {s[0], s[1], s[2], s[3]}) == 0;
            g.lock();
            state = ok ? "FM-1: stored in slot " + std::to_string(s[0] + 1) : "FM-1: storing failed";
            continue;
        }
        if (synced && (!pending.empty() || !spending.empty())) {   // one change at a time, each answered
            const bool snd = !pending.empty();
            auto c = snd ? pending.front() : spending.front();
            (snd ? pending : spending).pop_front();
            g.unlock();
            if (ask(snd ? 'V' : 'U', {(uint8_t)(c.first >> 7), (uint8_t)(c.first & 127), (uint8_t)(c.second >> 7),
                           (uint8_t)(c.second & 127)}) < 0) {
                close();                         // (no answer: unplugged; look for it again)
                std::lock_guard<std::mutex> g2(lock);
                state = "FM-1: lost; looking for it";
            }
            continue;
        }
        if (!synced) {                          // the hello's answer not here yet: ask again now and then
            if (std::chrono::steady_clock::now() - asked > 2s) {
                asked = std::chrono::steady_clock::now();
                state = "FM-1: no answer (it needs Prysm 0.8 or later)";
                g.unlock();
                send('H', {});
                continue;
            }
        }
        if (!busy) wake.wait_for(g, 5ms);
    }
}
