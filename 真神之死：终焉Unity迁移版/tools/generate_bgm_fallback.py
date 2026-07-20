import json
import math
import random
import struct
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "Assets" / "StreamingAssets" / "audio" / "bgm"
MANIFEST = OUT_DIR / "bgm_manifest.json"
SAMPLE_RATE = 22050
DURATION = 28.0


TRACKS = [
    ("chapter_0", "序章：梦境", (57, 60, 64, 67), 64, "dream"),
    ("boss_0", "序章 Boss：魔王？？？", (45, 48, 52, 55), 72, "boss"),
    ("chapter_1", "第一章：微风如诗", (55, 59, 62, 67), 76, "field"),
    ("boss_1", "第一章 Boss：阿拉贡", (50, 55, 57, 62), 78, "knight"),
    ("chapter_2", "第二章：猩红平原", (48, 51, 55, 60), 66, "blood"),
    ("boss_2", "第二章 Boss：偷窃者米格", (46, 50, 53, 58), 82, "stealth"),
    ("chapter_3", "第三章：赤心跃动", (52, 55, 60, 64), 68, "heart"),
    ("boss_3", "第三章 Boss：艾琳", (49, 52, 56, 61), 70, "eileen"),
    ("chapter_4", "第四章：精灵圣地", (57, 60, 64, 69), 72, "forest"),
    ("boss_4", "第四章 Boss：阿格尼", (50, 53, 57, 62), 70, "agni"),
    ("chapter_5", "第五章：撒冷", (60, 64, 67, 72), 64, "angel"),
    ("boss_5", "第五章 Boss：米凯尔", (48, 55, 60, 64), 68, "michael"),
    ("chapter_6", "第六章：魔境", (45, 50, 53, 57), 74, "demon"),
    ("boss_6", "第六章 Boss：伊维尔", (43, 48, 52, 55), 76, "evil"),
    ("chapter_7", "第七章：终焉", (52, 55, 59, 64), 58, "end"),
    ("boss_7", "第七章 Boss：莱索恩", (40, 47, 52, 55), 64, "lysorn"),
    ("chapter_8", "第八章：世界", (55, 60, 64, 67), 60, "world"),
    ("boss_8", "第八章 Boss：米凯尔与阿格尼", (48, 52, 55, 60), 66, "world_boss"),
    ("ending", "终焉之后", (55, 59, 62, 67), 56, "ending"),
]


def midi_to_freq(note):
    return 440.0 * (2.0 ** ((note - 69) / 12.0))


def env(t, attack=0.04, release=0.22, length=1.0):
    if t < 0:
        return 0.0
    if t < attack:
        return t / attack
    remain = max(0.0, length - t)
    if remain < release:
        return remain / release
    return 1.0


def tone(freq, t):
    sine = math.sin(2 * math.pi * freq * t)
    soft = math.sin(2 * math.pi * freq * 0.5 * t) * 0.45
    shimmer = math.sin(2 * math.pi * freq * 2.0 * t) * 0.08
    return sine * 0.72 + soft + shimmer


def bell(freq, local_t, length):
    e = math.exp(-2.6 * local_t) * env(local_t, 0.01, 0.28, length)
    return e * (
        math.sin(2 * math.pi * freq * local_t)
        + 0.36 * math.sin(2 * math.pi * freq * 2.01 * local_t)
        + 0.14 * math.sin(2 * math.pi * freq * 3.99 * local_t)
    )


def render_track(key, title, chord, bpm, mood):
    random.seed(key)
    beat = 60.0 / bpm
    bar = beat * 4
    progression = [
        chord,
        (chord[0] - 2, chord[1] + 1, chord[2] + 1, chord[3] - 2),
        (chord[0] - 5, chord[1] - 2, chord[2], chord[3] - 5),
        (chord[0] - 7, chord[1] - 3, chord[2] - 2, chord[3] - 7),
    ]
    boss = key.startswith("boss")
    chapter_gain = 0.28 if not boss else 0.34
    samples = []
    total = int(SAMPLE_RATE * DURATION)
    for i in range(total):
        t = i / SAMPLE_RATE
        bar_index = int(t / bar) % len(progression)
        local_bar = t % bar
        notes = progression[bar_index]

        pad = 0.0
        for n in notes:
            pad += tone(midi_to_freq(n - 12), t + n * 0.001) * 0.10
        pad *= 0.42 + 0.08 * math.sin(2 * math.pi * 0.08 * t)

        arp = 0.0
        step = int(local_bar / (beat * 0.5)) % 8
        arp_note = [notes[0], notes[2], notes[1], notes[3], notes[2], notes[1], notes[0], notes[3]][step]
        arp_start = int(t / (beat * 0.5)) * beat * 0.5
        arp_local = t - arp_start
        arp = bell(midi_to_freq(arp_note + 12), arp_local, beat * 0.52) * (0.18 if not boss else 0.13)

        bass = tone(midi_to_freq(notes[0] - 24), t) * 0.11
        if boss:
            pulse_phase = local_bar % (beat * 2)
            drum = math.exp(-10 * pulse_phase) * math.sin(2 * math.pi * 58 * pulse_phase) * 0.22
            bass += drum

        air = math.sin(2 * math.pi * 0.17 * t + random.random() * 0.00001) * 0.015
        sample = (pad + arp + bass + air) * chapter_gain
        sample = max(-0.92, min(0.92, sample))
        samples.append(int(sample * 32767))

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / f"{key}.wav"
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(b"".join(struct.pack("<h", s) for s in samples))
    return path


def main():
    manifest = {}
    for key, title, chord, bpm, mood in TRACKS:
        path = render_track(key, title, chord, bpm, mood)
        manifest[key] = {
            "title": title,
            "file": f"audio/bgm/{path.name}",
            "source": "local fallback",
            "description": "Quiet instrumental placeholder loop; API mp3 with the same key will take priority.",
        }
        print(f"wrote {path.name}")
    MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
