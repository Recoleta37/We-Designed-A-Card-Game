import json
import re
from pathlib import Path

ROOT = Path("C:/Users/liuxi/OneDrive/Desktop/卡牌游戏")
PROJECT = ROOT / "WORLD_PVE_极限测试版2"
SCRIPT_PATH = ROOT / "视觉" / "_extracted_context" / "打字机脚本.txt"

CN_NUMS = {"序": 0, "一": 1, "二": 2, "三": 3, "四": 4, "五": 5, "六": 6, "七": 7, "八": 8}


def heading_before(lines, idx):
    j = idx - 1
    while j >= 0 and not lines[j]:
        j -= 1
    if j >= 1 and lines[j - 1] == "Image2 Prompt":
        j -= 2
        while j >= 0 and not lines[j]:
            j -= 1
    return j, lines[j] if j >= 0 else ""


def chapter_from_line(line, old):
    m = re.search(r"^第([一二三四五六七八])章", line)
    if m:
        return CN_NUMS[m.group(1)]
    if line.startswith("序章"):
        return 0
    return old


def explicit_key(heading, chapter):
    for token in heading.split():
        if token.startswith("c") and "_l" in token:
            return token
        if token.startswith("boss_") or token.startswith("ally_"):
            return token
    if "开局序章" in heading or "Prologue" in heading:
        return "prologue"
    if "游戏标题" in heading:
        return None
    if "序章 Boss 登场" in heading:
        return "boss_魔王？？？"
    if "序章 Boss 战结束" in heading:
        return "c0_l10_after"
    if "世界问题一" in heading:
        return "boss_米凯尔与阿格尼"
    if "世界复战结束" in heading:
        return "c8_l2_after"
    if "回答正确最终问题" in heading:
        return "c8_l9_after"
    if "最终结局" in heading or "Ending" in heading:
        return "ending"
    m = re.search(r"第([一二三四五六七八])章第\s*(\d+)\s*关开始", heading)
    if m:
        return f"c{CN_NUMS[m.group(1)]}_l{int(m.group(2))}"
    m = re.search(r"第\s*(\d+)\s*关结束", heading)
    if m and chapter is not None:
        return f"c{chapter}_l{int(m.group(1))}_after"
    return None


def parse_dialog(block):
    items = []
    speaker = None

    def clean_text(text):
        text = text.strip()
        while text.endswith("。"):
            text = text[:-1]
        return text

    for line in block:
        if not line or line.startswith("### SOURCE") or line.startswith("《真神之死") or line == "Image2 Prompt":
            continue
        if re.match(r"^(序章|第[一二三四五六七八]章)[：:]", line):
            continue
        if line.endswith("：") and len(line) <= 18:
            speaker = line[:-1]
            continue
        text = clean_text(line)
        if text:
            items.append({"speaker": speaker or "Narrator", "text": text})
    return items


def image_for_key(key, idx, count, speaker):
    base = "assets/story/"
    if key == "prologue":
        seq = ["prologue/0-1.png", "prologue/0-2.png", "prologue/0-3.png"]
        return base + seq[min(idx, len(seq) - 1)]
    if key == "boss_魔王？？？":
        return base + "prologue/boss.png"
    if key == "c0_l10_after":
        return base + "prologue/0-3.png"

    m = re.match(r"c(\d+)_l(\d+)(_after)?$", key)
    if m:
        ch, lv, after = int(m.group(1)), int(m.group(2)), bool(m.group(3))
        if ch == 1:
            if lv == 10 and after:
                return base + ("chapter1/ally_elf_envoy.png" if speaker == "精灵使者" else "chapter1/memory_1.png")
            num = lv + 1 if after else lv
            return base + f"chapter1/1-{min(num, 10)}.png"
        if ch == 2:
            if lv == 5 and after:
                return base + "chapter2/memory_count.png"
            if lv == 10 and after:
                seq = [f"chapter2/memory_mig_{i}.png" for i in range(1, 9)]
                return base + seq[min(idx * len(seq) // max(count, 1), len(seq) - 1)]
            num = lv + 1 if after else lv
            if num == 5:
                num = 4
            return base + f"chapter2/2-{min(num, 9)}.png"
        if ch == 3:
            if lv == 1 and not after:
                return base + "chapter3/ally_ranger.png"
            if lv == 10 and after:
                seq = [f"chapter3/memory_{i}.png" for i in range(1, 5)]
                return base + seq[min(idx * len(seq) // max(count, 1), len(seq) - 1)]
            if lv in (8, 9) and after:
                return base + f"chapter3/3-{lv}.png"
        if ch == 4:
            if lv == 10 and after:
                seq = [f"chapter4/memory_{i}.png" for i in range(1, 5)]
                return base + seq[min(idx * len(seq) // max(count, 1), len(seq) - 1)]
            num = lv + 1 if after else lv
            return base + f"chapter4/4-{min(num, 10)}.png"
        if ch == 5:
            if lv == 10 and after:
                seq = [f"chapter5/memory_{i}.png" for i in range(1, 5)]
                return base + seq[min(idx * len(seq) // max(count, 1), len(seq) - 1)]
            num = lv + 1 if after else lv
            return base + f"chapter5/5-{min(num, 10)}.png"
        if ch == 6:
            if lv == 10 and after:
                seq = [f"chapter6/memory_{i}.png" for i in range(1, 6)]
                return base + seq[min(idx * len(seq) // max(count, 1), len(seq) - 1)]
            num = lv + 1 if after else lv
            return base + f"chapter6/6-{min(num, 10)}.png"
        if ch == 7:
            if lv == 10 and after:
                if idx == 0:
                    return base + "chapter7/return_beginning.png"
                return base + ("chapter7/awakening.png" if idx >= max(count - 3, 1) else "chapter7/lysorn_end.png")
            num = lv + 1 if after else lv
            return base + f"chapter7/7-{min(num, 10)}.png"
        if ch == 8:
            if lv == 2 and after:
                return base + "chapter8/world_rematch_after.png"
            if lv == 9 and after:
                return base + "chapter8/void_island.png"

    boss_images = {
        "boss_阿拉贡": "chapter1/boss.png",
        "boss_吸血鬼伯爵": "chapter2/boss_count.png",
        "boss_偷窃者米格": "chapter2/boss_mig.png",
        "boss_艾琳": "chapter3/boss.png",
        "boss_阿格尼": "chapter4/boss.png",
        "boss_米凯尔": "chapter5/boss.png",
        "boss_伊维尔": "chapter6/boss.png",
        "boss_莱索恩": "chapter7/boss.png",
        "boss_米凯尔与阿格尼": "chapter8/world_rematch.png",
        "boss_最终问题": "chapter8/void_island.png",
    }
    if key in boss_images:
        return base + boss_images[key]

    ally_images = {
        "ally_阿拉贡": "chapter1/ally_aragorn.png",
        "ally_游侠": "chapter3/ally_ranger.png",
        "ally_加百列": "chapter5/ally_gabriel.png",
    }
    if key in ally_images:
        return base + ally_images[key]
    if key == "ending":
        return base + "chapter8/ending.png"
    return ""


def main():
    lines = [line.strip() for line in SCRIPT_PATH.read_text(encoding="utf-8").splitlines()]
    indices = [i for i, line in enumerate(lines) if line == "打字机"]
    headings = [heading_before(lines, i) for i in indices]
    current_chapter = None
    story = {}

    for n, idx in enumerate(indices):
        h_idx, heading = headings[n]
        current_chapter = chapter_from_line(heading, current_chapter)
        key = explicit_key(heading, current_chapter)
        if not key:
            continue
        next_h_idx = headings[n + 1][0] if n + 1 < len(headings) else len(lines)
        parsed = parse_dialog(lines[idx + 1:next_h_idx])
        if not parsed:
            continue
        count = len(parsed)
        for i, item in enumerate(parsed):
            item["image"] = image_for_key(key, i, count, item["speaker"])
        story[key] = parsed

    if "c8_l9_after" in story and "boss_最终问题" not in story:
        story["boss_最终问题"] = story["c8_l9_after"]

    out = PROJECT / "data" / "story.json"
    out.write_text(json.dumps(story, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"wrote {len(story)} scenes to {out}")


if __name__ == "__main__":
    main()
