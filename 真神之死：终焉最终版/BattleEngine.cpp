/// [Recoleta37] Phase 3: BattleEngine 实现
/// Step 1: 基础能力 - 单位创建 / 棋盘查询 / 清理

#include "BattleEngine.h"

#include <algorithm>

#include <QCheckBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QVBoxLayout>

#include "MapManager.h"
#include "StoryManager.h"

namespace
{
QLabel* smallLabel(const QString& text)
{
    QLabel* label = new QLabel(text);
    label->setObjectName("smallLabel");
    label->setWordWrap(true);
    return label;
}
} // anonymous namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

BattleEngine::BattleEngine(QWidget* parent, StoryManager* story, MapManager* map)
    : QObject(parent)
    , parentWidget_(parent)
    , storyManager_(story)
    , mapManager_(map)
{
}

// ============================================================================
// Step 1: 单位创建
// ============================================================================

UnitInstance* BattleEngine::createUnit(const UnitTemplate& t, bool hero, bool boss) const
{
    UnitInstance* u = new UnitInstance;
    u->base = t;
    u->hp = t.hp;
    u->hero = hero;
    u->boss = boss;
    return u;
}

UnitTemplate BattleEngine::heroTemplate(int chapterIndex) const
{
    QVector<QPair<int, int>> stats = {{30,5},{30,5},{45,8},{60,12},{80,16},{110,24},{140,35},{180,50},{250,70}};
    int idx = std::min(chapterIndex, int(stats.size()) - 1);
    int bonus = chapterIndex * 2;
    int worldHp = hasRelic(QStringLiteral("世界")) ? 100 : 0;
    int worldAtk = hasRelic(QStringLiteral("世界")) ? 50 : 0;
    return {QStringLiteral("主角"), QStringLiteral("命运"), QStringLiteral("中排"), QStringLiteral("命运之刃"),
            stats[idx].first + bonus + worldHp + heroHpGrowth_ + heroPermanentHpBonus_,
            stats[idx].second + bonus + worldAtk + heroPermanentAtkBonus_};
}

UnitTemplate BattleEngine::templateByName(const QString& name) const
{
    const UnitTemplate* bestRosterMatch = nullptr;
    for (const UnitTemplate& t : roster_)
    {
        if (t.name != name)
        {
            continue;
        }
        if (!bestRosterMatch || t.atk > bestRosterMatch->atk ||
            (t.atk == bestRosterMatch->atk && t.hp > bestRosterMatch->hp))
        {
            bestRosterMatch = &t;
        }
    }
    if (bestRosterMatch) return *bestRosterMatch;

    if (name == QStringLiteral("游侠")) return {name, QStringLiteral("人族"), QStringLiteral("后排"), QStringLiteral("箭雨"), 44, 16};
    if (name == QStringLiteral("精灵使者")) return {name, QStringLiteral("精灵族"), QStringLiteral("后排"), QStringLiteral("森语祝福"), 46, 12};
    if (name == QStringLiteral("加百列")) return {name, QStringLiteral("天使"), QStringLiteral("中排"), QStringLiteral("圣光"), 70, 14};
    return {name, QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("斩击"), 48, 8};
}

UnitTemplate BattleEngine::makeEnemyTemplate(const QString& name, bool boss, int chapterIndex, int levelIndex) const
{
    int scale = chapterIndex * 8 + levelIndex * 3;
    int hp = (boss ? 90 : 34) + scale * (boss ? 3 : 1);
    int atk = (boss ? 12 : 5) + chapterIndex * 3 + levelIndex;
    QString row = boss ? QStringLiteral("中排") : QStringLiteral("前排");
    if (name.contains(QStringLiteral("弓")) || name.contains(QStringLiteral("术")) || name.contains(QStringLiteral("刺"))) row = QStringLiteral("后排");
    if (name == QStringLiteral("偷窃者米格") && playerUnits_[kHeroSlot]) row = playerUnits_[kHeroSlot]->base.row;
    if (name == QStringLiteral("艾琳")) row = QStringLiteral("前排");
    if (name == QStringLiteral("莱索恩"))
    {
        hp += 10;
        atk += 10;
    }
    QString skill = name;
    if (name == QStringLiteral("阿拉贡")) skill = QStringLiteral("炬火·耀");
    else if (name == QStringLiteral("偷窃者米格")) skill = QStringLiteral("偷窃 / 一无所有");
    else if (name == QStringLiteral("吸血鬼伯爵")) skill = QStringLiteral("吸血");
    else if (name == QStringLiteral("阿格尼")) skill = QStringLiteral("森海永恒 / 永恒毒恶");
    else if (name == QStringLiteral("艾琳")) skill = QStringLiteral("始祖 / 血魔 / 不灭");
    else if (name == QStringLiteral("米凯尔")) skill = QStringLiteral("神御 / 号角 / 六翼制裁");
    else if (name == QStringLiteral("伊维尔")) skill = QStringLiteral("业火 / 魔主 / 炽焰 / 熔岩");
    else if (name == QStringLiteral("莱索恩")) skill = QStringLiteral("终焉 / 四象 / 真·魔主 / 灭尽");
    return {name, QStringLiteral("敌人"), row, skill, hp, atk};
}

// ============================================================================
// Step 1: 棋盘查询
// ============================================================================

/// [Recoleta37] 棋子严格按排分配：前排→{0,1}，中排→{2,3}，后排→{4}
/// 不再 fallback 到其他排，避免前排棋子放入后排空位等逻辑错误
int BattleEngine::slotForRow(const QString& row, const std::array<UnitInstance*, 5>& board) const
{
    QList<int> slotList;
    if (row == QStringLiteral("前排")) slotList = {0, 1};
    else if (row == QStringLiteral("中排")) slotList = {2, 3};
    else slotList = {4};
    for (int s : slotList)
    {
        if (s != kHeroSlot && board[s] == nullptr) return s;
    }
    return -1;
}

/// [Recoleta37] 检查指定排是否有空位，仅查该排槽位，不 fallback
/// 与 slotForRow() 保持一致：棋子只能放入自己属性对应的排
bool BattleEngine::hasEmptySlotForRow(const QString& row) const
{
    QList<int> slotList;
    if (row == QStringLiteral("前排")) slotList = {0, 1};
    else if (row == QStringLiteral("中排")) slotList = {2, 3};
    else slotList = {4};
    for (int s : slotList)
    {
        if (s != kHeroSlot && playerUnits_[s] == nullptr) return true;
    }
    return false;
}

QList<UnitInstance*> BattleEngine::alive(const std::array<UnitInstance*, 5>& board) const
{
    QList<UnitInstance*> result;
    for (UnitInstance* u : board) if (u && u->hp > 0) result << u;
    return result;
}

UnitInstance* BattleEngine::firstAlive(const std::array<UnitInstance*, 5>& board) const
{
    for (UnitInstance* u : board) if (u && u->hp > 0) return u;
    return nullptr;
}

UnitInstance* BattleEngine::lowestHp(const std::array<UnitInstance*, 5>& board) const
{
    UnitInstance* best = nullptr;
    for (UnitInstance* u : board)
    {
        if (u && u->hp > 0 && (!best || u->hp < best->hp)) best = u;
    }
    return best;
}

UnitInstance* BattleEngine::mostWounded(const std::array<UnitInstance*, 5>& board) const
{
    UnitInstance* best = nullptr;
    int bestDeficit = 0;
    for (UnitInstance* u : board)
    {
        if (!u || u->hp <= 0) continue;
        int deficit = u->base.hp - u->hp; // 已损失血量
        if (deficit > bestDeficit)
        {
            bestDeficit = deficit;
            best = u;
        }
    }
    return best;
}

UnitInstance* BattleEngine::highestAtk(const std::array<UnitInstance*, 5>& board) const
{
    UnitInstance* best = nullptr;
    for (UnitInstance* u : board)
    {
        if (u && u->hp > 0 && (!best || u->base.atk > best->base.atk)) best = u;
    }
    return best;
}

// ============================================================================
// Step 1: 棋盘清理
// ============================================================================

void BattleEngine::clearBoard(std::array<UnitInstance*, 5>& board)
{
    for (UnitInstance*& u : board)
    {
        delete u;
        u = nullptr;
    }
}

void BattleEngine::clearAllUnits()
{
    clearBoard(playerUnits_);
    clearBoard(enemyUnits_);
}

bool BattleEngine::removeOneRosterCard(const QString& unitName)
{
    for (int i = 0; i < roster_.size(); ++i)
    {
        if (roster_[i].name == unitName)
        {
            roster_.removeAt(i);
            return true;
        }
    }
    return false;
}

void BattleEngine::saveChapterSnapshot()
{
    auto saveBoard = [](const std::array<UnitInstance*, 5>& board, BoardSnapshot& snapshot) {
        snapshot.present.fill(false);
        for (int i = 0; i < 5; ++i)
        {
            if (board[i])
            {
                snapshot.units[i] = *board[i];
                snapshot.present[i] = true;
            }
        }
    };

    snapshotBattleRound_ = battleRound_;
    snapshotGold_ = gold_;
    snapshotSkillsUsedThisTurn_ = skillsUsedThisTurn_;
    snapshotHeroHpGrowth_ = heroHpGrowth_;
    snapshotFateBladeHealBonus_ = fateBladeHealBonus_;
    snapshotHeroPermanentAtkBonus_ = heroPermanentAtkBonus_;
    snapshotHeroPermanentHpBonus_ = heroPermanentHpBonus_;
    snapshotGoldAltarPurchases_ = goldAltarPurchases_;
    snapshotInBattle_ = inBattle_;
    snapshotEndingShown_ = endingShown_;
    saveBoard(playerUnits_, snapshotPlayerUnits_);
    saveBoard(enemyUnits_, snapshotEnemyUnits_);
    snapshotRoster_ = roster_;
    snapshotSkillSlots_ = skillSlots_;
    snapshotRelics_ = relics_;
    snapshotMigEntranceSkillSnapshot_ = migEntranceSkillSnapshot_;
    snapshotAgniPoisonDamage_ = agniPoisonDamage_;
    snapshotPendingRefillRows_ = pendingRefillRows_;
    snapshotEileenNextReviveSlot_ = eileenNextReviveSlot_;
    snapshotLogLines_ = logLines_;
    chapterSnapshotValid_ = true;
}

void BattleEngine::restoreChapterSnapshot()
{
    if (!chapterSnapshotValid_)
    {
        return;
    }

    auto restoreBoard = [this](std::array<UnitInstance*, 5>& board, const BoardSnapshot& snapshot) {
        clearBoard(board);
        for (int i = 0; i < 5; ++i)
        {
            if (snapshot.present[i])
            {
                board[i] = new UnitInstance(snapshot.units[i]);
            }
        }
    };

    battleRound_ = snapshotBattleRound_;
    gold_ = snapshotGold_;
    skillsUsedThisTurn_ = snapshotSkillsUsedThisTurn_;
    heroHpGrowth_ = snapshotHeroHpGrowth_;
    fateBladeHealBonus_ = snapshotFateBladeHealBonus_;
    heroPermanentAtkBonus_ = snapshotHeroPermanentAtkBonus_;
    heroPermanentHpBonus_ = snapshotHeroPermanentHpBonus_;
    goldAltarPurchases_ = snapshotGoldAltarPurchases_;
    inBattle_ = snapshotInBattle_;
    endingShown_ = snapshotEndingShown_;
    restoreBoard(playerUnits_, snapshotPlayerUnits_);
    restoreBoard(enemyUnits_, snapshotEnemyUnits_);
    roster_ = snapshotRoster_;
    skillSlots_ = snapshotSkillSlots_;
    relics_ = snapshotRelics_;
    migEntranceSkillSnapshot_ = snapshotMigEntranceSkillSnapshot_;
    agniPoisonDamage_ = snapshotAgniPoisonDamage_;
    pendingRefillRows_ = snapshotPendingRefillRows_;
    eileenNextReviveSlot_ = snapshotEileenNextReviveSlot_;
    iyvelOriginalAtk_.clear();
    logLines_ = snapshotLogLines_;
    appendLog(QStringLiteral("已恢复到本大章节开始时的状态。"));
    if (refreshCallback_) refreshCallback_();
}

void BattleEngine::resetRunState()
{
    battleRound_ = 1;
    gold_ = 20;
    skillsUsedThisTurn_ = 0;
    heroHpGrowth_ = 0;
    fateBladeHealBonus_ = 0;
    heroPermanentAtkBonus_ = 0;
    heroPermanentHpBonus_ = 0;
    goldAltarPurchases_ = 0;
    inBattle_ = false;
    endingShown_ = false;
    chapterSnapshotValid_ = false;
    migEntranceSkillSnapshot_.clear();
    agniPoisonDamage_ = 2;
    iyvelOriginalAtk_.clear();
    eileenNextReviveSlot_ = 1;
    pendingRefillRows_.clear();
    eileenNextReviveSlot_ = 1;
    pendingCombatEvents_.clear();
    eventBatchId_ = 0;
}

// ============================================================================
// Step 2: 描述
// ============================================================================

QString BattleEngine::relicDescription(const QString& relic) const
{
    static const QMap<QString, QString> descriptions = {
        // --- 普通遗物（修改后）---
        {QStringLiteral("铁剑"), QStringLiteral("每回合开始时，己方全体获得2点攻击。")},
        {QStringLiteral("旧盾"), QStringLiteral("战斗开始时，己方全体获得5点护盾。")},
        {QStringLiteral("战鼓"), QStringLiteral("战斗开始时，己方全体获得5点攻击。")},
        {QStringLiteral("铜钱袋"), QStringLiteral("战斗胜利后，获得5金币")},
        {QStringLiteral("破碎护符"), QStringLiteral("主角死亡时，免死并将生命回复1点（每场战斗最多生效一次）")},
        {QStringLiteral("医疗包"), QStringLiteral("每回合开始时，主角回复3点生命。")},
        {QStringLiteral("魔法书"), QStringLiteral("己方全体的技能伤害增加3点。")},
        {QStringLiteral("幸运骰子"), QStringLiteral("战斗开始时，己方随机1个单位获得10点攻击。")},
        {QStringLiteral("人王徽记"), QStringLiteral("每回合开始时，己方人族单位获得4点攻击。")},
        {QStringLiteral("猩红酒杯"), QStringLiteral("己方吸血鬼单位造成伤害时，回复3点生命。")},
        {QStringLiteral("精灵树枝"), QStringLiteral("己方精灵族单位死亡时，对敌方全体造成5点伤害。")},
        {QStringLiteral("圣河水滴"), QStringLiteral("己方天使的治疗技能额外回复4点生命。")},
        {QStringLiteral("魔王残角"), QStringLiteral("战斗开始时，己方魔族单位失去5点生命，获得6点攻击。")},
        // --- 新增普通遗物 ---
        {QStringLiteral("断爪"), QStringLiteral("战斗开始时，给予敌方全体2层脆弱。")},
        {QStringLiteral("许愿骨"), QStringLiteral("立即获得3次遗物奖励。")},
        {QStringLiteral("铁玫瑰"), QStringLiteral("战斗开始时，己方全体获得1层荆棘。")},
        {QStringLiteral("黄金祭坛"), QStringLiteral("boss关卡开始前，可选择花费金币换取遗物奖励")},
        {QStringLiteral("染血符咒"), QStringLiteral("主角失去5点生命，永久增加1点攻击力")},
        {QStringLiteral("锈蚀胸甲"), QStringLiteral("每回合开始时，主角获得5点护盾。")},
        {QStringLiteral("鎏金坩埚"), QStringLiteral("消耗10金币，生成一张主角的特殊技能⌈铸剑⌋。")},
        {QStringLiteral("死亡圣契"), QStringLiteral("主角永久失去5点生命上限，将己方全体的生命回复至上限。")},
        {QStringLiteral("高塔石碑"), QStringLiteral("战斗开始时，己方全体获得1层回响。")},
        {QStringLiteral("石像鬼雕像"), QStringLiteral("战斗开始时，给予敌方全体1层石化。")},
        // --- 剧情遗物（不变）---
        {QStringLiteral("跃动赤心"), QStringLiteral("回合开始治疗全体友军10点生命。")},
        {QStringLiteral("精灵王冠"), QStringLiteral("友军首次死亡时复苏至1点生命。")},
        {QStringLiteral("圣洁六翼"), QStringLiteral("战斗开始时全体友军获得20点护盾。")},
        {QStringLiteral("邪神赐福"), QStringLiteral("友军伤害提升，但每回合损失生命。")},
        {QStringLiteral("世界"), QStringLiteral("主角生命+100、攻击+50，复制技能。")}
    };
    return descriptions.value(relic, QStringLiteral("未登记效果。"));
}

QString BattleEngine::skillDescription(const QString& skill) const
{
    static const QMap<QString, QString> descriptions = {
        {QStringLiteral("斩击"), QStringLiteral("对前排首个敌人造成8点物理伤害。")},
        {QStringLiteral("箭雨"), QStringLiteral("对敌方全体造成4点伤害。")},
        {QStringLiteral("鼓舞"), QStringLiteral("所有友军攻击+2。")},
        {QStringLiteral("守护"), QStringLiteral("主角获得10点护盾。")},
        {QStringLiteral("治疗术"), QStringLiteral("治疗失血最多友军12点生命。")},
        {QStringLiteral("吸血"), QStringLiteral("造成10点伤害并治疗主角10。")},
        {QStringLiteral("血雾"), QStringLiteral("敌方全体攻击-2")},
        {QStringLiteral("赤心爆发"), QStringLiteral("对首个敌人造成20点伤害。")},
        {QStringLiteral("生命转移"), QStringLiteral("简化：治疗主角15。")},
        {QStringLiteral("永生之血"), QStringLiteral("简化：治疗主角10。")},
        {QStringLiteral("精灵箭"), QStringLiteral("造成8点伤害")},
        {QStringLiteral("森语祝福"), QStringLiteral("全体友军攻击+2并治疗4点生命。")},
        {QStringLiteral("毒雾"), QStringLiteral("敌方全体受到3点伤害。")},
        {QStringLiteral("古木再生"), QStringLiteral("治疗双方受伤最重单位25点生命。")},
        {QStringLiteral("古树根须"), QStringLiteral("最低血友军获得15护盾")},
        {QStringLiteral("藤蔓缠绕"), QStringLiteral("所有敌人攻击-1。")},
        {QStringLiteral("圣光"), QStringLiteral("治疗全体友军8。")},
        {QStringLiteral("审判"), QStringLiteral("对攻击最高敌人造成25点伤害。")},
        {QStringLiteral("六翼庇护"), QStringLiteral("全体友军获得12护盾")},
        {QStringLiteral("命运改写"), QStringLiteral("本回合主角首次死亡不会死。")},
        {QStringLiteral("圣河回响"), QStringLiteral("复制一张已有技能牌")},
        {QStringLiteral("火球"), QStringLiteral("对单体造成18点法术伤害。")},
        {QStringLiteral("深渊爪击"), QStringLiteral("优先对后排造成22点伤害。")},
        {QStringLiteral("狂暴"), QStringLiteral("最高攻击友军攻击+10，生命-5。")},
        {QStringLiteral("魔焰"), QStringLiteral("敌方全体受到10点伤害。")},
        {QStringLiteral("邪神赐福"), QStringLiteral("本回合所有友军攻击翻倍。")},
        {QStringLiteral("命运之刃"), QStringLiteral("造成主角攻击力伤害，并治疗全体友军。")},
        {QStringLiteral("铸剑"), QStringLiteral("主角下一次造成的伤害变为3倍（次数可叠加）。")}
    };
    return descriptions.value(skill, QStringLiteral("未登记技能效果。"));
}

bool BattleEngine::isStoryRelic(const QString& relic) const
{
    static const QStringList storyRelics = {
        QStringLiteral("跃动赤心"),
        QStringLiteral("精灵王冠"),
        QStringLiteral("圣洁六翼"),
        QStringLiteral("邪神赐福"),
        QStringLiteral("世界")
    };
    return storyRelics.contains(relic);
}

// ============================================================================
// Step 3: 伤害/治疗/攻击结算
// ============================================================================

int BattleEngine::boardCardIndexOf(UnitInstance* target) const
{
    if (!target) return -1;
    for (int i = 0; i < 5; ++i)
    {
        if (enemyUnits_[i] == target)  return i;       // 0-4
        if (playerUnits_[i] == target) return 5 + i;   // 5-9
    }
    return -1;
}

void BattleEngine::queueFlash(UnitInstance* target)
{
    int idx = boardCardIndexOf(target);
    if (idx >= 0) pendingCombatEvents_.push_back({CombatEvent::Flash, idx, -1, 0, eventBatchId_});
}

bool BattleEngine::isMechanicBossName(const QString& name) const
{
    static const QStringList bosses = {
        QStringLiteral("阿拉贡"),
        QStringLiteral("吸血鬼伯爵"),
        QStringLiteral("偷窃者米格"),
        QStringLiteral("艾琳"),
        QStringLiteral("阿格尼"),
        QStringLiteral("米凯尔"),
        QStringLiteral("伊维尔"),
        QStringLiteral("莱索恩")
    };
    return bosses.contains(name);
}

UnitTemplate BattleEngine::randomDemonTemplate() const
{
    QVector<UnitTemplate> demons = {
        {QStringLiteral("小恶魔"), QStringLiteral("魔族"), QStringLiteral("前排"), QStringLiteral("斩击"), 38, 8},
        {QStringLiteral("魔族战士"), QStringLiteral("魔族"), QStringLiteral("前排"), QStringLiteral("狂暴"), 48, 10},
        {QStringLiteral("火焰术士"), QStringLiteral("魔族"), QStringLiteral("中排"), QStringLiteral("魔焰"), 36, 13},
        {QStringLiteral("深渊刺客"), QStringLiteral("魔族"), QStringLiteral("后排"), QStringLiteral("深渊爪击"), 32, 16}
    };
    return demons[QRandomGenerator::global()->bounded(demons.size())];
}

UnitTemplate BattleEngine::randomVampireTemplate() const
{
    QVector<UnitTemplate> vampires = {
        {QStringLiteral("血仆"), QStringLiteral("吸血鬼"), QStringLiteral("前排"), QStringLiteral("吸血"), 40, 8},
        {QStringLiteral("血术师"), QStringLiteral("吸血鬼"), QStringLiteral("中排"), QStringLiteral("赤心爆发"), 34, 10},
        {QStringLiteral("夜行者"), QStringLiteral("吸血鬼"), QStringLiteral("后排"), QStringLiteral("吸血"), 30, 13},
        {QStringLiteral("血裔"), QStringLiteral("吸血鬼"), QStringLiteral("前排"), QStringLiteral("永生之血"), 52, 6}
    };
    return vampires[QRandomGenerator::global()->bounded(vampires.size())];
}

int BattleEngine::randomCardPrice() const
{
    static const QVector<int> weighted = {3, 4, 4, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 10};
    return weighted[QRandomGenerator::global()->bounded(weighted.size())];
}

UnitTemplate BattleEngine::scaledRewardUnit(UnitTemplate unit, int chapterIndex, int levelIndex) const
{
    const int chapterHp = chapterIndex * 12;
    const int levelHp = std::max(0, levelIndex - 1) * 3;
    const int chapterAtk = chapterIndex * 3;
    const int levelAtk = std::max(0, levelIndex - 1) / 2;
    unit.hp += chapterHp + levelHp;
    unit.atk += chapterAtk + levelAtk;

    if (levelIndex >= 9)
    {
        unit.hp += 8;
        unit.atk += 2;
    }
    if (chapterIndex >= 6)
    {
        unit.hp += 10;
        unit.atk += 2;
    }
    return unit;
}

QVector<UnitTemplate> BattleEngine::rewardPoolForRow(const QString& row, int chapterIndex) const
{
    QVector<UnitTemplate> pool = {
        {QStringLiteral("见习剑士"), QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("斩击"), 42, 7},
        {QStringLiteral("盾卫"), QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("守护"), 58, 4},
        {QStringLiteral("血仆"), QStringLiteral("吸血鬼"), QStringLiteral("前排"), QStringLiteral("吸血"), 40, 8},
        {QStringLiteral("守护天使"), QStringLiteral("天使"), QStringLiteral("前排"), QStringLiteral("六翼庇护"), 60, 6},
        {QStringLiteral("魔族战士"), QStringLiteral("魔族"), QStringLiteral("前排"), QStringLiteral("狂暴"), 54, 9},
        {QStringLiteral("古木守卫"), QStringLiteral("精灵族"), QStringLiteral("前排"), QStringLiteral("古木再生"), 62, 5},

        {QStringLiteral("牧师"), QStringLiteral("人族"), QStringLiteral("中排"), QStringLiteral("治疗术"), 32, 5},
        {QStringLiteral("血术师"), QStringLiteral("吸血鬼"), QStringLiteral("中排"), QStringLiteral("赤心爆发"), 34, 10},
        {QStringLiteral("毒叶法师"), QStringLiteral("精灵族"), QStringLiteral("中排"), QStringLiteral("毒雾"), 35, 9},
        {QStringLiteral("圣光侍从"), QStringLiteral("天使"), QStringLiteral("中排"), QStringLiteral("圣光"), 38, 6},
        {QStringLiteral("圣河少女"), QStringLiteral("天使"), QStringLiteral("中排"), QStringLiteral("圣河回响"), 36, 7},
        {QStringLiteral("火焰术士"), QStringLiteral("魔族"), QStringLiteral("中排"), QStringLiteral("魔焰"), 36, 13},

        {QStringLiteral("弓箭手"), QStringLiteral("人族"), QStringLiteral("后排"), QStringLiteral("箭雨"), 28, 11},
        {QStringLiteral("夜行者"), QStringLiteral("吸血鬼"), QStringLiteral("后排"), QStringLiteral("吸血"), 30, 13},
        {QStringLiteral("精灵射手"), QStringLiteral("精灵族"), QStringLiteral("后排"), QStringLiteral("精灵箭"), 32, 12},
        {QStringLiteral("审判者"), QStringLiteral("天使"), QStringLiteral("后排"), QStringLiteral("审判"), 34, 14},
        {QStringLiteral("小恶魔"), QStringLiteral("魔族"), QStringLiteral("后排"), QStringLiteral("火球"), 30, 15},
        {QStringLiteral("深渊刺客"), QStringLiteral("魔族"), QStringLiteral("后排"), QStringLiteral("深渊爪击"), 32, 16}
    };

    QString preferredFaction;
    if (chapterIndex <= 1) preferredFaction = QStringLiteral("人族");
    else if (chapterIndex <= 3) preferredFaction = QStringLiteral("吸血鬼");
    else if (chapterIndex == 4) preferredFaction = QStringLiteral("精灵族");
    else if (chapterIndex == 5) preferredFaction = QStringLiteral("天使");
    else if (chapterIndex <= 7) preferredFaction = QStringLiteral("魔族");

    QVector<UnitTemplate> filtered;
    for (const UnitTemplate& t : pool)
    {
        if (t.row != row) continue;
        filtered << t;
        if (!preferredFaction.isEmpty() && t.faction == preferredFaction)
        {
            filtered << t;
            filtered << t;
        }
    }
    return filtered;
}

bool BattleEngine::shouldOfferRewards(int levelIndex) const
{
    return levelIndex == 3 || levelIndex == 6 || levelIndex == 9 || levelIndex == 10;
}

void BattleEngine::growAllUnitsMaxHp()
{
    nextEventBatch();
    for (UnitInstance* u : alive(playerUnits_))
    {
        ++u->base.hp;
        ++u->hp;
    }
    for (UnitInstance* u : alive(enemyUnits_))
    {
        ++u->base.hp;
        ++u->hp;
    }
    appendLog(QStringLiteral("回合结算：所有在场棋子生命上限+1。"));
}

void BattleEngine::growHeroAfterChapterBoss()
{
    heroHpGrowth_ += 10;
    fateBladeHealBonus_ += 2;
    if (UnitInstance* hero = playerUnits_[kHeroSlot])
    {
        hero->base.hp += 10;
        hero->hp += 10;
        appendLog(QStringLiteral("章节Boss战结束：主角生命上限与生命+10，命运之刃治疗量+2。"));
    }
}

void BattleEngine::rememberFailedRefills(const QStringList& deadRows, const QStringList& filledRows)
{
    QStringList remainingFilled = filledRows;
    for (const QString& row : deadRows)
    {
        const int filledIndex = remainingFilled.indexOf(row);
        if (filledIndex >= 0)
        {
            remainingFilled.removeAt(filledIndex);
            continue;
        }
        if (hasEmptySlotForRow(row))
        {
            pendingRefillRows_ << row;
            appendLog(QStringLiteral("补员暂缺：%1没有合适棋子，之后买入同排棋子会自动上场。").arg(row));
        }
    }
}

bool BattleEngine::tryAutoFillPendingRefill(const UnitTemplate& unit)
{
    int pendingIndex = pendingRefillRows_.indexOf(unit.row);
    if (pendingIndex < 0) return false;
    int slot = slotForRow(unit.row, playerUnits_);
    if (slot < 0 || playerUnits_[slot] != nullptr) return false;

    playerUnits_[slot] = createUnit(unit, false, false);
    pendingRefillRows_.removeAt(pendingIndex);
    appendLog(QStringLiteral("自动补员：%1填补%2空位。").arg(unit.name, unit.row));
    queueFlash(playerUnits_[slot]);
    if (refreshCallback_) refreshCallback_();
    return true;
}

void BattleEngine::dealDamage(UnitInstance* target, int amount, const QString& reason, UnitInstance* source)
{
    if (!target || amount <= 0) return;
    if (target->boss && target->base.name == QStringLiteral("莱索恩") && battleRound_ <= 6)
    {
        queueFlash(target);
        appendLog(QStringLiteral("终焉：莱索恩在前六回合没有受到伤害。"));
        return;
    }
    if (target->boss && target->base.name == QStringLiteral("米凯尔") && battleRound_ % 6 != 0)
    {
        queueFlash(target);
        appendLog(QStringLiteral("绝对存续的圣洁神御：米凯尔没有受到伤害。"));
        return;
    }
    int shieldHit = std::min(target->shield, amount);
    target->shield -= shieldHit;
    int hpDmg = amount - shieldHit;

    // 脆弱：受到的伤害+50%（向下取整）
    if (hpDmg > 0 && hasStatus(target, QStringLiteral("脆弱")))
        hpDmg = int(hpDmg * 1.5);

    // 铸剑：主角技能伤害x3（由 castSkill 激活和清除，支持 AOE 全体翻倍）
    if (hpDmg > 0 && forgeActive_)
        hpDmg *= 3;

    int totalDmg = shieldHit + hpDmg;
    target->hp -= hpDmg;
    logLines_ << QStringLiteral("%1 造成 %2 伤害 -> %3").arg(reason).arg(totalDmg).arg(target->base.name);
    while (logLines_.size() > 80) logLines_.removeFirst();
    if (hpDmg > 0)
    {
        int tidx = boardCardIndexOf(target);
        if (tidx >= 0)
            pendingCombatEvents_.push_back({CombatEvent::Damage, tidx, boardCardIndexOf(source), hpDmg, eventBatchId_});

        // 荆棘：受到攻击时反伤=层数（不触发荆棘链）
        if (source && hasStatus(target, QStringLiteral("荆棘")) && reason != QStringLiteral("荆棘"))
        {
            int reflect = statusLayers(target, QStringLiteral("荆棘"));
            dealDamage(source, reflect, QStringLiteral("荆棘"), target);
        }
    }
}

void BattleEngine::heal(UnitInstance* target, int amount)
{
    if (!target || amount <= 0) return;
    target->hp = std::min(target->hp + amount, target->base.hp);
    int idx = boardCardIndexOf(target);
    if (idx >= 0)
        pendingCombatEvents_.push_back({CombatEvent::Heal, idx, -1, amount, eventBatchId_});
}

void BattleEngine::addShield(UnitInstance* target, int amount, const QString& /*reason*/)
{
    if (!target || amount <= 0) return;
    target->shield += amount;
    int idx = boardCardIndexOf(target);
    if (idx >= 0)
        pendingCombatEvents_.push_back({CombatEvent::ShieldGain, idx, -1, amount, eventBatchId_});
}

// ============================================================================
// 状态效果
// ============================================================================

void BattleEngine::addStatus(UnitInstance* target, const QString& name, int layers, bool decays)
{
    if (!target || layers <= 0) return;
    // 已有同名状态则叠加层数
    for (int i = 0; i < target->statuses.size(); ++i)
    {
        if (target->statuses[i].name == name)
        {
            target->statuses[i].layers += layers;
            return;
        }
    }
    target->statuses.push_back({name, layers, decays});
}

void BattleEngine::removeStatus(UnitInstance* target, const QString& name)
{
    if (!target) return;
    for (int i = 0; i < target->statuses.size(); ++i)
    {
        if (target->statuses[i].name == name)
        {
            target->statuses.removeAt(i);
            return;
        }
    }
}

bool BattleEngine::hasStatus(const UnitInstance* target, const QString& name) const
{
    if (!target) return false;
    for (const StatusEffect& s : target->statuses)
    {
        if (s.name == name && s.layers > 0) return true;
    }
    return false;
}

int BattleEngine::statusLayers(const UnitInstance* target, const QString& name) const
{
    if (!target) return 0;
    for (const StatusEffect& s : target->statuses)
    {
        if (s.name == name) return s.layers;
    }
    return 0;
}

void BattleEngine::tickStatuses()
{
    auto tick = [this](std::array<UnitInstance*, 5>& board, const QString& side) {
        for (UnitInstance* u : board)
        {
            if (!u) continue;
            for (int i = u->statuses.size() - 1; i >= 0; --i)
            {
                if (!u->statuses[i].decays) continue;
                appendLog(QStringLiteral("状态衰减：%1 %2 %3 -> %4")
                              .arg(side)
                              .arg(u->base.name)
                              .arg(u->statuses[i].name)
                              .arg(u->statuses[i].layers)
                              .arg(u->statuses[i].layers - 1));
                --u->statuses[i].layers;
                if (u->statuses[i].layers <= 0)
                {
                    appendLog(QStringLiteral("状态消除：%1 %2 %3")
                                  .arg(side).arg(u->base.name).arg(u->statuses[i].name));
                    u->statuses.removeAt(i);
                }
            }
        }
    };
    tick(playerUnits_, QStringLiteral("己方"));
    tick(enemyUnits_, QStringLiteral("敌方"));
}

void BattleEngine::allAttack(std::array<UnitInstance*, 5>& attackers,
                              std::array<UnitInstance*, 5>& defenders,
                              bool playerSide)
{
    nextEventBatch();
    for (UnitInstance* u : alive(attackers))
    {
        // 石化：本回合无法攻击
        if (hasStatus(u, QStringLiteral("石化"))) continue;

        // 铸剑：主角普通攻击也触发 x3，消耗一层
        if (playerSide && u->hero && hasStatus(u, QStringLiteral("铸剑")))
        {
            forgeActive_ = true;
            const int remaining = statusLayers(u, QStringLiteral("铸剑"));
            if (remaining <= 1)
                removeStatus(u, QStringLiteral("铸剑"));
            else
            {
                for (int i = 0; i < u->statuses.size(); ++i)
                {
                    if (u->statuses[i].name == QStringLiteral("铸剑"))
                    {
                        --u->statuses[i].layers;
                        break;
                    }
                }
            }
            appendLog(QStringLiteral("铸剑：本次攻击伤害变3倍（剩余%1次）。").arg(remaining - 1));
        }

        UnitInstance* target = firstAlive(defenders);
        if (!target) { forgeActive_ = false; return; }
        int dmg = u->base.atk;
        if (playerSide && hasRelic(QStringLiteral("世界"))) dmg += 50;
        dealDamage(target, dmg, u->base.name, u);
        if (playerSide && hasRelic(QStringLiteral("猩红酒杯")) && u->base.faction == QStringLiteral("吸血鬼")) heal(u, 3);

        forgeActive_ = false;
        if (refreshCallback_) refreshCallback_();
    }
}

void BattleEngine::bossMechanics(int chapterIndex)
{
    nextEventBatch();
    for (UnitInstance* boss : alive(enemyUnits_))
    {
        if (!boss->boss) continue;
        QString n = boss->base.name;
        if (n == QStringLiteral("阿拉贡"))
        {
            boss->base.atk += 2;
            queueFlash(boss);
            appendLog(QStringLiteral("炬火·耀：阿拉贡自身攻击+2。"));
            if (battleRound_ % 3 == 0)
            {
                for (UnitInstance* e : alive(enemyUnits_))
                {
                    e->base.atk += 3;
                    queueFlash(e);
                }
                appendLog(QStringLiteral("炬火·耀：敌方全体攻击+3。"));
            }
        }
        else if (n == QStringLiteral("吸血鬼伯爵"))
        {
            UnitInstance* target = firstAlive(playerUnits_);
            int before = target ? target->hp : 0;
            dealDamage(target, 10, QStringLiteral("吸血"), boss);
            if (target && target->hp < before) heal(boss, before - target->hp);
        }
        else if (n == QStringLiteral("偷窃者米格"))
        {
            if (battleRound_ % 3 == 0)
            {
                QVector<UnitInstance*> candidates;
                for (UnitInstance* p : alive(playerUnits_))
                {
                    if (p->hero) continue;
                    if (slotForRow(p->base.row, enemyUnits_) >= 0) candidates << p;
                }
                if (!candidates.isEmpty())
                {
                    UnitInstance* stolen = candidates[QRandomGenerator::global()->bounded(candidates.size())];
                    UnitTemplate copy = stolen->base;
                    copy.name = QStringLiteral("偷来。") + copy.name;
                    copy.atk += 2;
                    int s = slotForRow(copy.row, enemyUnits_);
                    if (s >= 0)
                    {
                        enemyUnits_[s] = createUnit(copy, false, false);
                        enemyUnits_[s]->hp = std::max(1, std::min(stolen->hp, enemyUnits_[s]->base.hp));
                        queueFlash(enemyUnits_[s]);
                        appendLog(QStringLiteral("偷窃：米格偷来了 %1。").arg(stolen->base.name));
                        for (UnitInstance*& p : playerUnits_)
                        {
                            if (p == stolen)
                            {
                                delete p;
                                p = nullptr;
                                break;
                            }
                        }
                    }
                }
            }
            if (!migEntranceSkillSnapshot_.isEmpty())
            {
                for (int i = 0; i < 3; ++i)
                {
                    const SkillCard sk = migEntranceSkillSnapshot_[QRandomGenerator::global()->bounded(migEntranceSkillSnapshot_.size())];
                    castMigStolenSkill(sk.name, chapterIndex, boss);
                }
            }
        }
        else if (n == QStringLiteral("艾琳"))
        {
            if (battleRound_ % 4 == 0)
            {
                for (int i = 0; i < 2; ++i)
                {
                    UnitTemplate t = randomVampireTemplate();
                    int s = slotForRow(t.row, enemyUnits_);
                    if (s >= 0)
                    {
                        enemyUnits_[s] = createUnit(t, false, false);
                        queueFlash(enemyUnits_[s]);
                    }
                }
                appendLog(QStringLiteral("始祖：艾琳召唤吸血鬼眷属。"));
            }
            heal(boss, 12);
            for (int frontSlot : {0, 1})
            {
                if (playerUnits_[frontSlot] && playerUnits_[frontSlot]->hp > 0)
                {
                    dealDamage(playerUnits_[frontSlot], 12, QStringLiteral("血魔"), boss);
                }
            }
            appendLog(QStringLiteral("血魔：艾琳治疗自身并撕裂玩家前排。"));
        }
        else if (n == QStringLiteral("阿格尼"))
        {
            for (UnitInstance* p : alive(playerUnits_)) dealDamage(p, agniPoisonDamage_, QStringLiteral("永恒毒恶"), boss);
            appendLog(QStringLiteral("永恒毒恶：本回合造成%1点伤害。").arg(agniPoisonDamage_));
            agniPoisonDamage_ += 2;
        }
        else if (n == QStringLiteral("米凯尔") && battleRound_ % 3 == 0)
        {
            appendLog(QStringLiteral("米凯尔在合唱团吹奏号角。"));
            dealDamage(highestAtk(playerUnits_), 20, QStringLiteral("永恒燃烧的六翼制裁"), boss);
        }
        else if (n == QStringLiteral("伊维尔"))
        {
            for (UnitInstance* p : alive(playerUnits_)) dealDamage(p, 2, QStringLiteral("业火"), boss);
            if (battleRound_ % 4 == 0)
            {
                for (int i = 0; i < 2; ++i)
                {
                    UnitTemplate t = randomDemonTemplate();
                    int s = slotForRow(t.row, enemyUnits_);
                    if (s >= 0)
                    {
                        enemyUnits_[s] = createUnit(t, false, false);
                        queueFlash(enemyUnits_[s]);
                    }
                }
                appendLog(QStringLiteral("魔主：伊维尔召唤魔族棋子。"));
            }
            for (UnitInstance* p : alive(playerUnits_))
            {
                if (!iyvelOriginalAtk_.contains(p)) iyvelOriginalAtk_[p] = p->base.atk;
                const int originalAtk = iyvelOriginalAtk_[p];
                if (p->base.atk * 2 >= originalAtk)
                {
                    p->base.atk = std::max(0, p->base.atk - 1);
                    queueFlash(p);
                }
            }
            boss->base.atk += 3;
            queueFlash(boss);
            appendLog(QStringLiteral("熔岩：伊维尔攻击+3。"));
        }
        else if (n == QStringLiteral("莱索恩"))
        {
            int roll = QRandomGenerator::global()->bounded(4);
            if (roll == 0) addShield(boss, 20, QStringLiteral("四象"));
            if (roll == 1) heal(boss, 10);
            if (roll == 2) boss->base.atk *= 2;
            if (roll == 3) for (UnitInstance* p : alive(playerUnits_)) dealDamage(p, 4, QStringLiteral("四象"), boss);
            queueFlash(boss);
            if (battleRound_ % 4 == 0)
            {
                for (int i = 0; i < 3; ++i)
                {
                    UnitTemplate t = randomDemonTemplate();
                    int s = slotForRow(t.row, enemyUnits_);
                    if (s >= 0)
                    {
                        enemyUnits_[s] = createUnit(t, false, false);
                        queueFlash(enemyUnits_[s]);
                    }
                }
                appendLog(QStringLiteral("真·魔主：莱索恩召唤魔族棋子。"));
            }
            for (UnitInstance* p : alive(playerUnits_))
            {
                if (!p->hero && p->aliveRounds >= 12)
                {
                    dealDamage(p, p->hp + p->shield, QStringLiteral("灭尽"), boss);
                }
            }
        }
    }
    if (refreshCallback_) refreshCallback_();
}

bool BattleEngine::enemiesDefeated() const
{
    for (UnitInstance* u : enemyUnits_) if (u && u->hp > 0) return false;
    return true;
}

bool BattleEngine::playerDefeated() const
{
    return playerUnits_[kHeroSlot] == nullptr || playerUnits_[kHeroSlot]->hp <= 0;
}

// ============================================================================
// Step 4: 技能系统
// ============================================================================

void BattleEngine::generateSkills()
{
    for (UnitInstance* u : alive(playerUnits_))
    {
        ++u->aliveRounds;
        if (u->aliveRounds % 2 == 0 && skillSlots_.size() < kMaxSkills)
        {
            skillSlots_.push_back({u->base.skill, u->base.name, u->base.faction});
            logLines_ << QStringLiteral("%1 生成技能：%2").arg(u->base.name, u->base.skill);
            while (logLines_.size() > 80) logLines_.removeFirst();
        }
    }
}

void BattleEngine::castSkill(const QString& name, int chapterIndex)
{
    nextEventBatch();
    logLines_ << QStringLiteral("释放技能：%1").arg(name);
    while (logLines_.size() > 80) logLines_.removeFirst();
    int bonus = hasRelic(QStringLiteral("魔法书")) ? 3 : 0;
    // 查找技能来源棋子（用于伤害动画闪烁）和阵营（用于圣河水滴）
    UnitInstance* skillSource = nullptr;
    QString skillFaction;
    for (const auto& sk : skillSlots_) {
        if (sk.name == name) {
            skillFaction = sk.faction;
            for (UnitInstance* u : playerUnits_) {
                if (u && u->base.name == sk.source) { skillSource = u; break; }
            }
            break;
        }
    }

    // 铸剑 buff：主角释放技能前检查铸剑层数，每层提供一次 x3，消耗一层
    if (skillSource && skillSource->hero && hasStatus(skillSource, QStringLiteral("铸剑")))
    {
        forgeActive_ = true;
        const int remaining = statusLayers(skillSource, QStringLiteral("铸剑"));
        if (remaining <= 1)
            removeStatus(skillSource, QStringLiteral("铸剑"));
        else
        {
            // 减一层（addStatus 叠加逻辑不适用于减法，直接修改层数）
            for (int i = 0; i < skillSource->statuses.size(); ++i)
            {
                if (skillSource->statuses[i].name == QStringLiteral("铸剑"))
                {
                    --skillSource->statuses[i].layers;
                    break;
                }
            }
        }
        appendLog(QStringLiteral("铸剑：本次技能伤害变3倍（剩余%1次）。").arg(remaining - 1));
    }

    // 回响：来源棋子每层回响额外打出一次
    const int echoLayers = skillSource ? statusLayers(skillSource, QStringLiteral("回响")) : 0;
    const int totalCasts = 1 + echoLayers;

    for (int cast = 0; cast < totalCasts; ++cast)
    {
        if (name == QStringLiteral("斩击")) dealDamage(firstAlive(enemyUnits_), 8 + bonus, name, skillSource);
        else if (name == QStringLiteral("箭雨") || name == QStringLiteral("魔焰"))
        {
            for (UnitInstance* e : alive(enemyUnits_)) dealDamage(e, (name == QStringLiteral("箭雨") ? 4 : 10) + bonus, name, skillSource);
        }
        else if (name == QStringLiteral("鼓舞")) for (UnitInstance* u : alive(playerUnits_)) u->base.atk += 2;
        else if (name == QStringLiteral("守护") && playerUnits_[kHeroSlot]) addShield(playerUnits_[kHeroSlot], 10, QStringLiteral("守护"));
        else if (name == QStringLiteral("治疗术")) heal(mostWounded(playerUnits_), 12 + ((hasRelic(QStringLiteral("圣河水滴")) && skillFaction == QStringLiteral("天使")) ? 4 : 0));
        else if (name == QStringLiteral("吸血")) { dealDamage(firstAlive(enemyUnits_), 10 + bonus, name, skillSource); heal(playerUnits_[kHeroSlot], 10); }
        else if (name == QStringLiteral("血雾")) for (UnitInstance* e : alive(enemyUnits_)) e->base.atk = std::max(0, e->base.atk - 2);
        else if (name == QStringLiteral("赤心爆发")) dealDamage(firstAlive(enemyUnits_), 20 + bonus, name, skillSource);
        else if (name == QStringLiteral("永生之血")) heal(playerUnits_[kHeroSlot], 10);
        else if (name == QStringLiteral("精灵箭")) dealDamage(firstAlive(enemyUnits_), 8 + bonus, name, skillSource);
        else if (name == QStringLiteral("森语祝福"))
        {
            for (UnitInstance* u : alive(playerUnits_))
            {
                u->base.atk += 2;
                heal(u, 4);
            }
        }
        else if (name == QStringLiteral("毒雾")) for (UnitInstance* e : alive(enemyUnits_)) dealDamage(e, 3 + bonus, name, skillSource);
        else if (name == QStringLiteral("古木再生"))
        {
            heal(mostWounded(playerUnits_), 25);
            heal(mostWounded(enemyUnits_), 25);
        }
        else if (name == QStringLiteral("古树根须")) { if (UnitInstance* u = lowestHp(playerUnits_)) addShield(u, 15, QStringLiteral("古树根须")); }
        else if (name == QStringLiteral("藤蔓缠绕")) for (UnitInstance* e : alive(enemyUnits_)) e->base.atk = std::max(0, e->base.atk - 1);
        else if (name == QStringLiteral("圣光")) for (UnitInstance* u : alive(playerUnits_)) heal(u, 8 + ((hasRelic(QStringLiteral("圣河水滴")) && skillFaction == QStringLiteral("天使")) ? 4 : 0));
        else if (name == QStringLiteral("审判")) dealDamage(highestAtk(enemyUnits_), 25 + bonus, name, skillSource);
        else if (name == QStringLiteral("六翼庇护")) for (UnitInstance* u : alive(playerUnits_)) addShield(u, 12, QStringLiteral("六翼庇护"));
        else if (name == QStringLiteral("命运改写") && playerUnits_[kHeroSlot]) playerUnits_[kHeroSlot]->protectedDeath = true;
        else if (name == QStringLiteral("圣河回响") && !skillSlots_.isEmpty() && skillSlots_.size() < kMaxSkills) skillSlots_.push_back(skillSlots_.last());
        else if (name == QStringLiteral("火球")) dealDamage(firstAlive(enemyUnits_), 18 + bonus, name, skillSource);
        else if (name == QStringLiteral("深渊爪击")) dealDamage(enemyUnits_[4] ? enemyUnits_[4] : firstAlive(enemyUnits_), 22 + bonus, name, skillSource);
        else if (name == QStringLiteral("狂暴")) { if (UnitInstance* u = highestAtk(playerUnits_)) { u->base.atk += 10; u->hp -= 5; } }
        else if (name == QStringLiteral("邪神赐福")) for (UnitInstance* u : alive(playerUnits_)) u->base.atk *= 2;
        else if (name == QStringLiteral("命运之刃"))
        {
            dealDamage(firstAlive(enemyUnits_), playerUnits_[kHeroSlot] ? int(playerUnits_[kHeroSlot]->base.atk * (1.0 + chapterIndex * 0.1)) : 0, name, skillSource);
            const int healAmount = 5 + fateBladeHealBonus_;
            for (UnitInstance* u : alive(playerUnits_)) heal(u, healAmount);
        }
        else if (name == QStringLiteral("铸剑"))
        {
            // 铸剑技能：给主角叠加一层铸剑 buff。
            if (UnitInstance* hero = playerUnits_[kHeroSlot])
            {
                addStatus(hero, QStringLiteral("铸剑"), 1, false);
                appendLog(QStringLiteral("铸剑：主角获得一层铸剑（当前%1层）。").arg(statusLayers(hero, QStringLiteral("铸剑"))));
            }
        }
    }

    forgeActive_ = false;
    if (refreshCallback_) refreshCallback_();
}

void BattleEngine::castMigStolenSkill(const QString& name, int chapterIndex, UnitInstance* mig)
{
    Q_UNUSED(chapterIndex);
    if (!mig) return;
    nextEventBatch();
    queueFlash(mig);
    appendLog(QStringLiteral("一无所有：米格使用了%1。").arg(name));

    if (name == QStringLiteral("斩击")) dealDamage(firstAlive(playerUnits_), 8, name, mig);
    else if (name == QStringLiteral("箭雨") || name == QStringLiteral("魔焰"))
    {
        const int amount = name == QStringLiteral("箭雨") ? 4 : 10;
        for (UnitInstance* p : alive(playerUnits_)) dealDamage(p, amount, name, mig);
    }
    else if (name == QStringLiteral("鼓舞")) for (UnitInstance* e : alive(enemyUnits_)) { e->base.atk += 2; queueFlash(e); }
    else if (name == QStringLiteral("守护")) addShield(mig, 10, QStringLiteral("守护"));
    else if (name == QStringLiteral("治疗术")) heal(mostWounded(enemyUnits_), 12);
    else if (name == QStringLiteral("吸血")) { UnitInstance* target = firstAlive(playerUnits_); int before = target ? target->hp : 0; dealDamage(target, 10, name, mig); if (target && target->hp < before) heal(mig, before - target->hp); }
    else if (name == QStringLiteral("血雾")) for (UnitInstance* p : alive(playerUnits_)) { p->base.atk = std::max(0, p->base.atk - 2); queueFlash(p); }
    else if (name == QStringLiteral("赤心爆发")) dealDamage(firstAlive(playerUnits_), 20, name, mig);
    else if (name == QStringLiteral("永生之血")) heal(mig, 10);
    else if (name == QStringLiteral("精灵箭")) dealDamage(firstAlive(playerUnits_), 8, name, mig);
    else if (name == QStringLiteral("森语祝福"))
    {
        for (UnitInstance* e : alive(enemyUnits_))
        {
            e->base.atk += 2;
            heal(e, 4);
            queueFlash(e);
        }
    }
    else if (name == QStringLiteral("毒雾")) for (UnitInstance* p : alive(playerUnits_)) dealDamage(p, 3, name, mig);
    else if (name == QStringLiteral("古木再生"))
    {
        heal(mostWounded(enemyUnits_), 25);
        heal(mostWounded(playerUnits_), 25);
    }
    else if (name == QStringLiteral("古树根须")) addShield(lowestHp(enemyUnits_), 15, QStringLiteral("古树根须"));
    else if (name == QStringLiteral("藤蔓缠绕")) for (UnitInstance* p : alive(playerUnits_)) { p->base.atk = std::max(0, p->base.atk - 1); queueFlash(p); }
    else if (name == QStringLiteral("圣光")) for (UnitInstance* e : alive(enemyUnits_)) heal(e, 8);
    else if (name == QStringLiteral("审判")) dealDamage(highestAtk(playerUnits_), 25, name, mig);
    else if (name == QStringLiteral("六翼庇护")) for (UnitInstance* e : alive(enemyUnits_)) addShield(e, 12, QStringLiteral("六翼庇护"));
    else if (name == QStringLiteral("命运改写")) mig->protectedDeath = true;
    else if (name == QStringLiteral("圣河回响")) castMigStolenSkill(QStringLiteral("圣光"), chapterIndex, mig);
    else if (name == QStringLiteral("火球")) dealDamage(firstAlive(playerUnits_), 18, name, mig);
    else if (name == QStringLiteral("深渊爪击")) dealDamage(playerUnits_[4] ? playerUnits_[4] : firstAlive(playerUnits_), 22, name, mig);
    else if (name == QStringLiteral("狂暴")) { mig->base.atk += 10; mig->hp -= 5; queueFlash(mig); }
    else if (name == QStringLiteral("邪神赐福")) for (UnitInstance* e : alive(enemyUnits_)) { e->base.atk *= 2; queueFlash(e); }
    else if (name == QStringLiteral("命运之刃")) dealDamage(firstAlive(playerUnits_), mig->base.atk, name, mig);

    if (refreshCallback_) refreshCallback_();
}

void BattleEngine::showBossSkillIntro(const QString& bossName)
{
    QStringList lines;
    if (bossName == QStringLiteral("阿拉贡"))
        lines << QStringLiteral("炬火·耀：炬火照耀王旗，敌军将随回合逐渐昂扬。");
    else if (bossName == QStringLiteral("偷窃者米格"))
        lines << QStringLiteral("偷窃：每隔数回合窃走棋盘上的影子。")
              << QStringLiteral("一无所有：他记住你入场时拥有的技能，并反过来使用它们。");
    else if (bossName == QStringLiteral("吸血鬼伯爵"))
        lines << QStringLiteral("吸血：伯爵会以伤害汲取生命。");
    else if (bossName == QStringLiteral("阿格尼"))
        lines << QStringLiteral("森海永恒：死亡并不是终点。")
              << QStringLiteral("永恒毒恶：森林的恶意会逐回合变得浓烈。");
    else if (bossName == QStringLiteral("米凯尔"))
        lines << QStringLiteral("绝对存续的圣洁神御\n神御隔绝大多数伤害，唯有特定回合能够触及她。")
              << QStringLiteral("米凯尔在合唱团吹奏号角\n号角响起时，战场会被迫聆听。")
              << QStringLiteral("永恒燃烧的六翼制裁\n审判会周期性降下。");
    else if (bossName == QStringLiteral("伊维尔"))
        lines << QStringLiteral("业火：魔焰每回合灼烧玩家方。")
              << QStringLiteral("魔主：魔族会回应召唤。")
              << QStringLiteral("炽焰：友方锋芒会被逐渐熔去。")
              << QStringLiteral("熔岩：伊维尔会持续积蓄力量。");
    else if (bossName == QStringLiteral("莱索恩"))
        lines << QStringLiteral("终焉：前六回合，莱索恩不会受到伤害。")
              << QStringLiteral("四象：命运从四种征兆中翻涌。")
              << QStringLiteral("真·魔主：更深的魔族会被召来。")
              << QStringLiteral("灭尽：久存者会被终焉点名。");
    else
        return;

    QDialog dialog(parentWidget_);
    dialog.setWindowTitle(QStringLiteral("%1 的技。").arg(bossName));
    dialog.setMinimumWidth(bossName == QStringLiteral("米凯尔") ? 520 : 440);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* title = new QLabel(bossName);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName(QStringLiteral("skillNameLabel"));
    layout->addWidget(title);
    for (const QString& line : lines)
    {
        QLabel* label = smallLabel(line);
        label->setAlignment(line.contains(QLatin1Char('\n')) ? Qt::AlignCenter : Qt::AlignLeft);
        layout->addWidget(label);
    }
    QPushButton* ok = new QPushButton(QStringLiteral("进入战斗"));
    layout->addWidget(ok, 0, Qt::AlignCenter);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

// ============================================================================
// Step 5: 遗物系统
// ============================================================================

void BattleEngine::applyRelicsStart()
{
    nextEventBatch();

    // ---- 每回合对所有存活友军生效 ----
    for (UnitInstance* u : alive(playerUnits_))
    {
        if (hasRelic(QStringLiteral("铁剑"))) u->base.atk += 2;                                          // 每回合全体+2
        if (hasRelic(QStringLiteral("人王徽记")) && u->base.faction == QStringLiteral("人族")) u->base.atk += 4;  // 每回合人族+4
        if (hasRelic(QStringLiteral("邪神赐福"))) { u->base.atk = int(u->base.atk * 1.5); u->hp -= 3; }   // 邪神赐福
    }

    // ---- 第1回合一次性效果 ----
    if (battleRound_ == 1)
    {
        for (UnitInstance* u : alive(playerUnits_))
        {
            if (hasRelic(QStringLiteral("旧盾"))) addShield(u, 5, QStringLiteral("旧盾"));                // 全体+5护盾
            if (hasRelic(QStringLiteral("战鼓"))) u->base.atk += 5;                                      // 全体+5攻击
            if (hasRelic(QStringLiteral("圣洁六翼"))) addShield(u, 20, QStringLiteral("圣洁六翼"));       // 全体+20护盾
            if (hasRelic(QStringLiteral("魔王残角")) && u->base.faction == QStringLiteral("魔族"))
            {
                u->hp -= 5;                                                                              // 魔族-5血
                u->base.atk += 6;                                                                        // 魔族+6攻
            }
        }

        // 幸运骰子：随机1个存活友军+10攻击
        if (hasRelic(QStringLiteral("幸运骰子")))
        {
            QList<UnitInstance*> aliveList = alive(playerUnits_);
            if (!aliveList.isEmpty())
            {
                UnitInstance* lucky = aliveList[QRandomGenerator::global()->bounded(aliveList.size())];
                lucky->base.atk += 10;
                appendLog(QStringLiteral("幸运骰子：%1获得10点攻击。").arg(lucky->base.name));
            }
        }

        // 断爪：给予敌方全体脆弱
        if (hasRelic(QStringLiteral("断爪")))
        {
            for (UnitInstance* e : alive(enemyUnits_))
                addStatus(e, QStringLiteral("脆弱"), 2, true);
            appendLog(QStringLiteral("断爪：敌方全体获得2层脆弱。"));
            consumeRelic(QStringLiteral("断爪"));
        }

        // 铁玫瑰：己方全体获得1层荆棘
        if (hasRelic(QStringLiteral("铁玫瑰")))
        {
            for (UnitInstance* u : alive(playerUnits_))
                addStatus(u, QStringLiteral("荆棘"), 1, false);
            appendLog(QStringLiteral("铁玫瑰：己方全体获得1层荆棘。"));
        }

        // 高塔石碑：己方全体获得1层回响
        if (hasRelic(QStringLiteral("高塔石碑")))
        {
            for (UnitInstance* u : alive(playerUnits_))
                addStatus(u, QStringLiteral("回响"), 1, false);
            appendLog(QStringLiteral("高塔石碑：己方全体获得1层回响。"));
            consumeRelic(QStringLiteral("高塔石碑"));
        }

        // 石像鬼雕像：给予敌方全体1层石化
        if (hasRelic(QStringLiteral("石像鬼雕像")))
        {
            for (UnitInstance* e : alive(enemyUnits_))
                addStatus(e, QStringLiteral("石化"), 1, true);
            appendLog(QStringLiteral("石像鬼雕像：敌方全体获得1层石化。"));
            consumeRelic(QStringLiteral("石像鬼雕像"));
        }
    }

    // ---- 主角特效 ----
    if (UnitInstance* hero = playerUnits_[kHeroSlot])
    {
        if (hasRelic(QStringLiteral("医疗包"))) heal(hero, 3);                                          // 每回合治疗主角
        if (hasRelic(QStringLiteral("锈蚀胸甲"))) addShield(hero, 5, QStringLiteral("锈蚀胸甲"));        // 每回合主角+5护盾
        if (hasRelic(QStringLiteral("世界")) && skillSlots_.size() < kMaxSkills)
            skillSlots_.push_back({QStringLiteral("命运之刃"), QStringLiteral("世界"), QStringLiteral("命运")});
    }

    // ---- 跃动赤心：每回合全体治疗10 ----
    if (hasRelic(QStringLiteral("跃动赤心")))
    {
        for (UnitInstance* u : alive(playerUnits_)) heal(u, 10);
    }

    // ---- 清理次数归零的遗物 ----
    cleanupZeroUseRelics();
}

void BattleEngine::addRelic(const QString& relic, int uses, bool slotless, bool isStory)
{
    if (relic.isEmpty()) return;

    // 已拥有同名遗物（仅对非 slotless 永久遗物做去重检查）
    if (!slotless && uses == -1 && hasRelic(relic))
    {
        logLines_ << QStringLiteral("已拥有遗物：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        return;
    }

    // 一次性遗物：不检查槽位，直接存储
    if (slotless)
    {
        relics_.push_back({relic, uses, slotless});
        logLines_ << QStringLiteral("获得一次性遗物：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        return;
    }

    // 槽位未满，直接添加
    if (activeRelicCount() < kMaxRelics)
    {
        relics_.push_back({relic, uses, slotless});
        logLines_ << QStringLiteral("获得遗物：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        return;
    }

    // ---- 槽位已满，弹出替换对话框 ----
    QStringList replaceOptions;
    QVector<int> replaceIndexes;
    for (int i = 0; i < relics_.size(); ++i)
    {
        if (isStoryRelic(relics_[i].name) || relics_[i].slotless)
            continue;
        replaceIndexes.push_back(i);
        replaceOptions << QStringLiteral("%1号槽：%2 - %3")
                              .arg(i + 1)
                              .arg(relics_[i].name, relicDescription(relics_[i].name));
    }

    if (replaceOptions.isEmpty())
    {
        logLines_ << QStringLiteral("遗物槽已满，且没有可替换的非剧情遗物。未获得：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        QMessageBox::information(parentWidget_,
                                 QStringLiteral("遗物槽已满"),
                                 QStringLiteral("遗物槽都被剧情遗物占据，无法替换。\n未获得：%1").arg(relic));
        return;
    }

    const QString abandonText = isStory
        ? QStringLiteral("——放弃此遗物（警告：这是剧情遗物，放弃后无法再次获得！）——。")
        : QStringLiteral("——不替换，放弃此遗物——。");
    replaceOptions << abandonText;

    const QString dialogTitle = isStory
        ? QStringLiteral("选择替换遗物 - 注意：这是剧情遗物！")
        : QStringLiteral("选择替换遗物。");
    const QString dialogText = isStory
        ? QStringLiteral("遗物槽已满。这是剧情遗物，放弃后无法再次获得。请选择要替换的位置。")
        : QStringLiteral("遗物槽已满。选择要替换的位置，或选择「不替换」放弃：");

    bool ok = false;
    QString choice = QInputDialog::getItem(parentWidget_,
                                           dialogTitle,
                                           dialogText,
                                           replaceOptions,
                                           0,
                                           false,
                                           &ok);
    if (!ok)
    {
        logLines_ << QStringLiteral("放弃获得遗物：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        return;
    }

    int chosen = replaceOptions.indexOf(choice);
    if (chosen >= 0 && chosen < replaceIndexes.size())
    {
        int relicIndex = replaceIndexes[chosen];
        logLines_ << QStringLiteral("替换遗物：%1 -> %2").arg(relics_[relicIndex].name, relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        relics_[relicIndex] = {relic, uses, slotless};
    }
    else
    {
        logLines_ << QStringLiteral("放弃获得遗物：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
    }
}

void BattleEngine::tryFuseWorld()
{
    QStringList needed = {QStringLiteral("圣洁六翼"), QStringLiteral("精灵王冠"), QStringLiteral("邪神赐福"), QStringLiteral("跃动赤心")};
    for (const QString& n : needed)
    {
        if (!hasRelic(n)) return;
    }
    if (hasRelic(QStringLiteral("世界"))) return;

    // 移除四件剧情遗物
    for (const QString& n : needed)
    {
        for (int i = 0; i < relics_.size(); ++i)
        {
            if (relics_[i].name == n)
            {
                relics_.removeAt(i);
                break;
            }
        }
    }
    relics_.push_back({QStringLiteral("世界"), -1, false});
    logLines_ << QStringLiteral("四件剧情遗物融合为：世界");
    while (logLines_.size() > 80) logLines_.removeFirst();
}

// ============================================================================
// 遗物辅助方法
// ============================================================================

bool BattleEngine::hasRelic(const QString& name) const
{
    for (const RelicInstance& r : relics_)
    {
        if (r.name == name && r.uses != 0) return true;
    }
    return false;
}

int BattleEngine::relicUses(const QString& name) const
{
    for (const RelicInstance& r : relics_)
    {
        if (r.name == name) return r.uses;
    }
    return 0;
}

bool BattleEngine::consumeRelic(const QString& name)
{
    for (int i = 0; i < relics_.size(); ++i)
    {
        if (relics_[i].name == name && relics_[i].uses > 0)
        {
            --relics_[i].uses;
            if (relics_[i].uses == 0)
            {
                logLines_ << QStringLiteral("遗物次数耗尽：%1").arg(name);
                while (logLines_.size() > 80) logLines_.removeFirst();
                return true;  // 调用方应随后调用 cleanupZeroUseRelics
            }
            return false;
        }
    }
    return false;  // 未找到或 uses==-1(永久)
}

void BattleEngine::cleanupZeroUseRelics()
{
    for (int i = relics_.size() - 1; i >= 0; --i)
    {
        if (relics_[i].uses == 0)
            relics_.removeAt(i);
    }
}

int BattleEngine::activeRelicCount() const
{
    int count = 0;
    for (const RelicInstance& r : relics_)
    {
        if (!r.slotless && r.uses != 0) ++count;
    }
    return count;
}

// ============================================================================
// Step 6: 战斗流程
// ============================================================================

void BattleEngine::setupBattle(int chapterIndex, int levelIndex)
{
    clearBoard(enemyUnits_);
    preparePlayerForBattle(chapterIndex);
    migEntranceSkillSnapshot_.clear();
    agniPoisonDamage_ = 2;
    iyvelOriginalAtk_.clear();

    ChapterDef c = chapters()[chapterIndex];
    QString enemyName;
    bool boss = false;
    if (levelIndex == 4) enemyName = c.elite4;
    else if (levelIndex == 5) enemyName = c.boss5;
    else if (levelIndex == 9) enemyName = c.elite9;
    else if (levelIndex == 10) enemyName = c.boss10;
    else enemyName = c.normalEnemies[(levelIndex + chapterIndex) % c.normalEnemies.size()];
    boss = levelIndex == 5 || levelIndex == 10 || isMechanicBossName(enemyName);

    // 主敌人也使用模板自身 row，不再硬编码。
    UnitTemplate mainT = makeEnemyTemplate(enemyName, boss, chapterIndex, levelIndex);
    const bool minorLevel = levelIndex != 5 && levelIndex != 10;
    if (!boss && minorLevel && chapterIndex >= 3)
    {
        mainT.hp += chapterIndex * 3 + levelIndex * 2;
        mainT.atk += chapterIndex / 2;
    }
    int mainSlot = slotForRow(mainT.row, enemyUnits_);
    if (mainSlot >= 0)
    {
        enemyUnits_[mainSlot] = createUnit(mainT, false, boss);
    }
    // 额外敌人只允许进入模板对应排；超额放不下则按生成顺序忽略。
    auto placeExtraEnemy = [this, chapterIndex, levelIndex](UnitTemplate t) {
        if (levelIndex != 5 && levelIndex != 10 && chapterIndex >= 3)
        {
            t.hp += chapterIndex * 3 + levelIndex * 2;
            t.atk += chapterIndex / 2;
        }
        int s = slotForRow(t.row, enemyUnits_);
        if (s >= 0) enemyUnits_[s] = createUnit(t, false, false);
        else appendLog(QStringLiteral("敌人未入场（%1已满）：%2").arg(t.row, t.name));
    };

    int targetEnemyCount = 1;
    if (levelIndex == 10)
    {
        targetEnemyCount = 4;
    }
    else
    {
        int roll = QRandomGenerator::global()->bounded(10);
        if (roll < 3) targetEnemyCount = 3;
        else if (roll < 8) targetEnemyCount = 4;
        else targetEnemyCount = 5;
    }
    for (int i = 1; i < targetEnemyCount; ++i)
    {
        placeExtraEnemy(makeEnemyTemplate(c.normalEnemies[(levelIndex + i) % c.normalEnemies.size()], false, chapterIndex, levelIndex));
    }

    if (enemyName == QStringLiteral("偷窃者米格"))
    {
        migEntranceSkillSnapshot_ = skillSlots_;
        appendLog(QStringLiteral("米格记住了入场时的技能区域。"));
    }
    if (isMechanicBossName(enemyName)) showBossSkillIntro(enemyName);

    // 战斗开始：触发第1回合遗物效果（此时敌我双方均已部署）
    applyRelicsStart();

    inBattle_ = true;
    if (refreshCallback_) refreshCallback_();
}

void BattleEngine::setupWorldEvent(int chapterIndex, int& levelIndex)
{
    if (levelIndex == 1)
    {
        QDialog dialog(parentWidget_);
        dialog.setWindowTitle(QStringLiteral("世界：第一题"));
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        layout->addWidget(smallLabel(QStringLiteral("选择两名Boss。正确答案：米凯尔、阿格尼。")));
        QCheckBox* m = new QCheckBox(QStringLiteral("米凯尔"));
        QCheckBox* a = new QCheckBox(QStringLiteral("阿格尼"));
        QCheckBox* l = new QCheckBox(QStringLiteral("莱索恩"));
        QCheckBox* y = new QCheckBox(QStringLiteral("艾琳"));
        QCheckBox* e = new QCheckBox(QStringLiteral("伊维尔"));
        layout->addWidget(m);
        layout->addWidget(a);
        layout->addWidget(l);
        layout->addWidget(y);
        layout->addWidget(e);
        QPushButton* ok = new QPushButton(QStringLiteral("确认"));
        layout->addWidget(ok);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
        appendLog(QStringLiteral("第一题结束。无论答案如何，世界继续向前。"));
        levelIndex = 2;
        setupWorldEvent(chapterIndex, levelIndex);
        return;
    }
    if (levelIndex == 2)
    {
        auto setupWorldBoss = [this, chapterIndex, &levelIndex]() {
            clearBoard(enemyUnits_);
            preparePlayerForBattle(chapterIndex);
            agniPoisonDamage_ = 2;
            iyvelOriginalAtk_.clear();
            enemyUnits_[2] = createUnit(makeEnemyTemplate(QStringLiteral("米凯尔"), true, chapterIndex, levelIndex), false, true);
            enemyUnits_[3] = createUnit(makeEnemyTemplate(QStringLiteral("阿格尼"), true, chapterIndex, levelIndex), false, true);
            showBossSkillIntro(QStringLiteral("米凯尔"));
            showBossSkillIntro(QStringLiteral("阿格尼"));
            applyRelicsStart();
            inBattle_ = true;
            if (refreshCallback_) refreshCallback_();
        };
        if (storyManager_->hasScene(QStringLiteral("boss_米凯尔与阿格尼")))
        {
            storyManager_->showStoryKey(QStringLiteral("boss_米凯尔与阿格尼"), setupWorldBoss);
        }
        else
        {
            setupWorldBoss();
        }
        return;
    }
    if (levelIndex == 3)
    {
        addRelic(QStringLiteral("圣洁六翼"), -1, false, true);
        addRelic(QStringLiteral("精灵王冠"), -1, false, true);
        tryFuseWorld();
    }
    if (levelIndex == 9)
    {
        showGoldAltar();  // 决战前最后一次购买机会
        bool passed = false;
        while (!passed)
        {
            QDialog dialog(parentWidget_);
            dialog.setWindowTitle(QStringLiteral("最终问题"));
            dialog.setModal(true);
            QVBoxLayout* layout = new QVBoxLayout(&dialog);
            layout->addWidget(smallLabel(QStringLiteral("什么让我们不再永恒。")));

            QLineEdit* answerEdit = new QLineEdit;
            answerEdit->setPlaceholderText(QStringLiteral("填入答案"));
            layout->addWidget(answerEdit);

            QLabel* errorLabel = smallLabel(QString());
            errorLabel->setStyleSheet("color:#e98b8b;");
            layout->addWidget(errorLabel);

            QHBoxLayout* buttons = new QHBoxLayout;
            QPushButton* submitButton = new QPushButton(QStringLiteral("确认"));
            QPushButton* understandButton = new QPushButton(QStringLiteral("我已经明白"));
            buttons->addWidget(submitButton);
            buttons->addWidget(understandButton);
            layout->addLayout(buttons);

            auto passAnswer = [&]() {
                passed = true;
                dialog.accept();
            };
            auto checkAnswer = [&]() {
                const QString answer = answerEdit->text().trimmed();
                const QString lower = answer.toLower();
                const QStringList accepted = {
                    QStringLiteral("love"),
                    QStringLiteral("emotion"),
                    QStringLiteral("possibility"),
                    QStringLiteral("transience"),
                    QStringLiteral("。"),
                    QStringLiteral("情感"),
                    QStringLiteral("可能"),
                    QStringLiteral("短暂")
                };
                if (accepted.contains(answer) || accepted.contains(lower))
                {
                    passAnswer();
                    return;
                }
                errorLabel->setText(QStringLiteral("答案还没有抵达那里。"));
            };

            connect(submitButton, &QPushButton::clicked, &dialog, checkAnswer);
            connect(answerEdit, &QLineEdit::returnPressed, &dialog, checkAnswer);
            connect(understandButton, &QPushButton::clicked, &dialog, [&]() {
                answerEdit->setText(QStringLiteral("love"));
                passAnswer();
            });
            dialog.exec();
        }
        if (false)
        {
        bool ok = false;
        QInputDialog::getText(parentWidget_, QStringLiteral("最终问题"), QStringLiteral("什么让我们不再永恒。"), QLineEdit::Normal, QString(), &ok);
        }
        if (storyManager_->hasScene(QStringLiteral("c8_l9_after")))
        {
            storyManager_->showStoryKey(QStringLiteral("c8_l9_after"), [this]() { showEnding(); });
        }
        else
        {
            showEnding();
        }
        return;
    }
    if (levelIndex >= 10)
    {
        showEnding();
        return;
    }
    inBattle_ = false;
    appendLog(QStringLiteral("世界章节事件：%1 完成。").arg(levelIndex));
    if (refreshCallback_) refreshCallback_();
}

void BattleEngine::finishLevel(int chapterIndex, int levelIndex)
{
    inBattle_ = false;
    gold_ += 5 + chapterIndex;
    if (hasRelic(QStringLiteral("铜钱袋"))) gold_ += 5;
    appendLog(QStringLiteral("胜利：获得金币。"));

    QString bossRelic;
    QString bossAlly;
    if (levelIndex == 10 || (chapterIndex == 8 && levelIndex == 2))
    {
        bossRelic = chapters()[chapterIndex].bossRelic;
        bossAlly = chapters()[chapterIndex].bossAlly;
    }
    if (!bossAlly.isEmpty())
    {
        roster_.push_back(templateByName(bossAlly));
        appendLog(QStringLiteral("剧情角色加入：%1").arg(bossAlly));
    }
    if (chapterIndex == 1 && levelIndex == 10)
    {
        const QString rescuedName = QStringLiteral("精灵使者");
        const bool alreadyJoined = std::any_of(roster_.begin(), roster_.end(), [&rescuedName](const UnitTemplate& unit) {
            return unit.name == rescuedName;
        });
        if (!alreadyJoined)
        {
            roster_.push_back(templateByName(rescuedName));
            appendLog(QStringLiteral("剧情角色加入：%1").arg(rescuedName));
        }
    }
    if (levelIndex == 10)
    {
        growHeroAfterChapterBoss();
        if (chapterIndex < 8 && playerUnits_[kHeroSlot])
        {
            UnitInstance* h = playerUnits_[kHeroSlot];
            const int halfHp = h->base.hp / 2;
            if (h->hp < halfHp)
            {
                h->hp = halfHp;
                appendLog(QStringLiteral("Boss战胜利：主角生命回复至一半（%1）。").arg(halfHp));
            }
        }
    }
    if (refreshCallback_) refreshCallback_();
    if (shouldOfferRewards(levelIndex))
    {
        appendLog(QStringLiteral("奖励节点：选择卡牌和遗物。"));
        showUnitReward(chapterIndex, levelIndex, true);
        showRelicReward(bossRelic);
    }
    else if (!bossRelic.isEmpty())
    {
        addRelic(bossRelic);
    }
    tryFuseWorld();

    QStringList storyKeys;
    if (!bossAlly.isEmpty() && storyManager_->hasScene(QStringLiteral("ally_") + bossAlly))
    {
        storyKeys << QStringLiteral("ally_") + bossAlly;
    }
    QString key = QString("c%1_l%2_after").arg(chapterIndex).arg(levelIndex);
    if (storyManager_->hasScene(key))
    {
        storyKeys << key;
    }
    if (!storyKeys.isEmpty())
    {
        storyManager_->showStory(storyKeys, [this]() { if (refreshCallback_) refreshCallback_(); });
    }
    if (refreshCallback_) refreshCallback_();
}

void BattleEngine::finishDefeat(int chapterIndex, int levelIndex)
{
    Q_UNUSED(chapterIndex);
    Q_UNUSED(levelIndex);

    inBattle_ = false;
    appendLog(QStringLiteral("失败：主角倒下，本关战斗结束。"));
    if (refreshCallback_) refreshCallback_();

    QMessageBox box(parentWidget_);
    box.setWindowTitle(QStringLiteral("战败结算"));
    box.setText(QStringLiteral("主角已经倒下。"));
    box.setInformativeText(QStringLiteral("选择从本大章节开始挑战，或从第一章重新开始。"));
    QPushButton* chapterButton = box.addButton(QStringLiteral("从本大章节开始"), QMessageBox::AcceptRole);
    QPushButton* restartButton = box.addButton(QStringLiteral("从头挑战"), QMessageBox::DestructiveRole);
    box.setDefaultButton(chapterButton);
    box.exec();

    if (box.clickedButton() == restartButton)
    {
        appendLog(QStringLiteral("战败：从头挑战。"));
        if (restartGameCallback_) restartGameCallback_();
        return;
    }

    appendLog(QStringLiteral("战败：从本大章节开始挑战。"));
    if (restartChapterCallback_) restartChapterCallback_();
    else if (restartGameCallback_) restartGameCallback_();
}

void BattleEngine::showUnitReward(int chapterIndex, int levelIndex, bool priced)
{
    QVector<UnitTemplate> offer;
    QVector<int> prices;

    auto takeForRow = [this, chapterIndex, levelIndex](const QString& row) {
        QVector<UnitTemplate> pool = rewardPoolForRow(row, chapterIndex);
        UnitTemplate t = pool[QRandomGenerator::global()->bounded(pool.size())];
        return scaledRewardUnit(t, chapterIndex, levelIndex);
    };

    const QStringList requiredRows = {
        QStringLiteral("前排"),
        QStringLiteral("前排"),
        QStringLiteral("中排")
    };
    for (const QString& row : requiredRows)
    {
        offer.push_back(takeForRow(row));
        prices << randomCardPrice();
    }
    for (int i = 0; i < 2; ++i)
    {
        const QString row = QRandomGenerator::global()->bounded(2) == 0
            ? QStringLiteral("中排")
            : QStringLiteral("后排");
        offer.push_back(takeForRow(row));
        prices << randomCardPrice();
    }

    QDialog dialog(parentWidget_);
    dialog.setWindowTitle(QStringLiteral("棋子奖励"));
    dialog.setMinimumWidth(900);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(smallLabel(priced
        ? QStringLiteral("选择一张加入牌库。候选牌会随当前章节与关卡成长；金币不足的卡牌暂时无法购买。")
        : QStringLiteral("选择一张加入牌库。")));
    QHBoxLayout* cards = new QHBoxLayout;
    QVector<QPushButton*> buttons;
    int chosen = -1;
    for (int i = 0; i < offer.size(); ++i)
    {
        const UnitTemplate& t = offer[i];
        QString text = priced
            ? QStringLiteral("%1\n%2  HP %3  ATK %4\n%5\n价格：%6 金币")
                  .arg(t.name, t.row).arg(t.hp).arg(t.atk).arg(t.skill).arg(prices[i])
            : QStringLiteral("%1\n%2  HP %3  ATK %4\n%5")
                  .arg(t.name, t.row).arg(t.hp).arg(t.atk).arg(t.skill);
        QPushButton* button = new QPushButton(text);
        button->setMinimumSize(150, 128);
        button->setMaximumWidth(170);
        button->setEnabled(!priced || gold_ >= prices[i]);
        cards->addWidget(button);
        buttons << button;
        connect(button, &QPushButton::clicked, &dialog, [&, i]() {
            chosen = i;
            dialog.accept();
        });
    }
    layout->addLayout(cards);
    QPushButton* skip = new QPushButton(QStringLiteral("跳过"));
    layout->addWidget(skip, 0, Qt::AlignCenter);
    connect(skip, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted && chosen >= 0)
    {
        if (priced)
        {
            gold_ -= prices[chosen];
            appendLog(QStringLiteral("花费%1金币加入牌库：%2").arg(prices[chosen]).arg(offer[chosen].name));
        }
        roster_.push_back(offer[chosen]);
        tryAutoFillPendingRefill(offer[chosen]);
    }
    Q_UNUSED(buttons);
}

void BattleEngine::showRelicReward(const QString& fixedRelic)
{
    if (!fixedRelic.isEmpty())
    {
        addRelic(fixedRelic, -1, false, true);
        return;
    }
    showRelicChoice();
}

void BattleEngine::showRelicChoice()
{
    // 将新增遗物追加到池中
    QStringList pool = {
        QStringLiteral("铁剑"), QStringLiteral("旧盾"), QStringLiteral("战鼓"), QStringLiteral("铜钱袋"),
        QStringLiteral("破碎护符"), QStringLiteral("医疗包"), QStringLiteral("魔法书"), QStringLiteral("幸运骰子"),
        QStringLiteral("人王徽记"), QStringLiteral("猩红酒杯"), QStringLiteral("精灵树枝"), QStringLiteral("圣河水滴"),
        QStringLiteral("魔王残角"),
        // 新增遗物
        QStringLiteral("断爪"), QStringLiteral("许愿骨"), QStringLiteral("铁玫瑰"), QStringLiteral("黄金祭坛"),
        QStringLiteral("染血符咒"), QStringLiteral("锈蚀胸甲"), QStringLiteral("鎏金坩埚"), QStringLiteral("死亡圣契"),
        QStringLiteral("高塔石碑"), QStringLiteral("石像鬼雕像")
    };
    for (const RelicInstance& r : relics_)
    {
        pool.removeAll(r.name);
    }
    if (pool.isEmpty())
    {
        appendLog(QStringLiteral("遗物奖励池已无未拥有遗物。"));
        QMessageBox::information(parentWidget_,
                                 QStringLiteral("遗物奖励"),
                                 QStringLiteral("当前遗物奖励池中没有未拥有遗物。"));
        return;
    }

    // 遗物次数标注
    auto relicUsesTag = [](const QString& name) -> QString {
        if (name == QStringLiteral("许愿骨") || name == QStringLiteral("染血符咒")
            || name == QStringLiteral("鎏金坩埚") || name == QStringLiteral("死亡圣契"))
            return QStringLiteral("一次。");
        if (name == QStringLiteral("破碎护符") || name == QStringLiteral("高塔石碑"))
            return QStringLiteral("×1");
        if (name == QStringLiteral("断爪") || name == QStringLiteral("石像鬼雕像"))
            return QStringLiteral("×3");
        return QStringLiteral("。");
    };

    QStringList offer;
    const int offerCount = std::min(3, int(pool.size()));
    for (int i = 0; i < offerCount; ++i)
    {
        const int index = QRandomGenerator::global()->bounded(pool.size());
        const QString relic = pool.takeAt(index);
        offer << QStringLiteral("%1  |  %2  |  %3").arg(relic, relicDescription(relic), relicUsesTag(relic));
    }
    bool ok = false;
    QString choice = QInputDialog::getItem(parentWidget_, QStringLiteral("遗物奖励"),
                                           QStringLiteral("选择一个遗物"), offer, 0, false, &ok);
    if (ok)
    {
        const QString relicName = choice.section(QStringLiteral("  |  "), 0, 0);
        // 根据遗物名称确定 uses/slotless
        // 一次性遗物：许愿骨/染血符咒/鎏金坩埚/死亡圣契
        if (relicName == QStringLiteral("许愿骨")
            || relicName == QStringLiteral("染血符咒") || relicName == QStringLiteral("鎏金坩埚")
            || relicName == QStringLiteral("死亡圣契"))
        {
            addRelic(relicName, 1, true, false);  // uses=1, slotless=true
        }
        // 限次遗物：破碎护符(1)/断爪(3)/高塔石碑(1)/石像鬼雕像(3)
        else if (relicName == QStringLiteral("破碎护符"))
            addRelic(relicName, 1, false, false);
        else if (relicName == QStringLiteral("断爪"))
            addRelic(relicName, 3, false, false);
        else if (relicName == QStringLiteral("高塔石碑"))
            addRelic(relicName, 1, false, false);
        else if (relicName == QStringLiteral("石像鬼雕像"))
            addRelic(relicName, 3, false, false);
        // 其余为永久遗物
        else
            addRelic(relicName, -1, false, false);

        // ---- 一次性遗物的立即触发 ----
        if (relicName == QStringLiteral("许愿骨"))
        {
            for (int i = 0; i < 3; ++i)
                showRelicChoice();
        }
        else if (relicName == QStringLiteral("染血符咒"))
        {
            if (UnitInstance* hero = playerUnits_[kHeroSlot])
            {
                hero->hp -= 5;
                heroPermanentAtkBonus_ += 1;
                appendLog(QStringLiteral("染血符咒：主角失去5点生命，永久增加1点攻击力。"));
                if (hero->hp <= 0)
                {
                    appendLog(QStringLiteral("主角因染血符咒死亡。"));
                    if (refreshCallback_) refreshCallback_();
                }
            }
        }
        else if (relicName == QStringLiteral("死亡圣契"))
        {
            heroPermanentHpBonus_ -= 5;
            if (UnitInstance* hero = playerUnits_[kHeroSlot])
            {
                hero->base.hp = std::max(1, hero->base.hp - 5);
                if (hero->hp > hero->base.hp)
                    hero->hp = hero->base.hp;
            }
            for (UnitInstance* u : alive(playerUnits_))
                heal(u, u->base.hp - u->hp);
            appendLog(QStringLiteral("死亡圣契：主角永久失去5点生命上限，己方全体生命回复至上限。"));
            if (refreshCallback_) refreshCallback_();
        }
        else if (relicName == QStringLiteral("鎏金坩埚"))
        {
            if (gold_ < 20)
            {
                appendLog(QStringLiteral("鎏金坩埚：金币不足20，遗物白白丢弃。"));
            }
            else if (skillSlots_.size() < kMaxSkills)
            {
                gold_ -= 20;
                skillSlots_.push_back({QStringLiteral("铸剑"), QStringLiteral("主角"), QStringLiteral("命运")});
                appendLog(QStringLiteral("鎏金坩埚：消耗20金币，获得铸剑技能。"));
            }
            else
            {
                // 技能槽已满，选一个替换或放弃
                QStringList options;
                for (int i = 0; i < skillSlots_.size(); ++i)
                {
                    options << QStringLiteral("%1号槽：%2（来自%3）。")
                                   .arg(i + 1)
                                   .arg(skillSlots_[i].name, skillSlots_[i].source);
                }
                options << QStringLiteral("——不替换，放弃铸剑技能——。");

                bool ok = false;
                QString choice = QInputDialog::getItem(parentWidget_,
                                                       QStringLiteral("技能槽已满"),
                                                       QStringLiteral("技能槽已满。花费20金币选择要替换的技能，或放弃："),
                                                       options, 0, false, &ok);
                if (!ok) return;

                int chosen = options.indexOf(choice);
                if (chosen >= 0 && chosen < skillSlots_.size())
                {
                    gold_ -= 20;
                    skillSlots_[chosen] = {QStringLiteral("铸剑"), QStringLiteral("主角"), QStringLiteral("命运")};
                    appendLog(QStringLiteral("鎏金坩埚：消耗20金币，替换%1号槽获得铸剑技能。").arg(chosen + 1));
                }
                else
                {
                    appendLog(QStringLiteral("鎏金坩埚：放弃替换，铸剑技能白白丢弃。"));
                }
            }
            if (refreshCallback_) refreshCallback_();
        }
    }
}

bool BattleEngine::showGoldAltar()
{
    if (!hasRelic(QStringLiteral("黄金祭坛"))) return false;

    bool purchasedAny = false;
    while (true)
    {
        const int cost = 20 + goldAltarPurchases_ * 10;
        if (gold_ < cost) break;  // 金币不足，不再弹窗
        QDialog dialog(parentWidget_);
        dialog.setWindowTitle(QStringLiteral("黄金祭坛"));
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        layout->addWidget(smallLabel(
            QStringLiteral("当前金币：%1\n本次购买需要：%2 金币\n\n是否花费金币换取一次遗物奖励？")
                .arg(gold_).arg(cost)));

        QHBoxLayout* buttons = new QHBoxLayout;
        QPushButton* buy = new QPushButton(QStringLiteral("购买 (%1 金币)").arg(cost));
        QPushButton* skip = new QPushButton(QStringLiteral("离开"));
        buttons->addWidget(buy);
        buttons->addWidget(skip);
        layout->addLayout(buttons);

        bool bought = false;
        connect(buy, &QPushButton::clicked, &dialog, [&]() { bought = true; dialog.accept(); });
        connect(skip, &QPushButton::clicked, &dialog, &QDialog::reject);
        dialog.exec();

        if (!bought) break;

        gold_ -= cost;
        ++goldAltarPurchases_;
        purchasedAny = true;
        appendLog(QStringLiteral("黄金祭坛：花费%1金币换取遗物奖励（第%2次）。").arg(cost).arg(goldAltarPurchases_));
        if (refreshCallback_) refreshCallback_();
        showRelicChoice();
        if (refreshCallback_) refreshCallback_();
    }
    return purchasedAny;
}

void BattleEngine::autoDeployPlayer(int chapterIndex)
{
    playerUnits_[kHeroSlot] = createUnit(heroTemplate(chapterIndex), true, false);
    QVector<UnitTemplate> deployRoster = roster_;
    std::stable_sort(deployRoster.begin(), deployRoster.end(), [](const UnitTemplate& a, const UnitTemplate& b) {
        if (a.atk != b.atk) return a.atk > b.atk;
        return a.hp > b.hp;
    });

    for (const UnitTemplate& t : deployRoster)
    {
        int slot = slotForRow(t.row, playerUnits_);
        if (slot >= 0 && playerUnits_[slot] == nullptr)
        {
            playerUnits_[slot] = createUnit(t, false, false);
        }
    }
}

void BattleEngine::preparePlayerForBattle(int chapterIndex)
{
    bool hasInheritedUnit = false;
    for (UnitInstance* u : playerUnits_)
    {
        if (u && u->hp > 0)
        {
            hasInheritedUnit = true;
            break;
        }
    }

    if (!hasInheritedUnit)
    {
        clearBoard(playerUnits_);
        autoDeployPlayer(chapterIndex);
        return;
    }

    for (UnitInstance*& u : playerUnits_)
    {
        if (u == nullptr)
        {
            continue;
        }
        if (u->hp <= 0)
        {
            delete u;
            u = nullptr;
            continue;
        }

        const int inheritedHp = u->hp;
        const bool inheritedHero = u->hero;
        u->base = inheritedHero ? heroTemplate(chapterIndex) : templateByName(u->base.name);
        u->hp = inheritedHp;
        u->hero = inheritedHero;
        u->boss = false;
        u->shield = 0;
        u->aliveRounds = 0;
        u->revived = false;
        u->protectedDeath = false;
        u->statuses.clear();
    }
}

void BattleEngine::showDeckRefillDialog(const QStringList& deadRows)
{
    if (!inBattle_ || deadRows.isEmpty())
    {
        return;
    }
    if (roster_.isEmpty())
    {
        rememberFailedRefills(deadRows, {});
        return;
    }

    struct RefillOffer
    {
        UnitTemplate unit;
        QString targetRow;
        bool selectable = false;
    };

    auto rowPriority = [](const QString& row) {
        if (row == QStringLiteral("前排")) return 0;
        if (row == QStringLiteral("中排")) return 1;
        return 2;
    };

    const QStringList rows = {QStringLiteral("前排"), QStringLiteral("中排"), QStringLiteral("后排")};
    QMap<QString, int> deadCount;
    QMap<QString, int> allocation;
    for (const QString& row : rows)
    {
        deadCount[row] = deadRows.count(row);
        allocation[row] = deadCount[row];
    }

    const int offerLimit = 4;
    const int totalDead = deadRows.size();
    int allocated = 0;
    for (const QString& row : rows) allocated += allocation[row];
    while (allocated < offerLimit && totalDead > 0)
    {
        QString bestRow;
        double bestNeed = -1.0;
        for (const QString& row : rows)
        {
            if (deadCount[row] <= 0) continue;
            const double target = double(offerLimit) * double(deadCount[row]) / double(totalDead);
            const double need = target - double(allocation[row]);
            if (need > bestNeed || (need == bestNeed && rowPriority(row) < rowPriority(bestRow)))
            {
                bestNeed = need;
                bestRow = row;
            }
        }
        if (bestRow.isEmpty()) break;
        ++allocation[bestRow];
        ++allocated;
    }

    QVector<UnitTemplate> remaining = roster_;
    auto removeOneRemainingCard = [&remaining](const QString& unitName) {
        for (int i = 0; i < remaining.size(); ++i)
        {
            if (remaining[i].name == unitName)
            {
                remaining.removeAt(i);
                return;
            }
        }
    };
    for (UnitInstance* u : playerUnits_)
    {
        if (u && !u->hero && u->hp > 0)
        {
            removeOneRemainingCard(u->base.name);
        }
    }

    auto takeRandom = [&remaining](std::function<bool(const UnitTemplate&)> predicate, UnitTemplate& out) {
        QVector<int> indexes;
        for (int i = 0; i < remaining.size(); ++i)
        {
            if (predicate(remaining[i])) indexes.push_back(i);
        }
        if (indexes.isEmpty()) return false;
        const int chosen = indexes[QRandomGenerator::global()->bounded(indexes.size())];
        out = remaining[chosen];
        remaining.removeAt(chosen);
        return true;
    };

    QVector<RefillOffer> offer;
    for (const QString& row : rows)
    {
        for (int i = 0; i < allocation[row] && offer.size() < offerLimit; ++i)
        {
            UnitTemplate picked;
            if (takeRandom([&row](const UnitTemplate& t) { return t.row == row; }, picked))
            {
                offer.push_back({picked, row, true});
            }
            else if (takeRandom([&row](const UnitTemplate& t) { return t.row != row; }, picked))
            {
                offer.push_back({picked, row, false});
            }
        }
    }

    // 没有任何候选牌，或所有候选都是不可上场的补位牌时不弹窗。
    const bool anySelectable = std::any_of(offer.begin(), offer.end(),
                                           [](const RefillOffer& o) { return o.selectable; });
    if (offer.isEmpty() || !anySelectable)
    {
        rememberFailedRefills(deadRows, {});
        return;
    }

    QDialog dialog(parentWidget_);
    dialog.setWindowTitle(QStringLiteral("牌库补员：抽牌。"));
    dialog.setMinimumWidth(720);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(smallLabel(QStringLiteral("按死亡排位抽取替补；同排不足时用其他牌补位，补位牌不可上场。")));

    /// [Recoleta37] 按排计算空位数，每排独立限制勾选数
    /// 槽位布局: 0,1=前排 / 2(英雄),3=中排 / 4=后排
    int emptyFront = 0, emptyMiddle = 0, emptyBack = 0;
    for (int i = 0; i < 5; ++i)
    {
        if (i == kHeroSlot || playerUnits_[i] != nullptr) continue;
        if (i == 0 || i == 1) ++emptyFront;
        else if (i == 2 || i == 3) ++emptyMiddle;
        else ++emptyBack;
    }

    QVector<QCheckBox*> checks;
    QHBoxLayout* cards = new QHBoxLayout;
    for (int i = 0; i < offer.size(); ++i)
    {
        const UnitTemplate& t = offer[i].unit;
        const QString state = offer[i].selectable
            ? QStringLiteral("候选：%1").arg(offer[i].targetRow)
            : QStringLiteral("补位不可上场");
        QCheckBox* box = new QCheckBox(QStringLiteral("%1\n%2\nHP%3 ATK%4\n技能：%5\n%6")
                                           .arg(t.name, t.row)
                                           .arg(t.hp)
                                           .arg(t.atk)
                                           .arg(t.skill, state));
        bool placeable = offer[i].selectable && hasEmptySlotForRow(t.row);
        box->setEnabled(placeable);
        box->setChecked(false);
        box->setFixedSize(160, 150);
        const QString row = t.row;
        connect(box, &QCheckBox::clicked, this, [&checks, &offer, box, row, emptyFront, emptyMiddle, emptyBack]() {
            int rowLimit = (row == QStringLiteral("前排")) ? emptyFront
                         : (row == QStringLiteral("中排")) ? emptyMiddle
                         : emptyBack;
            int rowChecked = 0;
            for (int j = 0; j < checks.size(); ++j)
            {
                if (checks[j]->isChecked() && offer[j].selectable && offer[j].unit.row == row) ++rowChecked;
            }
            if (rowChecked > rowLimit)
                box->setChecked(false);
        });
        checks.push_back(box);
        cards->addWidget(box);
    }
    layout->addLayout(cards);

    QPushButton* ok = new QPushButton(QStringLiteral("确认上场"));
    layout->addWidget(ok);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();

    QStringList filledRows;
    for (int i = 0; i < offer.size(); ++i)
    {
        if (!checks[i]->isChecked())
        {
            continue;
        }
        int slot = slotForRow(offer[i].unit.row, playerUnits_);
        if (slot >= 0 && playerUnits_[slot] == nullptr)
        {
            playerUnits_[slot] = createUnit(offer[i].unit, false, false);
            filledRows << offer[i].unit.row;
            appendLog(QStringLiteral("补员上场：%1 -> %2").arg(offer[i].unit.name).arg(offer[i].unit.row));
        }
        else
        {
            appendLog(QStringLiteral("补员未上场，返回牌库：%1").arg(offer[i].unit.name));
        }
    }
    rememberFailedRefills(deadRows, filledRows);
}

void BattleEngine::runRound(int chapterIndex, int levelIndex)
{
    if (!inBattle_) return;
    skillsUsedThisTurn_ = 0;
    appendLog(QStringLiteral("第%1回合").arg(battleRound_));
    bossMechanics(chapterIndex);
    generateSkills();
    allAttack(playerUnits_, enemyUnits_, true);
    allAttack(enemyUnits_, playerUnits_, false);

    // 先播放所有伤害、治疗动画，再处理死亡和奖励。
    if (refreshCallback_) refreshCallback_();

    cleanupDeaths(enemyUnits_, false);
    cleanupDeaths(playerUnits_, true);
    growAllUnitsMaxHp();

    ++battleRound_;

    // 回合结束，新回合开始时触发"每回合开始时"遗物效果
    applyRelicsStart();

    // 死亡效果播放完毕后再检查胜负。
    if (refreshCallback_) refreshCallback_();

    // 回合结算特效播放完毕，衰减状态层数。
    tickStatuses();

    if (enemiesDefeated())
    {
        finishLevel(chapterIndex, levelIndex);
    }
    else if (playerDefeated())
    {
        finishDefeat(chapterIndex, levelIndex);
    }
}

/// 使用成员变量 skillsUsedThisTurn_ 跟踪本回合已释放次数。
/// 避免同一回合内多次点击按钮绕过每回合最多释放次数的限制。
void BattleEngine::cleanupDeaths(std::array<UnitInstance*, 5>& board, bool playerSide)
{
    nextEventBatch();
    bool needRefill = false;
    QStringList deadRows;
    for (int i = 0; i < 5; ++i)
    {
        UnitInstance* u = board[i];
        if (!u || u->hp > 0) continue;
        if (playerSide && u->hero && (hasRelic(QStringLiteral("破碎护符")) || u->protectedDeath) && !u->revived)
        {
            u->hp = 1;
            u->revived = true;
            appendLog(QStringLiteral("主角被保留在1点生命。"));
            consumeRelic(QStringLiteral("破碎护符"));
            cleanupZeroUseRelics();
            continue;
        }
        if (playerSide && hasRelic(QStringLiteral("精灵王冠")) && !u->revived)
        {
            u->hp = 8;
            u->revived = true;
            appendLog(QStringLiteral("%1 因精灵王冠复苏。").arg(u->base.name));
            continue;
        }
        if (!playerSide && u->boss && u->base.name == QStringLiteral("艾琳") && eileenNextReviveSlot_ <= 4)
        {
            const int targetSlot = eileenNextReviveSlot_++;
            if (targetSlot == 0 || targetSlot == 1) u->base.row = QStringLiteral("前排");
            else if (targetSlot == 2 || targetSlot == 3) u->base.row = QStringLiteral("中排");
            else u->base.row = QStringLiteral("后排");
            u->hp = u->base.hp;
            u->shield = 0;
            u->revived = false;
            if (targetSlot != i)
            {
                if (board[targetSlot] && board[targetSlot] != u) delete board[targetSlot];
                board[targetSlot] = u;
                board[i] = nullptr;
            }
            appendLog(QStringLiteral("不灭：艾琳满血复活并移动到%1。").arg(u->base.row));
            queueFlash(u);
            continue;
        }
        if (!playerSide && u->boss && !u->revived && (u->base.name == QStringLiteral("阿格尼") || u->base.name == QStringLiteral("米凯尔")))
        {
            if (u->base.name == QStringLiteral("阿格尼"))
            {
                u->hp = u->base.hp;
                u->base.atk *= 2;
            }
            else
            {
                u->hp = std::max(20, u->base.hp / 2);
            }
            u->revived = true;
            appendLog(QStringLiteral("%1 触发Boss复活机制。").arg(u->base.name));
            continue;
        }
        if (playerSide && hasRelic(QStringLiteral("精灵树枝")) && u->base.faction == QStringLiteral("精灵族"))
        {
            for (UnitInstance* e : alive(enemyUnits_)) dealDamage(e, 5, QStringLiteral("精灵树枝"));
        }
        if (playerSide && !u->hero)
        {
            needRefill = true;
            deadRows << u->base.row;
            removeOneRosterCard(u->base.name);
        }
        delete u;
        board[i] = nullptr;
    }
    if (needRefill && !(playerSide && playerDefeated()))
    {
        if (refreshCallback_) refreshCallback_();
        showDeckRefillDialog(deadRows);
    }
}

void BattleEngine::showEnding()
{
    if (endingShown_) return;
    endingShown_ = true;
    inBattle_ = false;
    mapManager_->showMapPoint(-1, true, [this]() {
    storyManager_->showStoryKey("ending", [this]() {
        storyManager_->showEndingTypewriter([this]() {
            QMessageBox::information(parentWidget_, QStringLiteral("游戏结束"), QStringLiteral("最终结局完成，返回标题界面。"));
            if (returnTitleCallback_) returnTitleCallback_();
            else if (restartGameCallback_) restartGameCallback_();
        });
    });
    });
}
