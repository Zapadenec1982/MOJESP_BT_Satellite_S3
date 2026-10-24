#!/usr/bin/env python3
"""Generate the test signals this firmware is actually worth checking against.

A commercial test disc is for judging a SYSTEM by ear — phase, imaging, tonal balance in a
room. These files are for judging the FIRMWARE: each one fails visibly or audibly if a
specific part is wrong, and each says which part.

    python tools/make_test_wavs.py D:/P4_DSP/testfiles

16-bit stereo WAV, because it is the one format that needs no encoder and no library, and
because a test signal should not arrive through a decoder that might itself be the fault.

What each file is for
---------------------
sine_1000_44100      The resampler. Same tone at 44.1 and 48 kHz: 48 bypasses resampling,
sine_1000_48000      44.1 goes through it. If they do not sound identical, it is the
                     resampler — and the log says which path ran.
sine_50_m1dbfs       Bass near full scale: the case that shows up limiter attack problems
                     and output-stage clipping, both of which are loudest at low frequency.
sweep_20_20000       Frequency response. Play it with the EQ flat, then with a band boosted,
                     and the spectrum row should trace the curve the EQ screen draws.
pink_m20dbfs         Spectrum calibration. The analyser uses log-spaced bands, and pink noise
                     has equal energy per octave — so it must read FLAT across all 28 bars.
                     Any tilt is the band reference or the window normalisation, not the
                     music. This is the only objective test of that display.
lr_ident             Which speaker is which, spoken by position: left channel only, then
                     right only, then both. Catches a swapped pair and a dead channel.
"""
import math
import os
import struct
import sys
import wave

RATE_DEFAULT = 44100
AMP_FULL = 32767


def write_wav(path, rate, frames):
    """frames: iterable of (left, right) floats in −1..1."""
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        data = bytearray()
        for l, r in frames:
            # Clamp rather than wrap: a generator that overflows silently would put the very
            # artefact we are testing for into the reference.
            li = max(-AMP_FULL, min(AMP_FULL, int(l * AMP_FULL)))
            ri = max(-AMP_FULL, min(AMP_FULL, int(r * AMP_FULL)))
            data += struct.pack("<hh", li, ri)
        w.writeframes(bytes(data))
    print(f"  {os.path.basename(path)}  {len(data) // 4} frames @ {rate} Hz")


def sine(rate, seconds, hz, dbfs):
    a = 10.0 ** (dbfs / 20.0)
    step = 2.0 * math.pi * hz / rate
    for i in range(int(rate * seconds)):
        v = a * math.sin(step * i)
        yield v, v


def sweep(rate, seconds, f0, f1, dbfs):
    """Logarithmic sweep: equal time per octave, which is how a response is read."""
    a = 10.0 ** (dbfs / 20.0)
    n = int(rate * seconds)
    k = math.log(f1 / f0)
    phase = 0.0
    for i in range(n):
        t = i / n
        f = f0 * math.exp(k * t)
        phase += 2.0 * math.pi * f / rate
        v = a * math.sin(phase)
        yield v, v


def pink(rate, seconds, dbfs):
    """Voss-McCartney: octave-spaced random sources, one more doubling its period each time.

    Cheap, stdlib-only, and flat per octave to well under a decibel — which is all this is
    asked to be, since the display it tests reads in bands, not bins.
    """
    import random
    random.seed(20260916)                      # reproducible: two runs must be comparable
    rows = 16
    values = [random.uniform(-1.0, 1.0) for _ in range(rows)]
    total = sum(values)
    a = 10.0 ** (dbfs / 20.0) / math.sqrt(rows)
    counter = 0
    for _ in range(int(rate * seconds)):
        counter += 1
        # Update row k on every 2^k-th sample — that is what makes the spectrum 1/f.
        for k in range(rows):
            if counter % (1 << k) == 0:
                total -= values[k]
                values[k] = random.uniform(-1.0, 1.0)
                total += values[k]
                break
        v = max(-1.0, min(1.0, total * a))
        yield v, v


def lr_ident(rate, dbfs):
    a = 10.0 ** (dbfs / 20.0)
    step = 2.0 * math.pi * 440.0 / rate
    def tone(seconds, left, right):
        for i in range(int(rate * seconds)):
            v = a * math.sin(step * i)
            yield v * left, v * right
    yield from tone(2.0, 1.0, 0.0)
    yield from tone(0.5, 0.0, 0.0)
    yield from tone(2.0, 0.0, 1.0)
    yield from tone(0.5, 0.0, 0.0)
    yield from tone(2.0, 1.0, 1.0)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "testfiles"
    os.makedirs(out, exist_ok=True)
    print(f"writing to {out}")
    j = lambda name: os.path.join(out, name)

    # The pair that isolates the resampler: identical tone, one rate each side of the output.
    write_wav(j("01_sine_1000Hz_44100.wav"), 44100, sine(44100, 10, 1000.0, -6.0))
    write_wav(j("02_sine_1000Hz_48000.wav"), 48000, sine(48000, 10, 1000.0, -6.0))
    # Bass at −1 dBFS: limiter attack and output clipping are both loudest down here.
    write_wav(j("03_sine_50Hz_m1dBFS.wav"), 44100, sine(44100, 10, 50.0, -1.0))
    # Response: play flat, then with a band boosted, and watch the spectrum follow the curve.
    write_wav(j("04_sweep_20Hz_20kHz.wav"), 44100, sweep(44100, 30, 20.0, 20000.0, -6.0))
    # Must read FLAT across all 28 bars. Anything else is the display, not the signal.
    write_wav(j("05_pink_m20dBFS.wav"), 44100, pink(44100, 20, -20.0))
    # Left, silence, right, silence, both.
    write_wav(j("06_left_right_check.wav"), 44100, lr_ident(44100, -12.0))

    # ── the clock follows the source, so the source had better be able to move it ──
    # 96 and 192 kHz exist to prove the retune, not because anything is mastered at 192.
    # A 1 kHz tone sounds the same at every rate; what is being listened for is that it
    # sounds at all, and that switching between these and a 44.1 file is silent.
    write_wav(j("07_sine_1000Hz_96000.wav"), 96000, sine(96000, 10, 1000.0, -6.0))
    write_wav(j("08_sine_1000Hz_192000.wav"), 192000, sine(192000, 10, 1000.0, -6.0))
    # And one that nothing can clock: the player must convert it rather than refuse it.
    write_wav(j("09_sine_1000Hz_22050.wav"), 22050, sine(22050, 10, 1000.0, -6.0))
    print("done")


if __name__ == "__main__":
    main()
