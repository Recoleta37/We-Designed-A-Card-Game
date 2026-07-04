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
    void resizeEvent(QResizeEvent* event) override;

private:
    int chapterIndex = 0;
    int levelIndex = 1;
    /// [Recoleta37] 章节标题/地图状态留在 MainWindow，不属战斗逻辑
    int lastChapterTitleShown = -1;
    bool secondChapterAncientCityShown = false;

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
    /// [Recoleta37] Phase 3: 战斗逻辑交由 BattleEngine
    BattleEngine battleEngine;

    void buildUi();
    QWidget* buildBoard();
    QWidget* buildBottomBar();
    QWidget* buildRightPanel();
    void updateOverlayGeometry();

    /// [Recoleta37] 留在 MainWindow: 跨系统编排
    void initData();
    void startGame();
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
};
