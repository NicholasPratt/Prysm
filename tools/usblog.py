#!/usr/bin/env python3
"""DEBUG: read the FM-1's USB setup log (DINGHY_USB_LOG in usb.c) over USB-MIDI and print it decoded.

  usblog.py [--port NAME]

Sends note on 0 on channel 16; the firmware answers with CCs on channel 16 (platform.c, usblog_dump_start).
"""
import argparse
import sys
import time

import mido

STD = {0x00: "GET_STATUS", 0x01: "CLEAR_FEATURE", 0x03: "SET_FEATURE", 0x05: "SET_ADDRESS", 0x06: "GET_DESCRIPTOR",
       0x08: "GET_CONFIGURATION", 0x09: "SET_CONFIGURATION", 0x0A: "GET_INTERFACE", 0x0B: "SET_INTERFACE"}
UAC = {0x01: "SET_CUR", 0x02: "SET_MIN", 0x03: "SET_MAX", 0x04: "SET_RES",
       0x81: "GET_CUR", 0x82: "GET_MIN", 0x83: "GET_MAX", 0x84: "GET_RES", 0xFF: "GET_STAT"}
DESC = {1: "device", 2: "config", 3: "string", 4: "interface", 5: "endpoint", 6: "qualifier",
        7: "other speed", 0x0F: "BOS", 0x21: "HID", 0x22: "report", 0x24: "class iface"}
RCPT = {0: "device", 1: "interface", 2: "endpoint", 3: "other"}


def describe(p):
    rt, rq = p[0], p[1]
    wv, wi, wl = p[2] | p[3] << 8, p[4] | p[5] << 8, p[6] | p[7] << 8
    kind, rcpt = (rt >> 5) & 3, RCPT.get(rt & 0x1F, f"rcpt {rt & 0x1F}")
    d = "IN " if rt & 0x80 else "OUT"
    if kind == 0:
        name = STD.get(rq, f"std {rq:#04x}")
        extra = ""
        if rq == 0x06:
            extra = f" {DESC.get(wv >> 8, hex(wv >> 8))} #{wv & 0xFF}"
        elif rq in (0x0B, 0x0A):
            extra = f" if {wi} alt {wv}"
        elif rq in (0x09, 0x05):
            extra = f" {wv}"
        return f"{d} {name}{extra}  ({rcpt} {wi:#06x}, len {wl})"
    if kind == 1:
        name = UAC.get(rq, f"class {rq:#04x}")
        sel = wv >> 8
        if rcpt == "endpoint":
            tgt = f"endpoint {wi & 0xFF:#04x} selector {sel}{' (sampling freq)' if sel == 1 else ''}"
        else:
            tgt = f"interface {wi & 0xFF} entity {wi >> 8} control {sel} ch {wv & 0xFF}"
        return f"{d} {name} {tgt}  (len {wl})"
    return f"{d} vendor/reserved req {rq:#04x} wValue {wv:#06x} wIndex {wi:#06x} len {wl}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="MIDI port name (default: the first matching Felucca / FM-1)")
    a = ap.parse_args()
    ins, outs = mido.get_input_names(), mido.get_output_names()
    pick = lambda names: next((n for n in names if (a.port or "felucca").lower() in n.lower()), None)
    i_name, o_name = pick(ins), pick(outs)
    if not i_name or not o_name:
        sys.exit(f"no FM-1 MIDI port (inputs {ins}, outputs {outs})")
    with mido.open_input(i_name) as inp, mido.open_output(o_name) as out:
        time.sleep(0.3)
        for _ in inp.iter_pending():
            pass
        out.send(mido.Message("note_on", channel=15, note=0, velocity=1))
        cur, entries, head, done, t0 = None, [], None, False, time.time()
        while not done and time.time() - t0 < 10:
            for m in inp.iter_pending():
                if m.type != "control_change" or m.channel != 15:
                    continue
                if m.control == 0x72:
                    head, cur, entries = {"n": m.value, "bytes": []}, head, []
                    cur = head["bytes"]
                elif m.control == 0x70:
                    cur = []
                    entries.append(cur)
                elif m.control in (0x60, 0x61) and cur is not None:
                    cur.append(m.value | (0x80 if m.control == 0x61 else 0))
                elif m.control == 0x73:
                    done = True
            time.sleep(0.01)
    if not head:
        sys.exit("no answer: is the DINGHY_USB_LOG build running?")
    hb = head["bytes"]
    total, stalls = hb[0] | hb[1] << 8, hb[2] | hb[3] << 8
    print(f"{head['n']} distinct requests logged, {total} setups in all, {stalls} stalled"
          + ("" if done else "  (incomplete: timed out)"))
    for i, e in enumerate(entries):
        if len(e) < 10:
            print(f"{i:2d}: truncated {e}")
            continue
        p, st, reps = e[:8], e[8], e[9]
        print(f"{i:2d}: {'STALL' if st else 'ok   '} {' '.join(f'{b:02x}' for b in p)}  "
              f"{describe(p)}{f'  x{reps + 1}' if reps else ''}")


if __name__ == "__main__":
    main()
