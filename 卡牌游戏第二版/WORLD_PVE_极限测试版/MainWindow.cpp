#include "MainWindow.h"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QGridLayout>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QVBoxLayout>

namespace
{
QPushButton* makeCardButton(const QString& name)
{
    QPushButton* button = new QPushButton;
    button->setObjectName(name);
    button->setCursor(Qt::PointingHandCursor);
    if (name == "skillCard")
    {
        button->setFixedSize(178, 148);
    }
    else if (name == "boardCell")
    {
        button->setFixedSize(132, 104);
    }
    else if (name == "relicCard")
    {
        button->setMinimumSize(150, 118);
    }
    else
    {
        button->setMinimumSize(92, 112);
    }
    return button;
}

QString boardCardStyle(UnitInstance* unit, bool enemySide)
{
    const QString baseText = "#211509";
    if (unit == nullptr)
    {
        return QStringLiteral(
            "QPushButton#boardCell, QPushButton#boardCell:disabled {"
            " background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #c8ad78, stop:0.55 #d3bd88, stop:1 #b6975f);"
            " color:#6c5738;"
            " border:2px solid #6b4526;"
            " border-radius:6px;"
            " padding:7px;"
            " font-size:13px;"
            " font-weight:700;"
            "}");
    }

    QString background = enemySide
        ? QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #b89463, stop:0.55 #d0b27a, stop:1 #9d7345)")
        : QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #c5aa72, stop:0.55 #dbc28b, stop:1 #b18a55)");
    QString border = enemySide ? QStringLiteral("#9b6930") : QStringLiteral("#d7a12f");
    QString glow = QStringLiteral("#6a3f19");

    if (unit->hero)
    {
        background = QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #73618a, stop:0.5 #b59cc5, stop:1 #5a416f)");
        border = QStringLiteral("#f4d36b");
        glow = QStringLiteral("#2d183d");
    }
    else if (unit->boss)
    {
        background = QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #7a3b2a, stop:0.48 #b47a45, stop:1 #4b2118)");
        border = QStringLiteral("#f0c94f");
        glow = QStringLiteral("#30120b");
    }

    return QStringLiteral(
        "QPushButton#boardCell, QPushButton#boardCell:disabled {"
        " background:%1;"
        " color:%2;"
        " border:3px solid %3;"
        " border-radius:7px;"
        " padding:7px;"
        " font-size:13px;"
        " font-weight:800;"
        "}"
        "QPushButton#boardCell:hover { border-color:#fff0a0; background:%1; }")
        .arg(background, baseText, border, glow);
}

QString wrapFixedLines(const QString& text, int lineWidth, int maxLines)
{
    QStringList lines;
    QString current;
    for (const QChar& ch : text)
    {
        current.append(ch);
        if (current.size() >= lineWidth)
        {
            lines << current;
            current.clear();
            if (lines.size() == maxLines) break;
        }
    }
    if (!current.isEmpty() && lines.size() < maxLines)
    {
        lines << current;
    }
    if (lines.size() == maxLines)
    {
        const int used = lineWidth * maxLines;
        if (text.size() > used && !lines.last().endsWith(QStringLiteral("...")))
        {
            QString last = lines.last();
            if (last.size() > 3) last.chop(3);
            lines.last() = last + QStringLiteral("...");
        }
    }
    while (lines.size() < maxLines)
    {
        lines << QString();
    }
    return lines.join("\n");
}

QLabel* smallLabel(const QString& text)
{
    QLabel* label = new QLabel(text);
    label->setObjectName("smallLabel");
    label->setWordWrap(true);
    return label;
}

QString styleAssetUrl(const QString& relativePath)
{
    const QStringList candidates = {
        QApplication::applicationDirPath() + "/" + relativePath,
        QApplication::applicationDirPath() + "/../" + relativePath,
        QApplication::applicationDirPath() + "/../../" + relativePath,
        QDir::currentPath() + "/" + relativePath,
        relativePath
    };
    for (const QString& path : candidates)
    {
        if (QFile::exists(path))
        {
            QString normalized = QDir::toNativeSeparators(path);
            normalized.replace("\\", "/");
            return normalized;
        }
    }
    return QString();
}
}

MainWindow::MainWindow(QWidget* parent) : QWidget(parent), storyManager(this), mapManager(this), battleEngine(this, &storyManager, &mapManager)
{
    setWindowTitle(QStringLiteral("WORLD Alpha 0.9 PVE 极限测试版"));
    setMinimumSize(1320, 820);
    resize(1500, 900);
    buildUi();
    battleEngine.setRefreshCallback([this]() { refreshUi(); });
    battleEngine.setRestartGameCallback([this]() { startGame(); });
    initData();
    storyManager.loadStory();
    startGame();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    return QWidget::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateOverlayGeometry();
}

void MainWindow::buildUi()
{
    QHBoxLayout* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    QGroupBox* left = new QGroupBox(QStringLiteral("状态 / 日志"));
    QVBoxLayout* leftLayout = new QVBoxLayout(left);
    titleLabel = new QLabel;
    titleLabel->setObjectName("titleLabel");
    progressLabel = smallLabel(QString());
    heroLabel = smallLabel(QString());
    goldLabel = smallLabel(QString());
    logView = new QTextEdit;
    logView->setReadOnly(true);
    leftLayout->addWidget(titleLabel);
    leftLayout->addWidget(progressLabel);
    leftLayout->addWidget(heroLabel);
    leftLayout->addWidget(goldLabel);
    leftLayout->addWidget(logView, 1);
    root->addWidget(left, 1);

    QVBoxLayout* center = new QVBoxLayout;
    center->addWidget(buildBoard(), 6);
    center->addWidget(buildBottomBar(), 1);
    root->addLayout(center, 4);
    root->addWidget(buildRightPanel(), 1);

    mapManager.buildChapterOverlay();
    mapManager.buildMapOverlay();

    const QString textureUrl = styleAssetUrl(QStringLiteral("assets/ui/parchment_tile.png"));
    const QString textureRule = textureUrl.isEmpty() ? QString() : QStringLiteral(" background-image:url(%1);").arg(textureUrl);
    setStyleSheet(QString(
        "QWidget { background:#6f5632;%1 color:#24170b; font-family:'Microsoft YaHei'; font-size:14px; }"
        "QGroupBox {"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #b99b63, stop:0.48 #c9af77, stop:1 #9e7c4c);"
        "  border:2px solid #4f2f17;"
        "  border-radius:7px;"
        "  margin-top:18px;"
        "  padding:10px;"
        "}"
        "QGroupBox::title {"
        "  background:#c8ad78;"
        "  color:#3c210e;"
        "  font-weight:700;"
        "  subcontrol-origin:margin;"
        "  left:14px;"
        "  padding:0 6px;"
        "}"
        "QLabel#titleLabel { color:#3a210e; font-size:25px; font-weight:800; }"
        "QLabel#smallLabel { color:#3f2c18; background:transparent; }"
        "QTextEdit {"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #d5bd86, stop:0.55 #e0cd9b, stop:1 #c3a46c);"
        "  color:#2c1b0d;"
        "  border:2px solid #5a371d;"
        "  border-radius:7px;"
        "  padding:8px;"
        "  selection-background-color:#9a6a2f;"
        "}"
        "QPushButton {"
        "  background:#7b4a25;"
        "  color:#f6e4b7;"
        "  border:2px solid #4a2a13;"
        "  border-radius:6px;"
        "  padding:8px;"
        "  font-weight:700;"
        "}"
        "QPushButton:hover { background:#a36e36; border-color:#2d180a; }"
        "QPushButton:pressed { background:#6b3f1e; }"
        "QPushButton:disabled { color:#8b7655; background:#d2bc8c; border-color:#8d6840; }"
        "QPushButton:checked { background:#f0c767; color:#2a1607; border-color:#fff0a6; }"
        "QPushButton#boardCell {"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #c8ad78, stop:0.55 #d3bd88, stop:1 #b6975f);"
        "  color:#2b1a0b;"
        "  border:2px solid #6b4526;"
        "  border-radius:6px;"
        "  padding:8px;"
        "  font-size:13px;"
        "  font-weight:700;"
        "}"
        "QPushButton#boardCell:disabled {"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #c8ad78, stop:0.55 #d3bd88, stop:1 #b6975f);"
        "  color:#2b1a0b;"
        "  border:2px solid #6b4526;"
        "}"
        "QPushButton#skillCard {"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #c7ad78, stop:0.55 #dcc48c, stop:1 #aa854f);"
        "  color:#241609;"
        "  border:2px solid #6b4526;"
        "  border-radius:6px;"
        "  font-size:13px;"
        "  line-height:95%;"
        "  padding:7px;"
        "}"
        "QPushButton#skillCard:checked { background:#f3cf70; border:3px solid #fff1a8; color:#1f1307; }"
        "QPushButton#skillCard:disabled { background:#b89a64; color:#6b5738; border-color:#6e4c2d; }"
        "QPushButton#relicCard {"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #b8955f, stop:0.55 #d1b77e, stop:1 #8f653b);"
        "  color:#2b1a0b;"
        "  border:2px solid #d7a12f;"
        "  border-radius:6px;"
        "  padding:8px;"
        "  font-size:13px;"
        "}"
        "QPushButton#relicCard:disabled { background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #b8955f, stop:0.55 #d1b77e, stop:1 #8f653b); color:#2b1a0b; border-color:#d7a12f; }"
        "QDialog, QMessageBox, QInputDialog { background:#e4cf9c; color:#2c1b0d; }"
        "QLineEdit { background:#f4e6c2; color:#2c1b0d; border:2px solid #7c4e28; border-radius:5px; padding:6px; }"
        "QLineEdit:selected { background:#8a5428; color:#f8e7b9; }"
        "QComboBox {"
        "  background:#c7a66b;"
        "  color:#24170b;"
        "  border:2px solid #6b3f1e;"
        "  border-radius:5px;"
        "  padding:5px 28px 5px 7px;"
        "  selection-background-color:#8a5428;"
        "  selection-color:#f8e7b9;"
        "}"
        "QComboBox:hover { border-color:#d7a12f; }"
        "QComboBox::drop-down {"
        "  subcontrol-origin:padding;"
        "  subcontrol-position:top right;"
        "  width:24px;"
        "  border-left:1px solid #6b3f1e;"
        "  background:#8a5428;"
        "}"
        "QComboBox::down-arrow { image:none; border:0; width:0; height:0; }"
        "QComboBox QAbstractItemView {"
        "  background:#d2b77b;"
        "  color:#24170b;"
        "  border:2px solid #6b3f1e;"
        "  outline:0;"
        "  selection-background-color:#8a5428;"
        "  selection-color:#f8e7b9;"
        "}"
        "QAbstractItemView {"
        "  background:#d2b77b;"
        "  color:#24170b;"
        "  border:2px solid #6b3f1e;"
        "  selection-background-color:#8a5428;"
        "  selection-color:#f8e7b9;"
        "  outline:0;"
        "}"
        "QCheckBox { background:#efe0bb; color:#2c1b0d; border:1px solid #9a7448; border-radius:5px; padding:8px; }"
        "QCheckBox:disabled { color:#8a7658; background:#d8c294; }"
        "QCheckBox::indicator { width:16px; height:16px; }"
        "QScrollBar:vertical { background:#a7834f; width:12px; margin:0; }"
        "QScrollBar::handle:vertical { background:#6c4020; border-radius:5px; min-height:22px; }").arg(textureRule));
}

QWidget* MainWindow::buildBoard()
{
    QGroupBox* box = new QGroupBox(QStringLiteral("棋盘"));
    QGridLayout* grid = new QGridLayout(box);
    grid->setContentsMargins(18, 26, 18, 26);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(12);

    QStringList labels = { QStringLiteral("敌后"), QStringLiteral("敌中"), QStringLiteral("敌前"),
                           QStringLiteral("己前"), QStringLiteral("己中"), QStringLiteral("己后") };
    int rows[] = {0, 1, 2, 4, 5, 6};
    for (int i = 0; i < labels.size(); ++i)
    {
        grid->addWidget(smallLabel(labels[i]), rows[i], 0);
    }
    QLabel* line = smallLabel(QStringLiteral("接战线"));
    line->setAlignment(Qt::AlignCenter);
    grid->addWidget(line, 3, 1, 1, 5);

    for (int i = 0; i < 10; ++i)
    {
        boardButtons[i] = makeCardButton("boardCell");
        boardButtons[i]->setEnabled(false);
    }

    grid->addWidget(boardButtons[4], 0, 3, Qt::AlignCenter);
    grid->addWidget(boardButtons[2], 1, 2, Qt::AlignCenter);
    grid->addWidget(boardButtons[3], 1, 4, Qt::AlignCenter);
    grid->addWidget(boardButtons[0], 2, 1, Qt::AlignCenter);
    grid->addWidget(boardButtons[1], 2, 5, Qt::AlignCenter);
    grid->addWidget(boardButtons[5], 4, 1, Qt::AlignCenter);
    grid->addWidget(boardButtons[6], 4, 5, Qt::AlignCenter);
    grid->addWidget(boardButtons[7], 5, 2, Qt::AlignCenter);
    grid->addWidget(boardButtons[8], 5, 4, Qt::AlignCenter);
    grid->addWidget(boardButtons[9], 6, 3, Qt::AlignCenter);

    return box;
}

QWidget* MainWindow::buildBottomBar()
{
    QWidget* panel = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* skillBox = new QGroupBox(QStringLiteral("技能槽：每回合最多选择2张"));
    QHBoxLayout* skills = new QHBoxLayout(skillBox);
    skills->setSpacing(8);
    for (int i = 0; i < kMaxSkills; ++i)
    {
        skillButtons[i] = makeCardButton("skillCard");
        skillButtons[i]->setCheckable(true);
        /// [Recoleta37] 限制可勾选数 = 本回合剩余释放次数（上限2，已用N次则最多再勾选 2-N 张）
        connect(skillButtons[i], &QPushButton::clicked, this, [this, i]() {
            int checkedCount = 0;
            for (int j = 0; j < kMaxSkills; ++j)
                if (skillButtons[j]->isChecked()) ++checkedCount;
            const int remaining = 2 - battleEngine.skillsUsedThisTurn();
            if (checkedCount > remaining)
                skillButtons[i]->setChecked(false);
        });
        skills->addWidget(skillButtons[i]);
    }
    layout->addWidget(skillBox, 3);

    QVBoxLayout* actions = new QVBoxLayout;
    skillCastButton = new QPushButton(QStringLiteral("释放选择技能"));
    roundButton = new QPushButton(QStringLiteral("结算一回合"));
    nextButton = new QPushButton(QStringLiteral("进入下一关"));
    actions->addWidget(skillCastButton);
    actions->addWidget(roundButton);
    actions->addWidget(nextButton);
    layout->addLayout(actions, 1);

    connect(skillCastButton, &QPushButton::clicked, this, [this]() { castSelectedSkills(); });
    connect(roundButton, &QPushButton::clicked, this, [this]() { battleEngine.runRound(chapterIndex, levelIndex); });
    connect(nextButton, &QPushButton::clicked, this, [this]() { advanceLevel(); });
    return panel;
}

QWidget* MainWindow::buildRightPanel()
{
    QGroupBox* box = new QGroupBox(QStringLiteral("遗物槽"));
    QVBoxLayout* layout = new QVBoxLayout(box);
    for (int i = 0; i < kMaxRelics; ++i)
    {
        relicButtons[i] = makeCardButton("relicCard");
        relicButtons[i]->setEnabled(false);
        layout->addWidget(relicButtons[i]);
    }
    return box;
}

/// [Recoleta37] Phase 4: overlay 几何更新拆分给 StoryManager 和 MapManager
void MainWindow::updateOverlayGeometry()
{
    const QRect fullRect(0, 0, width(), height());
    storyManager.updateOverlayGeometry(fullRect);
    mapManager.updateOverlayGeometry(fullRect);
}

void MainWindow::initData()
{
    battleEngine.rosterRef() = {
        {QStringLiteral("阿拉贡"), QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("鼓舞"), 70, 10},
        {QStringLiteral("见习剑士"), QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("斩击"), 42, 7},
        {QStringLiteral("弓箭手"), QStringLiteral("人族"), QStringLiteral("后排"), QStringLiteral("箭雨"), 28, 11},
        {QStringLiteral("牧师"), QStringLiteral("人族"), QStringLiteral("中排"), QStringLiteral("治疗术"), 32, 5},
        {QStringLiteral("盾卫"), QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("守护"), 58, 4},
        {QStringLiteral("血仆"), QStringLiteral("吸血鬼"), QStringLiteral("前排"), QStringLiteral("吸血"), 40, 8},
        {QStringLiteral("血术师"), QStringLiteral("吸血鬼"), QStringLiteral("中排"), QStringLiteral("赤心爆发"), 34, 10},
        {QStringLiteral("夜行者"), QStringLiteral("吸血鬼"), QStringLiteral("后排"), QStringLiteral("吸血"), 30, 13},
        {QStringLiteral("血卫"), QStringLiteral("吸血鬼"), QStringLiteral("前排"), QStringLiteral("永生之血"), 52, 6},
        {QStringLiteral("精灵射手"), QStringLiteral("精灵族"), QStringLiteral("后排"), QStringLiteral("精灵箭"), 32, 12},
        {QStringLiteral("古木守卫"), QStringLiteral("精灵族"), QStringLiteral("前排"), QStringLiteral("古木再生"), 62, 5},
        {QStringLiteral("毒叶法师"), QStringLiteral("精灵族"), QStringLiteral("中排"), QStringLiteral("毒雾"), 35, 9},
        {QStringLiteral("迷途精灵"), QStringLiteral("精灵族"), QStringLiteral("中排"), QStringLiteral("藤蔓缠绕"), 38, 8},
        {QStringLiteral("圣光侍从"), QStringLiteral("天使"), QStringLiteral("中排"), QStringLiteral("圣光"), 38, 6},
        {QStringLiteral("守护天使"), QStringLiteral("天使"), QStringLiteral("前排"), QStringLiteral("六翼庇护"), 60, 6},
        {QStringLiteral("审判者"), QStringLiteral("天使"), QStringLiteral("后排"), QStringLiteral("审判"), 34, 14},
        {QStringLiteral("圣河少女"), QStringLiteral("天使"), QStringLiteral("中排"), QStringLiteral("圣河回响"), 36, 7},
        {QStringLiteral("小恶魔"), QStringLiteral("魔族"), QStringLiteral("后排"), QStringLiteral("火球"), 30, 15},
        {QStringLiteral("魔族战士"), QStringLiteral("魔族"), QStringLiteral("前排"), QStringLiteral("狂暴"), 54, 9},
        {QStringLiteral("火焰术士"), QStringLiteral("魔族"), QStringLiteral("中排"), QStringLiteral("魔焰"), 36, 13},
        {QStringLiteral("深渊刺客"), QStringLiteral("魔族"), QStringLiteral("后排"), QStringLiteral("深渊爪击"), 32, 16}
    };
    battleEngine.rosterRef() = battleEngine.rosterRef().mid(0, 4);
    battleEngine.relicsRef().clear();
    battleEngine.skillSlotsRef().clear();
}

void MainWindow::startGame()
{
    battleEngine.endingShownRef() = false;
    lastChapterTitleShown = -1;
    secondChapterAncientCityShown = false;
    mapManager.resetMapPoints();
    chapterIndex = 0;
    levelIndex = 1;
    battleEngine.goldRef() = 0;
    battleEngine.relicsRef().clear();
    battleEngine.skillSlotsRef().clear();
    battleEngine.clearAllUnits();
    storyManager.showStoryKey("prologue", [this]() { enterCurrentLevel(); });
}

void MainWindow::enterCurrentLevel()
{
    if (chapterIndex >= chapters().size())
    {
        battleEngine.showEnding();
        return;
    }
    if (levelIndex == 1 && lastChapterTitleShown != chapterIndex)
    {
        mapManager.showChapterTitle(chapterIndex, [this]() {
            lastChapterTitleShown = chapterIndex;
            mapManager.showMapPoint(MapManager::mapPointForChapter(chapterIndex), false, [this]() { enterCurrentLevel(); });
        });
        return;
    }
    if (chapterIndex == 2 && levelIndex == 10 && !secondChapterAncientCityShown)
    {
        secondChapterAncientCityShown = true;
        mapManager.showMapPoint(8, false, [this]() { enterCurrentLevel(); });
        return;
    }
    battleEngine.inBattleRef() = false;
    battleEngine.battleRoundRef() = 1;
    appendLog(QStringLiteral("进入 %1 第%2关").arg(chapters()[chapterIndex].title).arg(levelIndex));

    if (chapterIndex == 8)
    {
        battleEngine.setupWorldEvent(chapterIndex, levelIndex);
        return;
    }

    ChapterDef c = chapters()[chapterIndex];
    QString enemyName;
    bool boss = false;
    if (levelIndex == 5)
    {
        enemyName = c.boss5;
    }
    else if (levelIndex == 10)
    {
        enemyName = c.boss10;
    }
    boss = isUniqueBossName(enemyName);

    QStringList storyKeys;
    QString key = QString("c%1_l%2").arg(chapterIndex).arg(levelIndex);
    if (storyManager.hasScene(key))
    {
        storyKeys << key;
    }
    if (boss && storyManager.hasScene(QStringLiteral("boss_") + enemyName))
    {
        storyKeys << QStringLiteral("boss_") + enemyName;
    }

    if (!storyKeys.isEmpty())
    {
        storyManager.showStory(storyKeys, [this]() { battleEngine.setupBattle(chapterIndex, levelIndex); });
    }
    else
    {
        battleEngine.setupBattle(chapterIndex, levelIndex);
    }
}


void MainWindow::advanceLevel()
{
    if (battleEngine.inBattleRef())
    {
        return;
    }
    ++levelIndex;
    if (levelIndex > 10)
    {
        ++chapterIndex;
        levelIndex = 1;
    }
    if (chapterIndex >= chapters().size())
    {
        battleEngine.showEnding();
        return;
    }
    enterCurrentLevel();
}
/// [Recoleta37] 修复：使用成员变量 battleEngine.skillsUsedThisTurnRef() 跟踪本回合已释放次数，
/// 避免同一回合内多次点击按钮绕过"每回合最多2张"的限制。
void MainWindow::castSelectedSkills()
{
    for (int i = battleEngine.skillSlotsRef().size() - 1; i >= 0; --i)
    {
        if (skillButtons[i]->isChecked() && battleEngine.skillsUsedThisTurnRef() < 2)
        {
            battleEngine.castSkill(battleEngine.skillSlotsRef()[i].name, chapterIndex);
            battleEngine.skillSlotsRef().removeAt(i);
            ++battleEngine.skillsUsedThisTurnRef();
        }
    }
    refreshUi();
}

void MainWindow::refreshUi()
{
    updateOverlayGeometry();
    titleLabel->setText(chapterIndex < chapters().size() ? chapters()[chapterIndex].title : QStringLiteral("结局"));
    progressLabel->setText(QStringLiteral("章节 %1/9  关卡 %2/10  战斗回合 %3").arg(chapterIndex + 1).arg(levelIndex).arg(battleEngine.battleRoundRef()));
    if (UnitInstance* hero = battleEngine.playerUnitsRef()[kHeroSlot])
    {
        heroLabel->setText(QStringLiteral("主角：HP %1 / ATK %2 / 护盾 %3").arg(hero->hp).arg(hero->base.atk).arg(hero->shield));
    }
    else
    {
        heroLabel->setText(QStringLiteral("主角：等待部署"));
    }
    goldLabel->setText(QStringLiteral("金币：%1  队伍牌库：%2").arg(battleEngine.goldRef()).arg(battleEngine.rosterRef().size()));
    logView->setPlainText(battleEngine.logLines().join("\n"));
    logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    refreshBoard();
    refreshSkills();
    refreshRelics();
    roundButton->setEnabled(battleEngine.inBattleRef());
    /// [Recoleta37] 技能槽为空 或 本回合已用完2次 → 禁用释放按钮
    skillCastButton->setEnabled(battleEngine.inBattleRef() && !battleEngine.skillSlotsRef().isEmpty() && battleEngine.skillsUsedThisTurnRef() < 2);
    nextButton->setEnabled(!battleEngine.inBattleRef() && !storyManager.isOverlayVisible() && !mapManager.isChapterOverlayVisible());
}

void MainWindow::refreshBoard()
{
    for (int i = 0; i < 5; ++i)
    {
        UnitInstance* e = battleEngine.enemyUnitsRef()[i];
        UnitInstance* p = battleEngine.playerUnitsRef()[i];
        boardButtons[i]->setText(e ? QStringLiteral("%1\n%2\nHP %3  ATK %4\n护盾 %5")
                                         .arg(e->base.name, e->base.row)
                                         .arg(e->hp)
                                         .arg(e->base.atk)
                                         .arg(e->shield)
                                   : QStringLiteral("空牌位\n\n待部署"));
        boardButtons[i]->setStyleSheet(boardCardStyle(e, true));
        boardButtons[5 + i]->setText(p ? QStringLiteral("%1\n%2\nHP %3  ATK %4\n护盾 %5")
                                             .arg(p->base.name, p->base.row)
                                             .arg(p->hp)
                                             .arg(p->base.atk)
                                             .arg(p->shield)
                                       : QStringLiteral("空牌位\n\n待部署"));
        boardButtons[5 + i]->setStyleSheet(boardCardStyle(p, false));
    }
}

void MainWindow::refreshSkills()
{
    for (int i = 0; i < kMaxSkills; ++i)
    {
        skillButtons[i]->setChecked(false);
        if (i < battleEngine.skillSlotsRef().size())
        {
            const QString desc = battleEngine.skillDescription(battleEngine.skillSlotsRef()[i].name);
            skillButtons[i]->setText(QStringLiteral("%1\n来自：%2\n%3")
                                          .arg(battleEngine.skillSlotsRef()[i].name,
                                               battleEngine.skillSlotsRef()[i].source,
                                               wrapFixedLines(desc, 12, 3)));
            skillButtons[i]->setToolTip(desc);
            skillButtons[i]->setEnabled(true);
        }
        else
        {
            skillButtons[i]->setText(QStringLiteral("空技能槽\n\n等待生成"));
            skillButtons[i]->setToolTip(QString());
            skillButtons[i]->setEnabled(false);
        }
    }
}

void MainWindow::refreshRelics()
{
    for (int i = 0; i < kMaxRelics; ++i)
    {
        if (i < battleEngine.relicsRef().size())
        {
            relicButtons[i]->setText(QStringLiteral("%1\n%2").arg(battleEngine.relicsRef()[i], battleEngine.relicDescription(battleEngine.relicsRef()[i])));
        }
        else
        {
            relicButtons[i]->setText(QStringLiteral("遗物槽 %1\n空").arg(i + 1));
        }
    }
}

void MainWindow::appendLog(const QString& text)
{
    battleEngine.logLinesRef() << text;
    while (battleEngine.logLinesRef().size() > 80) battleEngine.logLinesRef().removeFirst();
}
