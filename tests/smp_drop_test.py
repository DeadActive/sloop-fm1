#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# BLE MIDI: 2026 DEADACTIVE
"""tools/gen_samples.py FELUCCA_SMP_DROP (the Bluetooth build's flash room): a dropped built-in SAMPLE set keeps its
index, name and presets and has no data (one zone of length 0: eng_sample.c smp_set_missing), every other set (the GM
kit, PERC, among them) keeps its samples, and nothing changes while the variable is unset. SLOOP's sets.
  smp_drop_test.py OUTDIR"""
import os
import re
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
GEN = SRC / "tools" / "gen_samples.py"


def gen(out, drop=None):
    env = {k: v for k, v in os.environ.items() if k != "FELUCCA_SMP_DROP"}
    if drop is not None:
        env["FELUCCA_SMP_DROP"] = drop
    return subprocess.run([sys.executable, str(GEN), str(out)], env=env, capture_output=True, text=True)


def parse(text):
    data = [int(v) for v in re.search(r"SMP_DATA\[\d+\] = \{(.*?)\};", text, re.S).group(1).replace("\n", "").split(",")
            if v.strip()]
    zones = [tuple(int(v) for v in m.group(1).split(","))
             for m in re.finditer(r"^    \{([-\d, ]+)\},$", re.search(r"SMP_ZONES\[\] = \{(.*?)\};", text, re.S).group(1),
                                  re.M)]
    sets = [(m.group(1), int(m.group(2)), int(m.group(3)))
            for m in re.finditer(r'\{"(\w+)", (\d+), (\d+)\},', re.search(r"SMP_SETS\[\] = \{(.*?)\};", text, re.S).group(1))]
    rest = {k: re.search(r"^.*" + k + r".*$", text, re.M).group(0)
            for k in ("define SMP_NSETS", "define SMP_SET_NAMES_INIT", "SMP_SET_NAMES\\[\\]")}
    presets = re.search(r"SMP_PRESET_TABLE\[\] = \{(.*?)\};", text, re.S).group(1)
    return data, zones, sets, rest, presets


def zone_bytes(data, z):
    return bytes(data[z[0]:z[0] + (z[1] + 1) // 2])


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    if not any((SRC / "assets" / "samples-cc0").glob("*/*.wav")):
        print("smp_drop_test: skipped (no CC0 library)")
        return 0
    r = gen(out / "all.h")
    assert r.returncode == 0, r.stdout + r.stderr
    full = (out / "all.h").read_text()
    r = gen(out / "empty.h", "")
    assert r.returncode == 0 and (out / "empty.h").read_text() == full, "an empty FELUCCA_SMP_DROP changes the header"
    r = gen(out / "bad.h", "FLUTE,TUBA")
    assert r.returncode != 0 and "TUBA" in r.stdout + r.stderr, "an unknown set name not refused by name"
    r = gen(out / "bad.h", "PERC")
    assert r.returncode != 0, "the GM kit (PERC, the drum lanes') is not a set to leave out"
    d0, z0, s0, rest0, p0 = parse(full)
    names = [n for n, _, _ in s0]
    for drop in (["FLUTE"], ["BASS", "VIBES", "HORNS", "FLUTE"]):
        r = gen(out / "drop.h", ",".join(drop))
        assert r.returncode == 0, r.stdout + r.stderr
        text = (out / "drop.h").read_text()
        d1, z1, s1, rest1, p1 = parse(text)
        assert [n for n, _, _ in s1] == names, f"{drop}: the set names or their order changed"
        assert rest1 == rest0 and p1 == p0, f"{drop}: SMP_NSETS / presets / SMP_SET_ORIG / the names changed"
        assert len(d1) < len(d0), f"{drop}: the data did not shrink"
        for (name, a0, n0), (_, a1, n1) in zip(s0, s1):
            if name in drop:        # no data: a single zone of length 0 over every key
                assert n1 == 1 and z1[a1][1] == 0 and z1[a1][8:10] == (0, 127), f"{drop}: {name} still has zones"
            else:                   # the same zones (keys, roots, loops, decoder state) and the same samples
                assert n1 == n0, f"{drop}: {name} lost zones"
                for k in range(n0):
                    assert z1[a1 + k][1:] == z0[a0 + k][1:], f"{drop}: {name} zone {k} changed"
                    assert zone_bytes(d1, z1[a1 + k]) == zone_bytes(d0, z0[a0 + k]), f"{drop}: {name} zone {k} data"
        assert "define SLC_BREAK_INIT" in text, f"{drop}: SLICE's BREAK line lost"
    print("smp_drop_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else SRC / "build" / "smp_drop"))
