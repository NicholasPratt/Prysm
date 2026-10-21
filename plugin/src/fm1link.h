// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 over USB-MIDI, from the plugin: its own port (RtMidi: CoreMIDI, Windows MIDI, ALSA), found by its name and
// found again after a replug. The editor's protocol (firmware/src/editor.c): 'H' hello -> the FM-1's sound ('E' .. 'F');
// 'V' a change, answered 'A'; 'C' a change made on the FM-1. The project's sound wins: after the hello, whatever the
// FM-1 has differently is sent to it.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "mirror.h"

class RtMidiIn;
class RtMidiOut;

class Fm1Link {
public:
    Fm1Link();
    ~Fm1Link();
    // a change made on the FM-1 (called on the link's own thread)
    std::function<void(uint32_t off, uint8_t v)> onChange;

    void sync(const uint8_t* patch, size_t n); // (main thread) what the FM-1 does not have yet goes to it
    void note(uint8_t st, uint8_t d1, uint8_t d2);   // (audio thread) a note, to be played by the FM-1 itself
    void store(int slot, const uint8_t* patch, size_t n, uint32_t version);   // (main thread) into a slot (0 .. 7)
    std::string status();

private:
    void run();
    bool open();
    void close();
    bool ask(char type, const std::vector<uint8_t>& data);   // a message the FM-1 answers; true: answered rc 0
    void send(char type, const std::vector<uint8_t>& data);
    void receive(const std::vector<unsigned char>& m);
    static void midiIn(double, std::vector<unsigned char>* m, void* self);

    std::unique_ptr<RtMidiIn> in;
    std::unique_ptr<RtMidiOut> out;
    std::thread thread;
    std::atomic<bool> quit{false};
    std::mutex lock;                             // everything below
    std::condition_variable wake;
    Mirror mirror;
    std::vector<uint8_t> dump;
    size_t patchSize = 0;
    std::deque<std::pair<uint32_t, uint8_t>> pending;
    std::deque<std::vector<uint8_t>> stores;    // (slot, size, version, patch ...)
    int answered = -2;                           // the last 'A' rc (-2: none yet)
    char awaiting = 0;
    bool connected = false, synced = false;
    std::string state = "FM-1: looking for it";
    // notes from the audio thread: a ring with one writer and one reader
    static constexpr unsigned NQ = 256;
    uint8_t nq[NQ][3];
    std::atomic<unsigned> nqW{0}, nqR{0};
};
