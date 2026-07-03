#pragma once

#include <array>
#include <functional>

#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPointF>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

#include "GameTypes.h"
#include "MapManager.h"
#include "StoryManager.h"

class MainWindow : public QWidget
{
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int chapterIndex = 0;
    int levelIndex = 1;
    int battleRound = 1;
    int gold = 0;
    /// [Recoleta37] 本回合已释放技能数量，上限2，每回合重置
    int skillsUsedThisTurn = 0;
    bool inBattle = false;
    bool endingShown = false;
    int lastChapterTitleShown = -1;
    bool secondChapterAncientCityShown = false;

    std::array<UnitInstance*, 5> playerUnits{};
    std::array<UnitInstance*, 5> enemyUnits{};
    QVector<UnitTemplate> roster;
    QVector<SkillCard> skillSlots;
    QStringList relics;
    QStringList logLines;
    QLabel* titleLabel = nullptr;
    QLabel* progressLabel = nullptr;
    QLabel* heroLabel = nullptr;
    QLabel* goldLabel = nullptr;
    QTextEdit* logView = nullptr;
    std::array<QPushButton*, 10> boardButtons{};
    std::array<QPushButton*, 5> skillButtons{};
    std::array<QPushButton*, 5> relicButtons{};
    QPushButton* skillCastButton = nullptr;
    QPushButton* roundButton = nullptr;
    QPushButton* nextButton = nullptr;

    /// [Recoleta37] Phase 2: 剧情管理交由 StoryManager
    StoryManager storyManager;
    /// [Recoleta37] Phase 4: 章节标题 + 地图 overlay 交由 MapManager
    MapManager mapManager;

    void buildUi();
    QWidget* buildBoard();
    QWidget* buildBottomBar();
    QWidget* buildRightPanel();
    void updateOverlayGeometry();

    void initData();
    void startGame();
    void enterCurrentLevel();
    void setupBattle();
    void setupWorldEvent();
    void finishLevel();
    void advanceLevel();
    void showUnitReward();
    void showRelicReward(const QString& fixedRelic = QString());
    void autoDeployPlayer();
    void showDeckRefillDialog();

    UnitTemplate heroTemplate() const;
    UnitTemplate templateByName(const QString& name) const;
    UnitInstance* createUnit(const UnitTemplate& t, bool hero = false, bool boss = false) const;
    UnitTemplate makeEnemyTemplate(const QString& name, bool boss) const;
    int slotForRow(const QString& row, const std::array<UnitInstance*, 5>& board) const;
    bool hasEmptySlotForRow(const QString& row) const;
    QList<UnitInstance*> alive(std::array<UnitInstance*, 5>& board) const;
    UnitInstance* firstAlive(std::array<UnitInstance*, 5>& board) const;
    UnitInstance* lowestHp(std::array<UnitInstance*, 5>& board) const;
    UnitInstance* highestAtk(std::array<UnitInstance*, 5>& board) const;

    void runRound();
    void applyRelicsStart();
    void generateSkills();
    void castSelectedSkills();
    void castSkill(const QString& name);
    void allAttack(std::array<UnitInstance*, 5>& attackers, std::array<UnitInstance*, 5>& defenders, bool playerSide);
    void dealDamage(UnitInstance* target, int amount, const QString& reason);
    void heal(UnitInstance* target, int amount);
    void cleanupDeaths(std::array<UnitInstance*, 5>& board, bool playerSide);
    void bossMechanics();
    bool enemiesDefeated() const;
    bool playerDefeated() const;

    void showEnding();

    void refreshUi();
    void refreshBoard();
    void refreshSkills();
    void refreshRelics();
    void appendLog(const QString& text);
    void clearBoard(std::array<UnitInstance*, 5>& board);
    void clearAllUnits();
    void addRelic(const QString& relic);
    QString relicDescription(const QString& relic) const;
    QString skillDescription(const QString& skill) const;
    bool isStoryRelic(const QString& relic) const;
    void tryFuseWorld();
};
