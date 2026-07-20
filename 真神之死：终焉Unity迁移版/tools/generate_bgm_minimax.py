import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "Assets" / "StreamingAssets" / "audio" / "bgm"
MANIFEST = OUT_DIR / "bgm_manifest.json"
ENDPOINTS = [
    "https://api.minimax.io/v1/music_generation",
    "https://api.minimaxi.com/v1/music_generation",
]
MODEL_CANDIDATES = ["music-3.0-free", "Music-3.0-free", "music-2.6-free"]


TRACKS = [
    ("chapter_0", "序章：梦境", "Quiet instrumental fantasy RPG background music, dreamlike prologue, soft piano, distant choir pad, light strings, tiny bells, slow tempo, calm and mysterious, no vocals, no lyrics, seamless loop feeling."),
    ("boss_0", "序章 Boss：魔王？？？", "Instrumental fantasy RPG boss music, calm but ominous, dark low strings, slow war drum, distant choir pad, restrained melody, dream ending and vanished true god atmosphere, no vocals, no lyrics."),
    ("chapter_1", "第一章：微风如诗", "Quiet instrumental medieval fantasy field music, breeze over farmland and distant royal city, harp, flute, lute, soft strings, warm calm journey feeling, no vocals, no lyrics."),
    ("boss_1", "第一章 Boss：阿拉贡", "Instrumental medieval knight boss theme, solemn but calm, restrained brass, low drum, noble strings, fortress and loyal commander atmosphere, no vocals, no lyrics."),
    ("chapter_2", "第二章：猩红平原", "Quiet instrumental dark fantasy background music, crimson plain, vampire shadow, cold piano, low cello, faint bells, red dusk atmosphere, calm and oppressive, no vocals, no lyrics."),
    ("boss_2", "第二章 Boss：偷窃者米格", "Instrumental stealthy vampire boss music, quiet tension, pizzicato strings, low cello, faint bell, thief in shadow, restrained and calm, no vocals, no lyrics."),
    ("chapter_3", "第三章：赤心跃动", "Quiet instrumental gothic castle fantasy music, bloodline memory and beating heart, soft piano, strings, subtle heartbeat pulse, sad but calm, no vocals, no lyrics."),
    ("boss_3", "第三章 Boss：艾琳", "Instrumental vampire princess boss theme, elegant sad piano and strings, restrained dark pulse, tragic truth and crimson heart, calm pressure, no vocals, no lyrics."),
    ("chapter_4", "第四章：精灵圣地", "Quiet instrumental elven sanctuary fantasy music, ancient forest, harp, woodwinds, soft strings, nature ambience, green mysterious calm, no vocals, no lyrics."),
    ("boss_4", "第四章 Boss：阿格尼", "Instrumental elven boss theme, ancient tree and immortality curse, deep forest drones, strings, restrained percussion, sacred but heavy, no vocals, no lyrics."),
    ("chapter_5", "第五章：撒冷", "Quiet instrumental holy city fantasy music, angels and Salem, organ, harp, bright strings, golden light atmosphere, solemn and calm, no vocals, no lyrics."),
    ("boss_5", "第五章 Boss：米凯尔", "Instrumental archangel boss theme, sacred organ, low drum, bright strings, golden choir pad, majestic but restrained, no vocals, no lyrics."),
    ("chapter_6", "第六章：魔境", "Quiet instrumental demon realm fantasy music, dark frontier, low strings, soft war drums, lute fragments, distant fire ambience, calm danger, no vocals, no lyrics."),
    ("boss_6", "第六章 Boss：伊维尔", "Instrumental demon king boss theme, heavy strings, slow drums, dark brass, tragic old king atmosphere, serious but not loud, no vocals, no lyrics."),
    ("chapter_7", "第七章：终焉", "Quiet instrumental end-of-world fantasy music, void, white enemies, final land, minimal piano, transparent synth pad, low strings, tiny bells, spacious and calm, no vocals, no lyrics."),
    ("boss_7", "第七章 Boss：莱索恩", "Instrumental final demon lord boss theme, void low tones, slow drums, dark strings, distant choir pad, solemn oppressive ending, restrained, no vocals, no lyrics."),
    ("chapter_8", "第八章：世界", "Quiet instrumental world rematch fantasy music, return to the beginning, reflective piano, warm strings, clear bells, calm and resolved, no vocals, no lyrics."),
    ("boss_8", "第八章 Boss：米凯尔与阿格尼", "Instrumental final rematch boss music, archangel gold and elven forest intertwined, piano, strings, low drum, solemn calm finale, no vocals, no lyrics."),
    ("ending", "终焉之后", "Quiet instrumental ending music, story truth and developer reflection, gentle piano, soft strings, airy ambience, sincere calm closure, returning to the beginning, no vocals, no lyrics."),
]


def post_music(api_key, prompt, model, endpoint):
    payload = {
        "model": model,
        "prompt": prompt,
        "is_instrumental": True,
        "output_format": "hex",
        "audio_setting": {
            "sample_rate": 44100,
            "bitrate": 256000,
            "format": "mp3",
        },
    }
    request = urllib.request.Request(
        endpoint,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=420) as response:
        return json.loads(response.read().decode("utf-8"))


def extract_audio_bytes(result):
    data = result.get("data", {})
    audio = data.get("audio", "")
    if not audio:
        return None
    if audio.startswith("http://") or audio.startswith("https://"):
        req = urllib.request.Request(audio, headers={"User-Agent": "CodexMiniMaxBgm/1.0"})
        with urllib.request.urlopen(req, timeout=240) as response:
            return response.read()
    return bytes.fromhex(audio)


def main():
    api_key = os.environ.get("MINIMAX_API_KEY")
    if not api_key:
        print("MINIMAX_API_KEY is not set.", file=sys.stderr)
        return 2

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    manifest = {}
    if MANIFEST.exists():
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    first_only = "--first-only" in sys.argv
    failures = []
    model_candidates = os.environ.get("MINIMAX_MUSIC_MODELS")
    models = [m.strip() for m in model_candidates.split(",")] if model_candidates else MODEL_CANDIDATES

    for key, title, prompt in TRACKS:
        if first_only and key != TRACKS[0][0]:
            continue
        out_file = OUT_DIR / f"{key}.mp3"
        if out_file.exists() and out_file.stat().st_size > 4096 and "--force" not in sys.argv:
            print(f"skip existing {key}")
            continue

        print(f"generating {key}: {title}", flush=True)
        last_error = None
        try:
            result = None
            used_model = ""
            for endpoint in ENDPOINTS:
                for model in models:
                    result = post_music(api_key, prompt, model, endpoint)
                    base = result.get("base_resp", {})
                    status_code = base.get("status_code", 0)
                    if status_code in (0, "0", None):
                        used_model = model
                        break
                    last_error = json.dumps(base, ensure_ascii=False)
                    print(f"  {endpoint.rsplit('/', 2)[0]} {model}: {last_error}", flush=True)
                    if "insufficient balance" not in last_error and "invalid" not in last_error.lower() and "model" not in last_error.lower():
                        break
                if used_model:
                    break
            if not result or not used_model:
                raise RuntimeError(last_error or "no successful model")
            audio_bytes = extract_audio_bytes(result)
            if not audio_bytes:
                raise RuntimeError(json.dumps(result, ensure_ascii=False)[:800])
            out_file.write_bytes(audio_bytes)
            manifest[key] = {
                "title": title,
                "file": f"audio/bgm/{key}.mp3",
                "source": "MiniMax " + used_model,
                "prompt": prompt,
                "trace_id": result.get("trace_id", ""),
                "extra_info": result.get("extra_info", {}),
            }
            MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
            print(f"saved {out_file.name} via {used_model} ({out_file.stat().st_size} bytes)", flush=True)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            failures.append((key, f"HTTP {exc.code}: {detail[:600]}"))
            print(f"failed {key}: HTTP {exc.code}", flush=True)
            if exc.code in (401, 403):
                break
        except Exception as exc:
            failures.append((key, repr(exc)))
            print(f"failed {key}: {exc!r}", flush=True)
        time.sleep(2)

    if failures:
        print("failures:", json.dumps(failures, ensure_ascii=False, indent=2), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
