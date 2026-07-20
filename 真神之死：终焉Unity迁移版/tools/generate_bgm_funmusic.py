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
    "https://dashscope.aliyuncs.com/api/v1/services/audio/music/generation",
]

TRACKS = [
    ("chapter_0", "序章：梦境", "安静、朦胧、梦境感的纯音乐循环，古典西幻RPG背景音乐。柔和钢琴、远处合唱垫音、很轻的弦乐和风铃，速度慢，情绪像真神消失后的不安梦境。不要人声，不要歌词，不要突兀旋律，适合长时间战斗背景。"),
    ("boss_0", "序章 Boss：魔王？？？", "平静但带压迫感的纯音乐循环，古典西幻RPG Boss战背景。低音弦乐、缓慢战鼓、暗色合唱垫音，表现梦境尽头的魔王身影。不要人声，不要歌词，旋律克制，不要太激烈。"),
    ("chapter_1", "第一章：微风如诗", "温柔田野与王城边境的纯音乐循环，古典西幻RPG背景。木吉他、竖琴、长笛、轻弦乐，平静、清澈、带一点启程感。不要人声，不要歌词，适合普通关卡。"),
    ("boss_1", "第一章 Boss：阿拉贡", "骑士与王城守卫主题的纯音乐循环，古典西幻RPG Boss战。铜管很克制、低鼓、弦乐进行，庄重但不过分激烈，表现阿拉贡的责任与城墙。不要人声，不要歌词。"),
    ("chapter_2", "第二章：猩红平原", "猩红荒原与吸血鬼阴影的纯音乐循环，古典西幻RPG背景。低音大提琴、冷钢琴、微弱钟声、暗红色氛围，平静压抑。不要人声，不要歌词。"),
    ("boss_2", "第二章 Boss：偷窃者米格", "吸血鬼与偷窃者的阴影Boss纯音乐循环。拨弦、低音弦乐、轻微钟声、潜行感节奏，安静但紧张，表现米格偷走关键要素。不要人声，不要歌词。"),
    ("chapter_3", "第三章：赤心跃动", "城堡、血脉与记忆的纯音乐循环，古典西幻RPG背景。钢琴、弦乐、轻柔竖琴，带一点温柔悲伤和心跳般的低频脉冲，平静。不要人声，不要歌词。"),
    ("boss_3", "第三章 Boss：艾琳", "吸血鬼公主与赤心真相的Boss纯音乐循环。优雅弦乐、钢琴、低音脉冲，悲伤而克制，略有压迫但不尖锐。不要人声，不要歌词。"),
    ("chapter_4", "第四章：精灵圣地", "森林圣地与古树魔法的纯音乐循环，古典西幻RPG背景。竖琴、木管、柔和弦乐、自然环境感，绿色、宁静、神秘。不要人声，不要歌词。"),
    ("boss_4", "第四章 Boss：阿格尼", "精灵王冠与永生诅咒主题的Boss纯音乐循环。古树低音、弦乐、克制打击、神秘合唱垫音，平静中有沉重命运感。不要人声，不要歌词。"),
    ("chapter_5", "第五章：撒冷", "天使圣城撒冷的纯音乐循环，古典西幻RPG背景。管风琴、竖琴、清亮弦乐、金色圣光氛围，庄严而平静。不要人声，不要歌词。"),
    ("boss_5", "第五章 Boss：米凯尔", "大天使米凯尔主题Boss纯音乐循环。管风琴、低鼓、弦乐、金色合唱垫音，神圣、庄严、克制压迫。不要人声，不要歌词。"),
    ("chapter_6", "第六章：魔境", "魔族领域与暗色边境的纯音乐循环，古典西幻RPG背景。低音弦乐、暗色鼓点、琉特琴碎音、远处火焰氛围，平静但危险。不要人声，不要歌词。"),
    ("boss_6", "第六章 Boss：伊维尔", "魔王伊维尔主题Boss纯音乐循环。沉重弦乐、缓慢战鼓、暗色铜管，悲壮但不吵闹，像旧王亲卫与魔境深处。不要人声，不要歌词。"),
    ("chapter_7", "第七章：终焉", "终焉之地、虚空、白色敌人的纯音乐循环，古典西幻RPG背景。极简钢琴、透明合成垫音、低弦乐、很轻的钟声，空旷、安静、接近结局。不要人声，不要歌词。"),
    ("boss_7", "第七章 Boss：莱索恩", "莱索恩与邪神赐福主题Boss纯音乐循环。空旷低音、缓慢战鼓、弦乐暗涌、遥远合唱垫音，庄严压抑但不要过度激烈。不要人声，不要歌词。"),
    ("chapter_8", "第八章：世界", "世界复战与回到一切开始的纯音乐循环，古典西幻RPG背景。钢琴、温柔弦乐、透明钟声、轻微环境垫音，平静、回望、释然。不要人声，不要歌词。"),
    ("boss_8", "第八章 Boss：米凯尔与阿格尼", "世界复战最终Boss纯音乐循环。钢琴、弦乐、低鼓、金色与森林感交织，庄重、平静、有终局感但不喧闹。不要人声，不要歌词。"),
    ("ending", "终焉之后", "剧情真相与开发者感想的纯音乐循环。温柔钢琴、淡淡弦乐、空气感环境音，安静、真诚、收束故事，像旅程结束后回到起点。不要人声，不要歌词。"),
]


def request_json(url, api_key, prompt):
    payload = {
        "model": "fun-music-v1",
        "input": {
            "prompt": prompt,
            "format": "mp3",
        },
    }
    req = urllib.request.Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=360) as response:
        return json.loads(response.read().decode("utf-8"))


def download(url, path):
    req = urllib.request.Request(url, headers={"User-Agent": "CodexBgmFetcher/1.0"})
    with urllib.request.urlopen(req, timeout=240) as response:
        path.write_bytes(response.read())


def main():
    api_key = os.environ.get("DASHSCOPE_API_KEY")
    if not api_key:
        print("DASHSCOPE_API_KEY is not set.", file=sys.stderr)
        return 2

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    manifest = {}
    if MANIFEST.exists():
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    failures = []
    for key, title, prompt in TRACKS:
        out_file = OUT_DIR / f"{key}.mp3"
        if "--first-only" in sys.argv and key != TRACKS[0][0]:
            continue

        if out_file.exists() and out_file.stat().st_size > 4096:
            print(f"skip existing {key}")
            manifest[key] = {"title": title, "file": f"audio/bgm/{key}.mp3", "prompt": prompt}
            continue

        print(f"generating {key}: {title}", flush=True)
        result = None
        last_error = None
        for endpoint in ENDPOINTS:
            try:
                result = request_json(endpoint, api_key, prompt)
                break
            except urllib.error.HTTPError as exc:
                detail = exc.read().decode("utf-8", errors="replace")
                last_error = f"HTTP {exc.code}: {detail[:500]}"
            except Exception as exc:
                last_error = repr(exc)

        if not result:
            failures.append((key, last_error))
            print(f"failed {key}: {last_error}", flush=True)
            if "AccessDenied" in str(last_error):
                break
            continue

        audio = result.get("output", {}).get("audio", {})
        audio_url = audio.get("url")
        if not audio_url:
            failures.append((key, json.dumps(result, ensure_ascii=False)[:500]))
            print(f"failed {key}: no audio url", flush=True)
            continue

        download(audio_url, out_file)
        manifest[key] = {
            "title": title,
            "file": f"audio/bgm/{key}.mp3",
            "prompt": prompt,
            "request_id": result.get("request_id", ""),
            "duration": result.get("usage", {}).get("duration", 0),
        }
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"saved {out_file.name} ({out_file.stat().st_size} bytes)", flush=True)
        time.sleep(1)

    if failures:
        print("failures:", json.dumps(failures, ensure_ascii=False, indent=2), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
