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
const int kHeroSlot = 2;
const int kMaxSkills = 5;
const int kMaxRelics = 5;

bool isUniqueBossName(const QString& name)
{
    static const QStringList bosses = {
        QStringLiteral("魔王？？？"),
        QStringLiteral("阿拉贡"),
        QStringLiteral("偷窃者米格"),
        QStringLiteral("艾琳"),
        QStringLiteral("阿格尼"),
        QStringLiteral("米凯尔"),
        QStringLiteral("伊维尔"),
        QStringLiteral("莱索恩")
    };
    return bosses.contains(name);
}

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

MainWindow::MainWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("WORLD Alpha 0.9 PVE 极限测试版"));
    setMinimumSize(1320, 820);
    resize(1500, 900);
    buildUi();
    initData();
    loadStory();
    startGame();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == mapOverlay && event->type() == QEvent::MouseButtonPress)
    {
        if (mapReady)
        {
            mapOverlay->hide();
            auto callback = mapFinishedCallback;
            mapFinishedCallback = {};
            if (callback) callback();
        }
        return true;
    }
    if (watched == chapterOverlay && event->type() == QEvent::MouseButtonPress)
    {
        if (chapterTitleReady)
        {
            chapterOverlay->hide();
            auto callback = chapterTitleFinishedCallback;
            chapterTitleFinishedCallback = {};
            if (callback) callback();
        }
        return true;
    }
    if (watched == storyOverlay && event->type() == QEvent::MouseButtonPress)
    {
        nextStoryStep();
        return true;
    }
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

    buildStoryOverlay();
    buildChapterOverlay();
    buildMapOverlay();

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
    connect(roundButton, &QPushButton::clicked, this, [this]() { runRound(); });
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

void MainWindow::buildStoryOverlay()
{
    storyOverlay = new QWidget(this);
    storyOverlay->setStyleSheet("background:white;");
    storyOverlay->hide();
    storyOverlay->installEventFilter(this);

    QVBoxLayout* layout = new QVBoxLayout(storyOverlay);
    layout->addStretch();
    QWidget* textBox = new QWidget;
    textBox->setStyleSheet("background:#050505; border-radius:4px;");
    QVBoxLayout* textLayout = new QVBoxLayout(textBox);
    storySpeaker = new QLabel;
    storySpeaker->setStyleSheet("background:#050505; color:#e7c879; font-weight:700; font-size:18px;");
    storyText = new QLabel;
    storyText->setWordWrap(true);
    storyText->setStyleSheet("background:#050505; color:white; font-size:22px;");
    textLayout->addWidget(storySpeaker);
    textLayout->addWidget(storyText);
    layout->addWidget(textBox);

    connect(&storyTimer, &QTimer::timeout, this, [this]() { tickStory(); });
}

void MainWindow::buildChapterOverlay()
{
    chapterOverlay = new QWidget(this);
    chapterOverlay->setStyleSheet("background:white;");
    chapterOverlay->hide();
    chapterOverlay->installEventFilter(this);

    QVBoxLayout* layout = new QVBoxLayout(chapterOverlay);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();

    QWidget* titleArea = new QWidget;
    titleArea->setStyleSheet("background:white;");
    titleArea->setFixedHeight(190);
    QVBoxLayout* titleLayout = new QVBoxLayout(titleArea);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    chapterChineseLabel = new QLabel;
    chapterChineseLabel->setAlignment(Qt::AlignCenter);
    chapterChineseLabel->setStyleSheet("background:white; color:#111111; font-size:54px; font-weight:700; letter-spacing:0px;");

    chapterEnglishLabel = new QLabel;
    chapterEnglishLabel->setAlignment(Qt::AlignCenter);
    chapterEnglishLabel->setStyleSheet("background:white; color:#222222; font-family:'Segoe Script','Brush Script MT','Georgia'; font-size:30px; font-style:italic;");

    titleLayout->addWidget(chapterChineseLabel);
    titleLayout->addWidget(chapterEnglishLabel);
    layout->addWidget(titleArea);
    layout->addStretch();

    connect(&chapterTitleTimer, &QTimer::timeout, this, [this]() {
        chapterTitleReady = true;
        chapterTitleTimer.stop();
    });
}

void MainWindow::buildMapOverlay()
{
    mapOverlay = new QWidget(this);
    mapOverlay->setStyleSheet("background:#f4ead0;");
    mapOverlay->hide();
    mapOverlay->installEventFilter(this);

    mapImageLabel = new QLabel(mapOverlay);
    mapImageLabel->setAlignment(Qt::AlignCenter);
    mapImageLabel->setScaledContents(true);
    mapImageLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    mapImageLabel->setStyleSheet("background:#f4ead0;");

    const QStringList candidates = {
        QApplication::applicationDirPath() + "/assets/maps/world_map_fantasy_wide_ui_v1.png",
        QApplication::applicationDirPath() + "/assets/maps/world_map_from_distribution.png",
        QApplication::applicationDirPath() + "/../assets/maps/world_map_fantasy_wide_ui_v1.png",
        QApplication::applicationDirPath() + "/../assets/maps/world_map_from_distribution.png",
        QApplication::applicationDirPath() + "/../../assets/maps/world_map_fantasy_wide_ui_v1.png",
        QApplication::applicationDirPath() + "/../../assets/maps/world_map_from_distribution.png",
        QDir::currentPath() + "/assets/maps/world_map_fantasy_wide_ui_v1.png",
        QDir::currentPath() + "/assets/maps/world_map_from_distribution.png",
        "assets/maps/world_map_fantasy_wide_ui_v1.png",
        "assets/maps/world_map_from_distribution.png"
    };
    QPixmap mapPixmap;
    for (const QString& path : candidates)
    {
        if (mapPixmap.load(path))
        {
            break;
        }
    }
    if (!mapPixmap.isNull())
    {
        mapImageLabel->setPixmap(mapPixmap);
    }
    else
    {
        mapImageLabel->setText(QStringLiteral("WORLD Alpha 地图素材缺失\nassets/maps/world_map_fantasy_wide_ui_v1.png"));
        mapImageLabel->setStyleSheet("background:#efe4c6; color:#2a2015; font-size:24px;");
    }

    mapHintLabel = new QLabel(QStringLiteral("点击任意位置继续"), mapOverlay);
    mapHintLabel->setAlignment(Qt::AlignCenter);
    mapHintLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    mapHintLabel->setStyleSheet("background:rgba(40,28,16,190); color:#f3d993; border-radius:8px; font-size:18px; padding:8px;");

    for (int i = 0; i < 10; ++i)
    {
        QFrame* blackDot = new QFrame(mapOverlay);
        blackDot->setAttribute(Qt::WA_TransparentForMouseEvents);
        blackDot->setStyleSheet("background:#050505; border:1px solid #f1d58f;");
        blackDot->setVisible(i != 9);
        mapBlackDots.push_back(blackDot);

        QFrame* whiteDot = new QFrame(mapOverlay);
        whiteDot->setAttribute(Qt::WA_TransparentForMouseEvents);
        whiteDot->setStyleSheet("background:white; border:1px solid #111111;");
        QGraphicsOpacityEffect* opacity = new QGraphicsOpacityEffect(whiteDot);
        opacity->setOpacity(0.0);
        whiteDot->setGraphicsEffect(opacity);
        whiteDot->setVisible(i != 9);
        mapWhiteDots.push_back(whiteDot);
        mapWhiteDotEffects.push_back(opacity);
    }

    mapWhiteWash = new QWidget(mapOverlay);
    mapWhiteWash->setAttribute(Qt::WA_TransparentForMouseEvents);
    mapWhiteWash->setStyleSheet("background:white;");
    mapWhiteWashOpacity = new QGraphicsOpacityEffect(mapWhiteWash);
    mapWhiteWashOpacity->setOpacity(0.0);
    mapWhiteWash->setGraphicsEffect(mapWhiteWashOpacity);
    mapWhiteWash->hide();

    connect(&mapTimer, &QTimer::timeout, this, [this]() {
        mapReady = true;
        mapTimer.stop();
        if (mapHintLabel != nullptr)
        {
            mapHintLabel->show();
            mapHintLabel->raise();
        }
    });
}

void MainWindow::updateOverlayGeometry()
{
    const QRect fullRect(0, 0, width(), height());
    if (storyOverlay != nullptr)
    {
        storyOverlay->setGeometry(fullRect);
        if (storyOverlay->isVisible())
        {
            storyOverlay->raise();
        }
    }
    if (chapterOverlay != nullptr)
    {
        chapterOverlay->setGeometry(fullRect);
        if (chapterOverlay->isVisible())
        {
            chapterOverlay->raise();
        }
    }
    if (mapOverlay != nullptr)
    {
        mapOverlay->setGeometry(fullRect);
        const int margin = 34;
        QSize imageSize(width() - margin * 2, height() - margin * 2);
        const QPixmap mapPixmap = mapImageLabel->pixmap();
        if (!mapPixmap.isNull())
        {
            imageSize = mapPixmap.size();
            imageSize.scale(width() - margin * 2, height() - margin * 2, Qt::KeepAspectRatio);
        }
        const QRect imageRect((width() - imageSize.width()) / 2, (height() - imageSize.height()) / 2, imageSize.width(), imageSize.height());
        mapImageLabel->setGeometry(imageRect);

        const int dotSize = std::max(10, imageRect.width() / 80);
        for (int i = 0; i < mapBlackDots.size(); ++i)
        {
            const QPointF ratio = mapPointRatio(i);
            const int x = imageRect.x() + int(ratio.x() * imageRect.width()) - dotSize / 2;
            const int y = imageRect.y() + int(ratio.y() * imageRect.height()) - dotSize / 2;
            const QRect dotRect(x, y, dotSize, dotSize);
            mapBlackDots[i]->setGeometry(dotRect);
            mapWhiteDots[i]->setGeometry(dotRect);
        }

        mapHintLabel->setGeometry(width() / 2 - 130, height() - 76, 260, 42);
        mapWhiteWash->setGeometry(fullRect);
        if (mapOverlay->isVisible())
        {
            mapOverlay->raise();
        }
    }
}

void MainWindow::loadStory()
{
    const QStringList candidates = {
        QApplication::applicationDirPath() + "/data/story.json",
        QApplication::applicationDirPath() + "/../data/story.json",
        QApplication::applicationDirPath() + "/../../data/story.json",
        QDir::currentPath() + "/data/story.json",
        "data/story.json"
    };
    QByteArray bytes;
    for (const QString& path : candidates)
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
        {
            bytes = file.readAll();
            break;
        }
    }
    QJsonDocument doc = QJsonDocument::fromJson(bytes);
    QJsonObject root = doc.object();
    for (const QString& key : root.keys())
    {
        QVector<QPair<QString, QString>> lines;
        for (const QJsonValue& value : root.value(key).toArray())
        {
            QJsonObject item = value.toObject();
            lines.push_back({ item.value("speaker").toString("Narrator"), item.value("text").toString() });
        }
        storyScenes[key] = lines;
    }
}

void MainWindow::initData()
{
    roster = {
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
    roster = roster.mid(0, 4);
    relics.clear();
    skillSlots.clear();
}

void MainWindow::startGame()
{
    endingShown = false;
    lastChapterTitleShown = -1;
    secondChapterAncientCityShown = false;
    mapPointLit.fill(false);
    chapterIndex = 0;
    levelIndex = 1;
    gold = 0;
    relics.clear();
    skillSlots.clear();
    clearAllUnits();
    showStoryKey("prologue", [this]() { enterCurrentLevel(); });
}

QVector<ChapterDef> chapters()
{
    return {
        {QStringLiteral("序章：梦境"), {QStringLiteral("梦影"), QStringLiteral("异变魔影"), QStringLiteral("破碎信徒")}, QStringLiteral("梦境守卫"), QStringLiteral("真神残响"), QStringLiteral("梦境裂隙"), QStringLiteral("魔王？？？"), QString(), QString()},
        {QStringLiteral("第一章：微风如诗"), {QStringLiteral("野狼"), QStringLiteral("山贼"), QStringLiteral("森林弓手"), QStringLiteral("流浪剑士")}, QStringLiteral("山贼头目"), QStringLiteral("迷失骑士"), QStringLiteral("王城禁卫"), QStringLiteral("阿拉贡"), QString(), QStringLiteral("阿拉贡")},
        {QStringLiteral("第二章：猩红平原"), {QStringLiteral("血仆"), QStringLiteral("夜蝠"), QStringLiteral("吸血鬼侍从"), QStringLiteral("猩红猎犬")}, QStringLiteral("血骑士"), QStringLiteral("吸血鬼伯爵"), QStringLiteral("远古守卫"), QStringLiteral("偷窃者米格"), QString(), QString()},
        {QStringLiteral("第三章：赤心跃动"), {QStringLiteral("血术师"), QStringLiteral("夜行者"), QStringLiteral("血卫"), QStringLiteral("城堡守卫")}, QStringLiteral("赤心骑士"), QStringLiteral("伯爵残影"), QStringLiteral("王座守卫"), QStringLiteral("艾琳"), QStringLiteral("跃动赤心"), QStringLiteral("游侠")},
        {QStringLiteral("第四章：精灵圣地"), {QStringLiteral("精灵射手"), QStringLiteral("古木守卫"), QStringLiteral("毒叶法师"), QStringLiteral("迷途精灵")}, QStringLiteral("古树长老"), QStringLiteral("圣地看守者"), QStringLiteral("精灵祭司"), QStringLiteral("阿格尼"), QStringLiteral("精灵王冠"), QString()},
        {QStringLiteral("第五章：撒冷"), {QStringLiteral("圣光侍从"), QStringLiteral("审判者"), QStringLiteral("守护天使"), QStringLiteral("圣河少女")}, QStringLiteral("六翼候补"), QStringLiteral("天使裁决官"), QStringLiteral("圣河守门人"), QStringLiteral("米凯尔"), QStringLiteral("圣洁六翼"), QStringLiteral("加百列")},
        {QStringLiteral("第六章：魔境"), {QStringLiteral("小恶魔"), QStringLiteral("魔族战士"), QStringLiteral("火焰术士"), QStringLiteral("深渊刺客")}, QStringLiteral("魔境猎手"), QStringLiteral("深渊领主"), QStringLiteral("旧王亲卫"), QStringLiteral("伊维尔"), QString(), QString()},
        {QStringLiteral("第七章：终焉"), {QStringLiteral("终焉魔兵"), QStringLiteral("虚空术士"), QStringLiteral("异界刺客"), QStringLiteral("黑翼守卫")}, QStringLiteral("终焉看门人"), QStringLiteral("伊维尔残影"), QStringLiteral("邪神使徒"), QStringLiteral("莱索恩"), QStringLiteral("邪神赐福"), QString()},
        {QStringLiteral("第八章：世界"), {QStringLiteral("世界回声"), QStringLiteral("旧日生命"), QStringLiteral("白色梦境")}, QStringLiteral("永恒残响"), QStringLiteral("米凯尔与阿格尼"), QStringLiteral("世界门扉"), QStringLiteral("最终问题"), QStringLiteral("世界"), QString()}
    };
}

void MainWindow::enterCurrentLevel()
{
    if (chapterIndex >= chapters().size())
    {
        showEnding();
        return;
    }
    if (levelIndex == 1 && lastChapterTitleShown != chapterIndex)
    {
        showChapterTitle([this]() {
            lastChapterTitleShown = chapterIndex;
            showMapPoint(mapPointForChapter(chapterIndex), false, [this]() { enterCurrentLevel(); });
        });
        return;
    }
    if (chapterIndex == 2 && levelIndex == 10 && !secondChapterAncientCityShown)
    {
        secondChapterAncientCityShown = true;
        showMapPoint(8, false, [this]() { enterCurrentLevel(); });
        return;
    }
    inBattle = false;
    battleRound = 1;
    appendLog(QStringLiteral("进入 %1 第%2关").arg(chapters()[chapterIndex].title).arg(levelIndex));

    if (chapterIndex == 8)
    {
        setupWorldEvent();
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
    if (storyScenes.contains(key))
    {
        storyKeys << key;
    }
    if (boss && storyScenes.contains(QStringLiteral("boss_") + enemyName))
    {
        storyKeys << QStringLiteral("boss_") + enemyName;
    }

    if (!storyKeys.isEmpty())
    {
        showStory(storyKeys, [this]() { setupBattle(); });
    }
    else
    {
        setupBattle();
    }
}

void MainWindow::setupBattle()
{
    clearAllUnits();
    autoDeployPlayer();

    ChapterDef c = chapters()[chapterIndex];
    QString enemyName;
    bool boss = false;
    if (levelIndex == 4) enemyName = c.elite4;
    else if (levelIndex == 5) enemyName = c.boss5;
    else if (levelIndex == 9) enemyName = c.elite9;
    else if (levelIndex == 10) enemyName = c.boss10;
    else enemyName = c.normalEnemies[(levelIndex + chapterIndex) % c.normalEnemies.size()];
    boss = isUniqueBossName(enemyName);

    enemyUnits[slotForRow(boss ? QStringLiteral("中排") : QStringLiteral("前排"), enemyUnits)] = createUnit(makeEnemyTemplate(enemyName, boss), false, boss);
    if (levelIndex >= 6)
    {
        enemyUnits[slotForRow(QStringLiteral("后排"), enemyUnits)] = createUnit(makeEnemyTemplate(c.normalEnemies[(levelIndex + 1) % c.normalEnemies.size()], false), false, false);
    }
    if (levelIndex >= 9)
    {
        enemyUnits[slotForRow(QStringLiteral("前排"), enemyUnits)] = createUnit(makeEnemyTemplate(c.normalEnemies[(levelIndex + 2) % c.normalEnemies.size()], false), false, false);
    }

    if (enemyName == QStringLiteral("偷窃者米格"))
    {
        bool copiedOneUnit = false;
        for (int i = 0; i < 5; ++i)
        {
            if (!copiedOneUnit && i != kHeroSlot && playerUnits[i] != nullptr)
            {
                UnitTemplate copy = playerUnits[i]->base;
                copy.name = QStringLiteral("复制") + copy.name;
                int s = slotForRow(copy.row, enemyUnits);
                if (s >= 0 && enemyUnits[s] == nullptr)
                {
                    enemyUnits[s] = createUnit(copy, false, false);
                    copiedOneUnit = true;
                    appendLog(QStringLiteral("米格开局复制了一个单位：%1。").arg(playerUnits[i]->base.name));
                }
            }
        }
    }

    inBattle = true;
    refreshUi();
}

void MainWindow::setupWorldEvent()
{
    if (levelIndex == 1)
    {
        QDialog dialog(this);
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
        setupWorldEvent();
        return;
    }
    if (levelIndex == 2)
    {
        auto setupWorldBoss = [this]() {
            clearAllUnits();
            autoDeployPlayer();
            enemyUnits[2] = createUnit(makeEnemyTemplate(QStringLiteral("米凯尔"), true), false, true);
            enemyUnits[3] = createUnit(makeEnemyTemplate(QStringLiteral("阿格尼"), true), false, true);
            inBattle = true;
            refreshUi();
        };
        if (storyScenes.contains(QStringLiteral("boss_米凯尔与阿格尼")))
        {
            showStoryKey(QStringLiteral("boss_米凯尔与阿格尼"), setupWorldBoss);
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
            QDialog dialog(this);
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
        QInputDialog::getText(this, QStringLiteral("最终问题"), QStringLiteral("什么让我们不再永恒？"), QLineEdit::Normal, QString(), &ok);
        }
    }
    if (levelIndex >= 10)
    {
        showEnding();
        return;
    }
    inBattle = false;
    appendLog(QStringLiteral("世界章节事件关 %1 完成。").arg(levelIndex));
    refreshUi();
}

void MainWindow::finishLevel()
{
    inBattle = false;
    gold += 5 + chapterIndex;
    if (relics.contains(QStringLiteral("铜钱袋"))) gold += 2;
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
        roster.push_back(templateByName(bossAlly));
        appendLog(QStringLiteral("剧情角色加入：%1").arg(bossAlly));
    }
    if (chapterIndex == 1 && levelIndex == 10)
    {
        const QString rescuedName = QStringLiteral("精灵使者");
        const bool alreadyJoined = std::any_of(roster.begin(), roster.end(), [&rescuedName](const UnitTemplate& unit) {
            return unit.name == rescuedName;
        });
        if (!alreadyJoined)
        {
            roster.push_back(templateByName(rescuedName));
            appendLog(QStringLiteral("剧情角色加入：%1").arg(rescuedName));
        }
    }
    showUnitReward();
    showRelicReward(bossRelic);
    tryFuseWorld();

    QStringList storyKeys;
    if (!bossAlly.isEmpty() && storyScenes.contains(QStringLiteral("ally_") + bossAlly))
    {
        storyKeys << QStringLiteral("ally_") + bossAlly;
    }
    QString key = QString("c%1_l%2_after").arg(chapterIndex).arg(levelIndex);
    if (storyScenes.contains(key))
    {
        storyKeys << key;
    }
    if (!storyKeys.isEmpty())
    {
        showStory(storyKeys, [this]() { refreshUi(); });
    }
    refreshUi();
}

void MainWindow::advanceLevel()
{
    if (inBattle)
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
        showEnding();
        return;
    }
    enterCurrentLevel();
}

void MainWindow::showUnitReward()
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
    QString choice = QInputDialog::getItem(this, QStringLiteral("棋子奖励"), QStringLiteral("选择一张加入牌库"), names, 0, false, &ok);
    if (ok)
    {
        int idx = names.indexOf(choice);
        if (idx >= 0) roster.push_back(offer[idx]);
    }
}

void MainWindow::showRelicReward(const QString& fixedRelic)
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
    QString choice = QInputDialog::getItem(this, QStringLiteral("遗物奖励"), QStringLiteral("选择一个遗物"), offer, 0, false, &ok);
    if (ok)
    {
        addRelic(choice.section(QStringLiteral("  |  "), 0, 0));
    }
}

void MainWindow::autoDeployPlayer()
{
    playerUnits[kHeroSlot] = createUnit(heroTemplate(), true, false);
    for (const UnitTemplate& t : roster)
    {
        int slot = slotForRow(t.row, playerUnits);
        if (slot >= 0 && playerUnits[slot] == nullptr)
        {
            playerUnits[slot] = createUnit(t, false, false);
        }
    }
    applyRelicsStart();
}

void MainWindow::showDeckRefillDialog()
{
    if (roster.isEmpty() || !inBattle)
    {
        return;
    }

    QVector<UnitTemplate> offer;
    for (int i = 0; i < 4 && !roster.isEmpty(); ++i)
    {
        offer.push_back(roster[QRandomGenerator::global()->bounded(roster.size())]);
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

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("牌库补员：抽取4张"));
    dialog.setMinimumWidth(720);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(smallLabel(QStringLiteral("非主角棋子死亡。选择任意张可放置棋子上场；放不下或未选择的牌返回牌库。")));

    QVector<QCheckBox*> checks;
    QHBoxLayout* cards = new QHBoxLayout;
    for (const UnitTemplate& t : offer)
    {
        QCheckBox* box = new QCheckBox(QStringLiteral("%1\n%2\nHP%3 ATK%4\n技能:%5")
                                           .arg(t.name, t.row)
                                           .arg(t.hp)
                                           .arg(t.atk)
                                           .arg(t.skill));
        box->setEnabled(hasEmptySlotForRow(t.row));
        box->setChecked(box->isEnabled());
        box->setFixedSize(160, 150);
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
        int slot = slotForRow(offer[i].row, playerUnits);
        if (slot >= 0 && playerUnits[slot] == nullptr)
        {
            playerUnits[slot] = createUnit(offer[i], false, false);
            appendLog(QStringLiteral("补员上场：%1 -> %2").arg(offer[i].name).arg(offer[i].row));
        }
        else
        {
            appendLog(QStringLiteral("补员未上场，返回牌库：%1").arg(offer[i].name));
        }
    }
}

UnitTemplate MainWindow::heroTemplate() const
{
    QVector<QPair<int, int>> stats = {{30,5},{30,5},{45,8},{60,12},{80,16},{110,24},{140,35},{180,50},{250,70}};
    int idx = std::min(chapterIndex, int(stats.size()) - 1);
    int bonus = chapterIndex * 2;
    int worldHp = relics.contains(QStringLiteral("世界")) ? 100 : 0;
    int worldAtk = relics.contains(QStringLiteral("世界")) ? 50 : 0;
    return {QStringLiteral("主角"), QStringLiteral("命运"), QStringLiteral("中排"), QStringLiteral("命运之刃"), stats[idx].first + bonus + worldHp, stats[idx].second + bonus + worldAtk};
}

UnitTemplate MainWindow::templateByName(const QString& name) const
{
    if (name == QStringLiteral("游侠")) return {name, QStringLiteral("人族"), QStringLiteral("后排"), QStringLiteral("箭雨"), 44, 16};
    if (name == QStringLiteral("精灵使者")) return {name, QStringLiteral("精灵族"), QStringLiteral("后排"), QStringLiteral("森语祝福"), 46, 12};
    if (name == QStringLiteral("加百列")) return {name, QStringLiteral("天使"), QStringLiteral("中排"), QStringLiteral("圣光"), 70, 14};
    for (const UnitTemplate& t : roster)
    {
        if (t.name == name) return t;
    }
    return {name, QStringLiteral("人族"), QStringLiteral("前排"), QStringLiteral("斩击"), 48, 8};
}

UnitInstance* MainWindow::createUnit(const UnitTemplate& t, bool hero, bool boss) const
{
    UnitInstance* u = new UnitInstance;
    u->base = t;
    u->hp = t.hp;
    u->hero = hero;
    u->boss = boss;
    return u;
}

UnitTemplate MainWindow::makeEnemyTemplate(const QString& name, bool boss) const
{
    int scale = chapterIndex * 9 + levelIndex * 3;
    int hp = (boss ? 95 : 36) + scale * (boss ? 3 : 1);
    int atk = (boss ? 13 : 6) + chapterIndex * 3 + levelIndex;
    QString row = boss ? QStringLiteral("中排") : QStringLiteral("前排");
    if (name.contains(QStringLiteral("弓")) || name.contains(QStringLiteral("术")) || name.contains(QStringLiteral("刺"))) row = QStringLiteral("后排");
    return {name, QStringLiteral("敌人"), row, name, hp, atk};
}

int MainWindow::slotForRow(const QString& row, const std::array<UnitInstance*, 5>& board) const
{
    QList<int> slotList;
    if (row == QStringLiteral("前排")) slotList = {0, 1};
    else if (row == QStringLiteral("中排")) slotList = {2, 3};
    else slotList = {4};
    for (int s : slotList)
    {
        if (s != kHeroSlot && board[s] == nullptr) return s;
    }
    for (int i = 0; i < 5; ++i)
    {
        if (i != kHeroSlot && board[i] == nullptr) return i;
    }
    return -1;
}

bool MainWindow::hasEmptySlotForRow(const QString& row) const
{
    QList<int> slotList;
    if (row == QStringLiteral("前排")) slotList = {0, 1};
    else if (row == QStringLiteral("中排")) slotList = {2, 3};
    else slotList = {4};
    for (int s : slotList)
    {
        if (s != kHeroSlot && playerUnits[s] == nullptr) return true;
    }
    return false;
}

QList<UnitInstance*> MainWindow::alive(std::array<UnitInstance*, 5>& board) const
{
    QList<UnitInstance*> result;
    for (UnitInstance* u : board) if (u && u->hp > 0) result << u;
    return result;
}

UnitInstance* MainWindow::firstAlive(std::array<UnitInstance*, 5>& board) const
{
    for (UnitInstance* u : board) if (u && u->hp > 0) return u;
    return nullptr;
}

UnitInstance* MainWindow::lowestHp(std::array<UnitInstance*, 5>& board) const
{
    UnitInstance* best = nullptr;
    for (UnitInstance* u : board)
    {
        if (u && u->hp > 0 && (!best || u->hp < best->hp)) best = u;
    }
    return best;
}

UnitInstance* MainWindow::highestAtk(std::array<UnitInstance*, 5>& board) const
{
    UnitInstance* best = nullptr;
    for (UnitInstance* u : board)
    {
        if (u && u->hp > 0 && (!best || u->base.atk > best->base.atk)) best = u;
    }
    return best;
}

void MainWindow::runRound()
{
    if (!inBattle) return;
    appendLog(QStringLiteral("第%1回合").arg(battleRound));
    applyRelicsStart();
    bossMechanics();
    generateSkills();
    allAttack(playerUnits, enemyUnits, true);
    allAttack(enemyUnits, playerUnits, false);
    cleanupDeaths(enemyUnits, false);
    cleanupDeaths(playerUnits, true);
    ++battleRound;
    if (enemiesDefeated())
    {
        finishLevel();
    }
    else if (playerDefeated())
    {
        appendLog(QStringLiteral("失败：极限测试版自动重整后重试本关。"));
        setupBattle();
    }
    refreshUi();
}

void MainWindow::applyRelicsStart()
{
    for (UnitInstance* u : alive(playerUnits))
    {
        if (relics.contains(QStringLiteral("铁剑"))) u->base.atk += 2;
        if (relics.contains(QStringLiteral("旧盾")) && battleRound == 1) u->hp += 8;
        if (relics.contains(QStringLiteral("战鼓")) && battleRound == 1) u->base.atk += 2;
        if (relics.contains(QStringLiteral("旅人靴")) && battleRound == 1) u->base.atk += 3;
        if (relics.contains(QStringLiteral("幸运骰子")) && battleRound == 1 && QRandomGenerator::global()->bounded(4) == 0) u->base.atk += 5;
        if (relics.contains(QStringLiteral("人王徽记")) && u->base.faction == QStringLiteral("人族")) u->base.atk += 4;
        if (relics.contains(QStringLiteral("魔王残角")) && u->base.faction == QStringLiteral("魔族")) { u->base.atk += 6; u->hp -= 5; }
        if (relics.contains(QStringLiteral("圣洁六翼")) && battleRound == 1) u->shield += 20;
        if (relics.contains(QStringLiteral("邪神赐福"))) { u->base.atk = int(u->base.atk * 1.5); u->hp -= 3; }
    }
    if (UnitInstance* hero = playerUnits[kHeroSlot])
    {
        if (relics.contains(QStringLiteral("医疗包"))) heal(hero, 5);
        if (relics.contains(QStringLiteral("世界")) && skillSlots.size() < kMaxSkills) skillSlots.push_back({QStringLiteral("命运之刃"), QStringLiteral("世界")});
    }
    if (relics.contains(QStringLiteral("跃动赤心")))
    {
        for (UnitInstance* u : alive(playerUnits)) heal(u, 10);
    }
}

void MainWindow::generateSkills()
{
    for (UnitInstance* u : alive(playerUnits))
    {
        ++u->aliveRounds;
        if (u->aliveRounds % 2 == 0 && skillSlots.size() < kMaxSkills)
        {
            skillSlots.push_back({u->base.skill, u->base.name});
            appendLog(QStringLiteral("%1 生成技能：%2").arg(u->base.name, u->base.skill));
        }
    }
}

void MainWindow::castSelectedSkills()
{
    int used = 0;
    for (int i = skillSlots.size() - 1; i >= 0; --i)
    {
        if (skillButtons[i]->isChecked() && used < 2)
        {
            castSkill(skillSlots[i].name);
            skillSlots.removeAt(i);
            ++used;
        }
    }
    refreshUi();
}

void MainWindow::castSkill(const QString& name)
{
    appendLog(QStringLiteral("释放技能：%1").arg(name));
    int bonus = relics.contains(QStringLiteral("魔法书")) ? 3 : 0;
    if (name == QStringLiteral("斩击")) dealDamage(firstAlive(enemyUnits), 8 + bonus, name);
    else if (name == QStringLiteral("箭雨") || name == QStringLiteral("魔焰"))
    {
        for (UnitInstance* e : alive(enemyUnits)) dealDamage(e, (name == QStringLiteral("箭雨") ? 4 : 10) + bonus, name);
    }
    else if (name == QStringLiteral("鼓舞")) for (UnitInstance* u : alive(playerUnits)) u->base.atk += 2;
    else if (name == QStringLiteral("守护") && playerUnits[kHeroSlot]) playerUnits[kHeroSlot]->shield += 10;
    else if (name == QStringLiteral("治疗术")) heal(lowestHp(playerUnits), 12 + (relics.contains(QStringLiteral("圣河水滴")) ? 5 : 0));
    else if (name == QStringLiteral("吸血")) { dealDamage(firstAlive(enemyUnits), 10 + bonus, name); heal(playerUnits[kHeroSlot], 10); }
    else if (name == QStringLiteral("血雾")) for (UnitInstance* e : alive(enemyUnits)) e->base.atk = std::max(0, e->base.atk - 2);
    else if (name == QStringLiteral("赤心爆发")) dealDamage(firstAlive(enemyUnits), 20 + bonus, name);
    else if (name == QStringLiteral("永生之血")) heal(playerUnits[kHeroSlot], 10);
    else if (name == QStringLiteral("精灵箭")) dealDamage(firstAlive(enemyUnits), 8 + bonus, name);
    else if (name == QStringLiteral("森语祝福"))
    {
        for (UnitInstance* u : alive(playerUnits))
        {
            u->base.atk += 2;
            heal(u, 4);
        }
    }
    else if (name == QStringLiteral("毒雾")) for (UnitInstance* e : alive(enemyUnits)) dealDamage(e, 3 + bonus, name);
    else if (name == QStringLiteral("古木再生"))
    {
        int s = slotForRow(QStringLiteral("前排"), playerUnits);
        if (s >= 0) playerUnits[s] = createUnit({QStringLiteral("小树人"), QStringLiteral("精灵族"), QStringLiteral("前排"), QStringLiteral("斩击"), 18, 4});
    }
    else if (name == QStringLiteral("古树根须")) if (UnitInstance* u = lowestHp(playerUnits)) u->shield += 15;
    else if (name == QStringLiteral("藤蔓缠绕")) for (UnitInstance* e : alive(enemyUnits)) e->base.atk = std::max(0, e->base.atk - 1);
    else if (name == QStringLiteral("圣光")) for (UnitInstance* u : alive(playerUnits)) heal(u, 8 + (relics.contains(QStringLiteral("圣河水滴")) ? 5 : 0));
    else if (name == QStringLiteral("审判")) dealDamage(highestAtk(enemyUnits), 25 + bonus, name);
    else if (name == QStringLiteral("六翼庇护")) for (UnitInstance* u : alive(playerUnits)) u->shield += 12;
    else if (name == QStringLiteral("命运改写") && playerUnits[kHeroSlot]) playerUnits[kHeroSlot]->protectedDeath = true;
    else if (name == QStringLiteral("圣河回响") && !skillSlots.isEmpty() && skillSlots.size() < kMaxSkills) skillSlots.push_back(skillSlots.last());
    else if (name == QStringLiteral("火球")) dealDamage(firstAlive(enemyUnits), 18 + bonus, name);
    else if (name == QStringLiteral("深渊爪击")) dealDamage(enemyUnits[4] ? enemyUnits[4] : firstAlive(enemyUnits), 22 + bonus, name);
    else if (name == QStringLiteral("狂暴")) if (UnitInstance* u = highestAtk(playerUnits)) { u->base.atk += 10; u->hp -= 5; }
    else if (name == QStringLiteral("邪神赐福")) for (UnitInstance* u : alive(playerUnits)) u->base.atk *= 2;
    else if (name == QStringLiteral("命运之刃")) dealDamage(firstAlive(enemyUnits), playerUnits[kHeroSlot] ? int(playerUnits[kHeroSlot]->base.atk * (1.0 + chapterIndex * 0.1)) : 0, name);
}

void MainWindow::allAttack(std::array<UnitInstance*, 5>& attackers, std::array<UnitInstance*, 5>& defenders, bool playerSide)
{
    for (UnitInstance* u : alive(attackers))
    {
        UnitInstance* target = firstAlive(defenders);
        if (!target) return;
        int dmg = u->base.atk;
        if (playerSide && relics.contains(QStringLiteral("世界"))) dmg += 50;
        dealDamage(target, dmg, u->base.name);
        if (playerSide && relics.contains(QStringLiteral("猩红酒杯")) && u->base.faction == QStringLiteral("吸血鬼")) heal(u, 3);
    }
}

void MainWindow::dealDamage(UnitInstance* target, int amount, const QString& reason)
{
    if (!target || amount <= 0) return;
    int shieldHit = std::min(target->shield, amount);
    target->shield -= shieldHit;
    target->hp -= (amount - shieldHit);
    appendLog(QStringLiteral("%1 造成 %2 伤害 -> %3").arg(reason).arg(amount).arg(target->base.name));
}

void MainWindow::heal(UnitInstance* target, int amount)
{
    if (!target || amount <= 0) return;
    target->hp += amount;
}

void MainWindow::cleanupDeaths(std::array<UnitInstance*, 5>& board, bool playerSide)
{
    bool needRefill = false;
    for (int i = 0; i < 5; ++i)
    {
        UnitInstance* u = board[i];
        if (!u || u->hp > 0) continue;
        if (playerSide && u->hero && (relics.contains(QStringLiteral("破碎护符")) || u->protectedDeath) && !u->revived)
        {
            u->hp = 1;
            u->revived = true;
            appendLog(QStringLiteral("主角被保留在1点生命。"));
            continue;
        }
        if (playerSide && relics.contains(QStringLiteral("精灵王冠")) && !u->revived)
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
        if (playerSide && relics.contains(QStringLiteral("精灵树枝")) && u->base.faction == QStringLiteral("精灵族"))
        {
            for (UnitInstance* e : alive(enemyUnits)) dealDamage(e, 5, QStringLiteral("精灵树枝"));
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
        refreshUi();
        showDeckRefillDialog();
    }
}

void MainWindow::bossMechanics()
{
    for (UnitInstance* boss : alive(enemyUnits))
    {
        if (!boss->boss) continue;
        QString n = boss->base.name;
        if (n == QStringLiteral("阿拉贡") && battleRound % 3 == 0) for (UnitInstance* e : alive(enemyUnits)) e->base.atk += 3;
        else if (n == QStringLiteral("吸血鬼伯爵")) heal(boss, 8);
        else if (n == QStringLiteral("偷窃者米格") && !skillSlots.isEmpty()) castSkill(skillSlots[QRandomGenerator::global()->bounded(skillSlots.size())].name);
        else if (n == QStringLiteral("艾琳")) heal(boss, 12);
        else if (n == QStringLiteral("阿格尼")) for (UnitInstance* p : alive(playerUnits)) dealDamage(p, 3, QStringLiteral("精灵王威压"));
        else if (n == QStringLiteral("米凯尔") && battleRound % 3 == 0) dealDamage(highestAtk(playerUnits), 28, QStringLiteral("六翼审判"));
        else if (n == QStringLiteral("伊维尔")) boss->base.atk += 3;
        else if (n == QStringLiteral("莱索恩"))
        {
            int roll = QRandomGenerator::global()->bounded(4);
            if (roll == 0) boss->shield += 20;
            if (roll == 1) heal(boss, 10);
            if (roll == 2) boss->base.atk *= 2;
            if (roll == 3) for (UnitInstance* p : alive(playerUnits)) dealDamage(p, 4, QStringLiteral("邪神诅咒"));
            if (boss->hp < boss->base.hp * 0.3) for (UnitInstance* p : alive(playerUnits)) dealDamage(p, 5, QStringLiteral("终焉阶段"));
        }
    }
}

bool MainWindow::enemiesDefeated() const
{
    for (UnitInstance* u : enemyUnits) if (u && u->hp > 0) return false;
    return true;
}

bool MainWindow::playerDefeated() const
{
    return playerUnits[kHeroSlot] == nullptr || playerUnits[kHeroSlot]->hp <= 0;
}

void MainWindow::showStory(const QStringList& keys, std::function<void()> onFinished)
{
    pendingStoryKeys = keys;
    storyFinishedCallback = onFinished;
    nextStoryStep();
}

void MainWindow::showStoryKey(const QString& key, std::function<void()> onFinished)
{
    showStory(QStringList{key}, onFinished);
}

void MainWindow::nextStoryStep()
{
    if (!storyOverlay->isVisible())
    {
        updateOverlayGeometry();
        storyOverlay->raise();
        storyOverlay->show();
        updateOverlayGeometry();
    }
    if (activeStory.isEmpty() || storyLineIndex >= activeStory.size())
    {
        if (!pendingStoryKeys.isEmpty())
        {
            QString key = pendingStoryKeys.takeFirst();
            activeStory = storyScenes.value(key);
            if (activeStory.isEmpty())
            {
                activeStory = {{QStringLiteral("Narrator"), QStringLiteral("故事片段缺失：") + key}};
            }
            storyLineIndex = 0;
            storyCharIndex = 0;
            storyLineComplete = false;
            storySpeaker->setText(activeStory[0].first);
            storyText->clear();
            storyTimer.start(25);
        }
        else
        {
            storyOverlay->hide();
            auto callback = storyFinishedCallback;
            storyFinishedCallback = {};
            if (callback) callback();
        }
        return;
    }
    if (!storyLineComplete)
    {
        storyCharIndex = activeStory.value(storyLineIndex).second.size();
        storyText->setText(activeStory.value(storyLineIndex).second);
        storyLineComplete = true;
        return;
    }
    ++storyLineIndex;
    if (storyLineIndex < activeStory.size())
    {
        storyCharIndex = 0;
        storyLineComplete = false;
        storySpeaker->setText(activeStory[storyLineIndex].first);
        storyText->clear();
        storyTimer.start(25);
        return;
    }
    if (!pendingStoryKeys.isEmpty())
    {
        QString key = pendingStoryKeys.takeFirst();
        activeStory = storyScenes.value(key);
        if (activeStory.isEmpty())
        {
            activeStory = {{QStringLiteral("Narrator"), QStringLiteral("故事片段缺失：") + key}};
        }
        storyLineIndex = 0;
        storyCharIndex = 0;
        storyLineComplete = false;
        storySpeaker->setText(activeStory[0].first);
        storyText->clear();
        storyTimer.start(25);
        return;
    }
    storyTimer.stop();
    storyOverlay->hide();
    auto callback = storyFinishedCallback;
    storyFinishedCallback = {};
    if (callback) callback();
}

void MainWindow::tickStory()
{
    if (activeStory.isEmpty()) return;
    const QString full = activeStory[storyLineIndex].second;
    ++storyCharIndex;
    storyText->setText(full.left(storyCharIndex));
    if (storyCharIndex >= full.size())
    {
        storyLineComplete = true;
        storyTimer.stop();
    }
}

void MainWindow::showEnding()
{
    if (endingShown) return;
    endingShown = true;
    inBattle = false;
    showMapPoint(-1, true, [this]() {
    showStoryKey("ending", [this]() {
        QMessageBox::information(this, QStringLiteral("游戏结束"), QStringLiteral("最终结局完成，返回主菜单。"));
        startGame();
    });
    });
}

void MainWindow::showChapterTitle(std::function<void()> onFinished)
{
    if (chapterIndex >= chapters().size())
    {
        if (onFinished) onFinished();
        return;
    }

    chapterTitleReady = false;
    chapterTitleFinishedCallback = onFinished;
    updateOverlayGeometry();
    chapterOverlay->raise();

    QString title = chapters()[chapterIndex].title;
    title.replace(QStringLiteral("："), QStringLiteral("\n"));
    chapterChineseLabel->setText(title);
    chapterEnglishLabel->setText(chapterEnglishName(chapterIndex));

    QGraphicsOpacityEffect* chineseOpacity = new QGraphicsOpacityEffect(chapterChineseLabel);
    QGraphicsOpacityEffect* englishOpacity = new QGraphicsOpacityEffect(chapterEnglishLabel);
    chineseOpacity->setOpacity(0.0);
    englishOpacity->setOpacity(0.0);
    chapterChineseLabel->setGraphicsEffect(chineseOpacity);
    chapterEnglishLabel->setGraphicsEffect(englishOpacity);

    QPropertyAnimation* chineseFade = new QPropertyAnimation(chineseOpacity, "opacity", chapterOverlay);
    chineseFade->setDuration(4000);
    chineseFade->setStartValue(0.0);
    chineseFade->setEndValue(1.0);

    QPropertyAnimation* englishFade = new QPropertyAnimation(englishOpacity, "opacity", chapterOverlay);
    englishFade->setDuration(4000);
    englishFade->setStartValue(0.0);
    englishFade->setEndValue(1.0);

    chapterOverlay->show();
    updateOverlayGeometry();
    chineseFade->start(QAbstractAnimation::DeleteWhenStopped);
    englishFade->start(QAbstractAnimation::DeleteWhenStopped);
    chapterTitleTimer.start(4000);
}

void MainWindow::showMapPoint(int pointIndex, bool fadeWholeMap, std::function<void()> onFinished)
{
    mapReady = false;
    mapFinishedCallback = onFinished;
    if (mapHintLabel != nullptr)
    {
        mapHintLabel->hide();
    }
    if (mapWhiteWashOpacity != nullptr)
    {
        mapWhiteWashOpacity->setOpacity(0.0);
    }
    if (mapWhiteWash != nullptr)
    {
        mapWhiteWash->hide();
    }

    for (int i = 0; i < mapWhiteDotEffects.size(); ++i)
    {
        const bool isOutsideMapPoint = (i == 9);
        const bool shouldShowPoint = !isOutsideMapPoint || mapPointLit[i] || pointIndex == i;
        mapWhiteDotEffects[i]->setOpacity(mapPointLit[i] ? 1.0 : 0.0);
        mapBlackDots[i]->setVisible(shouldShowPoint);
        mapWhiteDots[i]->setVisible(shouldShowPoint);
    }

    mapOverlay->show();
    updateOverlayGeometry();
    mapOverlay->raise();
    mapImageLabel->raise();
    for (QFrame* dot : mapBlackDots) dot->raise();
    for (QFrame* dot : mapWhiteDots) dot->raise();

    if (fadeWholeMap)
    {
        if (mapWhiteWash != nullptr)
        {
            mapWhiteWash->show();
            mapWhiteWash->raise();
        }
        QPropertyAnimation* fade = new QPropertyAnimation(mapWhiteWashOpacity, "opacity", mapOverlay);
        fade->setDuration(2600);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
        mapTimer.start(2600);
        return;
    }

    if (pointIndex >= 0 && pointIndex < mapWhiteDotEffects.size())
    {
        mapPointLit[pointIndex] = true;
        mapWhiteDotEffects[pointIndex]->setOpacity(0.0);
        QPropertyAnimation* fade = new QPropertyAnimation(mapWhiteDotEffects[pointIndex], "opacity", mapOverlay);
        fade->setDuration(1600);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
    mapTimer.start(1600);
}

int MainWindow::mapPointForChapter(int index) const
{
    static const std::array<int, 9> chapterPoints = {
        0, // 序章：开始点
        1, // 第一章：王城 / 人类诸国
        2, // 第二章：猩红平原，第二章第十关会另亮远古城市
        3, // 第三章：艾琳堡垒
        4, // 第四章：精灵圣地
        5, // 第五章：撒冷
        6, // 第六章：魔境
        7, // 第七章：魔都
        9  // 第八章：地图外，点在地图下方
    };
    if (index >= 0 && index < int(chapterPoints.size()))
    {
        return chapterPoints[index];
    }
    return 9;
}

QPointF MainWindow::mapPointRatio(int pointIndex) const
{
    static const std::array<QPointF, 10> points = {
        QPointF(0.190, 0.690), // 开始点
        QPointF(0.255, 0.515), // 王城
        QPointF(0.352, 0.475), // 猩红平原
        QPointF(0.318, 0.388), // 艾琳堡垒
        QPointF(0.364, 0.263), // 精灵圣地
        QPointF(0.530, 0.407), // 撒冷
        QPointF(0.848, 0.402), // 魔境
        QPointF(0.790, 0.522), // 魔都
        QPointF(0.095, 0.474), // 远古城市
        QPointF(0.500, 0.898)  // 地图外
    };
    if (pointIndex >= 0 && pointIndex < int(points.size()))
    {
        return points[pointIndex];
    }
    return QPointF(0.5, 0.5);
}

QString MainWindow::chapterEnglishName(int index) const
{
    static const QStringList names = {
        QStringLiteral("Dream"),
        QStringLiteral("Breeze as Poetry"),
        QStringLiteral("Scarlet Plain"),
        QStringLiteral("Crimson Heartbeat"),
        QStringLiteral("Secret of Undeath"),
        QStringLiteral("Salem"),
        QStringLiteral("Demon Realm"),
        QStringLiteral("Finale"),
        QStringLiteral("World")
    };
    if (index >= 0 && index < names.size())
    {
        return names[index];
    }
    return QStringLiteral("World");
}

void MainWindow::refreshUi()
{
    updateOverlayGeometry();
    titleLabel->setText(chapterIndex < chapters().size() ? chapters()[chapterIndex].title : QStringLiteral("结局"));
    progressLabel->setText(QStringLiteral("章节 %1/9  关卡 %2/10  战斗回合 %3").arg(chapterIndex + 1).arg(levelIndex).arg(battleRound));
    if (UnitInstance* hero = playerUnits[kHeroSlot])
    {
        heroLabel->setText(QStringLiteral("主角：HP %1 / ATK %2 / 护盾 %3").arg(hero->hp).arg(hero->base.atk).arg(hero->shield));
    }
    else
    {
        heroLabel->setText(QStringLiteral("主角：等待部署"));
    }
    goldLabel->setText(QStringLiteral("金币：%1  队伍牌库：%2").arg(gold).arg(roster.size()));
    logView->setPlainText(logLines.join("\n"));
    logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    refreshBoard();
    refreshSkills();
    refreshRelics();
    roundButton->setEnabled(inBattle);
    skillCastButton->setEnabled(inBattle && !skillSlots.isEmpty());
    nextButton->setEnabled(!inBattle && !storyOverlay->isVisible() && !chapterOverlay->isVisible());
}

void MainWindow::refreshBoard()
{
    for (int i = 0; i < 5; ++i)
    {
        UnitInstance* e = enemyUnits[i];
        UnitInstance* p = playerUnits[i];
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
        if (i < skillSlots.size())
        {
            const QString desc = skillDescription(skillSlots[i].name);
            skillButtons[i]->setText(QStringLiteral("%1\n来自：%2\n%3")
                                          .arg(skillSlots[i].name,
                                               skillSlots[i].source,
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
        if (i < relics.size())
        {
            relicButtons[i]->setText(QStringLiteral("%1\n%2").arg(relics[i], relicDescription(relics[i])));
        }
        else
        {
            relicButtons[i]->setText(QStringLiteral("遗物槽 %1\n空").arg(i + 1));
        }
    }
}

void MainWindow::appendLog(const QString& text)
{
    logLines << text;
    while (logLines.size() > 80) logLines.removeFirst();
}

void MainWindow::clearBoard(std::array<UnitInstance*, 5>& board)
{
    for (UnitInstance*& u : board)
    {
        delete u;
        u = nullptr;
    }
}

void MainWindow::clearAllUnits()
{
    clearBoard(playerUnits);
    clearBoard(enemyUnits);
}

void MainWindow::addRelic(const QString& relic)
{
    if (relic.isEmpty()) return;
    if (relics.contains(relic))
    {
        appendLog(QStringLiteral("已拥有遗物：%1").arg(relic));
        return;
    }

    if (relics.size() < kMaxRelics)
    {
        relics << relic;
        appendLog(QStringLiteral("获得遗物：%1").arg(relic));
        return;
    }

    QStringList replaceOptions;
    QVector<int> replaceIndexes;
    for (int i = 0; i < relics.size(); ++i)
    {
        if (isStoryRelic(relics[i]))
        {
            continue;
        }
        replaceIndexes.push_back(i);
        replaceOptions << QStringLiteral("%1号槽：%2 - %3")
                              .arg(i + 1)
                              .arg(relics[i], relicDescription(relics[i]));
    }

    if (replaceOptions.isEmpty())
    {
        appendLog(QStringLiteral("遗物槽已满，且没有可替换的非剧情遗物。未获得：%1").arg(relic));
        QMessageBox::information(this,
                                 QStringLiteral("遗物槽已满"),
                                 QStringLiteral("5个遗物槽都被剧情遗物或世界占据，无法替换。\n未获得：%1").arg(relic));
        return;
    }

    bool ok = false;
    QString choice = QInputDialog::getItem(this,
                                           QStringLiteral("选择替换遗物槽"),
                                           QStringLiteral("遗物槽已满。选择要替换的位置："),
                                           replaceOptions,
                                           0,
                                           false,
                                           &ok);
    if (!ok)
    {
        appendLog(QStringLiteral("放弃获得遗物：%1").arg(relic));
        return;
    }

    int chosen = replaceOptions.indexOf(choice);
    if (chosen >= 0)
    {
        int relicIndex = replaceIndexes[chosen];
        appendLog(QStringLiteral("替换遗物：%1 -> %2").arg(relics[relicIndex], relic));
        relics[relicIndex] = relic;
    }
}

QString MainWindow::relicDescription(const QString& relic) const
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

QString MainWindow::skillDescription(const QString& skill) const
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

bool MainWindow::isStoryRelic(const QString& relic) const
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

void MainWindow::tryFuseWorld()
{
    QStringList needed = {QStringLiteral("圣洁六翼"), QStringLiteral("精灵王冠"), QStringLiteral("邪神赐福"), QStringLiteral("跃动赤心")};
    for (const QString& n : needed)
    {
        if (!relics.contains(n)) return;
    }
    if (relics.contains(QStringLiteral("世界"))) return;
    for (const QString& n : needed)
    {
        relics.removeAll(n);
    }
    appendLog(QStringLiteral("四件剧情遗物融合为：世界"));
    addRelic(QStringLiteral("世界"));
}
