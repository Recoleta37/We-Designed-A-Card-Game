#include "MapManager.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QMouseEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include "GameTypes.h"

MapManager::MapManager(QWidget* parent)
    : QObject(parent)
    , parentWidget(parent)
{
}

/// [Recoleta37] Phase 4: 构建章节标题 overlay
/// 白色全屏 + 中/英文双行标题居中，默认隐藏。
/// 章节标题区固定高度 190px，上方 stretch 使标题偏上。
/// 定时器 4 秒后将 chapterTitleReady 置 true，允许点击关闭。
void MapManager::buildChapterOverlay()
{
    chapterOverlay = new QWidget(parentWidget);
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

/// [Recoleta37] Phase 4: 构建世界地图 overlay
/// 从多个候选路径加载地图图片，创建 10 个坐标点（黑底白点），
/// 外加白色遮罩（结局淡出用）和底部提示文字。
/// mapPointLit 初始全 false，由 showMapPoint() 逐个点亮。
/// 地图外点（index 9）初始不可见，仅在 showMapPoint 指定时显示。
void MapManager::buildMapOverlay()
{
    mapOverlay = new QWidget(parentWidget);
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

/// [Recoleta37] Phase 4: 响应窗口 resize，重新计算 overlay 几何
/// - 章节 overlay：直接铺满 fullRect
/// - 地图 overlay：按 margin=34 缩放图片保持比例，重新计算各点的像素坐标
///   点大小 = max(10, 图片宽度/80)，由 mapPointRatio 的 (0..1) 相对坐标映射
void MapManager::updateOverlayGeometry(const QRect& fullRect)
{
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
        QSize imageSize(fullRect.width() - margin * 2, fullRect.height() - margin * 2);
        const QPixmap mapPixmap = mapImageLabel->pixmap();
        if (!mapPixmap.isNull())
        {
            imageSize = mapPixmap.size();
            imageSize.scale(fullRect.width() - margin * 2, fullRect.height() - margin * 2, Qt::KeepAspectRatio);
        }
        const QRect imageRect((fullRect.width() - imageSize.width()) / 2,
                              (fullRect.height() - imageSize.height()) / 2,
                              imageSize.width(), imageSize.height());
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

        mapHintLabel->setGeometry(fullRect.width() / 2 - 130, fullRect.height() - 76, 260, 42);
        mapWhiteWash->setGeometry(fullRect);
        if (mapOverlay->isVisible())
        {
            mapOverlay->raise();
        }
    }
}

/// [Recoleta37] Phase 4: 播放章节标题动画
/// 流程：
///   1. 将标题中的全角冒号替换为换行，设置中/英文文本
///   2. 创建两个独立的 QGraphicsOpacityEffect + QPropertyAnimation（4s 淡入）
///   3. 同时启动 4s 定时器 → chapterTitleReady = true
///   4. 用户点击 或 定时器到期 → eventFilter 触发 → hide overlay → 回调 onFinished
/// 注：动画对象设为 DeleteWhenStopped，无需手动清理。
void MapManager::showChapterTitle(int chapterIndex, std::function<void()> onFinished)
{
    if (chapterIndex >= chapters().size())
    {
        if (onFinished) onFinished();
        return;
    }

    chapterTitleReady = false;
    chapterTitleFinishedCallback = onFinished;
    updateOverlayGeometry(QRect(0, 0, parentWidget->width(), parentWidget->height()));
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
    updateOverlayGeometry(QRect(0, 0, parentWidget->width(), parentWidget->height()));
    chineseFade->start(QAbstractAnimation::DeleteWhenStopped);
    englishFade->start(QAbstractAnimation::DeleteWhenStopped);
    chapterTitleTimer.start(4000);
}

/// [Recoleta37] Phase 4: 显示地图并点亮指定坐标点
/// 参数：
///   pointIndex < 0  → 不点亮任何点（仅显示地图现状）
///   fadeWholeMap    → true: 白色遮罩从 0→1 淡入 2.6s（结局白幕）
///                     false: 点亮 pointIndex 点的白色光晕 1.6s
///
/// 点位显示规则（循环内）：
///   - index 9 是"地图外"点，默认隐藏，仅当已被点亮或恰为当前 pointIndex 时出现
///   - 已点亮的点（mapPointLit[i]==true）维持白色不透明
///   - 未点亮的点维持黑色
///
/// 完成后：定时器到期 → mapReady=true → 显示"点击继续"提示 → 等待点击
/// 点击后 → hide mapOverlay → 触发 onFinished 回调
void MapManager::showMapPoint(int pointIndex, bool fadeWholeMap, std::function<void()> onFinished)
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
    updateOverlayGeometry(QRect(0, 0, parentWidget->width(), parentWidget->height()));
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

/// [Recoleta37] Phase 4: 拦截 overlay 点击事件
/// 两个 overlay 都采用"ready 状态门"机制：
///   动画/定时器运行中 → ready=false → 点击无效（防止动画未完成就关闭）
///   定时器到期 → ready=true  → 点击关闭 overlay 并触发回调
/// 回调执行后清空（{}），防止重复触发。
bool MapManager::eventFilter(QObject* watched, QEvent* event)
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
    return QObject::eventFilter(watched, event);
}

bool MapManager::isChapterOverlayVisible() const
{
    return chapterOverlay != nullptr && chapterOverlay->isVisible();
}

/// [Recoleta37] Phase 4: 新游戏开始时重置所有地图点亮状态
void MapManager::resetMapPoints()
{
    mapPointLit.fill(false);
}

/// 将 0-based 章节索引映射到地图上的坐标点索引
/// 序章→0(开始点), 第一章→1(王城), ..., 第八章→9(地图外)
int MapManager::mapPointForChapter(int index)
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

/// 返回地图上第 pointIndex 个点的相对坐标 (0..1)
/// 用于 updateOverlayGeometry() 中将相对位置映射到像素坐标
QPointF MapManager::mapPointRatio(int pointIndex)
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

/// 返回章节索引对应的英文标题（显示在章节标题 overlay 第二行）
QString MapManager::chapterEnglishName(int index)
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
