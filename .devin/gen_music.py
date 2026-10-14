# EXIT INTERVIEW menu themes - synthesized dark-ambient loops.
# Renders float64 stereo -> 16-bit WAV, then ffmpeg -> MP3 into assets/Music.
# Both tracks are written to loop: after mixing, the last XF seconds are
# folded into the head so the seam lands on the same material both sides.
import numpy as np
import subprocess, os, struct, wave

SR = 44100
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MUSIC = os.path.join(ROOT, "assets", "Music")
OUT = os.path.join(ROOT, ".devin", "musictmp")
os.makedirs(MUSIC, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

rng = np.random.default_rng(20240117)


def t(dur):
    return np.arange(int(SR * dur)) / SR


def sine(f, dur, ph=0.0):
    return np.sin(2 * np.pi * f * t(dur) + ph)


def adsr(dur, a, d, s, r, slev=0.7):
    """attack/decay/sustain/release envelope over dur seconds"""
    n = int(SR * dur)
    e = np.zeros(n)
    na, nd, nr = int(SR * a), int(SR * d), int(SR * r)
    ns = max(0, n - na - nd - nr)
    i = 0
    e[i:i + na] = np.linspace(0, 1, na, endpoint=False); i += na
    e[i:i + nd] = np.linspace(1, slev, nd, endpoint=False); i += nd
    e[i:i + ns] = slev; i += ns
    e[i:i + nr] = np.linspace(slev, 0, nr); i += nr
    return e[:n] if i >= n else np.pad(e[:i], (0, n - i))


def lpf(x, fc):
    """one-pole lowpass"""
    a = 1 - np.exp(-2 * np.pi * fc / SR)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += a * (x[i] - acc)
        y[i] = acc
    return y


def lpfast(x, fc):
    """vectorized-ish one-pole via scipy-free recursive filter (chunked)"""
    return lpf(x, fc)


def pad_chord(freqs, dur, gain, wob=0.0):
    """detuned triangle-ish pad voice stack"""
    out = np.zeros(int(SR * dur))
    for f in freqs:
        for det in (-0.0035, 0.0, 0.0035):
            ph = rng.uniform(0, 6.28)
            v = sine(f * (1 + det), dur, ph)
            v += 0.30 * sine(f * 2 * (1 + det), dur, ph)
            v += 0.12 * sine(f * 3 * (1 + det), dur, ph)
            out += v / len(freqs) / 3
    if wob > 0:
        out *= 1 + wob * np.sin(2 * np.pi * 0.11 * t(dur))
    return out * gain


def keys_note(f, dur=2.6, gain=0.5, bright=0.5):
    """electric-piano-ish pluck: exp decay, few partials"""
    n = int(SR * dur)
    env = np.exp(-t(dur) / (dur * 0.38))
    v = sine(f, dur)
    v += bright * 0.4 * sine(f * 2.01, dur) * np.exp(-t(dur) / 0.6)
    v += 0.10 * sine(f * 3.02, dur) * np.exp(-t(dur) / 0.35)
    return (v * env)[:n] * gain


def mix_at(buf, sig, start):
    i = int(start * SR)
    src = 0
    if i < 0:                       # negative start: drop the head of sig
        src = -i
        i = 0
    if src >= len(sig) or i >= len(buf):
        return
    end = min(len(buf), i + len(sig) - src)
    buf[i:end] += sig[src:src + (end - i)]


def stereo_widen(L, R, wet, delay_ms=11, gain=0.5):
    """tiny Haas delay on R for width"""
    d = int(SR * delay_ms / 1000)
    L[:len(wet)] += wet * gain
    if d < len(R):
        n = min(len(wet), len(R) - d)
        R[d:d + n] += wet[:n] * gain


def loop_fold(x, xf=2.0):
    """fold the last xf seconds into the head so the file loops clean"""
    n = int(SR * xf)
    fade = np.linspace(0, 1, n)[:, None] if x.ndim == 2 else np.linspace(0, 1, n)
    tail = x[-n:]
    x = x[:-n]
    x[:n] = x[:n] * fade + tail * (1 - fade)
    return x


def norm(x, peak=0.82):
    return x * (peak / max(1e-6, np.abs(x).max()))


def write_wav(path, stereo):
    data = np.clip(stereo, -1, 1)
    inter = np.empty((len(data), 2), dtype=np.float32)
    inter[:, 0], inter[:, 1] = data[:, 0], data[:, 1]
    pcm = (inter * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def mp3(wav, name):
    dst = os.path.join(MUSIC, name)
    subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
                    "-i", wav, "-codec:a", "libmp3lame", "-b:a", "160k", dst],
                   check=True)
    print("wrote", dst)


# ---------------------------------------------------------------- The Shift
def the_shift():
    DUR = 54.0
    L = np.zeros(int(SR * DUR)); R = np.zeros(int(SR * DUR))

    # warm tape wobble: master pitch LFO applied per-source below
    # --- drone: detuned low saws through a slow lowpass sweep
    dr = np.zeros(int(SR * DUR))
    for f in (55.0, 55.14, 110.0, 82.4):
        saw = np.zeros(int(SR * DUR))
        for h in range(1, 6):
            saw += sine(f * h, DUR) / h
        dr += saw / 4
    # moving cutoff: darken/brighten over ~20s cycle
    a = 1 - np.exp(-2 * np.pi * (140 + 110 * (0.5 + 0.5 * np.sin(2 * np.pi * t(DUR) / 21))) / SR)
    y = np.zeros(len(dr)); acc = 0.0
    for i in range(len(dr)):
        acc += a[i] * (dr[i] - acc); y[i] = acc
    stereo_widen(L, R, y * 0.16, 9, 1.0)

    # --- pad chords: Am9 -> Fmaj7 -> Cmaj9 -> Em7, 13.5s each, xfade
    chords = [
        [110.0, 130.8, 164.8, 246.9],          # Am add9
        [87.3, 130.8, 174.6, 220.0],           # F maj7
        [98.0, 146.8, 196.0, 293.7],           # G add-ish colour
        [82.4, 123.5, 164.8, 196.0],           # Em7
    ]
    seg = DUR / 4
    for i, ch in enumerate(chords):
        pad = pad_chord(ch, seg + 1.2, 0.16, wob=0.012)
        env = np.ones(len(pad))
        xf = int(SR * 1.2)
        env[:xf] = np.linspace(0, 1, xf)
        env[-xf:] *= np.linspace(1, 0, xf)
        pad *= env
        mix_at(L, pad * 0.9, i * seg - 0.6)
        mix_at(R, pad, i * seg - 0.6 + 0.008)

    # --- sparse sad keys, Am pentatonic + the odd B
    notes = [220.0, 261.6, 293.7, 329.6, 392.0, 440.0, 493.9]
    times = [2.0, 7.5, 13.0, 19.5, 24.0, 30.5, 36.0, 41.5, 47.0, 52.0]
    rng_notes = np.random.default_rng(7)
    for i, tt in enumerate(times):
        f = notes[rng_notes.integers(0, len(notes))]
        mix_at(L, keys_note(f, 2.8, 0.20) * 0.9, tt)
        mix_at(R, keys_note(f * 1.0015, 2.8, 0.20), tt + 0.012)

    # --- vinyl hiss + crackle
    hiss = rng.standard_normal(int(SR * DUR))
    hiss = np.diff(hiss, prepend=hiss[0])          # crude highpass
    stereo_widen(L, R, lpf(hiss, 6000) * 0.020, 5, 1.0)
    pops = np.arange(3, DUR, 7.3)
    for tt in pops + rng.uniform(-1, 1, len(pops)):
        pop = np.zeros(int(SR * 0.02)); pop[:4] = 0.5
        pop *= np.exp(-t(0.02) / 0.003)
        mix_at(L, pop * 0.25, tt); mix_at(R, pop * 0.25, tt + 0.001)

    # --- distant knock at ~34s: muffled thud, like another floor
    th = sine(82, 0.5) * np.exp(-t(0.5) / 0.12)
    th += lpf(rng.standard_normal(int(SR * 0.5)), 300) * np.exp(-t(0.5) / 0.08) * 0.6
    for tt, g in ((33.8, 0.30), (34.15, 0.22)):
        mix_at(L, th * g, tt); mix_at(R, th * g * 0.9, tt + 0.004)

    # gentle 4Hz amplitude breathe + master fade edges, then loop-fold
    trem = 1 + 0.05 * np.sin(2 * np.pi * 0.09 * t(DUR))
    stereo = np.stack([L * trem, R * trem], axis=1)
    stereo = loop_fold(stereo, 2.0)
    return norm(stereo)


# ---------------------------------------------------------- Another Night
def another_night():
    DUR = 50.0
    L = np.zeros(int(SR * DUR)); R = np.zeros(int(SR * DUR))

    # --- industrial hum: mains drone + tremolo harmonic stack
    hum = (sine(47.0, DUR) * 0.5 + sine(94.2, DUR) * 0.22 +
           sine(141.0, DUR) * 0.10)
    hum *= 1 + 0.18 * np.sin(2 * np.pi * 0.07 * t(DUR) + 1.1)
    stereo_widen(L, R, hum * 0.14, 13, 1.0)

    # --- cold choir pad: Dm -> Bb add9 -> Asus4, slow swells
    chordseg = [[146.8, 174.6, 220.0, 293.7],
                [116.5, 174.6, 233.1, 293.7],
                [110.0, 164.8, 220.0, 246.9],
                [146.8, 174.6, 220.0, 349.2]]
    seg = DUR / 4
    for i, ch in enumerate(chordseg):
        pad = pad_chord(ch, seg + 1.5, 0.13)
        env = np.ones(len(pad))
        xf = int(SR * 1.5)
        env[:xf] = np.linspace(0, 1, xf) ** 1.6
        env[-xf:] *= np.linspace(1, 0, xf)
        mix_at(L, pad * env * 0.85, i * seg - 0.75)
        mix_at(R, pad * env, i * seg - 0.72)

    # --- music box: detuned high notes, D harmonic minor, irregular
    box_notes = [587.3, 698.5, 880.0, 739.9, 932.3, 1174.7, 1046.5]
    rng_b = np.random.default_rng(31)
    tt = 4.0
    while tt < DUR - 3:
        f = box_notes[rng_b.integers(0, len(box_notes))]
        g = 0.16 if rng_b.random() > 0.25 else 0.24   # occasional louder
        sig = keys_note(f, 3.0, g, bright=0.7)
        sig += 0.06 * sine(f * 1.006, 3.0) * np.exp(-t(3.0) / 1.1)  # detune shimmer
        mix_at(L, sig * 0.8, tt)
        mix_at(R, sig, tt + 0.02)
        tt += rng_b.uniform(3.0, 5.4)

    # --- HVAC room tone: brown noise bandpassed low
    bn = np.cumsum(rng.standard_normal(int(SR * DUR)))
    bn = bn / np.abs(bn).max()
    bn = bn - np.convolve(bn, np.ones(int(SR * 0.05)) / int(SR * 0.05), mode="same")
    stereo_widen(L, R, lpf(bn, 500) * 0.05, 7, 1.0)

    # --- heartbeat: double thump every ~4.2s, fades in halfway through
    kick = sine(58, 0.35) * np.exp(-t(0.35) / 0.09)
    tt = 18.0
    while tt < DUR - 2:
        env = min(1.0, (tt - 14) / 12)
        mix_at(L, kick * 0.30 * env, tt); mix_at(R, kick * 0.30 * env, tt)
        mix_at(L, kick * 0.20 * env, tt + 0.42); mix_at(R, kick * 0.20 * env, tt + 0.42)
        tt += 4.2

    # --- reversed swell at ~37s: a tone that inhales then stops
    sw_dur = 3.0
    sw = sine(293.7, sw_dur) + 0.4 * sine(440.0, sw_dur)
    sw *= np.linspace(0, 1, len(sw)) ** 2.2           # reversed envelope
    sw *= 0.10
    mix_at(L, sw, 37.0); mix_at(R, sw, 37.03)

    stereo = np.stack([L, R], axis=1)
    stereo = loop_fold(stereo, 2.0)
    return norm(stereo)


if __name__ == "__main__":
    w1 = os.path.join(OUT, "the_shift.wav")
    w2 = os.path.join(OUT, "another_night.wav")
    write_wav(w1, the_shift()); print("wav", w1)
    write_wav(w2, another_night()); print("wav", w2)
    mp3(w1, "Menu_The-Shift.mp3")
    mp3(w2, "Menu_Another-Night.mp3")
