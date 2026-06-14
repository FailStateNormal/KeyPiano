"""Generate a tiny General-MIDI piano SoundFont (bank 0, preset 0) from scratch.

No external SF2 library — we emit the SFv2 RIFF structure directly. The sample
is one cycle-ish of a decaying harmonic-rich waveform, looped, with a
piano-like amplitude envelope applied per-note by the synth via the SF2
volume envelope generators. Small (~a few hundred KB), self-contained, and
license-free (we authored the waveform), so it can ship in the repo to give
keypiano an out-of-the-box piano sound.

Output: resources/soundfonts/default_piano.sf2
"""
import struct
import math
import numpy as np
import os

SAMPLE_RATE = 44100

# ── Build one looped sample: a struck-string-ish tone at a known pitch ────────
# Root pitch A4 = 440 Hz (MIDI 69). The synth pitch-shifts it for other notes.
ROOT_KEY = 69
ROOT_HZ = 440.0


def make_sample():
    # A steady (non-decaying) harmonic tone. The piano-like decay is applied by
    # the SF2 volume envelope, not baked into the sample — so the looped body
    # stays at full amplitude and notes are not silent when sustained.
    # Use an integer number of cycles at ROOT_HZ so the loop is seamless.
    cycles = 80
    period = SAMPLE_RATE / ROOT_HZ
    n = int(round(cycles * period))
    t = np.arange(n) / SAMPLE_RATE

    partials = [(1, 1.0), (2, 0.5), (3, 0.32), (4, 0.18),
                (5, 0.10), (6, 0.06), (7, 0.04)]
    wave = np.zeros(n)
    for h, amp in partials:
        wave += amp * np.sin(2 * math.pi * ROOT_HZ * h * t)

    wave /= np.max(np.abs(wave)) + 1e-9
    pcm = np.clip(wave * 32000, -32768, 32767).astype('<i2')
    return pcm


def _chunk(tag, data):
    # RIFF chunk: 4-byte tag, 4-byte LE size, data, pad to even length.
    out = tag + struct.pack('<I', len(data)) + data
    if len(data) & 1:
        out += b'\x00'
    return out


def write_sf2(path, pcm):
    n = len(pcm)
    # The whole sample is a steady set of whole cycles → loop it end-to-end.
    loop_start = 0
    loop_end = n - 1

    # ── INFO ──────────────────────────────────────────────────────────────────
    def zstr(s, even=True):
        b = s.encode('ascii') + b'\x00'
        if even and (len(b) & 1):
            b += b'\x00'
        return b
    info = b'INFO'
    info += _chunk(b'ifil', struct.pack('<HH', 2, 1))      # SF version 2.01
    info += _chunk(b'isng', zstr('EMU8000'))
    info += _chunk(b'INAM', zstr('keypiano default piano'))
    info_chunk = _chunk(b'LIST', info)

    # ── sdta (sample data) ──────────────────────────────────────────────────────
    # SF2 wants >= 46 zero samples after each sample as a guard region.
    raw = pcm.tobytes() + b'\x00\x00' * 46
    sdta = b'sdta' + _chunk(b'smpl', raw)
    sdta_chunk = _chunk(b'LIST', sdta)

    # ── pdta (the "hydra") ──────────────────────────────────────────────────────
    # shdr: one sample header + terminal record (each 46 bytes).
    def shdr_rec(name, start, end, ls, le, rate, key):
        nm = name.encode('ascii')[:19].ljust(20, b'\x00')
        return nm + struct.pack('<IIIIIBbHH', start, end, ls, le, rate,
                                key, 0, 0, 1)  # sampleType=1 (mono)
    shdr = shdr_rec('piano', 0, n, loop_start, loop_end, SAMPLE_RATE, ROOT_KEY)
    shdr += shdr_rec('EOS', 0, 0, 0, 0, 0, 0)

    # gen helper: (oper, amount)
    def gen(op, amt):
        return struct.pack('<HH', op, amt & 0xFFFF)

    # inst: one instrument + terminal.
    inst = b'piano'.ljust(20, b'\x00') + struct.pack('<H', 0)   # bag index 0
    inst += b'EOI'.ljust(20, b'\x00') + struct.pack('<H', 1)

    # ibag: one bag (gen index 0, mod index 0) + terminal.
    # The terminal bag's genNdx marks the end of bag 0's generators, so it must
    # equal the number of *real* generators (excluding the terminal record).
    n_igen = 2  # sampleModes, sampleID
    ibag = struct.pack('<HH', 0, 0) + struct.pack('<HH', n_igen, 0)

    # imod: terminal only.
    imod = struct.pack('<HHHHH', 0, 0, 0, 0, 0)

    # igen: minimal + reliable. Only loop the sample and point at sampleID;
    # let FluidSynth apply its default volume envelope (tinkering with the
    # SF2 env generators here easily yields near-silent output). The note's
    # loudness comes from the full-scale sample itself.
    #   54 = sampleModes (1 = loop continuously), 53 = sampleID
    igen = b''
    igen += gen(54, 1)      # loop
    igen += gen(53, 0)      # sampleID 0  (MUST be last generator)
    igen += gen(0, 0)       # terminal

    # phdr: one preset (bank0/preset0 = GM Acoustic Grand) + terminal (EOP).
    phdr = b'piano'.ljust(20, b'\x00')
    phdr += struct.pack('<HHHIII', 0, 0, 0, 0, 0, 0)  # preset0 bank0 bag0
    phdr += b'EOP'.ljust(20, b'\x00')
    phdr += struct.pack('<HHHIII', 0, 0, 1, 0, 0, 0)  # terminal bag index 1

    # pbag: one bag (gen 0, mod 0) + terminal.
    pbag = struct.pack('<HH', 0, 0) + struct.pack('<HH', 1, 0)
    # pmod: terminal only.
    pmod = struct.pack('<HHHHH', 0, 0, 0, 0, 0)
    # pgen: instrument(41)=0, then terminal.
    pgen = gen(41, 0) + gen(0, 0)

    pdta = b'pdta'
    pdta += _chunk(b'phdr', phdr)
    pdta += _chunk(b'pbag', pbag)
    pdta += _chunk(b'pmod', pmod)
    pdta += _chunk(b'pgen', pgen)
    pdta += _chunk(b'inst', inst)
    pdta += _chunk(b'ibag', ibag)
    pdta += _chunk(b'imod', imod)
    pdta += _chunk(b'igen', igen)
    pdta += _chunk(b'shdr', shdr)
    pdta_chunk = _chunk(b'LIST', pdta)

    body = b'sfbk' + info_chunk + sdta_chunk + pdta_chunk
    riff = _chunk(b'RIFF', body)
    with open(path, 'wb') as f:
        f.write(riff)


def main():
    pcm = make_sample()
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(here, 'default_piano.sf2')
    write_sf2(out_path, pcm)
    print('wrote', out_path, os.path.getsize(out_path), 'bytes')


if __name__ == '__main__':
    main()
