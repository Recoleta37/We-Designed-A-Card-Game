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

struct UnitTemplate
{
    QString name;
    QString faction;
    QString row;
    QString skill;
    int hp;
    int atk;
};

struct UnitInstance
{
    UnitTemplate base;
    int hp = 0;
    int shield = 0;
    int aliveRounds = 0;
    bool hero = false;
    bool boss = false;
    bool revived = false;
    bool protectedDeath = false;
};

struct SkillCard
{
    QString name;
    QString source;
};

struct ChapterDef
{
    QString title;
    QStringList normalEnemies;
    QString elite4;
    QString boss5;
    QString elite9;
    QString boss10;
    QString bossRelic;
    QString bossAlly;
};

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
    bool inBattle = false;
    bool endingShown = false;
    bool chapterTitleReady = false;
    int lastChapterTitleShown = -1;
    bool secondChapterAncientCityShown = false;
    bool mapReady = false;
    std::array<bool, 10> mapPointLit{};

    std::array<UnitInstance*, 5> playerUnits{};
    std::array<UnitInstance*, 5> enemyUnits{};
    QVector<UnitTemplate> roster;
    QVector<SkillCard> skillSlots;
    QStringList relics;
    QStringList logLines;
    QMap<QString, QVector<QPair<QString, QString>>> storyScenes;
    QStringList pendingStoryKeys;
    std::function<void()> storyFinishedCallback;

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

    QWidget* storyOverlay = nullptr;
    QLabel* storySpeaker = nullptr;
    QLabel* storyText = nullptr;
    QTimer storyTimer;
    QVector<QPair<QString, QString>> activeStory;
    int storyLineIndex = 0;
    int storyCharIndex = 0;
    bool storyLineComplete = false;

    QWidget* chapterOverlay = nullptr;
    QLabel* chapterChineseLabel = nullptr;
    QLabel* chapterEnglishLabel = nullptr;
    QTimer chapterTitleTimer;
    std::function<void()> chapterTitleFinishedCallback;

    QWidget* mapOverlay = nullptr;
    QLabel* mapImageLabel = nullptr;
    QLabel* mapHintLabel = nullptr;
    QWidget* mapWhiteWash = nullptr;
    QGraphicsOpacityEffect* mapWhiteWashOpacity = nullptr;
    QVector<QFrame*> mapBlackDots;
    QVector<QFrame*> mapWhiteDots;
    QVector<QGraphicsOpacityEffect*> mapWhiteDotEffects;
    QTimer mapTimer;
    std::function<void()> mapFinishedCallback;

    void buildUi();
    QWidget* buildBoard();
    QWidget* buildBottomBar();
    QWidget* buildRightPanel();
    void buildStoryOverlay();
    void buildChapterOverlay();
    void buildMapOverlay();
    void updateOverlayGeometry();

    void loadStory();
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

    void showStory(const QStringList& keys, std::function<void()> onFinished = {});
    void showStoryKey(const QString& key, std::function<void()> onFinished = {});
    void nextStoryStep();
    void tickStory();
    void showEnding();
    void showChapterTitle(std::function<void()> onFinished);
    void showMapPoint(int pointIndex, bool fadeWholeMap, std::function<void()> onFinished);
    int mapPointForChapter(int index) const;
    QPointF mapPointRatio(int pointIndex) const;
    QString chapterEnglishName(int index) const;

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
