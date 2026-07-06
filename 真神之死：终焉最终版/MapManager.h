#pragma once

#include <array>
#include <functional>

#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

/// [Recoleta37] Phase 4: MapManager 封装章节标题与地图 overlay
/// - 章节标题：白色全屏 + 中/英文标题淡入动画，点击继续
/// - 地图：世界地图图片 + 坐标点亮动画 + 结局白幕淡出
/// - 自行处理 overlay 点击事件（installEventFilter）
///
/// 使用流程：
///   构造函数 → buildChapterOverlay() / buildMapOverlay()
///   showChapterTitle(chapterIndex, callback) → 点击或动画结束 → callback
///   showMapPoint(pointIndex, fadeWholeMap, callback) → 点击或动画结束 → callback
class MapManager : public QObject
{
    Q_OBJECT

public:
    explicit MapManager(QWidget* parent);

    /// 在 parent 上创建章节标题 overlay（白色全屏 + 双行文字 + 淡入）
    void buildChapterOverlay();
    /// 在 parent 上创建地图 overlay（世界地图 + 10 个坐标点 + 提示）
    void buildMapOverlay();
    /// 窗口大小变化时由 MainWindow 调用，保持 overlay 铺满并重算点位
    void updateOverlayGeometry(const QRect& fullRect);

    /// 播放章节标题动画（4s 淡入），完成后回调 onFinished
    void showChapterTitle(int chapterIndex, std::function<void()> onFinished);
    /// 点亮地图上的 pointIndex 点（动画 1.6s），完成后回调 onFinished
    /// pointIndex < 0 表示不点亮任何点；fadeWholeMap 表示白色淡出（结局用）
    void showMapPoint(int pointIndex, bool fadeWholeMap, std::function<void()> onFinished);

    /// 重置所有地图点亮状态（新游戏开始时调用）
    void resetMapPoints();
    /// 查询章节 overlay 是否正在显示（供 refreshUi 判断 nextButton 状态）
    bool isChapterOverlayVisible() const;

    /// 将章节索引映射到地图坐标点索引
    static int mapPointForChapter(int index);
    /// 返回地图上第 pointIndex 个点的相对坐标 (0..1)
    static QPointF mapPointRatio(int pointIndex);
    /// 返回章节索引对应的英文标题
    static QString chapterEnglishName(int index);

protected:
    /// 拦截 chapterOverlay / mapOverlay 的鼠标点击，推进状态
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* parentWidget;

    // ---- 章节标题 overlay ----
    QWidget* chapterOverlay = nullptr;
    QLabel* chapterChineseLabel = nullptr;
    QLabel* chapterEnglishLabel = nullptr;
    QTimer chapterTitleTimer;
    bool chapterTitleReady = false;
    std::function<void()> chapterTitleFinishedCallback;

    // ---- 地图 overlay ----
    QWidget* mapOverlay = nullptr;
    QLabel* mapImageLabel = nullptr;
    QLabel* mapHintLabel = nullptr;
    QWidget* mapWhiteWash = nullptr;
    QGraphicsOpacityEffect* mapWhiteWashOpacity = nullptr;
    QVector<QFrame*> mapBlackDots;
    QVector<QFrame*> mapWhiteDots;
    QVector<QGraphicsOpacityEffect*> mapWhiteDotEffects;
    QTimer mapTimer;
    bool mapReady = false;
    std::array<bool, 10> mapPointLit{};
    std::function<void()> mapFinishedCallback;
};
