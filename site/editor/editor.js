// SPDX-License-Identifier: GPL-3.0-only
// Prysm's editor: the FM-1's firmware running in the page (prysm.js / .wasm, built by emu/build.sh from
// firmware/src/prysm.c), an editor for every knob of the sound, and the real FM-1 kept in step over USB-MIDI.
//
// The emulator holds the sound. Every frame the page compares it with what the FM-1 last said it has, and sends the
// differences (SysEx F0 7D 50 'V' offset value, one at a time, each answered: firmware/src/editor.c); what is turned
// on the FM-1 comes back ('C') and is set in the emulator. So the editor's controls, the emulator's own knobs and the
// FM-1's all meet in one place.
//
// Inside the Prysm plugin (plugin/src/plugin.cpp, which serves this page in its window) the plugin makes the sound:
// the page then sends it every change and every key (prysm_set, prysm_note) instead of playing or using MIDI itself,
// and takes what changed in the plugin (automation, a connected FM-1) through window.prysmNative.
"use strict";
const PLUGIN = typeof window.prysm_hello === "function";

const BTNS = [["OCT-", 0], ["OCT+", 1], ["EDIT", 6], ["ENV", 4], ["LFO", 5], ["GLO", 7], ["FX", 2],
              ["ARP", 10], ["SEQ", 11], ["SELECT", 3], ["HOME", 8], ["SAVE", 9], ["PLAY", 12], ["REC", 13]];
const ENCS = [["SELECT", 0], ["ALGO", 1], ["KNOB1", 2], ["KNOB2", 3], ["KNOB3", 4], ["KNOB4", 5], ["PRESETS", 6]];
const CHOICE = new Set([4, 6, 7, 9, 11, 12, 16, 17, 18, 22, 32, 33, 34, 35]);   // ui.c's K_ kinds shown as a list
const LFO_ONLY_NOT = new Set([13, 14]);         // LFORATE, LFOAMT: the mod envelope's own, not the LFOs'
const KEYMAP = "awsedftgyhujkolp;'";            // C4 (key 7) up
const NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = (n) => NOTE[n % 12] + (Math.floor(n / 12) - 1);
const $ = (id) => document.getElementById(id);
const el = (tag, props = {}, kids = []) => { const e = Object.assign(document.createElement(tag), props); e.append(...kids); return e; };

let M, size, version, params = [], pages = [], tab = 0;
let ctx = null, node = null, gain = null, produced = 0, played = 0, playedBase = 0, t0 = performance.now();
let lastFb = -1, lastStore = -1, lastPatch = null, lastPage = -1, lastRow = -1;

// ---- the emulator
Prysm({
  onAudio(b) {                                  // a block of the sound: to the worklet (once the sound has started)
    produced += b.length / 2;
    if (node) node.port.postMessage(b, [b.buffer]);
  },
  ahead() {                                     // frames made but not heard yet (before the sound starts: by the clock)
    if (node) return produced - playedBase - played;
    let a = produced - (performance.now() - t0) * 44.1;
    if (a < -44100) { t0 = performance.now() - produced / 44.1; a = 0; }   // (a sleeping tab: no catching up)
    return a;
  },
}).then((m) => {
  M = m;
  restore();
  M.callMain();                                 // (returns at the first sleep; the app goes on between frames)
  size = M._web_patch_size();
  version = M._web_patch_version();
  readParams();
  buildFm1();
  buildTabs();
  if (PLUGIN) pluginMode();
  requestAnimationFrame(frame);
});

function restore() {                            // the patches and the settings, as this browser last had them
  try {
    const s = localStorage.getItem("prysm.store"), g = localStorage.getItem("prysm.settings");
    if (s) { const b = unb64(s); M.HEAPU8.set(b, M._web_store()); M._web_store_set_len(b.length); }
    if (g) M.HEAPU8.set(unb64(g).subarray(0, M._web_settings_size()), M._web_settings());
  } catch (e) { /* (no storage: a fresh FM-1) */ }
}
function keep() {
  try {
    const n = M._web_store_len();
    if (n) localStorage.setItem("prysm.store", b64(M.HEAPU8.subarray(M._web_store(), M._web_store() + n)));
    localStorage.setItem("prysm.settings", b64(M.HEAPU8.subarray(M._web_settings(), M._web_settings() + M._web_settings_size())));
  } catch (e) { /* (no storage) */ }
}
const b64 = (u8) => btoa(String.fromCharCode(...u8));
const unb64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
const patch = () => M.HEAPU8.subarray(M._web_patch(), M._web_patch() + size);

function readParams() {
  const n = M._web_nparams();
  for (let i = 0; i < n; i++) {
    const [page, row, label, knob, off, max, min, kind] = M.UTF8ToString(M._web_param(i)).split("|");
    const p = { i, page, row, label, knob: +knob, off: +off, max: +max, min: +min, kind: +kind };
    params.push(p);
    let pg = pages.find((x) => x.name === page);
    if (!pg) pages.push(pg = { name: page, index: pages.length, rows: [] });   // (ui.c's order: PG_OSC1 ..)
    let r = pg.rows.find((x) => x.name === row);
    if (!r) pg.rows.push(r = { name: row, params: [] });
    r.params.push(p);
  }
}
const fmt = (p, v) => M.UTF8ToString(M._web_fmt(p.i, v));

async function startSound() {
  ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
  await ctx.audioWorklet.addModule("worklet.js");
  node = new AudioWorkletNode(ctx, "prysm-out", { outputChannelCount: [2] });
  node.port.onmessage = (e) => { played = e.data; };
  gain = new GainNode(ctx);
  node.connect(gain).connect(ctx.destination);
  setVolume();
  playedBase = produced;
  await ctx.resume();
  $("start").remove();
}
function setVolume() { if (gain) gain.gain.value = ($("vol").value / 100) ** 2; }

// ---- the FM-1's panel: the screen, the knobs, the buttons, the keys
function buildFm1() {
  $("start").onclick = startSound;
  $("vol").oninput = setVolume;
  for (const [name, id] of ENCS) {
    const dial = el("div", { className: "dial" });
    const e = el("div", { className: "enc", title: name + ": drag up / down or scroll" }, [dial, el("span", { textContent: name })]);
    let angle = 0, y = null, acc = 0;
    const turn = (d) => { M._web_enc(id, d); angle += d * 12; dial.style.setProperty("--a", angle + "deg"); };
    e.onpointerdown = (ev) => { y = ev.clientY; acc = 0; e.setPointerCapture(ev.pointerId); };
    e.onpointermove = (ev) => {
      if (y === null) return;
      acc += y - ev.clientY; y = ev.clientY;
      while (acc >= 8) { turn(1); acc -= 8; }
      while (acc <= -8) { turn(-1); acc += 8; }
    };
    e.onpointerup = e.onpointercancel = () => { y = null; };
    e.onwheel = (ev) => { ev.preventDefault(); turn(ev.deltaY < 0 ? 1 : -1); };
    $("encs").append(e);
  }
  for (const [name, id] of BTNS) {
    const b = el("button", { textContent: name });
    b.onclick = () => M._web_btn(id);
    b.dataset.id = id;
    $("btns").append(b);
  }
  // 27 keys, F3 .. G5
  const black = (k) => [1, 3, 6, 8, 10].includes((5 + k) % 12);
  const whites = [...Array(27).keys()].filter((k) => !black(k)).length, w = 100 / whites;
  let x = 0;
  const held = new Map();
  for (let k = 0; k < 27; k++) {
    const d = el("div", { className: black(k) ? "b" : "w" });
    if (black(k)) Object.assign(d.style, { left: (x - w * 0.3) + "%", width: (w * 0.6) + "%" });
    else { Object.assign(d.style, { left: x + "%", width: w + "%" }); x += w; }
    d.dataset.k = k;
    $("keys").append(d);
  }
  const keyAt = (ev) => { const t = document.elementFromPoint(ev.clientX, ev.clientY); return t && t.dataset.k !== undefined && t.parentNode.id === "keys" ? +t.dataset.k : -1; };
  const kb = $("keys");
  kb.onpointerdown = (ev) => { const k = keyAt(ev); if (k < 0) return; kb.setPointerCapture(ev.pointerId); held.set(ev.pointerId, k); key(k, 1); };
  kb.onpointermove = (ev) => {
    if (!held.has(ev.pointerId)) return;
    const k = keyAt(ev), was = held.get(ev.pointerId);
    if (k >= 0 && k !== was) { key(was, 0); key(k, 1); held.set(ev.pointerId, k); }
  };
  kb.onpointerup = kb.onpointercancel = (ev) => { if (held.has(ev.pointerId)) { key(held.get(ev.pointerId), 0); held.delete(ev.pointerId); } };
  const typing = () => ["INPUT", "SELECT", "TEXTAREA"].includes(document.activeElement.tagName) && document.activeElement.type !== "range";
  addEventListener("keydown", (ev) => {
    const i = KEYMAP.indexOf(ev.key.toLowerCase());
    if (i < 0 || ev.repeat || ev.metaKey || ev.ctrlKey || typing()) return;
    key(7 + i, 1);
  });
  addEventListener("keyup", (ev) => { const i = KEYMAP.indexOf(ev.key.toLowerCase()); if (i >= 0) key(7 + i, 0); });
}
function key(k, down) {                         // a key of the FM-1's 27 (0 = F3): the emulator, and the plugin's sound
  M._web_key(k, down);
  if (PLUGIN) prysm_note(53 + 12 * M._web_oct() + k, down ? 127 : 0);
}

// ---- the editor: a tab a page, a box a row, a control a knob
function buildTabs() {
  pages.forEach((pg, n) => {
    const b = el("button", { textContent: pg.name });
    b.onclick = () => { showTab(n); goPage(pg.index); };
    $("tabs").append(b);
  });
  showTab(0);
}
function goPage(index, row) {                   // the emulator's screen to that page (and row): ALGORITHM, SELECT
  const at = M._web_page();
  if (at >= M._web_npages()) return;            // (the PATCH screen or the scope: left as they are)
  if (at !== index) M._web_enc(1, index - at);
  if (row !== undefined) setTimeout(() => { if (M._web_page() === index) M._web_enc(0, row - M._web_row()); }, 30);
}
function showTab(n) {
  tab = n;
  [...$("tabs").children].forEach((b, i) => b.classList.toggle("on", i === n));
  const pg = pages[n], box = $("page");
  box.replaceChildren();
  pg.rows.forEach((r, ri) => {
    const h = el("h3", { textContent: r.name, title: "Show this row on the screen" });
    h.onclick = () => goPage(pg.index, rowIndex(pg, r));
    const grid = el("div", { className: "prms" });
    for (const p of r.params) {
      const val = el("div", { className: "val" });
      let input;
      if (CHOICE.has(p.kind)) {
        input = el("select");
        for (let v = p.min; v <= p.max; v++)
          if (!((p.kind === 33 || p.kind === 34) && LFO_ONLY_NOT.has(v))) input.append(el("option", { value: v, textContent: fmt(p, v) }));
      } else input = el("input", { type: "range", min: p.min, max: p.max, step: 1 });
      input.oninput = () => { M._web_set(p.off, +input.value); };
      input.onfocus = () => goPage(pg.index, rowIndex(pg, r));
      p.input = input; p.valEl = val; p.shown = -1;
      grid.append(el("div", { className: "prm" }, [el("label", { textContent: p.label }), val, input]));
    }
    r.box = el("div", { className: "prow" }, [h, grid]);
    box.append(r.box);
  });
  if (pg.name === "SEQ") box.append(el("div", { className: "prow" }, [el("h3", { textContent: "STEPS: click note / accent / rest, shift-click slide, scroll the note" }), seqGrid()]));
  lastPatch = null;                             // (draw every value afresh)
}
const rowIndex = (pg, r) => {                   // the row's place on the FM-1 (the SEQ page's STEP row has no knobs here)
  if (pg.name === "SEQ") return 1;
  return pg.rows.indexOf(r);
};

let seqCells = [];
function seqGrid() {
  const g = el("div", { id: "seq" });
  const [on, ot, os] = [0, 1, 2].map((w) => M._web_seq_off(w));
  seqCells = [];
  for (let i = 0; i < 64; i++) {
    const c = el("div");
    c.onclick = (ev) => {
      const pt = patch();
      if (ev.shiftKey) M._web_set(os + i, pt[os + i] ? 0 : 1);
      else M._web_set(ot + i, (pt[ot + i] + 1) % 3);   // REST -> NOTE -> ACCENT
    };
    c.onwheel = (ev) => { ev.preventDefault(); const pt = patch(); M._web_set(on + i, Math.max(0, Math.min(127, pt[on + i] + (ev.deltaY < 0 ? 1 : -1)))); };
    seqCells.push(c);
    g.append(c);
  }
  return g;
}

function refresh(pt) {                          // the controls and values of the shown tab, from the sound
  const pg = pages[tab];
  for (const r of pg.rows)
    for (const p of r.params) {
      const v = pt[p.off];
      if (document.activeElement !== p.input || p.input.tagName === "SELECT") p.input.value = v;
      p.valEl.textContent = fmt(p, v);
    }
  if (pg.name === "SEQ" && seqCells.length) {
    const [on, ot, os] = [0, 1, 2].map((w) => M._web_seq_off(w)), len = pt[params.find((p) => p.label === "LENGTH").off];
    seqCells.forEach((c, i) => {
      const t = pt[ot + i];
      c.className = (t === 1 ? "note" : t === 2 ? "acc" : "") + (pt[os + i] ? " slide" : "") + (i >= len ? " past" : "");
      c.textContent = t ? noteName(pt[on + i]) : "";
    });
  }
}

// ---- each frame: the screen, the LEDs, the editor, the FM-1
const img = new ImageData(240, 240);
function frame() {
  const g = M._web_fb_gen();
  if (g !== lastFb) {                           // the screen (RGB565) -> the canvas
    lastFb = g;
    const fb = M.HEAPU16.subarray(M._web_fb() >> 1, (M._web_fb() >> 1) + 240 * 240), d = img.data;
    for (let i = 0, j = 0; i < fb.length; i++, j += 4) {
      const c = fb[i];
      d[j] = ((c >> 11) * 255 / 31) | 0; d[j + 1] = ((c >> 5 & 63) * 255 / 63) | 0; d[j + 2] = ((c & 31) * 255 / 31) | 0; d[j + 3] = 255;
    }
    $("screen").getContext("2d").putImageData(img, 0, 0);
  }
  const leds = M.HEAPU8.subarray(M._web_leds(), M._web_leds() + 48);
  for (const b of $("btns").children) b.classList.toggle("lit", leds[+b.dataset.id] === 1);
  for (const k of $("keys").children) k.classList.toggle("lit", leds[14 + +k.dataset.k] === 1);
  const pg = M._web_page(), row = M._web_row();
  if (pg !== lastPage || row !== lastRow) {     // the emulator moved: the editor follows
    lastPage = pg; lastRow = row;
    const n = pages.findIndex((x) => x.index === pg);
    if (n >= 0 && n !== tab) showTab(n);
    if (n >= 0) pages[n].rows.forEach((r, i) => r.box && r.box.classList.toggle("focus", rowIndex(pages[n], r) === row));
  }
  const pt = patch();
  if (!lastPatch || pt.some((v, i) => v !== lastPatch[i])) { refresh(pt); lastPatch = pt.slice(); }
  if (M._web_store_gen() !== lastStore) { lastStore = M._web_store_gen(); keep(); }
  (PLUGIN ? plug : dev).sync(pt);
  requestAnimationFrame(frame);
}

// ---- the FM-1 over USB-MIDI
const dev = {
  out: null, inp: null, shadow: null, dump: null, busy: false, waiting: null,
  send(type, data = []) { this.out.send([0xF0, 0x7D, 0x50, type.charCodeAt(0), ...data, 0xF7]); },
  sync(pt) {                                    // what the FM-1 does not have yet: one 'V' at a time
    if (!this.shadow || this.busy || this.waiting) return;
    for (let o = 0; o < size; o++)
      if (pt[o] !== this.shadow[o]) {
        const v = pt[o];
        this.shadow[o] = v;
        this.ask("V", [o >> 7, o & 127, v >> 7, v & 127]);
        return;
      }
  },
  ask(type, data) {                             // a message the FM-1 answers ('A' type ... rc): resolves with rc
    return new Promise((resolve) => {
      const tries = { n: 0 };
      const go = () => {
        this.send(type, data);
        this.waiting = { type, resolve, timer: setTimeout(() => { if (++tries.n < 3) go(); else { this.waiting = null; resolve(-1); } }, 400) };
      };
      go();
    });
  },
  receive(d) {                                  // F0 7D 50 type ... F7
    if (d.length < 5 || d[1] !== 0x7D || d[2] !== 0x50) return;
    const t = String.fromCharCode(d[3]), b = d.subarray(4, d.length - 1);
    if (t === "A" && this.waiting && b[0] === this.waiting.type.charCodeAt(0)) {
      clearTimeout(this.waiting.timer);
      const w = this.waiting; this.waiting = null; w.resolve(b[2]);
    } else if (t === "E") {                     // the dump: 64 bytes at b[0] * 64
      this.dump = this.dump || new Uint8Array(size);
      for (let j = 0; j < 64 && b[0] * 64 + j < size; j++) this.dump[b[0] * 64 + j] = b[1 + 2 * j] << 4 | b[2 + 2 * j];
    } else if (t === "F") {                     // the dump is complete: into the emulator
      const n = b[0] << 7 | b[1];
      if (n !== size || b[2] !== version) { status(`FM-1: its firmware's patch format differs (${n} B, v${b[2]}): update one of them`); this.shadow = null; return; }
      M.HEAPU8.set(this.dump, M._web_patch());
      M._web_load(M._web_patch(), size, b[3] - 1);
      this.shadow = this.dump.slice();
      this.dump = null;
      status("FM-1: connected" + (b[3] ? `, slot ${b[3]}` : ""));
    } else if (t === "C" && this.shadow) {      // turned on the FM-1
      const o = b[0] << 7 | b[1], v = b[2] << 7 | b[3];
      this.shadow[o] = v;
      M._web_set(o, v);
    } else if (t === "N") status("FM-1: connected" + (b[0] ? `, slot ${b[0]}` : ""));
  },
};
const status = (s) => { $("status").textContent = s; };

$("connect").onclick = async () => {
  if (!navigator.requestMIDIAccess) return status("FM-1: this browser has no Web MIDI (use Chrome or Edge)");
  let access;
  try { access = await navigator.requestMIDIAccess({ sysex: true }); } catch (e) { return status("FM-1: MIDI access refused"); }
  const isFm1 = (p) => /fm-1|prysm|felucca/i.test(p.name);
  dev.out = [...access.outputs.values()].find(isFm1);
  dev.inp = [...access.inputs.values()].find(isFm1);
  for (const i of access.inputs.values())       // other keyboards play the emulator
    if (!isFm1(i)) i.onmidimessage = (e) => { const [s, a, b] = e.data; if (s < 0xF0) M._web_midi(s, a || 0, b || 0); };
  if (!dev.out || !dev.inp) return status("FM-1: not found (connect it by USB; close other apps using its MIDI)");
  dev.inp.onmidimessage = (e) => { if (e.data[0] === 0xF0) dev.receive(e.data); };
  status("FM-1: asking…");
  dev.send("H");
  setTimeout(() => { if (!dev.shadow) status("FM-1: no answer. Install Prysm 0.8 or later on it"); }, 2000);
  $("connect").classList.add("on");
};

// ---- patch files and the FM-1's slots
$("save-file").onclick = () => {
  const head = new Uint8Array([0x50, 0x52, 0x59, 0x53, version, size >> 8, size & 255]);   // "PRYS", format, size
  const a = el("a", { download: "prysm-patch.prysm", href: URL.createObjectURL(new Blob([head, patch()])) });
  a.click();
};
$("load-file").onclick = () => $("file").click();
$("file").onchange = async () => {
  const f = $("file").files[0];
  if (!f) return;
  const b = new Uint8Array(await f.arrayBuffer());
  $("file").value = "";
  if (b.length < 7 || String.fromCharCode(...b.subarray(0, 4)) !== "PRYS") return alertLine("not a Prysm patch file");
  if (b[4] !== version || (b[5] << 8 | b[6]) !== size || b.length !== 7 + size) return alertLine(`a patch of another format (v${b[4]}): this editor has v${version}`);
  M.HEAPU8.set(b.subarray(7), M._web_patch());
  M._web_load(M._web_patch(), size, -1);         // (the FM-1 follows: the differences go to it)
};
$("send-slot").onchange = async () => {
  const s = +$("send-slot").value;
  $("send-slot").value = "";
  if (!s) return;
  if (PLUGIN) return prysm_store(s);           // (the plugin's FM-1 link stores it; its status says how it went)
  if (!dev.shadow) return alertLine("connect the FM-1 first");
  if (!confirm(`Store this sound in slot ${s} of the FM-1? What is in the slot now is replaced.`)) return;
  dev.busy = true;
  while (dev.waiting) await new Promise((r) => setTimeout(r, 10));
  const pt = patch().slice();
  let rc = 0;
  for (let i = 0; i < size && rc === 0; i += 64) {
    const chunk = [...pt.subarray(i, i + 64)];
    while (chunk.length < 64) chunk.push(0);
    rc = await dev.ask("P", [i / 64, ...chunk.flatMap((v) => [v >> 4, v & 15])]);
  }
  if (rc === 0) rc = await dev.ask("W", [s - 1, size >> 7, size & 127, version]);
  dev.busy = false;
  status(rc === 0 ? `FM-1: stored in slot ${s}` : `FM-1: storing failed (${rc})`);
};
const alertLine = (s) => status(s);

// ---- inside the plugin
const plug = {
  shadow: null,                                 // the sound as the plugin has it
  async hello() {
    const r = JSON.parse(await prysm_hello());
    const b = Uint8Array.from(r.patch);
    if (b.length !== size) return status("the plugin and this page differ in patch format");
    M.HEAPU8.set(b, M._web_patch());
    M._web_load(M._web_patch(), size, -1);
    this.shadow = b.slice();
    status(r.status || "");
  },
  sync(pt) {                                    // what the plugin does not have yet
    if (!this.shadow) return;
    const out = [];
    for (let o = 0; o < size; o++) if (pt[o] !== this.shadow[o]) { this.shadow[o] = pt[o]; out.push(o, pt[o]); }
    if (out.length) prysm_set(...out);
  },
};
window.prysmNative = {
  changes(list) {                               // [offset, value, ...]: changed in the plugin (automation, the FM-1)
    if (!plug.shadow) return;
    for (let i = 0; i + 1 < list.length; i += 2) { plug.shadow[list[i]] = list[i + 1]; M._web_set(list[i], list[i + 1]); }
  },
  status(s) { status(s); },
  reload() { plug.shadow = null; plug.hello(); },   // (the project loaded another sound)
};
function pluginMode() {
  for (const id of ["connect", "save-file", "load-file", "start"]) $(id).remove();
  $("vol").parentNode.remove();
  document.querySelector("footer").remove();
  plug.hello();
}
