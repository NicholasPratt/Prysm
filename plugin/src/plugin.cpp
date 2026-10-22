// SPDX-License-Identifier: GPL-3.0-only
// Prysm as a CLAP instrument: the FM-1 firmware's own engine making the sound in the DAW (plugin/core), every knob of
// the sound a DAW parameter, the browser editor (web/editor/) as its window, and a connected FM-1 kept in step
// (fm1link.cpp). One sound, many watchers: the DAW's automation, the window and the FM-1 all change the one patch in
// the core (on the audio thread, through a queue), and each is told what changed that it did not do itself (Mirror).
#include <clap/clap.h>
#include "choc/gui/choc_WebView.h"
#include "assets.h"
#include "fm1link.h"
#include "mirror.h"
#include "prysm_core.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#if defined(__linux__)
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace {

constexpr const char* ID = "com.nicholaspratt.prysm";
const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                CLAP_PLUGIN_FEATURE_STEREO, nullptr};
const clap_plugin_descriptor_t DESC = {
    CLAP_VERSION_INIT, ID, "Prysm", "Nicholas Pratt", "https://github.com/NicholasPratt/Prysm",
    "https://nicholaspratt.github.io/Prysm/manual.html", "https://github.com/NicholasPratt/Prysm/issues",
    PRYSM_PLUGIN_VERSION, "Prysm, the M-VAVE FM-1 synth firmware: its engine in your DAW, and its editor", FEATURES};

// the plugin's own parameters, beside the sound's (whose ids are their byte in patch_t)
enum : clap_id { P_SYNC = 10000, P_SEQ, P_SOUND, P_FM1 };
struct Extra { clap_id id; const char* name; double def; };
const Extra EXTRAS[] = {{P_SYNC, "Arp & sequencer on DAW tempo", 1}, {P_SEQ, "Sequencer run", 0},
                        {P_SOUND, "Plugin sound", 1}, {P_FM1, "Play the FM-1 too", 0}};
const std::vector<uint32_t> LIST_KINDS = {4, 6, 7, 9, 11, 12, 16, 17, 18, 22, 32, 33, 34, 35};   // (ui.c's K_ kinds)

std::atomic<void*> g_owner{nullptr};            // the core is one per process: the first Prysm has it

struct Param { clap_id id; uint32_t index, off, max, min, kind; std::string name, module; double def; };

struct Op { uint8_t type, v; uint16_t off; };   // to the audio thread: set off = v, note on (off, v), note off, a pattern byte
enum { OP_SET, OP_ON, OP_OFF, OP_SEQ };

struct Prysm {
    clap_plugin_t plugin{};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_timer_support_t* hostTimer = nullptr;
    bool owner = false, active = false;
    double rate = 48000;
    std::vector<Param> params;
    std::atomic<int> extra[4] = {{1}, {0}, {1}, {0}};

    // main -> audio: a ring with one writer (the main thread) and one reader
    static constexpr unsigned NOPS = 4096;
    Op ops[NOPS];
    std::atomic<unsigned> opsW{0}, opsR{0};
    std::vector<uint8_t> loadPending;           // a whole sound from the project (main -> audio, under loadLock)
    std::vector<uint8_t> seqsPending;           // and its patterns (empty: none came)
    std::mutex loadLock;
    std::atomic<bool> loadReady{false};

    // the engine's 44.1 kHz -> the DAW's rate (4-point Hermite), and its clock from the DAW's transport
    float srcL[64]{}, srcR[64]{};
    uint64_t srcW = 0;
    double srcPos = 1;
    bool playing = false;
    double beat = 0, beatsPerSrc = 0;

    // the watchers
    std::unique_ptr<Fm1Link> link;
    std::mutex inboxLock;
    std::vector<std::pair<uint32_t, uint8_t>> inbox, sinbox;   // changes made on the FM-1 (from its thread)
    Mirror guiMirror, guiSMirror;
    std::unique_ptr<choc::ui::WebView> web;
    bool guiReady = false;
    std::string guiStatus;
    uint32_t width = 1200, height = 800;
    clap_id timer = CLAP_INVALID_ID;
#if defined(__linux__)
    GtkWidget* plug = nullptr;
#endif

    Prysm* self() { return this; }
    void push(Op o)
    {
        unsigned w = opsW.load(std::memory_order_relaxed);
        if (w - opsR.load(std::memory_order_acquire) >= NOPS) return;
        ops[w % NOPS] = o;
        opsW.store(w + 1, std::memory_order_release);
        if (!active) drain(nullptr);            // (no audio thread: here and now)
        else if (hostParams) hostParams->request_flush(host);
    }

    void tellHost(const clap_output_events_t* out, clap_id id, double v)
    {
        if (!out) return;
        clap_event_param_value_t e{};
        e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
        out->try_push(out, &e.header);
    }

    void drain(const clap_output_events_t* out) // (the audio thread, or the main thread while inactive)
    {
        if (!owner) { opsR.store(opsW.load()); return; }
        if (loadReady.exchange(false)) {
            std::lock_guard<std::mutex> g(loadLock);
            prysm_load(loadPending.data() + 4, (uint32_t)loadPending.size() - 4, loadPending[0]);
            if (seqsPending.size() > 1) prysm_seqs_load(seqsPending.data() + 1, (uint32_t)seqsPending.size() - 1, seqsPending[0]);
        }
        for (unsigned r; (r = opsR.load(std::memory_order_relaxed)) != opsW.load(std::memory_order_acquire);) {
            const Op o = ops[r % NOPS];
            opsR.store(r + 1, std::memory_order_release);
            if (o.type == OP_SET) {
                prysm_set(o.off, o.v);
                tellHost(out, o.off, prysm_patch()[o.off]);
            } else if (o.type == OP_SEQ) prysm_seq_set(o.off, o.v);
            else note(o.off, o.type == OP_ON ? (o.v ? o.v : 1) : 0);
        }
    }

    void note(uint32_t key, uint32_t vel)
    {
        prysm_note(key, vel);
        if (extra[3] && link) link->note(vel ? 0x90 : 0x80, (uint8_t)key, (uint8_t)vel);
    }

    void setExtra(clap_id id, double v)
    {
        const int i = (int)(id - P_SYNC), on = v >= 0.5;
        extra[i] = on;
        if (id == P_SEQ && owner) prysm_seq_play(on);
        if (id == P_SYNC && owner && !on && playing) { prysm_clock(0xFC, 0); prysm_sync(0); playing = false; }
    }

    void event(const clap_event_header_t* h)
    {
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        switch (h->type) {
        case CLAP_EVENT_NOTE_ON: case CLAP_EVENT_NOTE_OFF: {
            auto e = (const clap_event_note_t*)h;
            if (e->key < 0 || e->key > 127) break;
            note((uint32_t)e->key, h->type == CLAP_EVENT_NOTE_ON ? std::max(1, (int)std::lround(e->velocity * 127)) : 0);
            break;
        }
        case CLAP_EVENT_MIDI: {
            auto e = (const clap_event_midi_t*)h;
            const uint8_t s = e->data[0] & 0xF0;
            if (s == 0x90 && e->data[2]) note(e->data[1], e->data[2]);
            else if (s == 0x80 || s == 0x90) note(e->data[1], 0);
            else if (s == 0xE0) { prysm_bend((uint32_t)e->data[1] | (uint32_t)e->data[2] << 7, 2); if (extra[3] && link) link->note(e->data[0], e->data[1], e->data[2]); }
            else if (s == 0xB0 && (e->data[1] == 123 || e->data[1] == 120)) prysm_all_off();
            break;
        }
        case CLAP_EVENT_PARAM_VALUE: {
            auto e = (const clap_event_param_value_t*)h;
            if (e->param_id >= P_SYNC) setExtra(e->param_id, e->value);
            else prysm_set(e->param_id, (uint32_t)std::lround(std::max(0.0, e->value)));
            break;
        }
        }
    }

    void transport(const clap_event_transport_t* t)   // the DAW's tempo -> the engine's MIDI clock
    {
        const bool on = t && extra[0] && (t->flags & CLAP_TRANSPORT_HAS_TEMPO) && (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) &&
                        (t->flags & CLAP_TRANSPORT_IS_PLAYING);
        if (on) {
            beatsPerSrc = t->tempo / 60.0 / PRYSM_RATE;
            const double lead = (double)srcW - srcPos;   // (frames made ahead of what is heard now)
            const double at = (double)t->song_pos_beats / CLAP_BEATTIME_FACTOR + lead * beatsPerSrc;
            if (!playing) {
                prysm_sync(1);
                prysm_clock(0xFA, 0);
                playing = true;
                beat = at;
            } else if (std::fabs(at - beat) > 1.0 / 48) beat = at;   // (a jump: a loop, a relocate)
        } else if (playing) {
            prysm_clock(0xFC, 0);
            prysm_sync(0);
            playing = false;
        }
    }

    void render16()                             // 16 frames of the engine, with the clock's ticks in them
    {
        float l[PRYSM_BLOCK], r[PRYSM_BLOCK];
        if (playing) {
            const double b1 = beat + PRYSM_BLOCK * beatsPerSrc;
            for (double k = std::floor(beat * 24) + 1; k <= std::floor(b1 * 24); k++)
                prysm_clock(0xF8, (uint32_t)std::max(0.0, (k / 24 - beat) / beatsPerSrc));
            beat = b1;
        }
        prysm_render(l, r, PRYSM_BLOCK);
        for (unsigned i = 0; i < PRYSM_BLOCK; i++, srcW++) { srcL[srcW & 63] = l[i]; srcR[srcW & 63] = r[i]; }
    }

    void sample(float& outL, float& outR)
    {
        const uint64_t i = (uint64_t)srcPos;
        while (srcW < i + 3) render16();
        const float t = (float)(srcPos - (double)i);
        auto herm = [t](const float* s, uint64_t i) {
            const float y0 = s[(i - 1) & 63], y1 = s[i & 63], y2 = s[(i + 1) & 63], y3 = s[(i + 2) & 63];
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2 * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        };
        outL = herm(srcL, i);
        outR = herm(srcR, i);
        srcPos += PRYSM_RATE / rate;
    }

    clap_process_status process(const clap_process_t* p)
    {
        float* L = p->audio_outputs[0].data32[0];
        float* R = p->audio_outputs[0].data32[1];
        const uint32_t n = p->frames_count;
        if (!owner) {
            std::memset(L, 0, n * sizeof(float)); std::memset(R, 0, n * sizeof(float));
            return CLAP_PROCESS_SLEEP;
        }
        drain(p->out_events);
        transport(p->transport);
        const uint32_t ne = p->in_events->size(p->in_events);
        uint32_t ei = 0;
        for (uint32_t f = 0; f < n; f++) {
            while (ei < ne) {
                auto h = p->in_events->get(p->in_events, ei);
                if (h->time > f) break;
                if (h->type == CLAP_EVENT_TRANSPORT && h->space_id == CLAP_CORE_EVENT_SPACE_ID) transport((const clap_event_transport_t*)h);
                else event(h);
                ei++;
            }
            sample(L[f], R[f]);
            if (!extra[2]) L[f] = R[f] = 0;
        }
        for (; ei < ne; ei++) event(p->in_events->get(p->in_events, ei));
        return CLAP_PROCESS_CONTINUE;
    }

    // ---- the window: the editor page, served from the plugin itself
    static std::string bytesJson(const uint8_t* pt, uint32_t n)
    {
        std::string s = "[";
        for (uint32_t i = 0; i < n; i++) s += (i ? "," : "") + std::to_string(pt[i]);
        return s + "]";
    }

    void js(const std::string& code) { if (web) web->evaluateJavascript("window.prysmNative && " + code); }

    bool guiCreate()
    {
        choc::ui::WebView::Options o;
        o.acceptsFirstMouseClick = true;
        o.fetchResource = [](const std::string& path) -> std::optional<choc::ui::WebView::Options::Resource> {
            const std::string p = path == "/" ? "/index.html" : path.substr(0, path.find('?'));
            for (size_t i = 0; i < prysm_assets_n; i++)
                if (p == std::string("/") + prysm_assets[i].path)
                    return choc::ui::WebView::Options::Resource(
                        std::string_view((const char*)prysm_assets[i].data, prysm_assets[i].size), prysm_assets[i].mime);
            return {};
        };
        web = std::make_unique<choc::ui::WebView>(o);
        if (!web->loadedOK()) { web.reset(); return false; }
        if (!owner) {
            web->setHTML("<body style='background:#000;color:#fff;font:15px monospace;padding:40px'>PRYSM<p>Only one "
                         "Prysm runs in each process (the firmware is one FM-1). Run this one in a process of its own: "
                         "in Reaper, the FX window's menu, <i>Run as</i>, <i>Separate process</i>.</p></body>");
            return true;
        }
        web->bind("prysm_hello", [this](const choc::value::ValueView&) -> choc::value::Value {
            guiMirror.reset(prysm_patch(), prysm_patch_size());
            guiSMirror.reset(prysm_seqs(), prysm_seqs_size());
            guiReady = true;
            guiStatus.clear();
            return choc::value::createString("{\"patch\":" + bytesJson(prysm_patch(), prysm_patch_size()) + ",\"seqs\":" +
                                             bytesJson(prysm_seqs(), prysm_seqs_size()) + ",\"status\":\"" +
                                             (link ? link->status() : "") + "\"}");
        });
        web->bind("prysm_set", [this](const choc::value::ValueView& a) -> choc::value::Value {
            for (uint32_t i = 0; i + 1 < a.size(); i += 2) {
                const uint32_t o = (uint32_t)a[i].getWithDefault<int64_t>(0), v = (uint32_t)a[i + 1].getWithDefault<int64_t>(0);
                if (o >= prysm_patch_size()) continue;
                guiMirror.mine(o, (uint8_t)v);
                push({OP_SET, (uint8_t)v, (uint16_t)o});
            }
            return {};
        });
        web->bind("prysm_seqset", [this](const choc::value::ValueView& a) -> choc::value::Value {
            for (uint32_t i = 0; i + 1 < a.size(); i += 2) {
                const uint32_t o = (uint32_t)a[i].getWithDefault<int64_t>(0), v = (uint32_t)a[i + 1].getWithDefault<int64_t>(0);
                if (o >= prysm_seqs_size()) continue;
                guiSMirror.mine(o, (uint8_t)v);
                push({OP_SEQ, (uint8_t)v, (uint16_t)o});
            }
            return {};
        });
        web->bind("prysm_note", [this](const choc::value::ValueView& a) -> choc::value::Value {
            const int k = (int)a[0].getWithDefault<int64_t>(-1), v = (int)a[1].getWithDefault<int64_t>(0);
            if (k >= 0 && k < 128) push({(uint8_t)(v ? OP_ON : OP_OFF), (uint8_t)v, (uint16_t)k});
            return {};
        });
        web->bind("prysm_store", [this](const choc::value::ValueView& a) -> choc::value::Value {
            const int s = (int)a[0].getWithDefault<int64_t>(0);
            if (link && s >= 1 && s <= 8) link->store(s - 1, prysm_patch(), prysm_patch_size(), prysm_patch_version());
            return {};
        });
        return true;
    }

    void guiDestroy()
    {
        guiReady = false;
        if (!web) return;
#if defined(__APPLE__)
        choc::objc::call<void>((id)web->getViewHandle(), "removeFromSuperview");
#elif defined(_WIN32)
        ShowWindow((HWND)web->getViewHandle(), SW_HIDE);
        SetParent((HWND)web->getViewHandle(), nullptr);
#elif defined(__linux__)
        if (plug) { gtk_container_remove(GTK_CONTAINER(plug), (GtkWidget*)web->getViewHandle()); gtk_widget_destroy(plug); plug = nullptr; }
#endif
        web.reset();
    }

    bool guiParent(const clap_window_t* w)
    {
        if (!web) return false;
#if defined(__APPLE__)
        auto v = (id)web->getViewHandle();
        choc::objc::call<void>(v, "setFrame:", choc::objc::CGRect{{0, 0}, {(choc::objc::CGFloat)width, (choc::objc::CGFloat)height}});
        choc::objc::call<void>((id)w->cocoa, "addSubview:", v);
#elif defined(_WIN32)
        auto v = (HWND)web->getViewHandle();
        SetWindowLongPtr(v, GWL_STYLE, (GetWindowLongPtr(v, GWL_STYLE) & ~WS_POPUP) | WS_CHILD);
        SetParent(v, (HWND)w->win32);
        SetWindowPos(v, nullptr, 0, 0, (int)width, (int)height, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(v, SW_SHOW);
#elif defined(__linux__)
        plug = gtk_plug_new((Window)w->x11);
        gtk_container_add(GTK_CONTAINER(plug), (GtkWidget*)web->getViewHandle());
        gtk_window_resize(GTK_WINDOW(plug), (int)width, (int)height);
        gtk_widget_show_all(plug);
#endif
        return true;
    }

    void guiSize()
    {
        if (!web) return;
#if defined(__APPLE__)
        choc::objc::call<void>((id)web->getViewHandle(), "setFrame:",
                               choc::objc::CGRect{{0, 0}, {(choc::objc::CGFloat)width, (choc::objc::CGFloat)height}});
#elif defined(_WIN32)
        SetWindowPos((HWND)web->getViewHandle(), nullptr, 0, 0, (int)width, (int)height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
#elif defined(__linux__)
        if (plug) gtk_window_resize(GTK_WINDOW(plug), (int)width, (int)height);
#endif
    }

    void onTimer()                              // (main thread, 30 times a second) the watchers
    {
#if defined(__linux__)
        while (gtk_events_pending()) gtk_main_iteration_do(false);
#endif
        if (!owner) return;
        {
            std::vector<std::pair<uint32_t, uint8_t>> in;
            { std::lock_guard<std::mutex> g(inboxLock); in.swap(inbox); }
            for (auto& c : in) push({OP_SET, c.second, (uint16_t)c.first});
            std::vector<std::pair<uint32_t, uint8_t>> sin;
            { std::lock_guard<std::mutex> g(inboxLock); sin.swap(sinbox); }
            for (auto& c : sin) push({OP_SEQ, c.second, (uint16_t)c.first});
        }
        std::vector<uint8_t> pt(prysm_patch(), prysm_patch() + prysm_patch_size());
        std::vector<uint8_t> sq(prysm_seqs(), prysm_seqs() + prysm_seqs_size());
        if (link) link->sync(pt.data(), pt.size(), sq.data(), sq.size());
        if (web && guiReady) {
            auto ch = guiMirror.diff(pt.data(), pt.size());
            if (!ch.empty()) {
                std::string s = "prysmNative.changes([";
                for (size_t i = 0; i < ch.size(); i++) s += (i ? "," : "") + std::to_string(ch[i].first) + "," + std::to_string(ch[i].second);
                js(s + "])");
            }
            auto sc = guiSMirror.diff(sq.data(), sq.size());
            if (!sc.empty()) {
                std::string s = "prysmNative.seqChanges([";
                for (size_t i = 0; i < sc.size(); i++) s += (i ? "," : "") + std::to_string(sc[i].first) + "," + std::to_string(sc[i].second);
                js(s + "])");
            }
            const std::string st = link ? link->status() : "";
            if (st != guiStatus) { guiStatus = st; js("prysmNative.status(" + choc::json::getEscapedQuotedString(st) + ")"); }
        }
    }
};

Prysm* P(const clap_plugin_t* p) { return (Prysm*)p->plugin_data; }

// ---- extensions
const clap_plugin_audio_ports_t AUDIO_PORTS = {
    [](const clap_plugin_t*, bool in) -> uint32_t { return in ? 0 : 1; },
    [](const clap_plugin_t*, uint32_t i, bool in, clap_audio_port_info_t* info) -> bool {
        if (in || i) return false;
        info->id = 0;
        std::snprintf(info->name, sizeof info->name, "Prysm");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }};

const clap_plugin_note_ports_t NOTE_PORTS = {
    [](const clap_plugin_t*, bool in) -> uint32_t { return in ? 1 : 0; },
    [](const clap_plugin_t*, uint32_t i, bool in, clap_note_port_info_t* info) -> bool {
        if (!in || i) return false;
        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf(info->name, sizeof info->name, "Notes");
        return true;
    }};

const Param* findParam(Prysm* s, clap_id id)
{
    for (auto& p : s->params) if (p.id == id) return &p;
    return nullptr;
}

const clap_plugin_params_t PARAMS = {
    [](const clap_plugin_t* p) -> uint32_t { return (uint32_t)(P(p)->params.size() + 4); },
    [](const clap_plugin_t* p, uint32_t i, clap_param_info_t* info) -> bool {
        auto s = P(p);
        std::memset(info, 0, sizeof *info);
        if (i >= s->params.size()) {
            if (i >= s->params.size() + 4) return false;
            const Extra& e = EXTRAS[i - s->params.size()];
            info->id = e.id;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
            std::snprintf(info->name, sizeof info->name, "%s", e.name);
            std::snprintf(info->module, sizeof info->module, "Plugin");
            info->min_value = 0; info->max_value = 1; info->default_value = e.def;
            return true;
        }
        const Param& q = s->params[i];
        info->id = q.id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
        for (auto k : LIST_KINDS) if (k == q.kind) info->flags |= CLAP_PARAM_IS_ENUM;
        std::snprintf(info->name, sizeof info->name, "%s", q.name.c_str());
        std::snprintf(info->module, sizeof info->module, "%s", q.module.c_str());
        info->min_value = q.min; info->max_value = q.max; info->default_value = q.def;
        return true;
    },
    [](const clap_plugin_t* p, clap_id id, double* v) -> bool {
        auto s = P(p);
        if (id >= P_SYNC && id <= P_FM1) { *v = s->extra[id - P_SYNC]; return true; }
        if (!findParam(s, id) || !s->owner) return false;
        *v = prysm_patch()[id];
        return true;
    },
    [](const clap_plugin_t* p, clap_id id, double v, char* out, uint32_t size) -> bool {
        auto s = P(p);
        if (id >= P_SYNC && id <= P_FM1) { std::snprintf(out, size, "%s", v >= 0.5 ? "ON" : "OFF"); return true; }
        const Param* q = findParam(s, id);
        if (!q || !s->owner) return false;
        std::snprintf(out, size, "%s", prysm_fmt(q->index, (uint32_t)std::lround(v)));
        return true;
    },
    [](const clap_plugin_t* p, clap_id id, const char* text, double* v) -> bool {
        auto s = P(p);
        if (id >= P_SYNC && id <= P_FM1) { *v = (text[0] == 'O' && text[1] == 'N') || text[0] == '1'; return true; }
        const Param* q = findParam(s, id);
        if (!q || !s->owner) return false;
        for (uint32_t x = q->min; x <= q->max; x++)
            if (!std::strcmp(prysm_fmt(q->index, x), text)) { *v = x; return true; }
        char* end;
        const double d = std::strtod(text, &end);
        if (end == text) return false;
        *v = std::min<double>(q->max, std::max<double>(q->min, d));
        return true;
    },
    [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
        auto s = P(p);
        for (uint32_t i = 0; i < in->size(in); i++) s->event(in->get(in, i));
        s->drain(out);
    }};

// the project's copy: "PRYS", 2, the patch format, its size (2 bytes), the patch, the plugin's 4 switches, then the
// patterns' format, their size (2 bytes), the patterns and song (version 1: without them)
const clap_plugin_state_t STATE = {
    [](const clap_plugin_t* p, const clap_ostream_t* st) -> bool {
        auto s = P(p);
        if (!s->owner) return false;
        const uint32_t n = prysm_patch_size();
        const uint32_t sn = prysm_seqs_size();
        std::vector<uint8_t> b{'P', 'R', 'Y', 'S', 2, (uint8_t)prysm_patch_version(), (uint8_t)(n >> 8), (uint8_t)n};
        b.insert(b.end(), prysm_patch(), prysm_patch() + n);
        for (auto& e : s->extra) b.push_back((uint8_t)e.load());
        b.push_back((uint8_t)prysm_seqs_version());
        b.push_back((uint8_t)(sn >> 8));
        b.push_back((uint8_t)sn);
        b.insert(b.end(), prysm_seqs(), prysm_seqs() + sn);
        for (size_t at = 0; at < b.size();) {
            const int64_t w = st->write(st, b.data() + at, b.size() - at);
            if (w <= 0) return false;
            at += (size_t)w;
        }
        return true;
    },
    [](const clap_plugin_t* p, const clap_istream_t* st) -> bool {
        auto s = P(p);
        std::vector<uint8_t> b;
        uint8_t buf[1024];
        for (int64_t r; (r = st->read(st, buf, sizeof buf)) > 0;) b.insert(b.end(), buf, buf + r);
        if (b.size() < 8 || std::memcmp(b.data(), "PRYS", 4) || (b[4] != 1 && b[4] != 2)) return false;
        const uint32_t n = (uint32_t)b[6] << 8 | b[7];
        if (b.size() < 8 + n || !s->owner) return false;
        for (int i = 0; i < 4 && 8 + n + i < b.size(); i++) s->setExtra(P_SYNC + i, b[8 + n + i]);
        {
            std::lock_guard<std::mutex> g(s->loadLock);
            s->loadPending.assign({b[5], 0, 0, 0});
            s->loadPending.insert(s->loadPending.end(), b.begin() + 8, b.begin() + 8 + n);
            s->seqsPending.clear();
            const size_t at = 8 + n + 4;        // (version 2: the patterns after the switches)
            if (b[4] >= 2 && b.size() >= at + 3) {
                const size_t sn = (size_t)b[at + 1] << 8 | b[at + 2];
                if (b.size() >= at + 3 + sn) {
                    s->seqsPending.push_back(b[at]);
                    s->seqsPending.insert(s->seqsPending.end(), b.begin() + at + 3, b.begin() + at + 3 + sn);
                }
            }
        }
        s->loadReady = true;
        if (!s->active) s->drain(nullptr);
        else if (s->hostParams) s->hostParams->request_flush(s->host);
        if (s->web && s->guiReady) {            // (the window: the whole sound again, once it is in)
            s->guiReady = false;
            s->js("prysmNative.reload()");
        }
        return true;
    }};

#if defined(__APPLE__)
constexpr const char* GUI_API = CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
constexpr const char* GUI_API = CLAP_WINDOW_API_WIN32;
#else
constexpr const char* GUI_API = CLAP_WINDOW_API_X11;
#endif

const clap_plugin_gui_t GUI = {
    [](const clap_plugin_t*, const char* api, bool floating) -> bool { return !floating && !std::strcmp(api, GUI_API); },
    [](const clap_plugin_t*, const char** api, bool* floating) -> bool { *api = GUI_API; *floating = false; return true; },
    [](const clap_plugin_t* p, const char* api, bool floating) -> bool { return !floating && !std::strcmp(api, GUI_API) && P(p)->guiCreate(); },
    [](const clap_plugin_t* p) { P(p)->guiDestroy(); },
    [](const clap_plugin_t*, double) -> bool { return false; },
    [](const clap_plugin_t* p, uint32_t* w, uint32_t* h) -> bool { *w = P(p)->width; *h = P(p)->height; return true; },
    [](const clap_plugin_t*) -> bool { return true; },
    [](const clap_plugin_t*, clap_gui_resize_hints_t* r) -> bool {
        r->can_resize_horizontally = r->can_resize_vertically = true;
        r->preserve_aspect_ratio = false;
        return true;
    },
    [](const clap_plugin_t*, uint32_t* w, uint32_t* h) -> bool {
        *w = std::max<uint32_t>(*w, 720); *h = std::max<uint32_t>(*h, 560);
        return true;
    },
    [](const clap_plugin_t* p, uint32_t w, uint32_t h) -> bool { P(p)->width = w; P(p)->height = h; P(p)->guiSize(); return true; },
    [](const clap_plugin_t* p, const clap_window_t* w) -> bool { return P(p)->guiParent(w); },
    [](const clap_plugin_t*, const clap_window_t*) -> bool { return false; },
    [](const clap_plugin_t*, const char*) {},
    [](const clap_plugin_t* p) -> bool {
#if defined(__APPLE__)
        if (P(p)->web) choc::objc::call<void>((id)P(p)->web->getViewHandle(), "setHidden:", (BOOL)false);
#elif defined(_WIN32)
        if (P(p)->web) ShowWindow((HWND)P(p)->web->getViewHandle(), SW_SHOW);
#endif
        return true;
    },
    [](const clap_plugin_t* p) -> bool {
#if defined(__APPLE__)
        if (P(p)->web) choc::objc::call<void>((id)P(p)->web->getViewHandle(), "setHidden:", (BOOL)true);
#elif defined(_WIN32)
        if (P(p)->web) ShowWindow((HWND)P(p)->web->getViewHandle(), SW_HIDE);
#endif
        return true;
    }};

const clap_plugin_timer_support_t TIMER = {[](const clap_plugin_t* p, clap_id) { P(p)->onTimer(); }};

// ---- the plugin
const clap_plugin_t* create(const clap_host_t* host)
{
    auto s = new Prysm();
    s->host = host;
    s->plugin.desc = &DESC;
    s->plugin.plugin_data = s;
    s->plugin.init = [](const clap_plugin_t* p) -> bool {
        auto s = P(p);
        void* none = nullptr;
        s->owner = g_owner.compare_exchange_strong(none, s);
        s->hostParams = (const clap_host_params_t*)s->host->get_extension(s->host, CLAP_EXT_PARAMS);
        s->hostTimer = (const clap_host_timer_support_t*)s->host->get_extension(s->host, CLAP_EXT_TIMER_SUPPORT);
        if (s->owner) {
            prysm_init();
            prysm_seq_play(0);
            const uint8_t* def = prysm_patch();
            for (uint32_t i = 0; i < prysm_nparams(); i++) {
                std::string f[8], info = prysm_param(i);
                for (int k = 0, at = 0; k < 8; k++) {
                    const size_t bar = info.find('|', at);
                    f[k] = info.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
                    at = bar == std::string::npos ? (int)info.size() : (int)bar + 1;
                }
                Param q;
                q.index = i;
                q.off = q.id = (uint32_t)std::stoul(f[4]);
                q.max = (uint32_t)std::stoul(f[5]);
                q.min = (uint32_t)std::stoul(f[6]);
                q.kind = (uint32_t)std::stoul(f[7]);
                q.name = f[0] + " " + (f[1] != f[0] ? f[1] + " " : "") + f[2];
                q.module = f[0] + "/" + f[1];
                q.def = def[q.off];
                s->params.push_back(q);
            }
            s->link = std::make_unique<Fm1Link>();
            s->link->onChange = [s](uint32_t o, uint8_t v) { std::lock_guard<std::mutex> g(s->inboxLock); s->inbox.emplace_back(o, v); };
            s->link->onSeqChange = [s](uint32_t o, uint8_t v) { std::lock_guard<std::mutex> g(s->inboxLock); s->sinbox.emplace_back(o, v); };
        }
        if (s->hostTimer) s->hostTimer->register_timer(s->host, 33, &s->timer);
        return true;
    };
    s->plugin.destroy = [](const clap_plugin_t* p) {
        auto s = P(p);
        if (s->hostTimer && s->timer != CLAP_INVALID_ID) s->hostTimer->unregister_timer(s->host, s->timer);
        s->guiDestroy();
        s->link.reset();
        if (s->owner) { prysm_all_off(); g_owner = nullptr; }
        delete s;
    };
    s->plugin.activate = [](const clap_plugin_t* p, double rate, uint32_t, uint32_t) -> bool {
        auto s = P(p);
        s->rate = rate;
        s->srcPos = (double)s->srcW - 3 > 1 ? (double)s->srcW - 3 : 1;
        s->active = true;
        return true;
    };
    s->plugin.deactivate = [](const clap_plugin_t* p) { P(p)->active = false; };
    s->plugin.start_processing = [](const clap_plugin_t*) -> bool { return true; };
    s->plugin.stop_processing = [](const clap_plugin_t*) {};
    s->plugin.reset = [](const clap_plugin_t* p) { if (P(p)->owner) prysm_all_off(); };
    s->plugin.process = [](const clap_plugin_t* p, const clap_process_t* pr) { return P(p)->process(pr); };
    s->plugin.get_extension = [](const clap_plugin_t*, const char* id) -> const void* {
        if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
        if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
        if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS;
        if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
        if (!std::strcmp(id, CLAP_EXT_GUI)) return &GUI;
        if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &TIMER;
        return nullptr;
    };
    s->plugin.on_main_thread = [](const clap_plugin_t*) {};
    return &s->plugin;
}

const clap_plugin_factory_t FACTORY = {
    [](const clap_plugin_factory_t*) -> uint32_t { return 1; },
    [](const clap_plugin_factory_t*, uint32_t i) -> const clap_plugin_descriptor_t* { return i == 0 ? &DESC : nullptr; },
    [](const clap_plugin_factory_t*, const clap_host_t* host, const char* id) -> const clap_plugin_t* {
        return clap_version_is_compatible(host->clap_version) && !std::strcmp(id, ID) ? create(host) : nullptr;
    }};

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    [](const char*) -> bool { return true; },
    [] {},
    [](const char* id) -> const void* { return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &FACTORY : nullptr; }};
