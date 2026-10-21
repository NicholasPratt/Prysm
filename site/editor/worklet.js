// SPDX-License-Identifier: GPL-3.0-only
// Plays the browser FM-1's sound: blocks of interleaved stereo frames from the page, in order; tells the page how
// many frames it has played, so the emulator keeps just ahead of it (emu/wasm.c).
class PrysmOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = [];
    this.pos = 0;
    this.played = 0;
    this.n = 0;
    this.port.onmessage = (e) => this.q.push(e.data);
  }
  process(_inputs, outputs) {
    const [l, r] = outputs[0];
    for (let i = 0; i < l.length; i++) {
      const b = this.q[0];
      if (!b) { l[i] = r[i] = 0; continue; }     // (nothing yet: silence, and nothing counted)
      l[i] = b[this.pos];
      r[i] = b[this.pos + 1];
      this.played++;
      if ((this.pos += 2) >= b.length) { this.q.shift(); this.pos = 0; }
    }
    if ((++this.n & 3) === 0) this.port.postMessage(this.played);
    return true;
  }
}
registerProcessor("prysm-out", PrysmOut);
