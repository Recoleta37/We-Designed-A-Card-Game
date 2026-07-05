#pragma once

#include <array>
#include <functional>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "GameTypes.h"

class MapManager;
class StoryManager;

/// 战斗事件 —— 供 MainWindow 消费以播放浮动数字动画
struct CombatEvent {
    enum Type { Damage, Heal, ShieldGain };
    Type type;
    int targetIndex;   // 0-4=敌方, 5-9=己方
    int sourceIndex;   // 0-9 来源卡牌索引，-1 无来源
    int amount;        // 始终为正数
    int batchId;       // 同一 batch 内共享
};

/// [Recoleta37] Phase 3: BattleEngine 封装所有战斗相关逻辑
/// - 单位创建与棋盘管理
/// - 战斗结算（伤害/治疗/攻击/死亡清理）
/// - 技能生成与释放
/// - 遗物效果与融合
///
/// chapterIndex / levelIndex 留在 MainWindow，
/// BattleEngine 方法通过参数接收，避免双写不同步。
///
/// 使用回调与 MainWindow 解耦：
///   setLogCallback / setRefreshCallback 替代直接调用 MainWindow::appendLog / refreshUi
class BattleEngine : public QObject
{
    Q_OBJECT

public:
    /// @param parent 用于战斗内对话框的父窗口
    /// @param story  剧情管理器指针（Step 6 战斗流程中使用）
    /// @param map    地图管理器指针（Step 6 战斗流程中使用）
    explicit BattleEngine(QWidget* parent, StoryManager* story = nullptr, MapManager* map = nullptr);

    // ============================================================
    // 状态访问器（供 MainWindow::refreshUi 族使用）
    // ============================================================
    int battleRound() const { return battleRound_; }
    int gold() const { return gold_; }
    int skillsUsedThisTurn() const { return skillsUsedThisTurn_; }
    bool inBattle() const { return inBattle_; }
    bool endingShown() const { return endingShown_; }

    const std::array<UnitInstance*, 5>& playerUnits() const { return playerUnits_; }
    const std::array<UnitInstance*, 5>& enemyUnits() const { return enemyUnits_; }
    const QVector<UnitTemplate>& roster() const { return roster_; }
    const QVector<SkillCard>& skillSlots() const { return skillSlots_; }
    const QStringList& relics() const { return relics_; }
    const QStringList& logLines() const { return logLines_; }

    // ============================================================
    // 可变状态访问器
    // ============================================================
    /// @name 供 MainWindow 中仍未迁移的方法（initData / startGame / enterCurrentLevel / advanceLevel / refreshUi 等）读写战斗状态
    /// @{
    int& battleRoundRef() { return battleRound_; }
    int& goldRef() { return gold_; }
    int& skillsUsedThisTurnRef() { return skillsUsedThisTurn_; }
    bool& inBattleRef() { return inBattle_; }
    bool& endingShownRef() { return endingShown_; }
    std::array<UnitInstance*, 5>& playerUnitsRef() { return playerUnits_; }
    std::array<UnitInstance*, 5>& enemyUnitsRef() { return enemyUnits_; }
    QVector<UnitTemplate>& rosterRef() { return roster_; }
    QVector<SkillCard>& skillSlotsRef() { return skillSlots_; }
    QStringList& relicsRef() { return relics_; }
    QStringList& logLinesRef() { return logLines_; }
    /// @}

    // ============================================================
    // 回调设置
    // ============================================================
    void setLogCallback(std::function<void(const QString&)> callback) { logCallback_ = std::move(callback); }
    void setRefreshCallback(std::function<void()> callback) { refreshCallback_ = std::move(callback); }
    void setRestartGameCallback(std::function<void()> callback) { restartGameCallback_ = std::move(callback); }
    void setRestartChapterCallback(std::function<void()> callback) { restartChapterCallback_ = std::move(callback); }

    /// 保存/恢复大章节开头的完整战斗状态
    void saveChapterSnapshot();
    bool hasChapterSnapshot() const { return chapterSnapshotValid_; }
    void restoreChapterSnapshot();

    // ============================================================
    // Step 1: 基础层 —— 单位创建 / 棋盘查询 / 清理
    // ============================================================

    /// 根据模板创建单位实例（堆分配）
    UnitInstance* createUnit(const UnitTemplate& t, bool hero = false, bool boss = false) const;

    /// 计算主角模板（随章节成长 + 遗物加成）
    UnitTemplate heroTemplate(int chapterIndex) const;

    /// 按名称查找模板：先查特殊角色，再查牌库，最后返回默认
    UnitTemplate templateByName(const QString& name) const;

    /// 为敌人构造模板（随章节/关卡缩放属性）
    UnitTemplate makeEnemyTemplate(const QString& name, bool boss, int chapterIndex, int levelIndex) const;

    /// 返回指定排的第一个可用槽位索引，-1 表示无空位
    int slotForRow(const QString& row, const std::array<UnitInstance*, 5>& board) const;

    /// 己方棋盘某排是否有空位（仅查该排，不跨排 fallback）
    bool hasEmptySlotForRow(const QString& row) const;

    /// 返回棋盘上所有存活单位的列表
    QList<UnitInstance*> alive(const std::array<UnitInstance*, 5>& board) const;

    /// 返回棋盘上第一个存活单位（用于定位攻击目标）
    UnitInstance* firstAlive(const std::array<UnitInstance*, 5>& board) const;

    /// 返回棋盘上当前 HP 最低的存活单位
    UnitInstance* lowestHp(const std::array<UnitInstance*, 5>& board) const;

    /// 返回棋盘上失血最多的存活单位（base.hp - hp 最大），用于治疗
    UnitInstance* mostWounded(const std::array<UnitInstance*, 5>& board) const;

    /// 返回棋盘上当前攻击力最高的存活单位
    UnitInstance* highestAtk(const std::array<UnitInstance*, 5>& board) const;

    /// 删除棋盘上所有单位并置空指针
    void clearBoard(std::array<UnitInstance*, 5>& board);

    /// 清理双方棋盘
    void clearAllUnits();

    /// 从牌库中移除一张同名牌
    bool removeOneRosterCard(const QString& unitName);

    // ============================================================
    // Step 2: 描述表 —— 技能/遗物/剧情遗物查询（纯 const）
    // ============================================================

    /// 返回遗物的中文效果描述
    QString relicDescription(const QString& relic) const;

    /// 返回技能的中文效果描述
    QString skillDescription(const QString& skill) const;

    /// 判断遗物是否为不可替换的剧情遗物
    bool isStoryRelic(const QString& relic) const;

    // ============================================================
    // Step 3: 伤害/治疗/攻击结算
    // ============================================================

    /// 对目标造成伤害（先扣护盾再扣 HP），写入日志
    /// @param source 攻击者指针，用于定位来源卡牌闪烁；nullptr 表示无来源
    void dealDamage(UnitInstance* target, int amount, const QString& reason, UnitInstance* source = nullptr);

    /// 治疗目标
    void heal(UnitInstance* target, int amount);

    /// 添加护盾并记录 ShieldGain 事件
    void addShield(UnitInstance* target, int amount, const QString& reason = {});

    /// 战斗事件队列（供 MainWindow 消费后清除）
    const QVector<CombatEvent>& pendingCombatEvents() const { return pendingCombatEvents_; }
    void clearCombatEvents() { pendingCombatEvents_.clear(); }

    /// 开始新的事件批次（同一批次的数字同时飘出）
    void nextEventBatch() { ++eventBatchId_; }

    /// 一方全体攻击另一方，playerSide 控制遗物加成
    void allAttack(std::array<UnitInstance*, 5>& attackers,
                   std::array<UnitInstance*, 5>& defenders,
                   bool playerSide);

    /// Boss 专属机制（每回合触发）
    void bossMechanics(int chapterIndex);

    /// 敌方是否全灭
    bool enemiesDefeated() const;

    /// 主角是否死亡
    bool playerDefeated() const;

    // ============================================================
    // Step 4: 技能系统
    // ============================================================

    /// 每回合为存活单位生成技能卡（每 2 回合产一张）
    void generateSkills();

    /// 释放指定名称的技能
    void castSkill(const QString& name, int chapterIndex);

    // ============================================================
    // Step 5: 遗物系统
    // ============================================================

    /// 回合开始时触发所有遗物效果
    void applyRelicsStart();

    /// 获得遗物（槽满时弹出替换对话框）
    void addRelic(const QString& relic);

    /// 检查四件剧情遗物是否集齐，融合为"世界"
    void tryFuseWorld();

    // ============================================================
    // Step 6: 战斗流程
    // ============================================================

    /// 自动将牌库中的棋子部署到己方棋盘
    void autoDeployPlayer(int chapterIndex);

    /// 准备新战斗的己方棋盘：优先继承上一关存活棋子的 HP 和位置
    void preparePlayerForBattle(int chapterIndex);

    /// 初始化一场战斗（生成敌人并放置棋盘）
    void setupBattle(int chapterIndex, int levelIndex);

    /// 世界章节特殊事件（问答/Boss/最终问题）
    void setupWorldEvent(int chapterIndex, int& levelIndex);

    /// 执行一回合战斗结算
    void runRound(int chapterIndex, int levelIndex);

    /// 清理死亡单位并触发补员/遗物效果
    void cleanupDeaths(std::array<UnitInstance*, 5>& board, bool playerSide);

    /// 胜利结算（金币/奖励/剧情触发）
    void finishLevel(int chapterIndex, int levelIndex);

    /// 失败结算（主角死亡后选择从头或从本大章节开头挑战）
    void finishDefeat(int chapterIndex, int levelIndex);

    /// 棋子奖励选择对话框
    void showUnitReward();

    /// 遗物奖励选择对话框
    void showRelicReward(const QString& fixedRelic = QString());

    /// 牌库补员对话框（棋子死亡后按死亡排位抽取替补候选）
    void showDeckRefillDialog(const QStringList& deadRows);

    /// 结局播放（结束时通过 restartGameCallback 回到 MainWindow::startGame）
    void showEnding();

private:
    QWidget* parentWidget_;
    StoryManager* storyManager_;
    MapManager* mapManager_;

    // ---- 战斗状态 ----
    int battleRound_ = 1;
    int gold_ = 0;
    int skillsUsedThisTurn_ = 0;
    bool inBattle_ = false;
    bool endingShown_ = false;

    // ---- 棋盘与牌库 ----
    std::array<UnitInstance*, 5> playerUnits_{};
    std::array<UnitInstance*, 5> enemyUnits_{};
    QVector<UnitTemplate> roster_;
    QVector<SkillCard> skillSlots_;
    QStringList relics_;

    struct BoardSnapshot
    {
        std::array<UnitInstance, 5> units{};
        std::array<bool, 5> present{};
    };

    bool chapterSnapshotValid_ = false;
    int snapshotBattleRound_ = 1;
    int snapshotGold_ = 0;
    int snapshotSkillsUsedThisTurn_ = 0;
    bool snapshotInBattle_ = false;
    bool snapshotEndingShown_ = false;
    BoardSnapshot snapshotPlayerUnits_;
    BoardSnapshot snapshotEnemyUnits_;
    QVector<UnitTemplate> snapshotRoster_;
    QVector<SkillCard> snapshotSkillSlots_;
    QStringList snapshotRelics_;
    QStringList snapshotLogLines_;
    QStringList logLines_;

    // ---- 事件队列 ----
    QVector<CombatEvent> pendingCombatEvents_;
    int eventBatchId_ = 0;

    // ---- 回调 ----
    std::function<void(const QString&)> logCallback_;
    std::function<void()> refreshCallback_;
    std::function<void()> restartGameCallback_;
    std::function<void()> restartChapterCallback_;

    /// 内部日志写入（替代 MainWindow::appendLog）
    void appendLog(const QString& text)
    {
        logLines_ << text;
        while (logLines_.size() > 80) logLines_.removeFirst();
    }

    /// 根据单位指针查找棋盘卡牌索引（0-4=敌方, 5-9=己方, -1=未找到）
    int boardCardIndexOf(UnitInstance* target) const;
};
