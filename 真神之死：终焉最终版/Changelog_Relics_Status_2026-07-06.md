# 遗物系统重构 + 状态效果 + 新遗物 — 详细改动说明

> 基于 `docs/Design/Relics_2026-7-6.md` 实现
> 2026-07-06

---

## 文件改动量

| 文件 | +行 | -行 |
|---|---|---|
| `GameTypes.h` | ~29 | — |
| `BattleEngine.h` | ~51 | — |
| `BattleEngine.cpp` | ~790 | ~146 |
| `MainWindow.h` | ~5 | — |
| `MainWindow.cpp` | ~114 | — |

---

## 一、新增数据结构

### 1.1 RelicInstance（GameTypes.h L36-44）

```cpp
struct RelicInstance {
    QString name;
    int uses = -1;         // -1=永久, >0=剩余次数, 0=待清理
    bool slotless = false; // true=一次性不占槽（不计数、不在UI显示）
};
```

`relics_` 类型：`QStringList` → `QVector<RelicInstance>`。所有 `relics_.contains("X")` 改为 `hasRelic("X")`。

### 1.2 StatusEffect（GameTypes.h L46-53）

```cpp
struct StatusEffect {
    QString name;     // "石化"/"脆弱"/"荆棘"/"回响"/"铸剑"
    int layers = 1;
    bool decays = false; // 每回合-1
};
```

`UnitInstance` 新字段：`QVector<StatusEffect> statuses`。

### 1.3 SkillCard.faction（GameTypes.h L75）

```cpp
struct SkillCard {
    QString name;
    QString source;
    QString faction; // ← 新增，用于圣河水滴按阵营匹配
};
```

### 1.4 永久属性加成（BattleEngine.h L301-302）

```cpp
int heroPermanentAtkBonus_ = 0; // 染血符咒等
int heroPermanentHpBonus_ = 0;  // 死亡圣契等（可为负）
```

叠加于 `heroTemplate()` 计算结果之上，跨战斗继承，加入章节快照。

---

## 二、新增公共接口（BattleEngine.h）

### 遗物系统

| 方法 | 签名 | 说明 |
|---|---|---|
| `hasRelic` | `bool (const QString& name) const` | 替代旧 `relics_.contains()` |
| `relicUses` | `int (const QString& name) const` | 查询剩余次数，-1=永久 |
| `consumeRelic` | `bool (const QString& name)` | 次数-1，返回归零的需 cleanup |
| `cleanupZeroUseRelics` | `void ()` | 移除所有 uses==0 的遗物 |
| `activeRelicCount` | `int () const` | 不计 slotless 的占槽数 |
| `showRelicChoice` | `void ()` | 从 showRelicReward 拆出的普通三选一，可独立调用 |
| `addRelic` | `void (name, uses, slotless, isStory)` | 签名变更，增加三个参数 |

### 状态系统

| 方法 | 签名 |
|---|---|
| `addStatus` | `void (UnitInstance*, name, layers=1, decays=false)` |
| `removeStatus` | `void (UnitInstance*, const QString& name)` |
| `hasStatus` | `bool (const UnitInstance*, const QString& name) const` |
| `statusLayers` | `int (const UnitInstance*, const QString& name) const` |
| `tickStatuses` | `void ()` — 回合结束遍历全场，decays 状态-1 |

### 其他

| 方法 | 签名 |
|---|---|
| `showGoldAltar` | `bool ()` — Boss 关前金币购遗物弹窗 |
| `forgeActive_` | `bool` — 铸剑 buff 标记（成员变量，非接口） |

---

## 三、遗物变更详情

### 3.1 修改的现有遗物

| 遗物 | 旧 | 新 |
|---|---|---|
| 旧盾 | 全体+8HP | **全体+5护盾**（addShield） |
| 战鼓 | 全体+2ATK | **全体+5ATK** |
| 铜钱袋 | +2金币 | **+5金币** |
| 医疗包 | 主角+5HP | **主角+3HP** |
| 幸运骰子 | 每友军25%+5ATK | **随机1友军+10ATK** |
| 圣河水滴 | 按技能名匹配，+5 | **按天使阵营匹配，+4** |
| 魔王残角 | 每回合魔族+6ATK/-5HP | **仅第1回合** |
| 破碎护符 | 一次性不占槽 | **uses=1 占槽** |
| 旅人靴 | — | **移除** |

铁剑、人王徽记保持原样（每回合叠加）。

### 3.2 新增遗物（9个）

| 遗物 | uses | 触发时机 | 效果 |
|---|---|---|---|
| 锈蚀胸甲 | ∞ | 每回合 | 主角+5护盾 |
| 许愿骨 | 1(一次性) | 立即 | 连弹3次遗物三选一 |
| 染血符咒 | 1(一次性) | 立即 | 主角-5HP, `heroPermanentAtkBonus_`+1 |
| 死亡圣契 | 1(一次性) | 立即 | `heroPermanentHpBonus_`-5, 全体回满 |
| 黄金祭坛 | ∞ | Boss关前 | 20+(N×10)金币购遗物，可多次 |
| 断爪 | 3 | 战斗开始 | 敌方全体+2脆弱(decays) |
| 铁玫瑰 | ∞ | 战斗开始 | 己方全体+1荆棘 |
| 高塔石碑 | 1 | 战斗开始 | 己方全体+1回响 |
| 石像鬼雕像 | 3 | 战斗开始 | 敌方全体+1石化(decays) |
| 鎏金坩埚 | 1(一次性) | 立即 | 扣20金生成铸剑技能牌，槽满弹替换对话框 |

### 3.3 遗物槽 5→7

`kMaxRelics = 7`。剧情遗物固定槽位逻辑删除，改为正常添加 + `isStory` 标记控制替换对话框警告。

### 3.4 三选一显示次数

遗物选择对话框格式：`遗物名  |  描述  |  ∞/×N/一次性`

---

## 四、状态系统详情

### 4.1 状态行为

| 状态 | 属性 | 衰减 | 触发位置 | 机制 |
|---|---|---|---|---|
| 石化 | 负面 | ✓ | `allAttack()` L756 | 跳过攻击 |
| 脆弱 | 负面 | ✓ | `dealDamage()` L635 | 受伤×1.5 |
| 荆棘 | 正面 | ✗ | `dealDamage()` L651-656 | 反伤=层数，防递归(原因!='荆棘') |
| 回响 | 正面 | ✗ | `castSkill()` L1015-1016 | 技能循环(1+N)次 |
| 铸剑 | 正面 | ✗ | `castSkill()` L993 / `allAttack()` L757 | 伤害×3，消耗一层 |

### 4.2 衰减时机

`tickStatuses()` 在 `runRound()` 末尾调用，顺序：
```
combat → cleanup → ++battleRound → applyRelicsStart → refresh → tickStatuses → victory/defeat
```

### 4.3 不跨战斗继承

`preparePlayerForBattle()` 中 `u->statuses.clear()`。`createUnit()` 新建时自然为空。

### 4.4 铸剑技能（特殊）

- 牌面：橙色底色 `#e8934b`，选中 `#f0a050`
- 释放：主角 +1 层铸剑状态（`decays=false`）
- 铸剑可回响（去掉 break）
- 层数叠加：每次消耗一层，N 层 = N 次 ×3
- `forgeActive_` 标记由 `castSkill()`/`allAttack()` 首尾管理，`dealDamage()` 检测并按 ×3
- AOE 全目标翻倍（标记生命周期覆盖所有 damage 调用）
- 消耗：手动递减 `statuses[i].layers`，归零 removeStatus

---

## 五、UI 变更

### 5.1 棋盘卡牌状态显示

`BoardCard` 新增 `statusLabel`（`MainWindow.h` L61）。富文本格式：
- 正面（荆棘/回响/铸剑）：绿色 `#27ae60`，在上行
- 负面（石化/脆弱）：红色 `#c0392b`，在下行
- 显示全名+层数，如 `荆棘2 回响1` / `石化1 脆弱2`

### 5.2 技能牌回响标识

`SkillCard` 新增 `echoLabel`（`MainWindow.h` L70）。来源棋子有回响时牌面底部绿色显示 `回响×N`。

### 5.3 遗物面板

- `relicCards` 5→7（`MainWindow.h` L80）
- `RelicCard` 新增 `usesLabel`，显示 `∞` / `×N`
- 一次性遗物不在面板渲染

---

## 六、Bug 修复 & 逻辑调整

### 6.1 回合顺序修正

`applyRelicsStart()` 从 `runRound()` 开头移到 `++battleRound_` **之后**，确保"每回合开始时"效果在战斗结算完毕后触发（而非战斗之前）。

`autoDeployPlayer`/`preparePlayerForBattle` 中的 `applyRelicsStart` 移除。

### 6.2 战斗开始效果修复

`applyRelicsStart()` 改为在 `setupBattle`/**敌人部署后** 调用一次，解决断爪/石像鬼雕像等给敌方上 debuff 时目标不存在的问题。世界 Boss 战同理。

### 6.3 铸剑普通攻击

`allAttack()` 中主角攻击前检查铸剑状态并消耗层数（原本仅 `castSkill` 触发）。

### 6.4 补员空窗口

`showDeckRefillDialog`：`offer.isEmpty() || !anySelectable` 时直接 `rememberFailedRefills`，不弹空对话框。

### 6.5 dealDamage 日志准确性

日志显示 `shieldHit + hpDmg`（计算脆弱/铸剑倍率后的实际伤害），与浮动数字一致。

### 6.6 初始金币

`gold_` 默认值 0→20；`resetRunState` 和 `startGame` 同步。

### 6.7 Boss 战后回血

`finishLevel` 中 `levelIndex==10` 且 `chapterIndex<8` 时，主角 HP 低于一半回复至 `base.hp/2`（向下取整）。

### 6.8 状态衰减调试日志

`tickStatuses()` 每次衰减时写入战斗日志（后续可移除）。

---

## 七、关键设计决策

1. **状态系统用三元组 `(name, layers, decays)`**：铸剑虽非 buff/debuff 也复用此结构
2. **`forgeActive_` 用成员变量**：避免修改 `dealDamage` 签名扩散改动面
3. **回响在 `castSkill` 内循环整个 if-else 链**：确保所有技能类型均可回响
4. **荆棘防递归用 `reason != "荆棘"`**：无需额外参数
5. **鎏金坩埚槽满替换**：复用遗物替换对话框模式，独立实现

---

## 八、后续可做事项

- 移除 `tickStatuses` 调试日志
- 调整遗物平衡数值
- 增加更多状态类型和状态相关技能
- `growAllUnitsMaxHp` 机制评估（每回合全员+1HP上限是否合理）

---

## 九、从极限测试版2补录的未 PR 改动

> 以下为上一版已实现、但当时未同步到 GitHub PR 的视觉与剧情流程改动，现已合并进第三版。

### 9.1 标题界面

- 游戏启动后不再直接进入序章，而是先显示标题界面。
- 使用 `assets/story/prologue/title.png` 作为标题背景，并按 `Qt::KeepAspectRatioByExpanding` 缩放，避免拉伸素材比例。
- 标题文案为 `真神之死：终焉`。
- 标题下方依次淡入关键词：`祝福`、`诅咒`、`真神`、`邪神`。
- 关键词下方继续淡入 `以及一个问题`，并与 `祝福` 左边缘对齐。
- 最后淡入 `开始游戏` 按钮；点击后进入原本的序章流程。

### 9.2 莱索恩梦醒立绘

- `c7_l10_after` 中莱索恩战后梦醒段：
  - `你从梦境惊醒`
  - `梦境结束`
  - `你获得了邪神赐福`
- 上述三页立绘改为 `assets/story/chapter7/awakening.png`。
- 同步更新 `tools/generate_story_json.py`，避免重新生成剧情时丢失该映射。

### 9.3 最终问题后过渡剧情

- 第 8 章第 9 关的最终问题答完后，先播放 `c8_l9_after`，再进入最终 `ending`。
- `c8_l9_after` 文本更新为：
  - `你来到了一片不属于原本世界的空间`
  - `来到了虚空中的孤岛`
  - `除了庞大的能量`
  - `只剩虚无`
  - `这似乎是很久很久以前`
  - `似乎是一切的开始`
- 该段仍使用 `assets/story/chapter8/void_island.png`。
