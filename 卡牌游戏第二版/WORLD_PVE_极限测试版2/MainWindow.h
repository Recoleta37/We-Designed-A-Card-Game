#pragma once

#include <array>
#include <functional>

#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QPointF>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

#include "BattleEngine.h"
#include "GameTypes.h"
#include "MapManager.h"
#include "StoryManager.h"

/// [Recoleta37] Phase 1-5 重构后 MainWindow 仅保留 UI 构建 / 跨系统编排 / UI 刷新
/// 所有战斗逻辑已提取到 BattleEngine（Phase 3）
class MainWindow : public QWidget
{
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int chapterIndex = 0;
    int levelIndex = 1;
    /// [Recoleta37] 章节标题/地图状态留在 MainWindow，不属战斗逻辑
    int lastChapterTitleShown = -1;
    bool secondChapterAncientCityShown = false;
    bool chapterCheckpointValid = false;
    int checkpointChapterIndex = 0;
    int checkpointLastChapterTitleShown = -1;
    bool checkpointSecondChapterAncientCityShown = false;

    QLabel* titleLabel = nullptr;
    QLabel* progressLabel = nullptr;
    // [Recoleta37] 主角状态从纯文字改为图标 + 进度条
    QProgressBar* heroHpBar = nullptr;
    QProgressBar* heroShieldBar = nullptr;
    QLabel* heroAtkLabel = nullptr;
    QLabel* heroGoldLabel = nullptr;
    QTextEdit* logView = nullptr;
    // [Recoleta37] 卡牌 UI：QFrame 容器 + 内部子控件，替代纯文字 QPushButton
    struct BoardCard {
        QFrame* frame = nullptr;
        QLabel* rowLabel = nullptr;
        QLabel* atkLabel = nullptr;
        QLabel* nameLabel = nullptr;
        QProgressBar* hpBar = nullptr;
        QLabel* shieldLabel = nullptr;
    };
    std::array<BoardCard, 10> boardCards{};
    // [Recoleta37] 技能牌改为 QFrame + QLabel，支持富文本
    struct SkillCard {
        QFrame* frame = nullptr;
        QLabel* sourceLabel = nullptr;
        QLabel* nameLabel = nullptr;
        QLabel* descLabel = nullptr;
        bool checked = false;
    };
    std::array<SkillCard, 5> skillCards{};
    /// [Recoleta37] 已选中的技能卡数量
    int checkedSkillCount() const;
    // [Recoleta37] 遗物改为 QFrame + QLabel(word wrap)，修复长描述被截断
    struct RelicCard {
        QFrame* frame = nullptr;
        QLabel* nameLabel = nullptr;
        QLabel* descLabel = nullptr;
    };
    std::array<RelicCard, 5> relicCards{};
    QPushButton* skillCastButton = nullptr;
    QPushButton* roundButton = nullptr;
    QPushButton* nextButton = nullptr;

    /// [Recoleta37] Phase 2: 剧情管理交由 StoryManager
    StoryManager storyManager;
    /// [Recoleta37] Phase 4: 章节标题 + 地图 overlay 交由 MapManager
    MapManager mapManager;
    /// [Recoleta37] Phase 3: 战斗逻辑交由 BattleEngine
    BattleEngine battleEngine;

    void buildUi();
    QWidget* buildBoard();
    QWidget* buildBottomBar();
    QWidget* buildRightPanel();
    void updateOverlayGeometry();
    bool handleEscapeKey();

    /// [Recoleta37] 留在 MainWindow: 跨系统编排
    void initData();
    void startGame();
    void restartFromChapterCheckpoint();
    void enterCurrentLevel();
    void advanceLevel();

    /// [Recoleta37] 留在 MainWindow: 访问 UI 控件
    void castSelectedSkills();

    /// [Recoleta37] 留在 MainWindow: UI 刷新
    void refreshUi();
    void refreshBoard();
    void refreshSkills();
    void refreshRelics();
    void appendLog(const QString& text);

    /// 浮动伤害数字动画
    void animateCombatEvents();
    void spawnDamageLabel(const CombatEvent& event, int boardCardIndex, int yOffset);
    void flashCard(int boardCardIndex);

    bool animating_ = false;
};
