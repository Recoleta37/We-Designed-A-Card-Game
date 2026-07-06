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
#include <QScrollBar>
#include <QVBoxLayout>

StoryManager::StoryManager(QWidget* parent)
    : QObject(parent)
    , parentWidget(parent)
{
    buildStoryOverlay();
    buildEndingOverlay();
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

void StoryManager::buildEndingOverlay()
{
    endingOverlay = new QWidget(parentWidget);
    endingOverlay->setStyleSheet("background:#ffffff;");
    endingOverlay->hide();
    endingOverlay->installEventFilter(this);

    QVBoxLayout* layout = new QVBoxLayout(endingOverlay);
    layout->setContentsMargins(44, 34, 44, 26);
    layout->setSpacing(12);

    endingText = new QTextEdit(endingOverlay);
    endingText->setReadOnly(true);
    endingText->setFrameShape(QFrame::NoFrame);
    endingText->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    endingText->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    endingText->setStyleSheet(QStringLiteral(
        "QTextEdit { background:#ffffff; color:#000000; border:none;"
        "font-family:'Microsoft YaHei UI','Microsoft YaHei','SimSun'; font-size:12px; }"));
    layout->addWidget(endingText, 1);

    endingHint = new QLabel(QStringLiteral("点击：跳过本页 / 下一页"), endingOverlay);
    endingHint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    endingHint->setStyleSheet(QStringLiteral(
        "background:#ffffff; color:#555555; font-size:13px;"));
    layout->addWidget(endingHint);

    connect(&endingTimer, &QTimer::timeout, this, [this]() { tickEnding(); });
}

bool StoryManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == storyOverlay && event->type() == QEvent::MouseButtonPress)
    {
        nextStoryStep();
        return true;
    }
    if (watched == endingOverlay && event->type() == QEvent::MouseButtonPress)
    {
        nextEndingStep();
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

void StoryManager::showEndingTypewriter(std::function<void()> onFinished)
{
    endingPages = makeEndingPages();
    endingFinishedCallback = onFinished;
    endingPageIndex = 0;
    endingLineIndex = 0;
    endingCharIndex = 0;
    endingPageComplete = false;

    endingOverlay->setGeometry(0, 0, parentWidget->width(), parentWidget->height());
    endingOverlay->raise();
    endingOverlay->show();
    renderEndingPage(false);
    endingTimer.start(12);
}

QVector<QVector<StoryManager::EndingLine>> StoryManager::makeEndingPages() const
{
    return {
        {
            {QString(), QStringLiteral("由开发者hhhh大概聊一下剧情脉络")},
            {QStringLiteral("hhhh"), QStringLiteral("终焉之地是一片存在大量能量的空间，在无数年前那里什么都没有，整个世界也只有终焉之地")},
            {QStringLiteral("hhhh"), QStringLiteral("之后在那里出现了一位纯真而永恒的生命体，不会死亡，也没有情感，不需要进食，也不需要繁衍，终焉之地的能力使得他变得全能，他利用能量创造了世界，他被称为真神")},
            {QStringLiteral("hhhh"), QStringLiteral("而之后出现的神明邪神觊觎真神的位置，他希望霸占终焉之地，但他无法击败处于终焉之地的邪神，于是他寄希望于进行时空穿越，而时空穿越本质是对世界的修改，而这需要祝福诅咒与真神邪神的四种组合（也就是游戏开头出现的关键词）")},
            {QStringLiteral("hhhh"), QStringLiteral("但邪神也无法击败大天使获得真正的真神祝福，这使得他的计划濒临破产")},
            {QStringLiteral("hhhh"), QStringLiteral("这时他发现，大天使米凯尔的妹妹居然爱上了魔王，并且诞生了孩子莱索恩，那么这个孩子意外地具备了邪神渴求的真神祝福")},
            {QStringLiteral("hhhh"), QStringLiteral("因此他为莱索恩赐予了邪神力量，并且让他代行自己的意志，从而凑齐了时空穿越的条件")},
            {QStringLiteral("hhhh"), QStringLiteral("邪神获得其他三个条件是很容易的，而玩家结局也是获得了四个条件，也就是“回到了一切的开始”")},
            {QStringLiteral("hhhh"), QStringLiteral("米凯尔从真神（也就是金色身影）获得了天使最强的六翼，因此击败她获得的要素为“真神祝福”")},
            {QStringLiteral("hhhh"), QStringLiteral("艾琳由于父亲挑战生命本源被真神（还是身影）诅咒成为吸血鬼，因此击败她获得的要素为“真神诅咒”")},
            {QStringLiteral("hhhh"), QStringLiteral("莱索恩被邪神赐予了力量，因此击败他获得的要素为“真神赐福”")},
            {QStringLiteral("hhhh"), QStringLiteral("这四个要素决定了世界复战主角选择的答案")},
            {QStringLiteral("hhhh"), QStringLiteral("顺带一提伯爵在被变成吸血鬼后也尝试时空穿越拯救儿子，莱索恩（注意莱索恩是魔王，邪神是邪神）派遣米格偷走了他的关键要素，因为邪神无法接受有人再次时空穿越")},
            {QStringLiteral("hhhh"), QStringLiteral("阿格尼接受了邪神名为永生的诅咒，因此击败他获得的要素为“邪神诅咒”")},
            {QStringLiteral("hhhh"), QStringLiteral("那么既然真神是全能的，那真神为什么不阻止邪神呢")},
            {QStringLiteral("hhhh"), QStringLiteral("答案是，真神向往着有限生命才能拥有的东西，哪怕拥有一点或者曾经体验也行，于是真神接受被邪神击败，他的力量残存世间，指引着某些人进行旅程，凑齐时空穿越的要素取代邪神，而第一位完成的人将被定义为真神，他将再次创世，而这个真神已经拥有过有限生命才能拥有的东西了，而完成这趟旅程的就是主角")},
            {QStringLiteral("hhhh"), QStringLiteral("因此主角没有名字")},
            {QStringLiteral("hhhh"), QStringLiteral("而有限生命才能拥有的东西，正是最后谜题的答案")},
            {QStringLiteral("hhhh"), QStringLiteral("世界有太多永恒的事情和永恒的种族，这来自于魔法，魔法是真神永恒的体现，也就是魔法让我们永恒")},
            {QStringLiteral("hhhh"), QStringLiteral("那什么让我们特别是人类无法永恒，甚至放弃永恒呢，这也就是真神渴求的有限生命才能拥有的东西，纵观整个故事，我们体会到的可能不同，爱、情感，关怀、生命甚至是有限性本身")},
            {QStringLiteral("hhhh"), QStringLiteral("所以如果那个原初的生命体被称作真神，那么真神确实是死了，真神迎来了终焉")},
            {QStringLiteral("hhhh"), QStringLiteral("而如果主角也是真神，那么真神也没死，但莱索恩也说过了，终焉就是开始，而真神也迎来了开始")},
            {QStringLiteral("hhhh"), QStringLiteral("而这，就是题目的来源")}
        },
        {
            {QStringLiteral("hhhh"), QStringLiteral("我是个没接触过什么代码的人，真正意义上的熟练使用也许才一年")},
            {QStringLiteral("hhhh"), QStringLiteral("但我是个很喜欢讲故事的人，上面就是我很喜欢，也精心雕琢了很久的故事，久到比我熟练使用代码的时间还长")},
            {QStringLiteral("hhhh"), QStringLiteral("所以自私一点说，这个游戏对于我就是讲一个我爱故事")},
            {QStringLiteral("hhhh"), QStringLiteral("因此开发的所有困难，对我都是值得的")},
            {QStringLiteral("hhhh"), QStringLiteral("也许报告里需要我报告开发过程的感想，但这里我只想说")},
            {QStringLiteral("hhhh"), QStringLiteral("很开心能讲一个很好的故事")},
            {QStringLiteral("hhhh"), QStringLiteral("也很感谢主角您，能听我讲完一个故事")}
        },
        {
            {QStringLiteral("Recoleta37"), QStringLiteral("在梦中，我走进一个故事。")},
            {QStringLiteral("Recoleta37"), QStringLiteral("那是一方生机盎然的天地。")},
            {QStringLiteral("Recoleta37"), QStringLiteral("顺着无尽的木栈，走向分支的深处。")},
            {QStringLiteral("Recoleta37"), QStringLiteral("我辨识万物色彩的多态，我见证思绪灵光的继承。")},
            {QStringLiteral("Recoleta37"), QStringLiteral("我乘坐重载马车，驶进森林的闭包。")},
            {QStringLiteral("Recoleta37"), QStringLiteral("坐在一棵虚函树下，让自己静态变凉。")},
            {QStringLiteral("Recoleta37"), QStringLiteral("我拾起故事的碎片，")},
            {QStringLiteral("Recoleta37"), QStringLiteral("随心所欲地组合，")},
            {QStringLiteral("Recoleta37"), QStringLiteral("拼成一个又一个世界。")},
            {QStringLiteral("Recoleta37"), QStringLiteral("最后，在析构之前，")},
            {QStringLiteral("Recoleta37"), QStringLiteral("故事终于算出了返回值：")},
            {QStringLiteral("Recoleta37"), QStringLiteral("一个指针")},
            {QStringLiteral("Recoleta37"), QStringLiteral("指向")},
            {QStringLiteral("Recoleta37"), QStringLiteral("......")},
            {QStringLiteral("Recoleta37"), QStringLiteral("故事的起点。")}
        }
    };
}

void StoryManager::renderEndingPage(bool completePage)
{
    if (!endingText || endingPages.isEmpty() || endingPageIndex >= endingPages.size()) return;

    const QVector<EndingLine>& page = endingPages[endingPageIndex];
    QString html = QStringLiteral(
        "<html><head><style>"
        "body{margin:0;background:#fff;color:#000;font-family:'Microsoft YaHei UI','Microsoft YaHei','SimSun';font-size:12px;line-height:1.12;}"
        "table{width:100%;border-collapse:collapse;table-layout:fixed;}"
        "td{vertical-align:top;padding:1px 0;}"
        ".speaker{width:104px;font-weight:700;white-space:nowrap;padding-right:16px;}"
        ".text{word-wrap:break-word;}"
        ".intro{font-weight:700;font-size:13px;padding-bottom:7px;}"
        "</style></head><body><table>");

    const int maxLine = completePage ? page.size() : qMin(endingLineIndex + 1, page.size());
    for (int i = 0; i < maxLine; ++i)
    {
        QString text = page[i].text;
        if (!completePage && i == endingLineIndex)
        {
            text = text.left(endingCharIndex);
        }
        const QString escapedText = text.toHtmlEscaped();
        if (page[i].speaker.isEmpty())
        {
            html += QStringLiteral("<tr><td class='intro' colspan='2'>%1</td></tr>").arg(escapedText);
        }
        else
        {
            html += QStringLiteral("<tr><td class='speaker'>%1</td><td class='text'>%2</td></tr>")
                        .arg(page[i].speaker.toHtmlEscaped(), escapedText);
        }
    }
    html += QStringLiteral("</table></body></html>");
    endingText->setHtml(html);
    endingText->verticalScrollBar()->setValue(endingText->verticalScrollBar()->maximum());
}

void StoryManager::tickEnding()
{
    if (endingPages.isEmpty() || endingPageIndex >= endingPages.size())
    {
        endingTimer.stop();
        return;
    }

    const QVector<EndingLine>& page = endingPages[endingPageIndex];
    if (endingLineIndex >= page.size())
    {
        endingPageComplete = true;
        endingTimer.stop();
        renderEndingPage(true);
        return;
    }

    ++endingCharIndex;
    if (endingCharIndex > page[endingLineIndex].text.size())
    {
        ++endingLineIndex;
        endingCharIndex = 0;
    }

    if (endingLineIndex >= page.size())
    {
        endingPageComplete = true;
        endingTimer.stop();
        renderEndingPage(true);
        return;
    }

    renderEndingPage(false);
}

void StoryManager::nextEndingStep()
{
    if (endingPages.isEmpty()) return;

    if (!endingPageComplete)
    {
        endingPageComplete = true;
        endingTimer.stop();
        renderEndingPage(true);
        return;
    }

    ++endingPageIndex;
    if (endingPageIndex >= endingPages.size())
    {
        endingTimer.stop();
        endingOverlay->hide();
        auto callback = endingFinishedCallback;
        endingFinishedCallback = {};
        if (callback) callback();
        return;
    }

    endingLineIndex = 0;
    endingCharIndex = 0;
    endingPageComplete = false;
    renderEndingPage(false);
    endingTimer.start(12);
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
    return (storyOverlay != nullptr && storyOverlay->isVisible())
        || (endingOverlay != nullptr && endingOverlay->isVisible());
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
    if (endingOverlay != nullptr)
    {
        endingOverlay->setGeometry(fullRect);
        if (endingOverlay->isVisible())
        {
            endingOverlay->raise();
        }
    }
}
