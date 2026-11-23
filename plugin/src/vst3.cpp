// SPDX-License-Identifier: GPL-3.0-only
// Prysm Patch as a VST3 instrument: a lightweight patch manager for the M-VAVE FM-1. It holds a sound (patch_t) and
// its sequencer patterns / drum kit (seqs_t) inside the DAW project, with two manual buttons in its window:
//   - Patch Get:  pulls the current sound and patterns from the connected FM-1 into the plugin (and DAW project)
//   - Patch Send: sends the plugin's stored sound and patterns back to the connected FM-1 on demand (never automatically)
// MIDI notes and pitch bend received by the plugin are forwarded to the connected FM-1 over USB-MIDI.
#define INIT_CLASS_IID
#include "pluginterfaces/base/conststringtable.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include "pluginterfaces/vst/vsttypes.h"

#include "choc/gui/choc_MessageLoop.h"
#include "choc/gui/choc_WebView.h"
#include "prysm_core.h"
#include <RtMidi.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace std::chrono_literals;

namespace {

static const FUID PLUGIN_CID(0x50525953, 0x56535433, 0x50544348, 0x30303031);

enum : ParamID { P_GET = 1, P_SEND = 2 };

static bool isFm1(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return name.find("fm-1") != std::string::npos || name.find("prysm") != std::string::npos ||
           name.find("felucca") != std::string::npos;
}

static std::string nowStamp()
{
    std::time_t t = std::time(nullptr);
    char buf[32]{};
    struct tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::strftime(buf, sizeof buf, "%H:%M:%S", &tmBuf);
    return buf;
}

// ---- On-demand FM-1 USB-MIDI link (only pulls on Get or pushes on Send; never syncs automatically)
class PatchLink {
public:
    std::function<void(const std::vector<uint8_t>& patch, uint32_t pver, int slot,
                       const std::vector<uint8_t>& seqs, uint32_t sver)> onGot;

    PatchLink() { thread = std::thread([this] { run(); }); }
    ~PatchLink()
    {
        quit = true;
        wake.notify_all();
        if (thread.joinable()) thread.join();
        close();
    }

    std::string status()
    {
        std::lock_guard<std::mutex> g(lock);
        return state;
    }

    bool isConnected()
    {
        std::lock_guard<std::mutex> g(lock);
        return connected;
    }

    void requestGet()
    {
        std::lock_guard<std::mutex> g(lock);
        wantGet = true;
        wake.notify_all();
    }

    void requestSend(const std::vector<uint8_t>& patch, const std::vector<uint8_t>& seqs)
    {
        std::lock_guard<std::mutex> g(lock);
        sendPatch = patch;
        sendSeqs = seqs;
        wantSend = true;
        wake.notify_all();
    }

    void note(uint8_t st, uint8_t d1, uint8_t d2)
    {
        unsigned w = nqW.load(std::memory_order_relaxed);
        if (w - nqR.load(std::memory_order_acquire) >= NQ) return;
        nq[w % NQ][0] = st; nq[w % NQ][1] = d1; nq[w % NQ][2] = d2;
        nqW.store(w + 1, std::memory_order_release);
    }

private:
    bool open()
    {
        try {
            auto i = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "PrysmPatch");
            auto o = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "PrysmPatch");
            int pi = -1, po = -1;
            for (unsigned k = 0; k < i->getPortCount() && pi < 0; k++) if (isFm1(i->getPortName(k))) pi = (int)k;
            for (unsigned k = 0; k < o->getPortCount() && po < 0; k++) if (isFm1(o->getPortName(k))) po = (int)k;
            if (pi < 0 || po < 0) {
                std::lock_guard<std::mutex> g(lock);
                state = "FM-1: not found (connect it by USB)";
                return false;
            }
            i->ignoreTypes(false, true, true);
            i->setCallback(&PatchLink::midiIn, this);
            i->openPort((unsigned)pi, "Prysm Patch");
            o->openPort((unsigned)po, "Prysm Patch");
            in = std::move(i);
            out = std::move(o);
            return true;
        } catch (...) {
            std::lock_guard<std::mutex> g(lock);
            state = "FM-1: port is busy (close other apps or disable FM-1 in DAW MIDI settings)";
            return false;
        }
    }

    void close()
    {
        try {
            if (in) in->closePort();
            if (out) out->closePort();
        } catch (...) {}
        in.reset();
        out.reset();
        std::lock_guard<std::mutex> g(lock);
        connected = false;
        dump.clear();
        sdump.clear();
    }

    void sendSysex(char type, const std::vector<uint8_t>& data)
    {
        std::vector<unsigned char> m{0xF0, 0x7D, 0x50, (unsigned char)type};
        m.insert(m.end(), data.begin(), data.end());
        m.push_back(0xF7);
        if (out) out->sendMessage(&m);
    }

    int ask(char type, const std::vector<uint8_t>& data)
    {
        for (int tries = 0; tries < 3 && !quit; tries++) {
            std::unique_lock<std::mutex> g(lock);
            answered = -2;
            awaiting = type;
            g.unlock();
            sendSysex(type, data);
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

    bool waitDump(std::unique_lock<std::mutex>& g)
    {
        for (int tries = 0; tries < 3 && !quit; tries++) {
            dumpDone = false;
            dump.clear();
            sdump.clear();
            g.unlock();
            sendSysex('D', {});
            g.lock();
            if (wake.wait_for(g, 800ms, [this] { return dumpDone || quit; }))
                return !quit && dumpDone;
        }
        return false;
    }

    static void midiIn(double, std::vector<unsigned char>* m, void* self)
    {
        static_cast<PatchLink*>(self)->receive(*m);
    }

    void receive(const std::vector<unsigned char>& m)
    {
        if (m.size() < 5 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 0x50) return;
        const char t = (char)m[3];
        const unsigned char* b = m.data() + 4;
        const size_t n = m.size() - 5;
        std::unique_lock<std::mutex> g(lock);
        if (t == 'A' && n >= 3 && awaiting && b[0] == (unsigned char)awaiting) {
            answered = b[2];
            wake.notify_all();
        } else if (t == 'E' && n >= 129) {
            if (dump.size() < (b[0] + 1u) * 64) dump.resize((b[0] + 1u) * 64);
            for (int j = 0; j < 64; j++) dump[b[0] * 64 + j] = (uint8_t)(b[1 + 2 * j] << 4 | b[2 + 2 * j]);
        } else if (t == 'Q' && n >= 129) {
            if (sdump.size() < (b[0] + 1u) * 64) sdump.resize((b[0] + 1u) * 64);
            for (int j = 0; j < 64; j++) sdump[b[0] * 64 + j] = (uint8_t)(b[1 + 2 * j] << 4 | b[2 + 2 * j]);
        } else if (t == 'F' && n >= 4) {
            dumpPatchSize = (size_t)b[0] << 7 | b[1];
            dumpPatchVer = b[2];
            dumpSlot = b[3];
            dumpSeqsSize = n >= 7 ? ((size_t)b[4] << 7 | b[5]) : 0;
            dumpSeqsVer = n >= 7 ? b[6] : 0;
            dumpDone = true;
            wake.notify_all();
        }
    }

    void run()
    {
        auto lastTry = std::chrono::steady_clock::now() - 10s, lastCheck = lastTry;
        while (!quit) {
            if (!connected) {
                nqR.store(nqW.load(std::memory_order_acquire), std::memory_order_release);
                {
                    std::lock_guard<std::mutex> g(lock);
                    if (wantGet || wantSend) {
                        wantGet = wantSend = false;
                        state = "FM-1: not connected (connect it by USB)";
                    }
                }
                if (std::chrono::steady_clock::now() - lastTry < 2s) {
                    std::unique_lock<std::mutex> g(lock);
                    wake.wait_for(g, 50ms, [this] { return quit.load(); });
                    continue;
                }
                lastTry = std::chrono::steady_clock::now();
                if (!open()) continue;
                {
                    std::lock_guard<std::mutex> g(lock);
                    connected = true;
                    state = "FM-1: connected (ready)";
                }
                lastCheck = std::chrono::steady_clock::now();
                continue;
            }
            if (std::chrono::steady_clock::now() - lastCheck > 2s) {
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
            if (wantGet) {
                wantGet = false;
                state = "FM-1: pulling patch...";
                if (!waitDump(g)) {
                    state = "FM-1: no answer to Patch Get (check USB / Prysm firmware)";
                    continue;
                }
                if (dump.size() < dumpPatchSize) {
                    state = "FM-1: incomplete dump received";
                    continue;
                }
                std::vector<uint8_t> gotP(dump.begin(), dump.begin() + dumpPatchSize);
                std::vector<uint8_t> gotS;
                if (dumpSeqsSize && sdump.size() >= dumpSeqsSize)
                    gotS.assign(sdump.begin(), sdump.begin() + dumpSeqsSize);
                const uint32_t pv = dumpPatchVer, sv = dumpSeqsVer;
                const int sl = dumpSlot;
                auto cb = onGot;
                state = "FM-1: Patch Get complete (" + nowStamp() + (sl ? ", slot " + std::to_string(sl) : "") + ")";
                g.unlock();
                if (cb) cb(gotP, pv, sl, gotS, sv);
                continue;
            }
            if (wantSend) {
                wantSend = false;
                auto pt = std::move(sendPatch);
                auto sq = std::move(sendSeqs);
                state = "FM-1: reading current state before send...";
                if (!waitDump(g)) {
                    state = "FM-1: no answer to Patch Send (check USB / Prysm firmware)";
                    continue;
                }
                std::vector<uint8_t> curP = dump;
                std::vector<uint8_t> curS = sdump;
                const size_t fm1PSize = dumpPatchSize, fm1SSize = dumpSeqsSize;
                state = "FM-1: sending patch...";
                g.unlock();

                bool ok = true;
                size_t sent = 0;
                const size_t pn = std::min(pt.size(), fm1PSize ? fm1PSize : pt.size());
                for (size_t o = 0; o < pn && ok; o++) {
                    if (o >= curP.size() || pt[o] != curP[o]) {
                        const uint8_t v = pt[o];
                        if (ask('V', {(uint8_t)(o >> 7), (uint8_t)(o & 127), (uint8_t)(v >> 7), (uint8_t)(v & 127)}) < 0)
                            ok = false;
                        else
                            sent++;
                    }
                }
                const size_t sn = std::min(sq.size(), fm1SSize ? fm1SSize : sq.size());
                for (size_t o = 0; o < sn && ok; o++) {
                    if (o >= curS.size() || sq[o] != curS[o]) {
                        const uint8_t v = sq[o];
                        if (ask('U', {(uint8_t)(o >> 7), (uint8_t)(o & 127), (uint8_t)(v >> 7), (uint8_t)(v & 127)}) < 0)
                            ok = false;
                        else
                            sent++;
                    }
                }
                g.lock();
                if (ok)
                    state = "FM-1: Patch Send complete (" + std::to_string(sent) + " changes, " + nowStamp() + ")";
                else
                    state = "FM-1: Patch Send failed (connection lost)";
                continue;
            }
            if (!busy) wake.wait_for(g, 10ms);
        }
    }

    std::unique_ptr<RtMidiIn> in;
    std::unique_ptr<RtMidiOut> out;
    std::thread thread;
    std::atomic<bool> quit{false};
    std::mutex lock;
    std::condition_variable wake;
    std::vector<uint8_t> dump, sdump, sendPatch, sendSeqs;
    size_t dumpPatchSize = 0, dumpSeqsSize = 0;
    uint32_t dumpPatchVer = 0, dumpSeqsVer = 0;
    int dumpSlot = 0;
    bool dumpDone = false, wantGet = false, wantSend = false, connected = false;
    int answered = -2;
    char awaiting = 0;
    std::string state = "FM-1: looking for it";
    static constexpr unsigned NQ = 256;
    uint8_t nq[NQ][3]{};
    std::atomic<unsigned> nqW{0}, nqR{0};
};

// ---- HTML UI for the Patch Manager window
static const char* PATCH_HTML = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<style>
  :root { --bg: #080808; --fg: #fff; --mid: #969696; --faint: #484848; --box: #2e2e2e; --acc: #f5a623; --teal: #2cbeb0;
          color-scheme: dark; }
  * { box-sizing: border-box; user-select: none; }
  body { margin: 0; padding: 20px 24px; background: var(--bg); color: var(--fg);
         font: 13px/1.45 ui-monospace, "SF Mono", Menlo, Consolas, monospace;
         display: flex; flex-direction: column; height: 100vh; justify-content: space-between; }
  header { display: flex; align-items: baseline; justify-content: space-between; border-bottom: 1px solid var(--box);
           padding-bottom: 12px; }
  .brand { display: flex; align-items: baseline; gap: 12px; }
  h1 { margin: 0; font-size: 20px; letter-spacing: .28em; font-weight: 700; }
  .sub { color: var(--mid); font-size: 12px; letter-spacing: .08em; }
  .badge { font-size: 11px; padding: 2px 8px; border: 1px solid var(--box); color: var(--mid); }
  .badge.ok { border-color: var(--teal); color: var(--teal); }
  .actions { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; margin: 18px 0; }
  button { font: inherit; color: var(--fg); background: #121212; border: 1px solid var(--box);
           padding: 18px 16px; cursor: pointer; display: flex; flex-direction: column; align-items: center;
           gap: 6px; transition: border-color .12s, background .12s, transform .06s; }
  button:hover { border-color: var(--fg); background: #1a1a1a; }
  button:active { transform: translateY(1px); background: var(--fg); color: var(--bg); }
  button:active .hint { color: var(--bg); }
  .btn-title { font-size: 15px; font-weight: 700; letter-spacing: .16em; }
  .hint { font-size: 11px; color: var(--mid); }
  .info { border: 1px solid var(--box); background: #0d0d0d; padding: 12px 14px; display: flex;
          flex-direction: column; gap: 6px; }
  .row { display: flex; justify-content: space-between; gap: 12px; font-size: 12px; }
  .lbl { color: var(--mid); }
  .val { color: var(--fg); text-align: right; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  footer { display: flex; justify-content: space-between; align-items: center; color: var(--mid); font-size: 12px;
           border-top: 1px solid var(--box); padding-top: 10px; }
  #status { color: var(--fg); }
</style>
</head>
<body>
  <header>
    <div class="brand">
      <h1>PRYSM</h1>
      <span class="sub">VST3 PATCH LINK</span>
    </div>
    <span id="conn" class="badge">USB-MIDI</span>
  </header>

  <div class="actions">
    <button id="get" title="Pull the current sound and sequencer patterns from the FM-1 into this DAW project">
      <span class="btn-title">PATCH GET</span>
      <span class="hint">Pull from FM-1 &rarr; DAW</span>
    </button>
    <button id="send" title="Send this DAW project's sound and sequencer patterns to the FM-1 now">
      <span class="btn-title">PATCH SEND</span>
      <span class="hint">Push DAW &rarr; FM-1</span>
    </button>
  </div>

  <div class="info">
    <div class="row">
      <span class="lbl">PROJECT PATCH</span>
      <span class="val" id="summary">Init sound</span>
    </div>
    <div class="row">
      <span class="lbl">LAST ACTION</span>
      <span class="val" id="meta">Default (saved with DAW project)</span>
    </div>
  </div>

  <footer>
    <span id="status">FM-1: looking for it</span>
    <span>Manual transfer only</span>
  </footer>

<script>
"use strict";
const $ = (id) => document.getElementById(id);
function apply(r) {
  if (r.status !== undefined) $("status").textContent = r.status;
  if (r.connected !== undefined) {
    $("conn").textContent = r.connected ? "FM-1 READY" : "NO FM-1";
    $("conn").className = r.connected ? "badge ok" : "badge";
  }
  if (r.summary !== undefined) $("summary").textContent = r.summary;
  if (r.meta !== undefined) $("meta").textContent = r.meta;
}
window.prysmPatchUI = { update: apply };
$("get").onclick = async () => apply(JSON.parse(await prysm_patch_get()));
$("send").onclick = async () => apply(JSON.parse(await prysm_patch_send()));
prysm_patch_init().then((s) => apply(JSON.parse(s)));
</script>
</body>
</html>
)HTML";

class PrysmPatchPlugin;

// ---- IPlugView wrapping CHOC WebView
class PrysmPatchView : public IPlugView {
public:
    explicit PrysmPatchView(PrysmPatchPlugin* p) : plugin(p) {}
    virtual ~PrysmPatchView() { destroy(); }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override
    {
        if (!obj) return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) || FUnknownPrivate::iidEqual(_iid, IPlugView::iid)) {
            addRef();
            *obj = static_cast<IPlugView*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refCount; }
    uint32 PLUGIN_API release() override
    {
        uint32 r = --refCount;
        if (r == 0) delete this;
        return r;
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
#if defined(__APPLE__)
        return std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
#elif defined(_WIN32)
        return std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
#else
        return std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0 ? kResultTrue : kResultFalse;
#endif
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) override;
    tresult PLUGIN_API removed() override
    {
        destroy();
        return kResultOk;
    }
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect* size) override
    {
        if (!size) return kInvalidArgument;
        *size = ViewRect(0, 0, (int32)width, (int32)height);
        return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect* newSize) override
    {
        if (!newSize) return kInvalidArgument;
        width = (uint32_t)std::max<int32>(420, newSize->getWidth());
        height = (uint32_t)std::max<int32>(260, newSize->getHeight());
        resize();
        return kResultOk;
    }
    tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame* f) override { frame = f; return kResultOk; }
    tresult PLUGIN_API canResize() override { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) override
    {
        if (!rect) return kInvalidArgument;
        rect->right = rect->left + std::max<int32>(420, rect->getWidth());
        rect->bottom = rect->top + std::max<int32>(260, rect->getHeight());
        return kResultOk;
    }

    void eval(const std::string& js)
    {
        if (web) web->evaluateJavascript("window.prysmPatchUI && " + js);
    }

private:
    void destroy();
    void resize();

    std::atomic<uint32> refCount{1};
    PrysmPatchPlugin* plugin = nullptr;
    IPlugFrame* frame = nullptr;
    std::unique_ptr<choc::ui::WebView> web;
    choc::messageloop::Timer timer;
    uint32_t width = 520, height = 300;
#if defined(__linux__)
    GtkWidget* plug = nullptr;
#endif
};

// ---- Single-component VST3 plugin (IComponent + IAudioProcessor + IEditController)
class PrysmPatchPlugin : public IComponent, public IAudioProcessor, public IEditController {
public:
    PrysmPatchPlugin()
    {
        patch.assign(prysm_patch_init(), prysm_patch_init() + prysm_patch_size());
        seqs.resize(prysm_seqs_size());
        prysm_seqs_blank(seqs.data());
    }

    virtual ~PrysmPatchPlugin() { link.reset(); }

    // ---- FUnknown
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override
    {
        if (!obj) return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual(_iid, IPluginBase::iid) ||
            FUnknownPrivate::iidEqual(_iid, IComponent::iid)) {
            addRef();
            *obj = static_cast<IComponent*>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(_iid, IAudioProcessor::iid)) {
            addRef();
            *obj = static_cast<IAudioProcessor*>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(_iid, IEditController::iid)) {
            addRef();
            *obj = static_cast<IEditController*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refCount; }
    uint32 PLUGIN_API release() override
    {
        uint32 r = --refCount;
        if (r == 0) delete this;
        return r;
    }

    // ---- IPluginBase
    tresult PLUGIN_API initialize(FUnknown*) override
    {
        if (link) return kResultOk;
        choc::messageloop::initialise();
        link = std::make_unique<PatchLink>();
        link->onGot = [this](const std::vector<uint8_t>& p, uint32_t pv, int slot,
                             const std::vector<uint8_t>& s, uint32_t sv) {
            std::vector<uint8_t> np(prysm_patch_size());
            if (prysm_patch_convert(np.data(), p.data(), (uint32_t)p.size(), pv) != 0) {
                std::lock_guard<std::mutex> g(stateLock);
                meta = "Incompatible patch format from FM-1 (v" + std::to_string(pv) + ")";
                uiDirty = true;
                return;
            }
            std::vector<uint8_t> ns(prysm_seqs_size());
            if (!s.empty() && prysm_seqs_convert(ns.data(), s.data(), (uint32_t)s.size(), sv) == 0) {
                // converted or copied
            } else {
                std::lock_guard<std::mutex> g(stateLock);
                ns = seqs;
            }
            {
                std::lock_guard<std::mutex> g(stateLock);
                patch = std::move(np);
                seqs = std::move(ns);
                meta = "Pulled from FM-1 at " + nowStamp() + (slot ? " (slot " + std::to_string(slot) + ")" : "");
                uiDirty = true;
                markDirty = true;
            }
        };
        return kResultOk;
    }

    tresult PLUGIN_API terminate() override
    {
        link.reset();
        handler = nullptr;
        return kResultOk;
    }

    // ---- IComponent
    tresult PLUGIN_API getControllerClassId(TUID) override { return kNoInterface; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }

    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override
    {
        if (type == kAudio && dir == kOutput) return 1;
        if (type == kEvent && dir == kInput) return 1;
        return 0;
    }

    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override
    {
        if (index != 0) return kInvalidArgument;
        std::memset(&bus, 0, sizeof bus);
        if (type == kAudio && dir == kOutput) {
            bus.mediaType = kAudio;
            bus.direction = kOutput;
            bus.channelCount = 2;
            UString(bus.name, 128).fromAscii("Output");
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }
        if (type == kEvent && dir == kInput) {
            bus.mediaType = kEvent;
            bus.direction = kInput;
            bus.channelCount = 16;
            UString(bus.name, 128).fromAscii("MIDI In");
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }
        return kInvalidArgument;
    }

    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool) override { return kResultOk; }

    // Binary state format (compatible with Prysm CLAP state "PRYS" v2):
    //   "PRYS", 2, patch_version, patch_size_hi, patch_size_lo, patch[patch_size],
    //   4 switch bytes, seqs_version, seqs_size_hi, seqs_size_lo, seqs[seqs_size]
    tresult PLUGIN_API setState(IBStream* st) override
    {
        if (!st) return kInvalidArgument;
        std::vector<uint8_t> b;
        uint8_t buf[1024];
        for (;;) {
            int32 r = 0;
            if (st->read(buf, sizeof buf, &r) != kResultOk || r <= 0) break;
            b.insert(b.end(), buf, buf + r);
        }
        if (b.size() < 8 || std::memcmp(b.data(), "PRYS", 4) != 0 || (b[4] != 1 && b[4] != 2))
            return kResultFalse;
        const uint32_t n = (uint32_t)b[6] << 8 | b[7];
        if (b.size() < 8 + n) return kResultFalse;

        std::vector<uint8_t> np(prysm_patch_size());
        if (prysm_patch_convert(np.data(), b.data() + 8, n, b[5]) != 0)
            return kResultFalse;

        std::vector<uint8_t> ns(prysm_seqs_size());
        prysm_seqs_blank(ns.data());
        const size_t at = 8 + n + 4;
        if (b[4] >= 2 && b.size() >= at + 3) {
            const uint32_t sn = (uint32_t)b[at + 1] << 8 | b[at + 2];
            if (b.size() >= at + 3 + sn)
                prysm_seqs_convert(ns.data(), b.data() + at + 3, sn, b[at]);
        }

        {
            std::lock_guard<std::mutex> g(stateLock);
            patch = std::move(np);
            seqs = std::move(ns);
            meta = "Loaded from DAW project (" + nowStamp() + ")";
            uiDirty = true;
        }
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* st) override
    {
        if (!st) return kInvalidArgument;
        std::vector<uint8_t> pt, sq;
        {
            std::lock_guard<std::mutex> g(stateLock);
            pt = patch;
            sq = seqs;
        }
        const uint32_t n = (uint32_t)pt.size();
        const uint32_t sn = (uint32_t)sq.size();
        std::vector<uint8_t> b{'P', 'R', 'Y', 'S', 2, (uint8_t)prysm_patch_version(), (uint8_t)(n >> 8), (uint8_t)n};
        b.insert(b.end(), pt.begin(), pt.end());
        b.insert(b.end(), {1, 0, 1, 0});
        b.push_back((uint8_t)prysm_seqs_version());
        b.push_back((uint8_t)(sn >> 8));
        b.push_back((uint8_t)sn);
        b.insert(b.end(), sq.begin(), sq.end());

        int32 written = 0;
        return st->write(b.data(), (int32)b.size(), &written) == kResultOk && written == (int32)b.size()
                   ? kResultOk
                   : kResultFalse;
    }

    // ---- IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32 numIns, SpeakerArrangement* outputs, int32 numOuts) override
    {
        if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo) return kResultTrue;
        return kResultFalse;
    }

    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement& arr) override
    {
        if (dir == kOutput && index == 0) { arr = SpeakerArr::kStereo; return kResultOk; }
        return kInvalidArgument;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override
    {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }

    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup&) override { return kResultOk; }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    uint32 PLUGIN_API getTailSamples() override { return kNoTail; }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.inputParameterChanges) {
            const int32 np = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < np; i++) {
                if (auto* q = data.inputParameterChanges->getParameterData(i)) {
                    const int32 npts = q->getPointCount();
                    if (npts > 0) {
                        int32 ofs = 0;
                        ParamValue val = 0;
                        if (q->getPoint(npts - 1, ofs, val) == kResultOk && val >= 0.5) {
                            if (q->getParameterId() == P_GET) triggerGet();
                            else if (q->getParameterId() == P_SEND) triggerSend();
                        }
                    }
                }
            }
        }
        if (data.inputEvents && link) {
            const int32 ne = data.inputEvents->getEventCount();
            for (int32 i = 0; i < ne; i++) {
                Event e{};
                if (data.inputEvents->getEvent(i, e) != kResultOk) continue;
                const uint8_t ch = (uint8_t)(e.noteOn.channel & 15);
                if (e.type == Event::kNoteOnEvent && e.noteOn.pitch >= 0 && e.noteOn.pitch <= 127) {
                    const int vel = std::max(1, std::min(127, (int)std::lround(e.noteOn.velocity * 127.0f)));
                    link->note((uint8_t)(0x90 | ch), (uint8_t)e.noteOn.pitch, (uint8_t)vel);
                } else if (e.type == Event::kNoteOffEvent && e.noteOff.pitch >= 0 && e.noteOff.pitch <= 127) {
                    link->note((uint8_t)(0x80 | ch), (uint8_t)e.noteOff.pitch, 0);
                }
            }
        }
        for (int32 b = 0; b < data.numOutputs; b++) {
            for (int32 c = 0; c < data.outputs[b].numChannels; c++)
                if (data.outputs[b].channelBuffers32[c] && data.numSamples > 0)
                    std::memset(data.outputs[b].channelBuffers32[c], 0, (size_t)data.numSamples * sizeof(float));
            data.outputs[b].silenceFlags = (1ULL << data.outputs[b].numChannels) - 1ULL;
        }
        return kResultOk;
    }

    // ---- IEditController
    tresult PLUGIN_API setComponentState(IBStream*) override { return kResultOk; }

    int32 PLUGIN_API getParameterCount() override { return 2; }

    tresult PLUGIN_API getParameterInfo(int32 idx, ParameterInfo& info) override
    {
        if (idx < 0 || idx >= 2) return kInvalidArgument;
        std::memset(&info, 0, sizeof info);
        info.id = idx == 0 ? P_GET : P_SEND;
        UString(info.title, 128).fromAscii(idx == 0 ? "Patch Get" : "Patch Send");
        UString(info.shortTitle, 128).fromAscii(idx == 0 ? "Get" : "Send");
        info.stepCount = 1;
        info.defaultNormalizedValue = 0;
        info.unitId = kRootUnitId;
        info.flags = ParameterInfo::kCanAutomate;
        return kResultOk;
    }

    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue v, String128 str) override
    {
        UString(str, 128).fromAscii(v >= 0.5 ? "Trigger" : "Idle");
        return kResultOk;
    }

    tresult PLUGIN_API getParamValueByString(ParamID, TChar*, ParamValue& v) override
    {
        v = 0;
        return kResultOk;
    }

    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API getParamNormalized(ParamID) override { return 0; }

    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override
    {
        if (v >= 0.5) {
            if (id == P_GET) triggerGet();
            else if (id == P_SEND) triggerSend();
        }
        return kResultOk;
    }

    tresult PLUGIN_API setComponentHandler(IComponentHandler* h) override
    {
        handler = h;
        return kResultOk;
    }

    IPlugView* PLUGIN_API createView(FIDString name) override
    {
        if (name && std::strcmp(name, ViewType::kEditor) == 0)
            return new PrysmPatchView(this);
        return nullptr;
    }

    // ---- Actions & UI state
    void triggerGet()
    {
        if (link) link->requestGet();
    }

    void triggerSend()
    {
        if (!link) return;
        std::vector<uint8_t> pt, sq;
        {
            std::lock_guard<std::mutex> g(stateLock);
            pt = patch;
            sq = seqs;
            meta = "Sent to FM-1 at " + nowStamp();
            uiDirty = true;
        }
        link->requestSend(pt, sq);
    }

    std::string patchSummaryLocked() const
    {
        static const char* const BANKS[] = {"BASIC", "HARM", "PULSE", "VOCAL", "DIGI", "CMPLX"};
        static const char* const VMODES[] = {"SINGLE", "DUAL", "SPLIT", "LAYER"};
        static const char* const FLTS[] = {"LP 12", "LP 24", "COMB+", "COMB-"};
        if (patch.size() < 200) return "Loaded";
        const uint8_t b0 = patch[0] < 6 ? patch[0] : 0;
        const uint8_t b1 = patch[15] < 6 ? patch[15] : 0;
        const uint8_t cut = patch[30];
        const uint8_t res = patch[31];
        const uint8_t flt = patch[102] < 4 ? patch[102] : 0;
        const uint8_t vm = patch[169] < 4 ? patch[169] : 0;
        return std::string(VMODES[vm]) + " · OSC1 " + BANKS[b0] + " / OSC2 " + BANKS[b1] +
               " · " + FLTS[flt] + " CUT " + std::to_string(cut) + " RES " + std::to_string(res);
    }

    std::string stateJson()
    {
        std::lock_guard<std::mutex> g(stateLock);
        const std::string st = link ? link->status() : "FM-1: not initialized";
        const bool conn = link && link->isConnected();
        return "{\"status\":" + choc::json::getEscapedQuotedString(st) +
               ",\"connected\":" + (conn ? "true" : "false") +
               ",\"summary\":" + choc::json::getEscapedQuotedString(patchSummaryLocked()) +
               ",\"meta\":" + choc::json::getEscapedQuotedString(meta) + "}";
    }

    void pollUI(PrysmPatchView* view)
    {
        if (markDirty.exchange(false) && handler) {
            FUnknownPtr<IComponentHandler2> h2(handler);
            if (h2) h2->setDirty(true);
            else {
                handler->beginEdit(P_GET);
                handler->performEdit(P_GET, 0.0);
                handler->endEdit(P_GET);
            }
        }
        const std::string st = link ? link->status() : "";
        const bool conn = link && link->isConnected();
        if (uiDirty.exchange(false) || st != lastStatus || conn != lastConn) {
            lastStatus = st;
            lastConn = conn;
            if (view) view->eval("prysmPatchUI.update(" + stateJson() + ")");
        }
    }

private:
    std::atomic<uint32> refCount{1};
    IComponentHandler* handler = nullptr;
    std::unique_ptr<PatchLink> link;
    std::mutex stateLock;
    std::vector<uint8_t> patch, seqs;
    std::string meta = "Default (saved with DAW project)";
    std::string lastStatus;
    bool lastConn = false;
    std::atomic<bool> uiDirty{true}, markDirty{false};
};

tresult PLUGIN_API PrysmPatchView::attached(void* parent, FIDString type)
{
    if (isPlatformTypeSupported(type) != kResultTrue) return kInvalidArgument;
    choc::ui::WebView::Options o;
    o.acceptsFirstMouseClick = true;
    web = std::make_unique<choc::ui::WebView>(o);
    if (!web->loadedOK()) { web.reset(); return kInternalError; }

    web->bind("prysm_patch_init", [this](const choc::value::ValueView&) -> choc::value::Value {
        return choc::value::createString(plugin->stateJson());
    });
    web->bind("prysm_patch_get", [this](const choc::value::ValueView&) -> choc::value::Value {
        plugin->triggerGet();
        return choc::value::createString(plugin->stateJson());
    });
    web->bind("prysm_patch_send", [this](const choc::value::ValueView&) -> choc::value::Value {
        plugin->triggerSend();
        return choc::value::createString(plugin->stateJson());
    });
    web->setHTML(PATCH_HTML);

#if defined(__APPLE__)
    auto v = (id)web->getViewHandle();
    choc::objc::call<void>(v, "setFrame:", choc::objc::CGRect{{0, 0}, {(choc::objc::CGFloat)width, (choc::objc::CGFloat)height}});
    choc::objc::call<void>((id)parent, "addSubview:", v);
#elif defined(_WIN32)
    auto v = (HWND)web->getViewHandle();
    SetWindowLongPtr(v, GWL_STYLE, (GetWindowLongPtr(v, GWL_STYLE) & ~WS_POPUP) | WS_CHILD);
    SetParent(v, (HWND)parent);
    SetWindowPos(v, nullptr, 0, 0, (int)width, (int)height, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(v, SW_SHOW);
#elif defined(__linux__)
    plug = gtk_plug_new((Window)(uintptr_t)parent);
    gtk_container_add(GTK_CONTAINER(plug), (GtkWidget*)web->getViewHandle());
    gtk_window_resize(GTK_WINDOW(plug), (int)width, (int)height);
    gtk_widget_show_all(plug);
#endif

    timer = choc::messageloop::Timer(50, [this]() {
#if defined(__linux__)
        while (gtk_events_pending()) gtk_main_iteration_do(false);
#endif
        if (plugin) plugin->pollUI(this);
        return true;
    });
    return kResultOk;
}

void PrysmPatchView::destroy()
{
    timer.clear();
    if (!web) return;
#if defined(__APPLE__)
    choc::objc::call<void>((id)web->getViewHandle(), "removeFromSuperview");
#elif defined(_WIN32)
    ShowWindow((HWND)web->getViewHandle(), SW_HIDE);
    SetParent((HWND)web->getViewHandle(), nullptr);
#elif defined(__linux__)
    if (plug) {
        gtk_container_remove(GTK_CONTAINER(plug), (GtkWidget*)web->getViewHandle());
        gtk_widget_destroy(plug);
        plug = nullptr;
    }
#endif
    web.reset();
}

void PrysmPatchView::resize()
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

// ---- VST3 Factory
class PrysmPatchFactory : public IPluginFactory3 {
public:
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override
    {
        if (!obj) return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual(_iid, IPluginFactory::iid) ||
            FUnknownPrivate::iidEqual(_iid, IPluginFactory2::iid) ||
            FUnknownPrivate::iidEqual(_iid, IPluginFactory3::iid)) {
            addRef();
            *obj = static_cast<IPluginFactory3*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
    {
        if (!info) return kInvalidArgument;
        *info = PFactoryInfo("Nicholas Pratt", "https://github.com/NicholasPratt/Prysm",
                             "https://github.com/NicholasPratt/Prysm/issues", PFactoryInfo::kUnicode);
        return kResultOk;
    }

    int32 PLUGIN_API countClasses() override { return 1; }

    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override
    {
        if (index != 0 || !info) return kInvalidArgument;
        *info = PClassInfo(PLUGIN_CID.toTUID(), PClassInfo::kManyInstances, kVstAudioEffectClass, "Prysm Patch");
        return kResultOk;
    }

    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override
    {
        if (index != 0 || !info) return kInvalidArgument;
        *info = PClassInfo2(PLUGIN_CID.toTUID(), PClassInfo::kManyInstances, kVstAudioEffectClass, "Prysm Patch",
                            0, PlugType::kInstrumentExternal, "Nicholas Pratt", PRYSM_PLUGIN_VERSION, kVstVersionString);
        return kResultOk;
    }

    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override
    {
        if (index != 0 || !info) return kInvalidArgument;
        PClassInfo2 ci2;
        getClassInfo2(index, &ci2);
        info->fromAscii(ci2);
        return kResultOk;
    }

    tresult PLUGIN_API setHostContext(FUnknown*) override { return kResultOk; }

    tresult PLUGIN_API createInstance(FIDString cid, FIDString _iid, void** obj) override
    {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (!FUnknownPrivate::iidEqual(cid, PLUGIN_CID.toTUID())) return kNoInterface;
        auto* p = new PrysmPatchPlugin();
        tresult res = p->queryInterface(_iid, obj);
        p->release();
        return res;
    }
};

} // namespace

extern "C" {

SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory()
{
    static PrysmPatchFactory factory;
    return &factory;
}

#if defined(__APPLE__)
SMTG_EXPORT_SYMBOL bool bundleEntry(void*) { return true; }
SMTG_EXPORT_SYMBOL bool bundleExit() { return true; }
#elif defined(_WIN32)
SMTG_EXPORT_SYMBOL bool InitDll() { return true; }
SMTG_EXPORT_SYMBOL bool ExitDll() { return true; }
#else
SMTG_EXPORT_SYMBOL bool ModuleEntry(void*) { return true; }
SMTG_EXPORT_SYMBOL bool ModuleExit() { return true; }
#endif

} // extern "C"
