/// [Recoleta37] Phase 3: BattleEngine 实现
/// Step 1: 基础层 —— 单位创建 / 棋盘查询 / 清理

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
    int worldHp = relics_.contains(QStringLiteral("世界")) ? 100 : 0;
    int worldAtk = relics_.contains(QStringLiteral("世界")) ? 50 : 0;
    return {QStringLiteral("主角"), QStringLiteral("命运"), QStringLiteral("中排"), QStringLiteral("命运之刃"), stats[idx].first + bonus + worldHp, stats[idx].second + bonus + worldAtk};
}

UnitTemplate BattleEngine::templateByName(const QString& name) const
{
    if (name == QStringLiteral("游侠")) return {name, QStringLiteral("人族"), QStringLiteral("后排"), QStringLiteral("箭雨"), 44, 16};
    if (name == QStringLiteral("精灵使者")) return {name, QStringLiteral("精灵族"), QStringLiteral("后排"), QStringLiteral("森语祝福"), 46, 12};
    if (name == QStringLiteral("加百列")) return {name, QStringLiteral("天使"), QStringLiteral("中排"), QStringLiteral("圣光"), 70, 14};
    for (const UnitTemplate& t : roster_)
    {
        if (t.name == name) return t;
    }
    return {name, QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("斩击"), 48, 8};
}

UnitTemplate BattleEngine::makeEnemyTemplate(const QString& name, bool boss, int chapterIndex, int levelIndex) const
{
    int scale = chapterIndex * 9 + levelIndex * 3;
    int hp = (boss ? 95 : 36) + scale * (boss ? 3 : 1);
    int atk = (boss ? 13 : 6) + chapterIndex * 3 + levelIndex;
    QString row = boss ? QStringLiteral("中排") : QStringLiteral("前排");
    if (name.contains(QStringLiteral("弓")) || name.contains(QStringLiteral("术")) || name.contains(QStringLiteral("刺"))) row = QStringLiteral("后排");
    return {name, QStringLiteral("敌人"), row, name, hp, atk};
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

// ============================================================================
// Step 2: 描述表
// ============================================================================

QString BattleEngine::relicDescription(const QString& relic) const
{
    static const QMap<QString, QString> descriptions = {
        {QStringLiteral("铁剑"), QStringLiteral("友军攻击+2")},
        {QStringLiteral("旧盾"), QStringLiteral("战斗开始友军生命+8")},
        {QStringLiteral("旅人靴"), QStringLiteral("第一回合友军攻击+3")},
        {QStringLiteral("铜钱袋"), QStringLiteral("胜利后金币+2")},
        {QStringLiteral("破碎护符"), QStringLiteral("主角首次死亡保留1血")},
        {QStringLiteral("战鼓"), QStringLiteral("战斗开始友军攻击+2")},
        {QStringLiteral("医疗包"), QStringLiteral("回合开始治疗主角5")},
        {QStringLiteral("魔法书"), QStringLiteral("技能伤害+3")},
        {QStringLiteral("幸运骰子"), QStringLiteral("战斗开始随机友军攻击+5")},
        {QStringLiteral("空白遗物"), QStringLiteral("无效果，测试用")},
        {QStringLiteral("人王徽记"), QStringLiteral("人族单位攻击+4")},
        {QStringLiteral("猩红酒杯"), QStringLiteral("吸血鬼造成伤害后回复3")},
        {QStringLiteral("精灵树枝"), QStringLiteral("精灵族死亡时伤害敌方全体")},
        {QStringLiteral("圣河水滴"), QStringLiteral("天使治疗量+5")},
        {QStringLiteral("魔王残角"), QStringLiteral("魔族攻击+6，生命-5")},
        {QStringLiteral("跃动赤心"), QStringLiteral("回合开始治疗全体友军10")},
        {QStringLiteral("精灵王冠"), QStringLiteral("友军首次死亡复苏为8血")},
        {QStringLiteral("圣洁六翼"), QStringLiteral("战斗开始全体友军护盾+20")},
        {QStringLiteral("邪神赐福"), QStringLiteral("友军伤害提升，但每回合损失生命")},
        {QStringLiteral("世界"), QStringLiteral("主角生命+100攻击+50，复制技能")}
    };
    return descriptions.value(relic, QStringLiteral("未登记效果"));
}

QString BattleEngine::skillDescription(const QString& skill) const
{
    static const QMap<QString, QString> descriptions = {
        {QStringLiteral("斩击"), QStringLiteral("对前排/首个敌人造成8点物理伤害")},
        {QStringLiteral("箭雨"), QStringLiteral("对敌方全体造成4点伤害")},
        {QStringLiteral("鼓舞"), QStringLiteral("所有友军攻击+2")},
        {QStringLiteral("守护"), QStringLiteral("主角获得10点护盾")},
        {QStringLiteral("治疗术"), QStringLiteral("治疗最低血友军12点")},
        {QStringLiteral("吸血"), QStringLiteral("造成10点伤害并治疗主角10点")},
        {QStringLiteral("血雾"), QStringLiteral("敌方全体攻击-2")},
        {QStringLiteral("赤心爆发"), QStringLiteral("对首个敌人造成20点伤害")},
        {QStringLiteral("生命转移"), QStringLiteral("简化：治疗主角15点")},
        {QStringLiteral("永生之血"), QStringLiteral("简化：治疗主角10点")},
        {QStringLiteral("精灵箭"), QStringLiteral("造成8点伤害")},
        {QStringLiteral("森语祝福"), QStringLiteral("全体友军攻击+2并治疗4点")},
        {QStringLiteral("毒雾"), QStringLiteral("敌方全体受到3点伤害")},
        {QStringLiteral("古木再生"), QStringLiteral("召唤一个小树人")},
        {QStringLiteral("古树根须"), QStringLiteral("最低血友军获得15护盾")},
        {QStringLiteral("藤蔓缠绕"), QStringLiteral("所有敌人攻击-1")},
        {QStringLiteral("圣光"), QStringLiteral("治疗全体友军8点")},
        {QStringLiteral("审判"), QStringLiteral("对攻击最高敌人造成25点伤害")},
        {QStringLiteral("六翼庇护"), QStringLiteral("全体友军获得12护盾")},
        {QStringLiteral("命运改写"), QStringLiteral("本回合主角首次死亡不会死亡")},
        {QStringLiteral("圣河回响"), QStringLiteral("复制一张已有技能牌")},
        {QStringLiteral("火球"), QStringLiteral("对单体造成18点法术伤害")},
        {QStringLiteral("深渊爪击"), QStringLiteral("优先对后排造成22点伤害")},
        {QStringLiteral("狂暴"), QStringLiteral("最高攻击友军攻击+10，生命-5")},
        {QStringLiteral("魔焰"), QStringLiteral("敌方全体受到10点伤害")},
        {QStringLiteral("邪神赐福"), QStringLiteral("本回合所有友军攻击翻倍")},
        {QStringLiteral("命运之刃"), QStringLiteral("造成主角攻击力伤害，每章额外+10%")}
    };
    return descriptions.value(skill, QStringLiteral("未登记技能效果"));
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

void BattleEngine::dealDamage(UnitInstance* target, int amount, const QString& reason)
{
    if (!target || amount <= 0) return;
    int shieldHit = std::min(target->shield, amount);
    target->shield -= shieldHit;
    target->hp -= (amount - shieldHit);
    logLines_ << QStringLiteral("%1 造成 %2 伤害 -> %3").arg(reason).arg(amount).arg(target->base.name);
    while (logLines_.size() > 80) logLines_.removeFirst();
}

void BattleEngine::heal(UnitInstance* target, int amount)
{
    if (!target || amount <= 0) return;
    target->hp += amount;
}

void BattleEngine::allAttack(std::array<UnitInstance*, 5>& attackers,
                              std::array<UnitInstance*, 5>& defenders,
                              bool playerSide)
{
    for (UnitInstance* u : alive(attackers))
    {
        UnitInstance* target = firstAlive(defenders);
        if (!target) return;
        int dmg = u->base.atk;
        if (playerSide && relics_.contains(QStringLiteral("世界"))) dmg += 50;
        dealDamage(target, dmg, u->base.name);
        if (playerSide && relics_.contains(QStringLiteral("猩红酒杯")) && u->base.faction == QStringLiteral("吸血鬼")) heal(u, 3);
    }
}

void BattleEngine::bossMechanics(int chapterIndex)
{
    for (UnitInstance* boss : alive(enemyUnits_))
    {
        if (!boss->boss) continue;
        QString n = boss->base.name;
        if (n == QStringLiteral("阿拉贡") && battleRound_ % 3 == 0) for (UnitInstance* e : alive(enemyUnits_)) e->base.atk += 3;
        else if (n == QStringLiteral("吸血鬼伯爵")) heal(boss, 8);
        else if (n == QStringLiteral("偷窃者米格") && !skillSlots_.isEmpty()) castSkill(skillSlots_[QRandomGenerator::global()->bounded(skillSlots_.size())].name, chapterIndex);
        else if (n == QStringLiteral("艾琳")) heal(boss, 12);
        else if (n == QStringLiteral("阿格尼")) for (UnitInstance* p : alive(playerUnits_)) dealDamage(p, 3, QStringLiteral("精灵王威压"));
        else if (n == QStringLiteral("米凯尔") && battleRound_ % 3 == 0) dealDamage(highestAtk(playerUnits_), 28, QStringLiteral("六翼审判"));
        else if (n == QStringLiteral("伊维尔")) boss->base.atk += 3;
        else if (n == QStringLiteral("莱索恩"))
        {
            int roll = QRandomGenerator::global()->bounded(4);
            if (roll == 0) boss->shield += 20;
            if (roll == 1) heal(boss, 10);
            if (roll == 2) boss->base.atk *= 2;
            if (roll == 3) for (UnitInstance* p : alive(playerUnits_)) dealDamage(p, 4, QStringLiteral("邪神诅咒"));
            if (boss->hp < boss->base.hp * 0.3) for (UnitInstance* p : alive(playerUnits_)) dealDamage(p, 5, QStringLiteral("终焉阶段"));
        }
    }
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
            skillSlots_.push_back({u->base.skill, u->base.name});
            logLines_ << QStringLiteral("%1 生成技能：%2").arg(u->base.name, u->base.skill);
            while (logLines_.size() > 80) logLines_.removeFirst();
        }
    }
}

void BattleEngine::castSkill(const QString& name, int chapterIndex)
{
    logLines_ << QStringLiteral("释放技能：%1").arg(name);
    while (logLines_.size() > 80) logLines_.removeFirst();
    int bonus = relics_.contains(QStringLiteral("魔法书")) ? 3 : 0;
    if (name == QStringLiteral("斩击")) dealDamage(firstAlive(enemyUnits_), 8 + bonus, name);
    else if (name == QStringLiteral("箭雨") || name == QStringLiteral("魔焰"))
    {
        for (UnitInstance* e : alive(enemyUnits_)) dealDamage(e, (name == QStringLiteral("箭雨") ? 4 : 10) + bonus, name);
    }
    else if (name == QStringLiteral("鼓舞")) for (UnitInstance* u : alive(playerUnits_)) u->base.atk += 2;
    else if (name == QStringLiteral("守护") && playerUnits_[kHeroSlot]) playerUnits_[kHeroSlot]->shield += 10;
    else if (name == QStringLiteral("治疗术")) heal(lowestHp(playerUnits_), 12 + (relics_.contains(QStringLiteral("圣河水滴")) ? 5 : 0));
    else if (name == QStringLiteral("吸血")) { dealDamage(firstAlive(enemyUnits_), 10 + bonus, name); heal(playerUnits_[kHeroSlot], 10); }
    else if (name == QStringLiteral("血雾")) for (UnitInstance* e : alive(enemyUnits_)) e->base.atk = std::max(0, e->base.atk - 2);
    else if (name == QStringLiteral("赤心爆发")) dealDamage(firstAlive(enemyUnits_), 20 + bonus, name);
    else if (name == QStringLiteral("永生之血")) heal(playerUnits_[kHeroSlot], 10);
    else if (name == QStringLiteral("精灵箭")) dealDamage(firstAlive(enemyUnits_), 8 + bonus, name);
    else if (name == QStringLiteral("森语祝福"))
    {
        for (UnitInstance* u : alive(playerUnits_))
        {
            u->base.atk += 2;
            heal(u, 4);
        }
    }
    else if (name == QStringLiteral("毒雾")) for (UnitInstance* e : alive(enemyUnits_)) dealDamage(e, 3 + bonus, name);
    else if (name == QStringLiteral("古木再生"))
    {
        int s = slotForRow(QStringLiteral("前排"), playerUnits_);
        if (s >= 0) playerUnits_[s] = createUnit({QStringLiteral("小树人"), QStringLiteral("精灵族"), QStringLiteral("前排"), QStringLiteral("斩击"), 18, 4});
    }
    else if (name == QStringLiteral("古树根须")) { if (UnitInstance* u = lowestHp(playerUnits_)) u->shield += 15; }
    else if (name == QStringLiteral("藤蔓缠绕")) for (UnitInstance* e : alive(enemyUnits_)) e->base.atk = std::max(0, e->base.atk - 1);
    else if (name == QStringLiteral("圣光")) for (UnitInstance* u : alive(playerUnits_)) heal(u, 8 + (relics_.contains(QStringLiteral("圣河水滴")) ? 5 : 0));
    else if (name == QStringLiteral("审判")) dealDamage(highestAtk(enemyUnits_), 25 + bonus, name);
    else if (name == QStringLiteral("六翼庇护")) for (UnitInstance* u : alive(playerUnits_)) u->shield += 12;
    else if (name == QStringLiteral("命运改写") && playerUnits_[kHeroSlot]) playerUnits_[kHeroSlot]->protectedDeath = true;
    else if (name == QStringLiteral("圣河回响") && !skillSlots_.isEmpty() && skillSlots_.size() < kMaxSkills) skillSlots_.push_back(skillSlots_.last());
    else if (name == QStringLiteral("火球")) dealDamage(firstAlive(enemyUnits_), 18 + bonus, name);
    else if (name == QStringLiteral("深渊爪击")) dealDamage(enemyUnits_[4] ? enemyUnits_[4] : firstAlive(enemyUnits_), 22 + bonus, name);
    else if (name == QStringLiteral("狂暴")) { if (UnitInstance* u = highestAtk(playerUnits_)) { u->base.atk += 10; u->hp -= 5; } }
    else if (name == QStringLiteral("邪神赐福")) for (UnitInstance* u : alive(playerUnits_)) u->base.atk *= 2;
    else if (name == QStringLiteral("命运之刃")) dealDamage(firstAlive(enemyUnits_), playerUnits_[kHeroSlot] ? int(playerUnits_[kHeroSlot]->base.atk * (1.0 + chapterIndex * 0.1)) : 0, name);
}

// ============================================================================
// Step 5: 遗物系统
// ============================================================================

void BattleEngine::applyRelicsStart()
{
    for (UnitInstance* u : alive(playerUnits_))
    {
        if (relics_.contains(QStringLiteral("铁剑"))) u->base.atk += 2;
        if (relics_.contains(QStringLiteral("旧盾")) && battleRound_ == 1) u->hp += 8;
        if (relics_.contains(QStringLiteral("战鼓")) && battleRound_ == 1) u->base.atk += 2;
        if (relics_.contains(QStringLiteral("旅人靴")) && battleRound_ == 1) u->base.atk += 3;
        if (relics_.contains(QStringLiteral("幸运骰子")) && battleRound_ == 1 && QRandomGenerator::global()->bounded(4) == 0) u->base.atk += 5;
        if (relics_.contains(QStringLiteral("人王徽记")) && u->base.faction == QStringLiteral("人族")) u->base.atk += 4;
        if (relics_.contains(QStringLiteral("魔王残角")) && u->base.faction == QStringLiteral("魔族")) { u->base.atk += 6; u->hp -= 5; }
        if (relics_.contains(QStringLiteral("圣洁六翼")) && battleRound_ == 1) u->shield += 20;
        if (relics_.contains(QStringLiteral("邪神赐福"))) { u->base.atk = int(u->base.atk * 1.5); u->hp -= 3; }
    }
    if (UnitInstance* hero = playerUnits_[kHeroSlot])
    {
        if (relics_.contains(QStringLiteral("医疗包"))) heal(hero, 5);
        if (relics_.contains(QStringLiteral("世界")) && skillSlots_.size() < kMaxSkills) skillSlots_.push_back({QStringLiteral("命运之刃"), QStringLiteral("世界")});
    }
    if (relics_.contains(QStringLiteral("跃动赤心")))
    {
        for (UnitInstance* u : alive(playerUnits_)) heal(u, 10);
    }
}

void BattleEngine::addRelic(const QString& relic)
{
    if (relic.isEmpty()) return;
    if (relics_.contains(relic))
    {
        logLines_ << QStringLiteral("已拥有遗物：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        return;
    }

    if (relics_.size() < kMaxRelics)
    {
        relics_ << relic;
        logLines_ << QStringLiteral("获得遗物：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        return;
    }

    QStringList replaceOptions;
    QVector<int> replaceIndexes;
    for (int i = 0; i < relics_.size(); ++i)
    {
        if (isStoryRelic(relics_[i]))
        {
            continue;
        }
        replaceIndexes.push_back(i);
        replaceOptions << QStringLiteral("%1号槽：%2 - %3")
                              .arg(i + 1)
                              .arg(relics_[i], relicDescription(relics_[i]));
    }

    if (replaceOptions.isEmpty())
    {
        logLines_ << QStringLiteral("遗物槽已满，且没有可替换的非剧情遗物。未获得：%1").arg(relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        QMessageBox::information(parentWidget_,
                                 QStringLiteral("遗物槽已满"),
                                 QStringLiteral("5个遗物槽都被剧情遗物或世界占据，无法替换。\n未获得：%1").arg(relic));
        return;
    }

    /// [Recoleta37] 添加显式的"不替换"选项，避免用户只能靠取消来放弃
    replaceOptions << QStringLiteral("—— 不替换，放弃此遗物 ——");

    bool ok = false;
    QString choice = QInputDialog::getItem(parentWidget_,
                                           QStringLiteral("选择替换遗物槽"),
                                           QStringLiteral("遗物槽已满。选择要替换的位置，或选择「不替换」放弃："),
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
        logLines_ << QStringLiteral("替换遗物：%1 -> %2").arg(relics_[relicIndex], relic);
        while (logLines_.size() > 80) logLines_.removeFirst();
        relics_[relicIndex] = relic;
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
        if (!relics_.contains(n)) return;
    }
    if (relics_.contains(QStringLiteral("世界"))) return;
    for (const QString& n : needed)
    {
        relics_.removeAll(n);
    }
    logLines_ << QStringLiteral("四件剧情遗物融合为：世界");
    while (logLines_.size() > 80) logLines_.removeFirst();
    addRelic(QStringLiteral("世界"));
}

// ============================================================================
// Step 6: 战斗流程
// ============================================================================

void BattleEngine::setupBattle(int chapterIndex, int levelIndex)
{
    clearAllUnits();
    autoDeployPlayer(chapterIndex);

    ChapterDef c = chapters()[chapterIndex];
    QString enemyName;
    bool boss = false;
    if (levelIndex == 4) enemyName = c.elite4;
    else if (levelIndex == 5) enemyName = c.boss5;
    else if (levelIndex == 9) enemyName = c.elite9;
    else if (levelIndex == 10) enemyName = c.boss10;
    else enemyName = c.normalEnemies[(levelIndex + chapterIndex) % c.normalEnemies.size()];
    boss = isUniqueBossName(enemyName);

    // [Recoleta37] 主敌人也使用模板自身的 row，不再硬编码（如"血术师"含"术"应去后排）
    UnitTemplate mainT = makeEnemyTemplate(enemyName, boss, chapterIndex, levelIndex);
    enemyUnits_[slotForRow(mainT.row, enemyUnits_)] = createUnit(mainT, false, boss);
    // [Recoleta37] 修复：优先使用模板自身 row（弓/术/刺→后排，其余→前排）
    // 若该排已满则 fallback 到其他排，避免 slotForRow 返回 -1 导致数组越界
    // （如序章第9关梦影+异变魔影+破碎信徒全是前排，需一个让到后排）
    auto placeExtraEnemy = [this](const UnitTemplate& t) {
        int s = slotForRow(t.row, enemyUnits_);
        if (s < 0) s = slotForRow(t.row == QStringLiteral("前排") ? QStringLiteral("后排") : QStringLiteral("前排"), enemyUnits_);
        if (s < 0) s = slotForRow(QStringLiteral("中排"), enemyUnits_);
        if (s >= 0) enemyUnits_[s] = createUnit(t, false, false);
    };
    if (levelIndex >= 6)
    {
        placeExtraEnemy(makeEnemyTemplate(c.normalEnemies[(levelIndex + 1) % c.normalEnemies.size()], false, chapterIndex, levelIndex));
    }
    if (levelIndex >= 9)
    {
        placeExtraEnemy(makeEnemyTemplate(c.normalEnemies[(levelIndex + 2) % c.normalEnemies.size()], false, chapterIndex, levelIndex));
    }

    if (enemyName == QStringLiteral("偷窃者米格"))
    {
        bool copiedOneUnit = false;
        for (int i = 0; i < 5; ++i)
        {
            if (!copiedOneUnit && i != kHeroSlot && playerUnits_[i] != nullptr)
            {
                UnitTemplate copy = playerUnits_[i]->base;
                copy.name = QStringLiteral("复制") + copy.name;
                int s = slotForRow(copy.row, enemyUnits_);
                if (s >= 0 && enemyUnits_[s] == nullptr)
                {
                    enemyUnits_[s] = createUnit(copy, false, false);
                    copiedOneUnit = true;
                    appendLog(QStringLiteral("米格开局复制了一个单位：%1。").arg(playerUnits_[i]->base.name));
                }
            }
        }
    }

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
            clearAllUnits();
            autoDeployPlayer(chapterIndex);
            enemyUnits_[2] = createUnit(makeEnemyTemplate(QStringLiteral("米凯尔"), true, chapterIndex, levelIndex), false, true);
            enemyUnits_[3] = createUnit(makeEnemyTemplate(QStringLiteral("阿格尼"), true, chapterIndex, levelIndex), false, true);
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
        addRelic(QStringLiteral("圣洁六翼"));
        addRelic(QStringLiteral("精灵王冠"));
        tryFuseWorld();
    }
    if (levelIndex == 9)
    {
        bool passed = false;
        while (!passed)
        {
            QDialog dialog(parentWidget_);
            dialog.setWindowTitle(QStringLiteral("最终问题"));
            dialog.setModal(true);
            QVBoxLayout* layout = new QVBoxLayout(&dialog);
            layout->addWidget(smallLabel(QStringLiteral("什么让我们不再永恒？")));

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
                    QStringLiteral("爱"),
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
        QInputDialog::getText(parentWidget_, QStringLiteral("最终问题"), QStringLiteral("什么让我们不再永恒？"), QLineEdit::Normal, QString(), &ok);
        }
    }
    if (levelIndex >= 10)
    {
        showEnding();
        return;
    }
    inBattle_ = false;
    appendLog(QStringLiteral("世界章节事件关 %1 完成。").arg(levelIndex));
    if (refreshCallback_) refreshCallback_();
}

void BattleEngine::finishLevel(int chapterIndex, int levelIndex)
{
    inBattle_ = false;
    gold_ += 5 + chapterIndex;
    if (relics_.contains(QStringLiteral("铜钱袋"))) gold_ += 2;
    appendLog(QStringLiteral("胜利：获得金币，选择奖励。"));

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
    showUnitReward();
    showRelicReward(bossRelic);
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

void BattleEngine::showUnitReward()
{
    QVector<UnitTemplate> pool = {
        {QStringLiteral("见习剑士"), QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("斩击"), 42, 7},
        {QStringLiteral("弓箭手"), QStringLiteral("人族"), QStringLiteral("后排"), QStringLiteral("箭雨"), 28, 11},
        {QStringLiteral("牧师"), QStringLiteral("人族"), QStringLiteral("中排"), QStringLiteral("治疗术"), 32, 5},
        {QStringLiteral("盾卫"), QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("守护"), 58, 4},
        {QStringLiteral("血仆"), QStringLiteral("吸血鬼"), QStringLiteral("前排"), QStringLiteral("吸血"), 40, 8},
        {QStringLiteral("血术师"), QStringLiteral("吸血鬼"), QStringLiteral("中排"), QStringLiteral("赤心爆发"), 34, 10},
        {QStringLiteral("精灵射手"), QStringLiteral("精灵族"), QStringLiteral("后排"), QStringLiteral("精灵箭"), 32, 12},
        {QStringLiteral("守护天使"), QStringLiteral("天使"), QStringLiteral("前排"), QStringLiteral("六翼庇护"), 60, 6},
        {QStringLiteral("火焰术士"), QStringLiteral("魔族"), QStringLiteral("中排"), QStringLiteral("魔焰"), 36, 13},
        {QStringLiteral("深渊刺客"), QStringLiteral("魔族"), QStringLiteral("后排"), QStringLiteral("深渊爪击"), 32, 16}
    };
    QStringList names;
    QVector<UnitTemplate> offer;
    for (int i = 0; i < 3; ++i)
    {
        UnitTemplate t = pool[QRandomGenerator::global()->bounded(pool.size())];
        offer.push_back(t);
        names << QStringLiteral("%1  |  %2  |  HP %3  ATK %4  |  %5")
                     .arg(t.name, t.row)
                     .arg(t.hp)
                     .arg(t.atk)
                     .arg(t.skill);
    }
    bool ok = false;
    QString choice = QInputDialog::getItem(parentWidget_, QStringLiteral("棋子奖励"), QStringLiteral("选择一张加入牌库"), names, 0, false, &ok);
    if (ok)
    {
        int idx = names.indexOf(choice);
        if (idx >= 0) roster_.push_back(offer[idx]);
    }
}

void BattleEngine::showRelicReward(const QString& fixedRelic)
{
    if (!fixedRelic.isEmpty())
    {
        addRelic(fixedRelic);
        return;
    }
    QStringList pool = {
        QStringLiteral("铁剑"), QStringLiteral("旧盾"), QStringLiteral("旅人靴"), QStringLiteral("铜钱袋"), QStringLiteral("破碎护符"),
        QStringLiteral("战鼓"), QStringLiteral("医疗包"), QStringLiteral("魔法书"), QStringLiteral("幸运骰子"), QStringLiteral("空白遗物"),
        QStringLiteral("人王徽记"), QStringLiteral("猩红酒杯"), QStringLiteral("精灵树枝"), QStringLiteral("圣河水滴"), QStringLiteral("魔王残角")
    };
    QStringList offer;
    for (int i = 0; i < 3; ++i)
    {
        const QString relic = pool[QRandomGenerator::global()->bounded(pool.size())];
        offer << QStringLiteral("%1  |  %2").arg(relic, relicDescription(relic));
    }
    bool ok = false;
    QString choice = QInputDialog::getItem(parentWidget_, QStringLiteral("遗物奖励"), QStringLiteral("选择一个遗物"), offer, 0, false, &ok);
    if (ok)
    {
        addRelic(choice.section(QStringLiteral("  |  "), 0, 0));
    }
}

void BattleEngine::autoDeployPlayer(int chapterIndex)
{
    playerUnits_[kHeroSlot] = createUnit(heroTemplate(chapterIndex), true, false);
    for (const UnitTemplate& t : roster_)
    {
        int slot = slotForRow(t.row, playerUnits_);
        if (slot >= 0 && playerUnits_[slot] == nullptr)
        {
            playerUnits_[slot] = createUnit(t, false, false);
        }
    }
    applyRelicsStart();
}

void BattleEngine::showDeckRefillDialog()
{
    if (roster_.isEmpty() || !inBattle_)
    {
        return;
    }

    QVector<UnitTemplate> offer;
    for (int i = 0; i < 4 && !roster_.isEmpty(); ++i)
    {
        offer.push_back(roster_[QRandomGenerator::global()->bounded(roster_.size())]);
    }

    auto rowPriority = [](const QString& row) {
        if (row == QStringLiteral("前排")) return 0;
        if (row == QStringLiteral("中排")) return 1;
        return 2;
    };
    std::sort(offer.begin(), offer.end(), [this, rowPriority](const UnitTemplate& a, const UnitTemplate& b) {
        const bool aPlaceable = hasEmptySlotForRow(a.row);
        const bool bPlaceable = hasEmptySlotForRow(b.row);
        if (aPlaceable != bPlaceable) return aPlaceable > bPlaceable;
        return rowPriority(a.row) < rowPriority(b.row);
    });

    QDialog dialog(parentWidget_);
    dialog.setWindowTitle(QStringLiteral("牌库补员：抽取4张"));
    dialog.setMinimumWidth(720);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    // [Recoleta37] 修改描述：实际选择受对应排空位限制，简化文字
    layout->addWidget(smallLabel(QStringLiteral("非主角棋子死亡，可选择对应位置棋子上场；未选择的牌返回牌库。")));

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
        const UnitTemplate& t = offer[i];
        QCheckBox* box = new QCheckBox(QStringLiteral("%1\n%2\nHP%3 ATK%4\n技能:%5")
                                           .arg(t.name, t.row)
                                           .arg(t.hp)
                                           .arg(t.atk)
                                           .arg(t.skill));
        bool placeable = hasEmptySlotForRow(t.row);
        box->setEnabled(placeable);
        box->setChecked(false);
        box->setFixedSize(160, 150);
        /// [Recoleta37] 按排限制勾选上限：该排已选数 > 该排空位数时自动取消
        const QString row = t.row;
        connect(box, &QCheckBox::clicked, this, [&checks, &offer, box, row, emptyFront, emptyMiddle, emptyBack]() {
            int rowLimit = (row == QStringLiteral("前排")) ? emptyFront
                         : (row == QStringLiteral("中排")) ? emptyMiddle
                         : emptyBack;
            int rowChecked = 0;
            for (int j = 0; j < checks.size(); ++j)
            {
                if (checks[j]->isChecked() && offer[j].row == row) ++rowChecked;
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

    for (int i = 0; i < offer.size(); ++i)
    {
        if (!checks[i]->isChecked())
        {
            continue;
        }
        int slot = slotForRow(offer[i].row, playerUnits_);
        if (slot >= 0 && playerUnits_[slot] == nullptr)
        {
            playerUnits_[slot] = createUnit(offer[i], false, false);
            appendLog(QStringLiteral("补员上场：%1 -> %2").arg(offer[i].name).arg(offer[i].row));
        }
        else
        {
            appendLog(QStringLiteral("补员未上场，返回牌库：%1").arg(offer[i].name));
        }
    }
}

void BattleEngine::runRound(int chapterIndex, int levelIndex)
{
    if (!inBattle_) return;
    skillsUsedThisTurn_ = 0;  /// [Recoleta37] 每回合重置技能使用计数
    appendLog(QStringLiteral("第%1回合").arg(battleRound_));
    applyRelicsStart();
    bossMechanics(chapterIndex);
    generateSkills();
    allAttack(playerUnits_, enemyUnits_, true);
    allAttack(enemyUnits_, playerUnits_, false);
    cleanupDeaths(enemyUnits_, false);
    cleanupDeaths(playerUnits_, true);
    ++battleRound_;
    if (enemiesDefeated())
    {
        finishLevel(chapterIndex, levelIndex);
    }
    else if (playerDefeated())
    {
        appendLog(QStringLiteral("失败：极限测试版自动重整后重试本关。"));
        setupBattle(chapterIndex, levelIndex);
    }
    if (refreshCallback_) refreshCallback_();
}

/// [Recoleta37] 修复：使用成员变量 skillsUsedThisTurn_ 跟踪本回合已释放次数，
/// 避免同一回合内多次点击按钮绕过"每回合最多2张"的限制。
void BattleEngine::cleanupDeaths(std::array<UnitInstance*, 5>& board, bool playerSide)
{
    bool needRefill = false;
    for (int i = 0; i < 5; ++i)
    {
        UnitInstance* u = board[i];
        if (!u || u->hp > 0) continue;
        if (playerSide && u->hero && (relics_.contains(QStringLiteral("破碎护符")) || u->protectedDeath) && !u->revived)
        {
            u->hp = 1;
            u->revived = true;
            appendLog(QStringLiteral("主角被保留在1点生命。"));
            continue;
        }
        if (playerSide && relics_.contains(QStringLiteral("精灵王冠")) && !u->revived)
        {
            u->hp = 8;
            u->revived = true;
            appendLog(QStringLiteral("%1 因精灵王冠复苏。").arg(u->base.name));
            continue;
        }
        if (!playerSide && u->boss && !u->revived && (u->base.name == QStringLiteral("艾琳") || u->base.name == QStringLiteral("阿格尼") || u->base.name == QStringLiteral("米凯尔")))
        {
            u->hp = std::max(20, u->base.hp / 2);
            u->base.atk += (u->base.name == QStringLiteral("阿格尼") ? u->base.atk : 0);
            u->revived = true;
            appendLog(QStringLiteral("%1 触发Boss复活机制。").arg(u->base.name));
            continue;
        }
        if (playerSide && relics_.contains(QStringLiteral("精灵树枝")) && u->base.faction == QStringLiteral("精灵族"))
        {
            for (UnitInstance* e : alive(enemyUnits_)) dealDamage(e, 5, QStringLiteral("精灵树枝"));
        }
        if (playerSide && !u->hero)
        {
            needRefill = true;
        }
        delete u;
        board[i] = nullptr;
    }
    if (needRefill)
    {
        if (refreshCallback_) refreshCallback_();
        showDeckRefillDialog();
    }
}

void BattleEngine::showEnding()
{
    if (endingShown_) return;
    endingShown_ = true;
    inBattle_ = false;
    mapManager_->showMapPoint(-1, true, [this]() {
    storyManager_->showStoryKey("ending", [this]() {
        QMessageBox::information(parentWidget_, QStringLiteral("游戏结束"), QStringLiteral("最终结局完成，返回主菜单。"));
        if (restartGameCallback_) restartGameCallback_();
    });
    });
}

