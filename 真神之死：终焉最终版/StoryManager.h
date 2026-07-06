#pragma once

#include <functional>

#include <QEvent>
#include <QLabel>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QPixmap>
#include <QStringList>
#include <QTimer>
#include <QTextEdit>
#include <QVector>
#include <QWidget>

/// [Recoleta37] Phase 2: StoryManager 封装所有剧情播放逻辑
/// - 加载 story.json → storyScenes
/// - 打字机逐字播放 + 点击推进
/// - overlay UI 构建与几何更新
///
/// 状态机: pendingStoryKeys 队列 → activeStory → storyLineIndex/storyCharIndex
///   点击或当前行播完 → nextStoryStep() 推进
///   定时器 tickStory() → 逐字显示当前行
class StoryManager : public QObject
{
    Q_OBJECT

public:
    explicit StoryManager(QWidget* parent);

    /// 从 data/story.json 加载所有场景到 storyScenes
    void loadStory();

    /// 按顺序播放一组场景 key，完成后回调 onFinished
    void showStory(const QStringList& keys, std::function<void()> onFinished = {});
    void showEndingTypewriter(std::function<void()> onFinished = {});
    /// 播放单个场景 key 的便捷方法
    void showStoryKey(const QString& key, std::function<void()> onFinished = {});

    bool isOverlayVisible() const;
    /// 查询 storyScenes 中是否存在指定 key（调用方用于判断是否触发剧情）
    bool hasScene(const QString& key) const;
    /// 返回 overlay 控件指针，供 MainWindow::eventFilter 判断点击来源
    QWidget* overlay() const;
    /// 窗口大小变化时由 MainWindow 调用，保持 overlay 铺满窗口
    void updateOverlayGeometry(const QRect& fullRect);

protected:
    /// 拦截 overlay 上的鼠标点击 → 推进故事
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct StoryLine
    {
        QString speaker;
        QString text;
        QString image;
    };

    struct EndingLine
    {
        QString speaker;
        QString text;
    };

    void buildStoryOverlay();
    void buildEndingOverlay();
    /// 故事状态机核心：推进到下一行 / 下一个场景 / 结束
    void nextStoryStep();
    /// 定时器回调：每 25ms 显示一个字符，实现打字机效果
    void tickStory();
    void nextEndingStep();
    void tickEnding();
    void renderEndingPage(bool completePage);
    QVector<QVector<EndingLine>> makeEndingPages() const;
    void applyStoryImage(const QString& relativePath);
    void refreshStoryImage();

    QWidget* parentWidget;

    QMap<QString, QVector<StoryLine>> storyScenes;
    QStringList pendingStoryKeys;
    std::function<void()> storyFinishedCallback;

    QWidget* storyOverlay = nullptr;
    QLabel* storyImage = nullptr;
    QPixmap storyPixmap;
    QLabel* storySpeaker = nullptr;
    QLabel* storyText = nullptr;
    QTimer storyTimer;
    QVector<StoryLine> activeStory;
    int storyLineIndex = 0;
    int storyCharIndex = 0;
    bool storyLineComplete = false;

    QWidget* endingOverlay = nullptr;
    QTextEdit* endingText = nullptr;
    QLabel* endingHint = nullptr;
    QTimer endingTimer;
    QVector<QVector<EndingLine>> endingPages;
    std::function<void()> endingFinishedCallback;
    int endingPageIndex = 0;
    int endingLineIndex = 0;
    int endingCharIndex = 0;
    bool endingPageComplete = false;
};
