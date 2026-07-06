#include "MainWindow.h"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QEasingCurve>
#include <QEvent>
#include <QHash>
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
#include <QKeyEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QVBoxLayout>

static void animateBar(QProgressBar* bar, int newValue);

namespace
{

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
    qApp->installEventFilter(this);
    buildUi();
    battleEngine.setRefreshCallback([this]() { refreshUi(); });
    battleEngine.setRestartGameCallback([this]() { startGame(); });
    battleEngine.setRestartChapterCallback([this]() { restartFromChapterCheckpoint(); });
    initData();
    storyManager.loadStory();
    startGame();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        QWidget* active = QApplication::activeWindow();
        const bool mainWindowActive = active == this || (active != nullptr && isAncestorOf(active));
        if (keyEvent->key() == Qt::Key_Escape && mainWindowActive && handleEscapeKey())
        {
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonRelease)
    {
        for (int i = 0; i < kMaxSkills; ++i)
        {
            if (watched == skillCards[i].frame && skillCards[i].frame->isEnabled())
            {
                const int remaining = 2 - battleEngine.skillsUsedThisTurn();
                if (skillCards[i].checked)
                {
                    skillCards[i].checked = false;
                }
                else if (checkedSkillCount() < remaining)
                {
                    skillCards[i].checked = true;
                }
                refreshSkills();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && handleEscapeKey())
    {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool MainWindow::handleEscapeKey()
{
    if (!isFullScreen() && !isMaximized())
    {
        return false;
    }

    showNormal();
    resize(1500, 900);
    updateOverlayGeometry();
    appendLog(QStringLiteral("已退出最大化/全屏。"));
    return true;
}

int MainWindow::checkedSkillCount() const
{
    int count = 0;
    for (int i = 0; i < kMaxSkills; ++i)
        if (skillCards[i].checked) ++count;
    return count;
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
    // [Recoleta37] 主角状态：图标 + 进度条，替代纯文字
    QWidget* heroStatus = new QWidget;
    QVBoxLayout* heroStatusLayout = new QVBoxLayout(heroStatus);
    heroStatusLayout->setContentsMargins(0, 4, 0, 4);
    heroStatusLayout->setSpacing(4);

    // HP 行：❤️ + 红色血条
    QHBoxLayout* hpRow = new QHBoxLayout;
    QLabel* hpIcon = new QLabel(QStringLiteral("❤️"));
    hpIcon->setFixedWidth(24);
    heroHpBar = new QProgressBar;
    heroHpBar->setObjectName(QStringLiteral("hpBar"));
    heroHpBar->setTextVisible(true);
    heroHpBar->setFixedHeight(18);
    hpRow->addWidget(hpIcon);
    hpRow->addWidget(heroHpBar, 1);
    heroStatusLayout->addLayout(hpRow);

    // 护盾行：🛡️ + 浅蓝色护盾条（STS 风格）
    QHBoxLayout* shieldRow = new QHBoxLayout;
    QLabel* shieldIcon = new QLabel(QStringLiteral("🛡️"));
    shieldIcon->setFixedWidth(24);
    heroShieldBar = new QProgressBar;
    heroShieldBar->setObjectName(QStringLiteral("shieldBar"));
    heroShieldBar->setTextVisible(true);
    heroShieldBar->setFixedHeight(18);
    shieldRow->addWidget(shieldIcon);
    shieldRow->addWidget(heroShieldBar, 1);
    heroStatusLayout->addLayout(shieldRow);

    // ATK 行：⚔️ + 攻击力数字
    QHBoxLayout* atkRow = new QHBoxLayout;
    QLabel* atkIcon = new QLabel(QStringLiteral("⚔️"));
    atkIcon->setFixedWidth(24);
    heroAtkLabel = new QLabel;
    heroAtkLabel->setObjectName(QStringLiteral("smallLabel"));
    atkRow->addWidget(atkIcon);
    atkRow->addWidget(heroAtkLabel, 1);
    heroStatusLayout->addLayout(atkRow);

    // 金币行：💰 + 金币/牌库信息
    QHBoxLayout* goldRow = new QHBoxLayout;
    QLabel* goldIcon = new QLabel(QStringLiteral("💰"));
    goldIcon->setFixedWidth(24);
    heroGoldLabel = new QLabel;
    heroGoldLabel->setObjectName(QStringLiteral("smallLabel"));
    goldRow->addWidget(goldIcon);
    goldRow->addWidget(heroGoldLabel, 1);
    heroStatusLayout->addLayout(goldRow);

    logView = new QTextEdit;
    logView->setReadOnly(true);
    leftLayout->addWidget(titleLabel);
    leftLayout->addWidget(progressLabel);
    leftLayout->addWidget(heroStatus);
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
        // [Recoleta37] 技能牌改为 QFrame + QLabel，支持富文本
        "QFrame#skillCard {"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #c7ad78, stop:0.55 #dcc48c, stop:1 #aa854f);"
        "  border:2px solid #6b4526;"
        "  border-radius:6px;"
        "}"
        "QFrame#skillCard:disabled { background:#b89a64; border-color:#6e4c2d; }"
        "QLabel#skillSourceLabel { color:#5a3818; font-size:10px; background:transparent; }"
        "QLabel#skillNameLabel { color:#241609; font-size:16px; font-weight:800; background:transparent; }"
        "QLabel#skillDescLabel { color:#3d2a14; font-size:12px; background:transparent; }"
        // [Recoleta37] 遗物改为 QFrame + QLabel，支持自动换行
        "QFrame#relicCard {"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #b8955f, stop:0.55 #d1b77e, stop:1 #8f653b);"
        "  border:2px solid #d7a12f;"
        "  border-radius:6px;"
        "}"
        "QLabel#relicNameLabel { color:#2b1a0b; font-size:13px; font-weight:800; background:transparent; }"
        "QLabel#relicDescLabel { color:#3d2a14; font-size:13px; background:transparent; }"
        // [Recoleta37] 主角状态进度条：红色血条 + 浅蓝护盾条（STS 风格）
        "QProgressBar#hpBar {"
        "  background:#3d1a0a;"
        "  border:1px solid #6b3f1e;"
        "  border-radius:3px;"
        "  text-align:center;"
        "  color:#f0dbb0;"
        "  font-weight:bold;"
        "  font-size:11px;"
        "}"
        "QProgressBar#hpBar::chunk {"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #e74c3c, stop:1 #b71c1c);"
        "  border-radius:2px;"
        "}"
        "QProgressBar#shieldBar {"
        "  background:#1a2e3a;"
        "  border:1px solid #3d6070;"
        "  border-radius:3px;"
        "  text-align:center;"
        "  color:#c8e8f0;"
        "  font-weight:bold;"
        "  font-size:11px;"
        "}"
        "QProgressBar#shieldBar::chunk {"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #96daf0, stop:0.5 #7ec8e3, stop:1 #5aa8c4);"
        "  border-radius:2px;"
        "}"
        // [Recoleta37] 卡牌内部子控件样式（必须设 background:transparent 覆盖全局 QWidget 背景色）
        "QLabel#cardRowLabel { color:#5a3818; font-size:11px; font-weight:800; background:transparent; }"
        "QLabel#cardAtkLabel { color:#8b1a1a; font-size:12px; font-weight:800; background:transparent; }"
        "QLabel#cardNameLabel { color:#2b1a0b; font-size:13px; font-weight:800; background:transparent; }"
        "QProgressBar#cardHpBar {"
        "  background:#3d1a0a;"
        "  border:1px solid #5a371d;"
        "  border-radius:2px;"
        "  text-align:center;"
        "  color:#f0dbb0;"
        "  font-size:9px;"
        "  font-weight:bold;"
        "}"
        "QProgressBar#cardHpBar::chunk { background:#c0392b; border-radius:1px; }"
        "QLabel#cardShieldLabel { color:#4a90b8; font-size:10px; font-weight:800; background:transparent; }"
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

    // [Recoleta37] 卡牌改用 QFrame + 内部子控件（血条/护盾/ATK/FMB），替代纯文字 QPushButton
    // 10 张牌的 grid 位置：敌后(4)/敌中(2,3)/敌前(0,1) | 己前(5,6)/己中(7,8)/己后(9)
    const int placements[10][2] = {
        {2, 1}, {2, 5}, {1, 2}, {1, 4}, {0, 3},   // enemy: 0,1=前排 2,3=中排 4=后排
        {4, 1}, {4, 5}, {5, 2}, {5, 4}, {6, 3}    // player: 5,6=前排 7,8=中排 9=后排
    };
    for (int i = 0; i < 10; ++i)
    {
        BoardCard& card = boardCards[i];

        card.frame = new QFrame;
        card.frame->setObjectName(QStringLiteral("boardCard"));
        card.frame->setFixedSize(132, 108);
        card.frame->setCursor(Qt::PointingHandCursor);

        QVBoxLayout* cardLayout = new QVBoxLayout(card.frame);
        cardLayout->setContentsMargins(6, 4, 6, 4);
        cardLayout->setSpacing(1);

        // 顶行：F/M/B 排位（左）+ 攻击力（右）
        QHBoxLayout* topRow = new QHBoxLayout;
        card.rowLabel = new QLabel;
        card.rowLabel->setObjectName(QStringLiteral("cardRowLabel"));
        topRow->addWidget(card.rowLabel);
        topRow->addStretch();
        card.atkLabel = new QLabel;
        card.atkLabel->setObjectName(QStringLiteral("cardAtkLabel"));
        topRow->addWidget(card.atkLabel);
        cardLayout->addLayout(topRow);

        // 名字
        card.nameLabel = new QLabel;
        card.nameLabel->setObjectName(QStringLiteral("cardNameLabel"));
        card.nameLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(card.nameLabel);

        cardLayout->addStretch();

        // 底行：血条 + 护盾数字
        QHBoxLayout* bottomRow = new QHBoxLayout;
        bottomRow->setSpacing(2);
        card.hpBar = new QProgressBar;
        card.hpBar->setObjectName(QStringLiteral("cardHpBar"));
        card.hpBar->setTextVisible(true);
        card.hpBar->setFixedHeight(14);
        bottomRow->addWidget(card.hpBar, 1);
        card.shieldLabel = new QLabel;
        card.shieldLabel->setObjectName(QStringLiteral("cardShieldLabel"));
        card.shieldLabel->setVisible(false);
        bottomRow->addWidget(card.shieldLabel);
        cardLayout->addLayout(bottomRow);

        card.statusLabel = new QLabel;
        card.statusLabel->setObjectName(QStringLiteral("cardShieldLabel"));
        card.statusLabel->setAlignment(Qt::AlignCenter);
        card.statusLabel->setStyleSheet("color:#c0392b; font-size:10px; font-weight:700; background:transparent;");
        cardLayout->addWidget(card.statusLabel);

        grid->addWidget(card.frame, placements[i][0], placements[i][1], Qt::AlignCenter);
    }
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
        SkillCard& card = skillCards[i];
        card.frame = new QFrame;
        card.frame->setObjectName(QStringLiteral("skillCard"));
        card.frame->setFixedSize(178, 148);
        card.frame->setCursor(Qt::PointingHandCursor);

        QVBoxLayout* cardLayout = new QVBoxLayout(card.frame);
        cardLayout->setContentsMargins(8, 6, 8, 6);
        cardLayout->setSpacing(2);

        card.sourceLabel = new QLabel;
        card.sourceLabel->setObjectName(QStringLiteral("skillSourceLabel"));
        cardLayout->addWidget(card.sourceLabel);

        card.nameLabel = new QLabel;
        card.nameLabel->setObjectName(QStringLiteral("skillNameLabel"));
        card.nameLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(card.nameLabel);

        card.descLabel = new QLabel;
        card.descLabel->setObjectName(QStringLiteral("skillDescLabel"));
        card.descLabel->setWordWrap(true);
        card.descLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(card.descLabel, 1);

        card.echoLabel = new QLabel;
        card.echoLabel->setObjectName(QStringLiteral("skillDescLabel"));
        card.echoLabel->setAlignment(Qt::AlignCenter);
        card.echoLabel->setStyleSheet("color:#27ae60; font-size:11px; font-weight:700; background:transparent;");
        card.echoLabel->setVisible(false);
        cardLayout->addWidget(card.echoLabel);

        /// [Recoleta37] 点击切换勾选，上限 = 2 - 本回合已释放次数
        card.frame->installEventFilter(this);
        card.frame->setProperty("skillIndex", i);
        skills->addWidget(card.frame);
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
        RelicCard& card = relicCards[i];
        card.frame = new QFrame;
        card.frame->setObjectName(QStringLiteral("relicCard"));
        card.frame->setMinimumSize(150, 118);
        card.frame->setCursor(Qt::PointingHandCursor);
        card.frame->setEnabled(false);

        QVBoxLayout* cardLayout = new QVBoxLayout(card.frame);
        cardLayout->setContentsMargins(8, 6, 8, 6);
        cardLayout->setSpacing(2);

        card.nameLabel = new QLabel;
        card.nameLabel->setObjectName(QStringLiteral("relicNameLabel"));
        card.nameLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(card.nameLabel);

        card.descLabel = new QLabel;
        card.descLabel->setObjectName(QStringLiteral("relicDescLabel"));
        card.descLabel->setWordWrap(true);
        card.descLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(card.descLabel, 1);

        card.usesLabel = new QLabel;
        card.usesLabel->setObjectName(QStringLiteral("relicDescLabel"));
        card.usesLabel->setAlignment(Qt::AlignCenter);
        card.usesLabel->setStyleSheet("color:#6b4423; font-size:11px; font-weight:700; background:transparent;");
        cardLayout->addWidget(card.usesLabel);

        layout->addWidget(card.frame);
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
    battleEngine.resetRunState();
    initData();
    battleEngine.endingShownRef() = false;
    lastChapterTitleShown = -1;
    secondChapterAncientCityShown = false;
    chapterCheckpointValid = false;
    checkpointChapterIndex = 0;
    checkpointLastChapterTitleShown = -1;
    checkpointSecondChapterAncientCityShown = false;
    mapManager.resetMapPoints();
    chapterIndex = 0;
    levelIndex = 1;
    battleEngine.goldRef() = 20;
    battleEngine.relicsRef().clear();
    battleEngine.skillSlotsRef().clear();
    battleEngine.clearAllUnits();
    storyManager.showStoryKey("prologue", [this]() { enterCurrentLevel(); });
}

void MainWindow::restartFromChapterCheckpoint()
{
    if (!chapterCheckpointValid || !battleEngine.hasChapterSnapshot())
    {
        startGame();
        return;
    }

    chapterIndex = checkpointChapterIndex;
    levelIndex = 1;
    lastChapterTitleShown = checkpointLastChapterTitleShown;
    secondChapterAncientCityShown = checkpointSecondChapterAncientCityShown;
    battleEngine.restoreChapterSnapshot();
    appendLog(QStringLiteral("从本大章节开始挑战。"));
    enterCurrentLevel();
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
    if (levelIndex == 1)
    {
        battleEngine.saveChapterSnapshot();
        chapterCheckpointValid = true;
        checkpointChapterIndex = chapterIndex;
        checkpointLastChapterTitleShown = lastChapterTitleShown;
        checkpointSecondChapterAncientCityShown = secondChapterAncientCityShown;
        appendLog(QStringLiteral("已保存本大章节开始状态。"));
    }

    // 黄金祭坛：Boss 关卡开始前可花费金币购买遗物
    const bool isBossLevel = (levelIndex == 10) || (chapterIndex == 8 && levelIndex == 2);
    if (isBossLevel)
        battleEngine.showGoldAltar();

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
        if (skillCards[i].checked && battleEngine.skillsUsedThisTurnRef() < 2)
        {
            battleEngine.castSkill(battleEngine.skillSlotsRef()[i].name, chapterIndex);
            battleEngine.skillSlotsRef().removeAt(i);
            ++battleEngine.skillsUsedThisTurnRef();
        }
    }
    for (int i = 0; i < kMaxSkills; ++i) skillCards[i].checked = false;
    refreshUi();
}

void MainWindow::refreshUi()
{
    updateOverlayGeometry();
    titleLabel->setText(chapterIndex < chapters().size() ? chapters()[chapterIndex].title : QStringLiteral("结局"));
    progressLabel->setText(QStringLiteral("章节 %1/9  关卡 %2/10  战斗回合 %3").arg(chapterIndex + 1).arg(levelIndex).arg(battleEngine.battleRoundRef()));
    // [Recoleta37] 主角状态改为图标 + 进度条显示
    if (UnitInstance* hero = battleEngine.playerUnitsRef()[kHeroSlot])
    {
        const int maxHp = hero->base.hp;
        heroHpBar->setRange(0, maxHp);
        int displayHeroHp = hero->hp < 0 ? 0 : (hero->hp > maxHp ? maxHp : hero->hp);
        animateBar(heroHpBar, displayHeroHp);
        heroHpBar->setFormat(QStringLiteral("%1 / %2").arg(displayHeroHp).arg(maxHp));
        heroAtkLabel->setText(QStringLiteral("<span style='font-size:18px; font-weight:800;'>%1</span>").arg(hero->base.atk));
        heroShieldBar->setRange(0, maxHp);
        int displayHeroShield = hero->shield < 0 ? 0 : (hero->shield > maxHp ? maxHp : hero->shield);
        animateBar(heroShieldBar, displayHeroShield);
        heroShieldBar->setFormat(QStringLiteral("%1").arg(displayHeroShield));
    }
    else
    {
        heroHpBar->setRange(0, 1);
        heroHpBar->setValue(0);
        heroHpBar->setFormat(QStringLiteral("等待部署"));
        heroAtkLabel->setText(QStringLiteral("等待部署"));
        heroShieldBar->setRange(0, 1);
        heroShieldBar->setValue(0);
        heroShieldBar->setFormat(QStringLiteral("-"));
    }
    heroGoldLabel->setText(QStringLiteral("金币  %1    队伍牌库  %2").arg(battleEngine.goldRef()).arg(battleEngine.rosterRef().size()));
    logView->setPlainText(battleEngine.logLines().join("\n"));
    logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    refreshBoard();
    refreshSkills();
    refreshRelics();
    roundButton->setEnabled(battleEngine.inBattleRef());
    /// [Recoleta37] 技能槽为空 或 本回合已用完2次 → 禁用释放按钮
    skillCastButton->setEnabled(battleEngine.inBattleRef() && !battleEngine.skillSlotsRef().isEmpty() && battleEngine.skillsUsedThisTurnRef() < 2);
    nextButton->setEnabled(!battleEngine.inBattleRef() && !storyManager.isOverlayVisible() && !mapManager.isChapterOverlayVisible());

    animateCombatEvents();
}

/// [Recoleta37] 返回 QFrame 卡牌样式，复用原有 boardCardStyle 的配色逻辑
static QString cardFrameStyle(UnitInstance* unit, bool enemySide)
{
    const QString baseText = QStringLiteral("#211509");
    if (unit == nullptr)
    {
        return QStringLiteral(
            "QFrame#boardCard {"
            " background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #c8ad78, stop:0.55 #d3bd88, stop:1 #b6975f);"
            " border:2px dashed #8d6840;"
            " border-radius:6px;"
            "}");
    }

    QString background = enemySide
        ? QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #b89463, stop:0.55 #d0b27a, stop:1 #9d7345)")
        : QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #c5aa72, stop:0.55 #dbc28b, stop:1 #b18a55)");
    QString border = enemySide ? QStringLiteral("#9b6930") : QStringLiteral("#d7a12f");

    if (unit->hero)
    {
        background = QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #73618a, stop:0.5 #b59cc5, stop:1 #5a416f)");
        border = QStringLiteral("#f4d36b");
    }
    else if (unit->boss)
    {
        background = QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #7a3b2a, stop:0.48 #b47a45, stop:1 #4b2118)");
        border = QStringLiteral("#f0c94f");
    }

    return QStringLiteral(
        "QFrame#boardCard {"
        " background:%1;"
        " border:2px solid %2;"
        " border-radius:6px;"
        "}").arg(background, border);
}

void MainWindow::refreshBoard()
{
    // [Recoleta37] 更新卡牌子控件：排位、ATK、名字、血条、护盾
    auto populateCard = [](BoardCard& card, UnitInstance* u, bool enemySide) {
        if (u)
        {
            card.rowLabel->setVisible(true);
            card.atkLabel->setVisible(true);
            card.nameLabel->setVisible(true);
            card.hpBar->setVisible(true);
            // 排位缩写：前排→F  中排→M  后排→B
            QString rowAbbr;
            if (u->base.row == QStringLiteral("前排"))      rowAbbr = QStringLiteral("F");
            else if (u->base.row == QStringLiteral("中排")) rowAbbr = QStringLiteral("M");
            else                                            rowAbbr = QStringLiteral("B");
            card.rowLabel->setText(rowAbbr);
            card.atkLabel->setText(QStringLiteral("⚔️%1").arg(u->base.atk));
            card.nameLabel->setText(u->base.name);
            const int maxHp = u->base.hp;
            card.hpBar->setRange(0, maxHp);
            int displayHp = u->hp < 0 ? 0 : (u->hp > maxHp ? maxHp : u->hp);
            animateBar(card.hpBar, displayHp);
            card.hpBar->setFormat(QStringLiteral("%1 / %2").arg(displayHp).arg(maxHp));
            if (u->shield > 0)
            {
                card.shieldLabel->setText(QStringLiteral("🛡️%1").arg(u->shield));
                card.shieldLabel->setVisible(true);
                // STS 风格：有护盾时血条加蓝框
                card.hpBar->setStyleSheet(
                    QStringLiteral("QProgressBar#cardHpBar { border:2px solid #5aa8c4; }"
                                   "QProgressBar#cardHpBar::chunk { background:#c0392b; border-radius:1px; }"));
            }
            else
            {
                card.shieldLabel->setVisible(false);
                card.hpBar->setStyleSheet(
                    QStringLiteral("QProgressBar#cardHpBar { border:1px solid #5a371d; }"
                                   "QProgressBar#cardHpBar::chunk { background:#c0392b; border-radius:1px; }"));
            }

            // 状态显示：正面绿色在上，负面红色在下
            QStringList posTexts, negTexts;
            for (const StatusEffect& s : u->statuses)
            {
                const QString text = QStringLiteral("%1%2").arg(s.name).arg(s.layers);
                if (s.name == QStringLiteral("荆棘") || s.name == QStringLiteral("回响") || s.name == QStringLiteral("铸剑"))
                    posTexts << text;
                else
                    negTexts << text;
            }
            QString html;
            if (!posTexts.isEmpty())
                html += QStringLiteral("<span style='color:#27ae60;'>%1</span>").arg(posTexts.join(QStringLiteral(" ")));
            if (!negTexts.isEmpty())
            {
                if (!html.isEmpty()) html += QStringLiteral("<br>");
                html += QStringLiteral("<span style='color:#c0392b;'>%1</span>").arg(negTexts.join(QStringLiteral(" ")));
            }
            card.statusLabel->setText(html);
            card.statusLabel->setVisible(!html.isEmpty());
        }
        else
        {
            card.rowLabel->setVisible(false);
            card.atkLabel->setVisible(false);
            card.nameLabel->setText(QStringLiteral("待部署"));
            card.hpBar->setVisible(false);
            card.shieldLabel->setVisible(false);
            card.statusLabel->setVisible(false);
        }
        card.frame->setStyleSheet(cardFrameStyle(u, enemySide));
    };

    for (int i = 0; i < 5; ++i)
    {
        populateCard(boardCards[i],     battleEngine.enemyUnitsRef()[i], true);
        populateCard(boardCards[5 + i], battleEngine.playerUnitsRef()[i], false);
    }
}

void MainWindow::refreshSkills()
{
    for (int i = 0; i < kMaxSkills; ++i)
    {
        if (i < battleEngine.skillSlotsRef().size())
        {
            const QString& src = battleEngine.skillSlotsRef()[i].source;
            const QString& name = battleEngine.skillSlotsRef()[i].name;
            const QString desc = battleEngine.skillDescription(name);
            skillCards[i].sourceLabel->setText(src);
            skillCards[i].nameLabel->setText(name);
            skillCards[i].descLabel->setText(desc);
            skillCards[i].frame->setToolTip(desc);
            skillCards[i].frame->setEnabled(true);

            // 回响：查找来源棋子的回响层数
            int echoLayers = 0;
            for (UnitInstance* u : battleEngine.playerUnits())
            {
                if (u && u->base.name == src)
                {
                    echoLayers = battleEngine.statusLayers(u, QStringLiteral("回响"));
                    break;
                }
            }
            if (echoLayers > 0)
            {
                skillCards[i].echoLabel->setText(QStringLiteral("回响×%1").arg(echoLayers));
                skillCards[i].echoLabel->setVisible(true);
            }
            else
            {
                skillCards[i].echoLabel->setVisible(false);
            }
        }
        else
        {
            skillCards[i].sourceLabel->setText(QString());
            skillCards[i].nameLabel->setText(QStringLiteral("空技能槽"));
            skillCards[i].descLabel->setText(QString());
            skillCards[i].frame->setToolTip(QString());
            skillCards[i].frame->setEnabled(false);
            skillCards[i].echoLabel->setVisible(false);
        }
        // 更新选中/未选中样式
        if (skillCards[i].checked)
        {
            // 铸剑选中用橙色基调
            if (i < battleEngine.skillSlotsRef().size()
                && battleEngine.skillSlotsRef()[i].name == QStringLiteral("铸剑"))
            {
                skillCards[i].frame->setStyleSheet(
                    QStringLiteral("QFrame#skillCard { background:#f0a050; border:3px solid #f4b870; border-radius:6px; }"));
            }
            else
            {
                skillCards[i].frame->setStyleSheet(
                    QStringLiteral("QFrame#skillCard { background:#f3cf70; border:3px solid #fff1a8; border-radius:6px; }"));
            }
            skillCards[i].frame->raise();
        }
        else if (i < battleEngine.skillSlotsRef().size()
                 && battleEngine.skillSlotsRef()[i].name == QStringLiteral("铸剑"))
        {
            // 特殊技能牌：橙色底色
            skillCards[i].frame->setStyleSheet(
                QStringLiteral("QFrame#skillCard { background:#e8934b; border:2px solid #f4a462; border-radius:6px; }"));
        }
        else
        {
            skillCards[i].frame->setStyleSheet(QString());
        }
    }
}

void MainWindow::refreshRelics()
{
    // 先过滤出占槽遗物（非 slotless 且 uses!=0）
    QVector<int> visibleIndexes;
    for (int i = 0; i < battleEngine.relicsRef().size(); ++i)
    {
        const RelicInstance& r = battleEngine.relicsRef()[i];
        if (!r.slotless && r.uses != 0)
            visibleIndexes.push_back(i);
    }

    for (int i = 0; i < kMaxRelics; ++i)
    {
        if (i < visibleIndexes.size())
        {
            const RelicInstance& r = battleEngine.relicsRef()[visibleIndexes[i]];
            relicCards[i].nameLabel->setText(r.name);
            relicCards[i].descLabel->setText(battleEngine.relicDescription(r.name));
            // 次数标签
            if (r.uses == -1)
                relicCards[i].usesLabel->setText(QStringLiteral("∞"));
            else if (r.uses > 0)
                relicCards[i].usesLabel->setText(QStringLiteral("×%1").arg(r.uses));
            else
                relicCards[i].usesLabel->setText(QString());
        }
        else
        {
            relicCards[i].nameLabel->setText(QStringLiteral("遗物槽 %1").arg(i + 1));
            relicCards[i].descLabel->setText(QStringLiteral("空"));
            relicCards[i].usesLabel->setText(QString());
        }
    }
}

void MainWindow::appendLog(const QString& text)
{
    battleEngine.logLinesRef() << text;
    while (battleEngine.logLinesRef().size() > 80) battleEngine.logLinesRef().removeFirst();
}

// ============================================================
// 浮动伤害数字动画
// ============================================================

/// 手动插值血条动画（60fps），避免 QPropertyAnimation 在嵌套事件循环中的竞态
static void animateBar(QProgressBar* bar, int newValue)
{
    if (!bar) return;

    struct State { int start; int target; qint64 t0; int duration; };
    static QHash<QProgressBar*, State> states;
    static QTimer* timer = nullptr;

    State& s = states[bar];
    s.start = bar->value();       // 从当前显示值出发，保证视觉连续
    s.target = newValue;
    s.t0 = QDateTime::currentMSecsSinceEpoch();
    s.duration = std::min(300, std::abs(newValue - s.start) * 50 + 50);

    if (!timer)
    {
        timer = new QTimer(qApp);
        timer->setInterval(16); // ~60fps
        QObject::connect(timer, &QTimer::timeout, [] {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            for (auto it = states.begin(); it != states.end(); )
            {
                const State& st = it.value();
                double t = std::min(1.0, (now - st.t0) / double(st.duration));
                double eased = 1.0 - std::pow(1.0 - t, 3.0); // OutCubic
                st.start != st.target
                    ? it.key()->setValue(int(st.start + (st.target - st.start) * eased))
                    : it.key()->setValue(st.target);
                if (t >= 1.0)
                    it = states.erase(it);
                else
                    ++it;
            }
            if (states.isEmpty()) timer->stop();
        });
    }
    if (!timer->isActive()) timer->start();
}

void MainWindow::flashCard(int boardCardIndex)
{
    if (boardCardIndex < 0 || boardCardIndex >= 10) return;
    QFrame* card = boardCards[boardCardIndex].frame;
    if (!card) return;

    QString origStyle = card->styleSheet();
    // 白色闪烁，对比度高；快速闪两次
    auto doFlash = [card](const QString& base, int count) {
        if (count <= 0) return;
        card->setStyleSheet(base + QStringLiteral(
            " QFrame#boardCard { border:3px solid #ffffff; }"));
        QTimer::singleShot(100, card->parentWidget(), [card, base, count]() {
            card->setStyleSheet(base);  // 还原
            QTimer::singleShot(50, card->parentWidget(), [card, base, count]() {
                card->setStyleSheet(base + QStringLiteral(
                    " QFrame#boardCard { border:3px solid #ffffff; }"));
                QTimer::singleShot(100, card->parentWidget(), [card, base, count]() {
                    card->setStyleSheet(base);
                });
            });
        });
    };
    doFlash(origStyle, 2);
}

void MainWindow::animateCombatEvents()
{
    if (animating_) return;

    const auto& events = battleEngine.pendingCombatEvents();
    if (events.isEmpty()) return;

    QVector<CombatEvent> queue;
    for (const auto& e : events) queue.append(e);
    battleEngine.clearCombatEvents();

    animating_ = true;
    roundButton->setEnabled(false);
    skillCastButton->setEnabled(false);
    nextButton->setEnabled(false);

    int cardStackOffset[10] = {};
    int lastBatch = -1;

    for (int i = 0; i < queue.size(); )
    {
        const CombatEvent& first = queue[i];

        if (first.batchId != lastBatch)
        {
            for (int j = 0; j < 10; ++j) cardStackOffset[j] = 0;
            lastBatch = first.batchId;
        }

        if (first.type == CombatEvent::Damage && first.sourceIndex >= 0)
            flashCard(first.sourceIndex);

        if (first.type == CombatEvent::Flash)
        {
            flashCard(first.targetIndex);
            ++i;
            QApplication::processEvents();
            QEventLoop loop;
            QTimer::singleShot(250, &loop, &QEventLoop::quit);
            loop.exec();
            continue;
        }

        int groupEnd = i;
        while (groupEnd < queue.size() &&
               queue[groupEnd].batchId == first.batchId &&
               queue[groupEnd].sourceIndex == first.sourceIndex)
        {
            const auto& e = queue[groupEnd];
            if (e.type == CombatEvent::Flash) break;
            int idx = e.targetIndex;
            if (idx >= 0 && idx < 10)
            {
                spawnDamageLabel(e, idx, cardStackOffset[idx]);
                cardStackOffset[idx] += 28;
            }
            ++groupEnd;
        }

        i = groupEnd;

        QApplication::processEvents();

        int delay = (i < queue.size() && queue[i].batchId != first.batchId) ? 700 : 400;
        QEventLoop waitLoop;
        QTimer::singleShot(delay, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();
    }

    animating_ = false;
    roundButton->setEnabled(battleEngine.inBattleRef());
    skillCastButton->setEnabled(battleEngine.inBattleRef() && !battleEngine.skillSlotsRef().isEmpty() && battleEngine.skillsUsedThisTurnRef() < 2);
    nextButton->setEnabled(!battleEngine.inBattleRef() && !storyManager.isOverlayVisible() && !mapManager.isChapterOverlayVisible());
}

void MainWindow::spawnDamageLabel(const CombatEvent& event, int boardCardIndex, int yOffset)
{
    QFrame* card = boardCards[boardCardIndex].frame;
    if (!card) return;

    // 定位：卡牌顶部居中
    QPoint cardTopLeft = card->mapTo(this, QPoint(0, 0));
    QSize cardSize = card->size();

    // 创建标签
    QLabel* label = new QLabel(this);
    QString sign, color;
    if (event.type == CombatEvent::Damage)      { sign = QStringLiteral("-"); color = QStringLiteral("#ff4444"); }
    else if (event.type == CombatEvent::Heal)    { sign = QStringLiteral("+"); color = QStringLiteral("#44dd44"); }
    else                                         { sign = QStringLiteral("+"); color = QStringLiteral("#44aaff"); }
    label->setText(QStringLiteral("%1%2").arg(sign).arg(event.amount));
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->setAttribute(Qt::WA_ShowWithoutActivating);
    label->setStyleSheet(QStringLiteral(
        "font-size:22px; font-weight:800; color:%1; background:transparent;").arg(color));
    label->adjustSize();

    int labelX = cardTopLeft.x() + (cardSize.width() - label->width()) / 2;
    int labelY = cardTopLeft.y() - 6 - yOffset;
    label->move(labelX, labelY);
    label->raise();
    label->show();

    // 透明度效果
    QGraphicsOpacityEffect* opacity = new QGraphicsOpacityEffect(label);
    opacity->setOpacity(1.0);
    label->setGraphicsEffect(opacity);

    // 上飘动画
    QPropertyAnimation* posAnim = new QPropertyAnimation(label, "pos", this);
    posAnim->setDuration(800);
    posAnim->setStartValue(label->pos());
    posAnim->setEndValue(label->pos() + QPoint(0, -40));
    posAnim->setEasingCurve(QEasingCurve::OutQuad);

    // 淡出动画
    QPropertyAnimation* fadeAnim = new QPropertyAnimation(opacity, "opacity", this);
    fadeAnim->setDuration(800);
    fadeAnim->setStartValue(1.0);
    fadeAnim->setEndValue(0.0);
    fadeAnim->setEasingCurve(QEasingCurve::InQuad);

    // 动画结束后清理
    connect(fadeAnim, &QPropertyAnimation::finished, label, &QLabel::deleteLater);
    connect(fadeAnim, &QPropertyAnimation::finished, opacity, &QObject::deleteLater);

    posAnim->start(QAbstractAnimation::DeleteWhenStopped);
    fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
}
