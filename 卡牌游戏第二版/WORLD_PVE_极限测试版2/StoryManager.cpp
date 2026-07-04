#include "StoryManager.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPixmap>
#include <QVBoxLayout>

StoryManager::StoryManager(QWidget* parent)
    : QObject(parent)
    , parentWidget(parent)
{
    buildStoryOverlay();
}

/// [Recoleta37] Phase 2: 构建剧情 overlay（黑色半屏文字 + 打字机效果）
/// overlay 作为 MainWindow 的子控件，默认隐藏，播放时铺满窗口
void StoryManager::buildStoryOverlay()
{
    storyOverlay = new QWidget(parentWidget);
    storyOverlay->setStyleSheet("background:#050505;");
    storyOverlay->hide();
    storyOverlay->installEventFilter(this);

    QVBoxLayout* layout = new QVBoxLayout(storyOverlay);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    storyImage = new QLabel;
    storyImage->setAlignment(Qt::AlignCenter);
    storyImage->setScaledContents(false);
    storyImage->setStyleSheet("background:#d8c69a;");
    storyImage->setMinimumHeight(420);
    layout->addWidget(storyImage, 1);

    QWidget* textBox = new QWidget;
    textBox->setStyleSheet("background:#050505; border-top:1px solid #3d3227;");
    QVBoxLayout* textLayout = new QVBoxLayout(textBox);
    textLayout->setContentsMargins(36, 18, 36, 24);
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

bool StoryManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == storyOverlay && event->type() == QEvent::MouseButtonPress)
    {
        nextStoryStep();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

/// [Recoleta37] Phase 2: 从多个候选路径加载 story.json
/// JSON 格式: { "sceneKey": [{ "speaker": "...", "text": "..." }, ...], ... }
void StoryManager::loadStory()
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
        QVector<StoryLine> lines;
        for (const QJsonValue& value : root.value(key).toArray())
        {
            QJsonObject item = value.toObject();
            lines.push_back({ item.value("speaker").toString("Narrator"),
                              item.value("text").toString(),
                              item.value("image").toString() });
        }
        storyScenes[key] = lines;
    }
}

void StoryManager::applyStoryImage(const QString& relativePath)
{
    if (storyImage == nullptr) return;
    if (relativePath.isEmpty())
    {
        storyPixmap = QPixmap();
        storyImage->clear();
        storyImage->setText(QString());
        return;
    }

    const QStringList candidates = {
        QApplication::applicationDirPath() + "/" + relativePath,
        QApplication::applicationDirPath() + "/../" + relativePath,
        QApplication::applicationDirPath() + "/../../" + relativePath,
        QDir::currentPath() + "/" + relativePath,
        relativePath
    };
    QPixmap pixmap;
    for (const QString& path : candidates)
    {
        if (pixmap.load(path))
        {
            break;
        }
    }
    storyPixmap = pixmap;
    refreshStoryImage();
}

void StoryManager::refreshStoryImage()
{
    if (storyImage == nullptr) return;
    if (storyPixmap.isNull())
    {
        storyImage->clear();
        return;
    }
    const QSize target = storyImage->contentsRect().size();
    if (target.isEmpty())
    {
        storyImage->setPixmap(storyPixmap);
        return;
    }
    storyImage->setPixmap(storyPixmap.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void StoryManager::showStory(const QStringList& keys, std::function<void()> onFinished)
{
    pendingStoryKeys = keys;
    storyFinishedCallback = onFinished;
    nextStoryStep();
}

void StoryManager::showStoryKey(const QString& key, std::function<void()> onFinished)
{
    showStory(QStringList{key}, onFinished);
}

/// [Recoleta37] Phase 2: 故事状态机核心
/// 状态流转:
///   overlay 隐藏 → 显示 overlay
///   storyLineComplete==false → 跳过打字机，直接显示完整行
///   当前行未播完 → 跳过打字机，直接显示完整行
///   还有下一行 → 开始新行打字机
///   当前场景播完 && 队列非空 → 加载下一个场景
///   队列为空 → 隐藏 overlay，回调 onFinished
void StoryManager::nextStoryStep()
{
    if (!storyOverlay->isVisible())
    {
        storyOverlay->setGeometry(0, 0, parentWidget->width(), parentWidget->height());
        storyOverlay->raise();
        storyOverlay->show();
    }
    if (activeStory.isEmpty() || storyLineIndex >= activeStory.size())
    {
        if (!pendingStoryKeys.isEmpty())
        {
            QString key = pendingStoryKeys.takeFirst();
            activeStory = storyScenes.value(key);
            if (activeStory.isEmpty())
            {
                activeStory = {{QStringLiteral("Narrator"), QStringLiteral("故事片段缺失：") + key, QString()}};
            }
            storyLineIndex = 0;
            storyCharIndex = 0;
            storyLineComplete = false;
            storySpeaker->setText(activeStory[0].speaker);
            applyStoryImage(activeStory[0].image);
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
        storyCharIndex = activeStory.value(storyLineIndex).text.size();
        storyText->setText(activeStory.value(storyLineIndex).text);
        storyLineComplete = true;
        return;
    }
    ++storyLineIndex;
    if (storyLineIndex < activeStory.size())
    {
        storyCharIndex = 0;
        storyLineComplete = false;
        storySpeaker->setText(activeStory[storyLineIndex].speaker);
        applyStoryImage(activeStory[storyLineIndex].image);
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
            activeStory = {{QStringLiteral("Narrator"), QStringLiteral("故事片段缺失：") + key, QString()}};
        }
        storyLineIndex = 0;
        storyCharIndex = 0;
        storyLineComplete = false;
        storySpeaker->setText(activeStory[0].speaker);
        applyStoryImage(activeStory[0].image);
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

void StoryManager::tickStory()
{
    if (activeStory.isEmpty()) return;
    const QString full = activeStory[storyLineIndex].text;
    ++storyCharIndex;
    storyText->setText(full.left(storyCharIndex));
    if (storyCharIndex >= full.size())
    {
        storyLineComplete = true;
        storyTimer.stop();
    }
}

bool StoryManager::isOverlayVisible() const
{
    return storyOverlay != nullptr && storyOverlay->isVisible();
}

bool StoryManager::hasScene(const QString& key) const
{
    return storyScenes.contains(key);
}

QWidget* StoryManager::overlay() const
{
    return storyOverlay;
}

void StoryManager::updateOverlayGeometry(const QRect& fullRect)
{
    if (storyOverlay != nullptr)
    {
        storyOverlay->setGeometry(fullRect);
        if (storyOverlay->isVisible())
        {
            storyOverlay->raise();
        }
        refreshStoryImage();
    }
}
