#!/usr/bin/env python3
"""Synthesize Echelon's sound effects as small 16-bit mono WAV files.

No external deps. Run from anywhere; writes into ../sfx relative to this file.
Regenerate with:  python tools/generate_sfx.py
"""
import math, struct, os, random

RATE = 22050

def wav(path, samples):
    # samples: iterable of floats in [-1, 1]
    data = bytearray()
    for s in samples:
        v = int(max(-1.0, min(1.0, s)) * 32767)
        data += struct.pack('<h', v)
    n = len(data)
    hdr = b'RIFF' + struct.pack('<I', 36 + n) + b'WAVE'
    hdr += b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE * 2, 2, 16)
    hdr += b'data' + struct.pack('<I', n)
    with open(path, 'wb') as f:
        f.write(hdr + data)
    print("wrote", path, n // 2, "samples")

def env(t, dur, atk=0.005):
    """attack + exponential decay envelope"""
    if t < atk:
        return t / atk
    return math.exp(-(t - atk) / (dur * 0.35))

def square(t, f):
    return 1.0 if (t * f) % 1.0 < 0.5 else -1.0

def saw(t, f):
    return 2.0 * ((t * f) % 1.0) - 1.0

def gen(dur, fn, vol=0.7):
    n = int(RATE * dur)
    for i in range(n):
        t = i / RATE
        yield fn(t) * env(t, dur) * vol

def shoot():
    dur = 0.09
    def fn(t):
        f = 900 - (900 - 360) * (t / dur)      # falling pitch
        return square(t, f) * 0.6 + saw(t, f * 0.5) * 0.3
    return gen(dur, fn, 0.5)

def explode():
    dur = 0.34
    def fn(t):
        rum = math.sin(2 * math.pi * (150 - 80 * t / dur) * t)
        return (random.uniform(-1, 1) * 0.7 + rum * 0.5)
    return gen(dur, fn, 0.75)

def bomb():
    dur = 0.55
    def fn(t):
        low = math.sin(2 * math.pi * (95 - 45 * t / dur) * t)
        return random.uniform(-1, 1) * 0.5 + low * 0.8
    return gen(dur, fn, 0.9)

def powerup():
    dur = 0.20
    def fn(t):
        f = 620 if t < 0.09 else 940
        return square(t, f)
    return gen(dur, fn, 0.5)

def oneup():
    notes = [523, 659, 784, 1047]     # C E G C
    seg = 0.09
    dur = seg * len(notes)
    def fn(t):
        idx = min(int(t / seg), len(notes) - 1)
        return square(t, notes[idx])
    return gen(dur, fn, 0.5)

def death():
    dur = 0.5
    def fn(t):
        f = 520 - (520 - 110) * (t / dur)
        return saw(t, f) * 0.7 + random.uniform(-1, 1) * 0.2
    return gen(dur, fn, 0.6)

def main():
    random.seed(1234)                 # reproducible noise
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.normpath(os.path.join(here, '..', 'sfx'))
    os.makedirs(out, exist_ok=True)
    for name, g in [('shoot', shoot), ('explode', explode), ('bomb', bomb),
                    ('powerup', powerup), ('oneup', oneup), ('death', death)]:
        wav(os.path.join(out, name + '.wav'), g())

if __name__ == '__main__':
    main()
