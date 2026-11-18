#!/usr/bin/env python3
"""Analisa uma musica e gera um preset JSON (kick + baixo) para o KickBassForge.

Uso:  python analyze.py musica.mp3 -o preset.json
Requer: pip install librosa numpy scipy soundfile demucs torch
(sem demucs o script usa filtros simples, bem menos preciso)
"""
import argparse, json, subprocess, sys, tempfile
from collections import Counter
from pathlib import Path
import numpy as np
import librosa
import scipy.signal as sg

SR = 44100


def separate(path, workdir):
    try:
        subprocess.run([sys.executable, "-m", "demucs", "-n", "htdemucs", "-o", workdir, str(path)], check=True)
        d = Path(workdir) / "htdemucs" / Path(path).stem
        drums, _ = librosa.load(d / "drums.wav", sr=SR, mono=True)
        bass, _ = librosa.load(d / "bass.wav", sr=SR, mono=True)
        return drums, bass
    except Exception as e:
        print(f"[aviso] Demucs indisponivel ({e}); usando filtros simples.")
        y, _ = librosa.load(path, sr=SR, mono=True)
        low = sg.sosfilt(sg.butter(4, 150, "low", fs=SR, output="sos"), y)
        return y, low


def peak_freq(x, lo, hi):
    if len(x) < 32:
        return None
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), 16384))
    f = np.fft.rfftfreq(16384, 1 / SR)
    m = (f >= lo) & (f <= hi)
    return float(f[m][np.argmax(X[m])])


def kick_params(drums, onsets):
    lp = sg.butter(4, 400, "low", fs=SR, output="sos")
    fs_l, fe_l, fm_l, tau_l, clk_l, drv_l = [], [], [], [], [], []
    for t in onsets[:64]:
        i = int(t * SR)
        raw = drums[i:i + int(0.4 * SR)]
        if len(raw) < int(0.3 * SR):
            continue
        seg = sg.sosfilt(lp, raw)
        fe = peak_freq(seg[int(.08 * SR):int(.25 * SR)], 30, 120)
        fs = peak_freq(seg[int(.004 * SR):int(.024 * SR)], 40, 500)
        fm = peak_freq(seg[int(.03 * SR):int(.06 * SR)], 30, 400)
        if None in (fe, fs, fm):
            continue
        fs_l.append(fs); fe_l.append(fe); fm_l.append(fm)
        env = np.abs(sg.hilbert(seg))
        env = np.convolve(env, np.ones(64) / 64, "same")
        pk = int(np.argmax(env[:int(.05 * SR)]))
        below = np.where(env[pk:] < 0.1 * env[pk])[0]
        t10 = (below[0] / SR) if len(below) else 0.3
        tau_l.append(max(t10, 0.05) / np.log(10))
        e_all = np.sum(raw[:int(.1 * SR)] ** 2) + 1e-12
        hi = sg.sosfilt(sg.butter(4, 2000, "high", fs=SR, output="sos"), raw[:int(.008 * SR)])
        clk_l.append(np.sum(hi ** 2) / e_all)
        mid = sg.sosfilt(sg.butter(4, [150, 1000], "band", fs=SR, output="sos"), raw[int(.01 * SR):int(.15 * SR)])
        drv_l.append(np.sum(mid ** 2) / e_all)
    if not fe_l:
        return {}
    fe, fs, fm = np.median(fe_l), np.median(fs_l), np.median(fm_l)
    fe = float(np.clip(fe, 30, 90))
    if fs > fm > fe:
        ptau = 0.031 / np.log((fs - fe) / (fm - fe))
    else:
        ptau = 0.035
    ptau = float(np.clip(ptau, 0.008, 0.12))
    start = float(np.clip(fe + (fs - fe) * np.exp(0.014 / ptau), 80, 500))
    return {
        "startHz": round(start, 1), "endHz": round(fe, 1),
        "pitchDecayMs": round(ptau * 1000, 1),
        "decayMs": round(float(np.clip(np.median(tau_l) * 3000, 80, 900)), 1),
        "click": round(float(np.clip(np.median(clk_l) * 6, 0, 1)), 2),
        "drive": round(float(np.clip(np.median(drv_l) * 4, 0, 0.9)), 2),
    }


def bass_params(bass):
    S = np.abs(librosa.stft(bass, n_fft=4096)) ** 2
    f = librosa.fft_frequencies(sr=SR, n_fft=4096)
    tot = S.sum() + 1e-12
    sub = S[f < 90].sum() / tot
    hi = S[f > 500].sum() / tot
    cent = float(np.mean(librosa.feature.spectral_centroid(S=np.sqrt(S), sr=SR)))
    return {"cutoff": round(float(np.clip(cent * 1.2, 120, 3000)), 0),
            "sub": round(float(np.clip(sub * 1.2, 0.2, 0.95)), 2),
            "drive": round(float(np.clip(hi * 4, 0, 0.8)), 2)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audio"); ap.add_argument("-o", "--out", default="preset.json")
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        drums, bass = separate(a.audio, tmp)

    n = min(len(drums), len(bass)); drums, bass = drums[:n], bass[:n]
    tempo, beats = librosa.beat.beat_track(y=drums + bass, sr=SR, units="time")
    tempo = float(np.atleast_1d(tempo)[0])
    if len(beats) < 8:
        sys.exit("Nao consegui detectar o ritmo da musica.")
    grid = np.concatenate([beats[i] + (beats[i + 1] - beats[i]) * np.arange(4) / 4 for i in range(len(beats) - 1)])
    step_dur = 60.0 / tempo / 4

    # --- kick
    low = sg.sosfilt(sg.butter(4, 120, "low", fs=SR, output="sos"), drums)
    onsets = librosa.onset.onset_detect(y=low, sr=SR, units="time", backtrack=True)
    kh = np.zeros(16)
    for t in onsets:
        g = int(np.argmin(np.abs(grid - t)))
        if abs(grid[g] - t) < 0.5 * step_dur:
            kh[g % 16] += 1
    bars = max(len(grid) // 16, 1)
    rot = 4 * int(np.argmax([kh[0], kh[4], kh[8], kh[12]]))  # alinha o "1" no kick mais forte
    kick_prob = np.clip(np.roll(kh / bars, -rot), 0, 1)

    # --- baixo
    f0, vf, _ = librosa.pyin(bass, fmin=30, fmax=400, sr=SR, frame_length=4096, hop_length=512)
    times = librosa.times_like(f0, sr=SR, hop_length=512)
    midi = librosa.hz_to_midi(f0)
    notes_at = [[] for _ in range(16)]; voiced = np.zeros(16); total = np.zeros(16); hist = Counter()
    for k, g0 in enumerate(grid):
        m = (times >= g0) & (times < g0 + 0.6 * step_dur)
        pos = ((k % 16) - rot) % 16
        total[pos] += 1
        ok = m & vf & ~np.isnan(midi)
        if ok.sum() >= 2:
            note = int(round(float(np.median(midi[ok]))))
            while note > 60: note -= 12
            while note < 24: note += 12
            voiced[pos] += 1; notes_at[pos].append(note); hist[note] += 1
    bass_prob = voiced / np.maximum(total, 1)
    bass_pat = [Counter(notes_at[s]).most_common(1)[0][0] if bass_prob[s] > 0.5 and notes_at[s] else 0 for s in range(16)]
    wsum = sum(hist.values()) or 1
    pool = [{"note": int(k), "weight": round(v / wsum, 3)} for k, v in hist.most_common(12)]

    preset = {
        "bpm": round(tempo, 1),
        "kick": kick_params(drums, onsets),
        "bass": bass_params(bass),
        "kickPattern": [int(p > 0.5) for p in kick_prob],
        "bassPattern": bass_pat,
        "kickProb": [round(float(p), 2) for p in kick_prob],
        "bassProb": [round(float(p), 2) for p in bass_prob],
        "bassNotes": pool,
    }
    Path(a.out).write_text(json.dumps(preset, indent=2))
    print(f"Preset salvo em {a.out}  (BPM {tempo:.1f})")


if __name__ == "__main__":
    main()
