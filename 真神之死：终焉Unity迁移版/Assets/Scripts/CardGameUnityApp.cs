using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.Networking;
using UnityEngine.UI;

public sealed class CardGameUnityApp : MonoBehaviour
{
    private const int HeroSlot = 3;
    private const int MaxOrdinaryRelics = 7;
    private Sprite softCircleSprite;
    private Sprite sparkSprite;
    private Sprite bgmOnSprite;
    private Sprite bgmOffSprite;

    private sealed class Unit
    {
        public string Name;
        public string Faction;
        public string Row;
        public string Skill;
        public int MaxHp;
        public int Hp;
        public int Atk;
        public int Shield;
        public bool Enemy;
        public bool Hero;
        public bool Boss;
        public int AliveRounds;
        public int Vulnerable;
        public int Petrify;
        public int Thorn;
        public int Echo;
        public int Forge;
        public bool ProtectedDeath;
        public bool Revived;

        public Unit(string name, string faction, string row, string skill, int hp, int atk, bool enemy = false, bool hero = false, bool boss = false)
        {
            Name = name;
            Faction = faction;
            Row = row;
            Skill = skill;
            MaxHp = hp;
            Hp = hp;
            Atk = atk;
            Enemy = enemy;
            Hero = hero;
            Boss = boss;
        }

        public Unit Fresh(bool enemy = false, bool hero = false, bool boss = false)
        {
            return new Unit(Name, Faction, Row, Skill, MaxHp, Atk, enemy, hero, boss);
        }
    }

    private sealed class Chapter
    {
        public string Title;
        public string[] Enemies;
        public string Elite4;
        public string Boss5;
        public string Elite9;
        public string Boss10;
        public string Relic;
        public string Ally;

        public Chapter(string title, string[] enemies, string elite4, string boss5, string elite9, string boss10, string relic = "", string ally = "")
        {
            Title = title;
            Enemies = enemies;
            Elite4 = elite4;
            Boss5 = boss5;
            Elite9 = elite9;
            Boss10 = boss10;
            Relic = relic;
            Ally = ally;
        }
    }

    private sealed class StoryLine
    {
        public string Speaker;
        public string Text;
        public string Image;
    }

    private sealed class EndingLine
    {
        public string Speaker;
        public string Text;

        public EndingLine(string speaker, string text)
        {
            Speaker = speaker;
            Text = text;
        }
    }

    private struct AttackVisualRequest
    {
        public Unit Source;
        public Unit Target;
        public string Reason;
        public Action OnImpact;

        public AttackVisualRequest(Unit source, Unit target, string reason, Action onImpact)
        {
            Source = source;
            Target = target;
            Reason = reason;
            OnImpact = onImpact;
        }
    }

    private Canvas canvas;
    private Font font;
    private readonly List<string> logs = new List<string>();
    private readonly List<Unit> roster = new List<Unit>();
    private readonly List<string> relics = new List<string>();
    private readonly Dictionary<string, int> relicUses = new Dictionary<string, int>();
    private readonly List<string> skillSlots = new List<string>();
    private readonly HashSet<int> selectedSkillIndexes = new HashSet<int>();
    private readonly List<string> pendingDeadRows = new List<string>();
    private readonly List<string> pendingRefillRows = new List<string>();
    private readonly HashSet<string> shownStories = new HashSet<string>();
    private readonly Dictionary<string, Sprite> spriteCache = new Dictionary<string, Sprite>();
    private readonly Dictionary<string, Texture2D> textureCache = new Dictionary<string, Texture2D>();
    private readonly Dictionary<Unit, RectTransform> unitCardRects = new Dictionary<Unit, RectTransform>();
    private readonly Queue<AttackVisualRequest> attackVisualQueue = new Queue<AttackVisualRequest>();
    private readonly Unit[] playerBoard = new Unit[5];
    private readonly Unit[] enemyBoard = new Unit[5];
    private Coroutine activeTypewriter;
    private Coroutine bgmCoroutine;
    private AudioSource bgmSource;
    private Text activeTypewriterText;
    private string activeTypewriterFullText = "";
    private string currentBgmKey = "";
    private bool activeTypewriterDone = true;
    private bool bgmMuted;
    private Chapter[] chapters;
    private string storyJson = "";
    private int chapterIndex;
    private int levelIndex;
    private int round = 1;
    private int gold = 20;
    private int heroHpGrowth;
    private int fateBladeHealBonus;
    private int heroPermanentAtkBonus;
    private int heroPermanentHpBonus;
    private int goldAltarPurchases;
    private int skillsUsedThisTurn;
    private bool inBattle;
    private bool victoryPending;
    private bool actionAnimating;
    private bool processingAttackVisuals;
    private int pendingFinishedChapter;
    private int pendingFinishedLevel;
    private string pendingAllyStoryKey = "";

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    private static void Bootstrap()
    {
        if (FindObjectOfType<CardGameUnityApp>() != null) return;
        var go = new GameObject("CardGameUnityApp");
        DontDestroyOnLoad(go);
        go.AddComponent<CardGameUnityApp>();
    }

    private void Awake()
    {
        Application.targetFrameRate = 60;
        font = Resources.GetBuiltinResource<Font>("Arial.ttf");
        BuildEffectSprites();
        EnsureBgmSource();
        EnsureEventSystem();
        InitData();
        LoadStoryJson();
        ShowTitle();
    }

    private void EnsureEventSystem()
    {
        if (FindObjectOfType<EventSystem>() != null) return;
        var eventSystem = new GameObject("EventSystem");
        eventSystem.AddComponent<EventSystem>();
        eventSystem.AddComponent<StandaloneInputModule>();
        DontDestroyOnLoad(eventSystem);
    }

    private void EnsureBgmSource()
    {
        if (bgmSource != null) return;
        bgmSource = GetComponent<AudioSource>();
        if (bgmSource == null) bgmSource = gameObject.AddComponent<AudioSource>();
        bgmSource.playOnAwake = false;
        bgmSource.loop = true;
        bgmSource.volume = 0.34f;
        bgmSource.spatialBlend = 0f;
    }

    private string ChapterBgmKey()
    {
        return "chapter_" + Mathf.Clamp(chapterIndex, 0, 8);
    }

    private string BattleBgmKey()
    {
        int chapter = Mathf.Clamp(chapterIndex, 0, 8);
        bool finalWorldBoss = chapter == 8 && (levelIndex == 2 || levelIndex >= 9);
        bool chapterFinalBoss = levelIndex == 10;
        return (chapterFinalBoss || finalWorldBoss) ? "boss_" + chapter : "chapter_" + chapter;
    }

    private void PlayBgm(string key)
    {
        EnsureBgmSource();
        if (string.IsNullOrEmpty(key)) return;
        if (currentBgmKey == key && bgmSource.clip != null && bgmSource.isPlaying) return;
        currentBgmKey = key;
        if (bgmCoroutine != null) StopCoroutine(bgmCoroutine);
        bgmCoroutine = StartCoroutine(LoadAndPlayBgm(key));
    }

    private void StopBgm()
    {
        currentBgmKey = "";
        if (bgmCoroutine != null)
        {
            StopCoroutine(bgmCoroutine);
            bgmCoroutine = null;
        }
        if (bgmSource != null) bgmSource.Stop();
    }

    private IEnumerator LoadAndPlayBgm(string key)
    {
        string mp3 = Path.Combine(Application.streamingAssetsPath, "audio", "bgm", key + ".mp3");
        string wav = Path.Combine(Application.streamingAssetsPath, "audio", "bgm", key + ".wav");
        string file = File.Exists(mp3) ? mp3 : File.Exists(wav) ? wav : "";
        if (string.IsNullOrEmpty(file))
        {
            if (bgmSource != null) bgmSource.Stop();
            yield break;
        }

        string uri = new Uri(file).AbsoluteUri;
        AudioType type = file.EndsWith(".mp3", StringComparison.OrdinalIgnoreCase) ? AudioType.MPEG : AudioType.WAV;
        using (UnityWebRequest request = UnityWebRequestMultimedia.GetAudioClip(uri, type))
        {
            yield return request.SendWebRequest();
            if (request.result != UnityWebRequest.Result.Success) yield break;
            AudioClip clip = DownloadHandlerAudioClip.GetContent(request);
            if (clip == null || currentBgmKey != key) yield break;
            bgmSource.clip = clip;
            bgmSource.loop = true;
            if (!bgmMuted) bgmSource.Play();
        }
        bgmCoroutine = null;
    }

    private void ToggleBgmMuted()
    {
        bgmMuted = !bgmMuted;
        EnsureBgmSource();
        if (bgmMuted)
        {
            bgmSource.Pause();
        }
        else if (bgmSource.clip != null)
        {
            bgmSource.Play();
        }
        RenderBgmToggle();
    }

    private void InitData()
    {
        chapters = new[]
        {
            new Chapter("序章：梦境",
                new[] {"梦影", "异变魔影", "破碎信徒"},
                "梦境守卫", "真神残响", "梦境裂隙", "魔王？？？"),
            new Chapter("第一章：微风如诗",
                new[] {"野狼", "山贼", "森林弓手", "流浪剑士"},
                "山贼头目", "迷失骑士", "王城禁卫", "阿拉贡", "", "阿拉贡"),
            new Chapter("第二章：猩红平原",
                new[] {"血仆", "夜蝠", "吸血鬼侍从", "猩红猎犬"},
                "血骑士", "吸血鬼伯爵", "远古守卫", "偷窃者米格"),
            new Chapter("第三章：赤心跃动",
                new[] {"血术师", "夜行者", "血卫", "城堡守卫"},
                "赤心骑士", "伯爵残影", "王座守卫", "艾琳", "跃动赤心", "游侠"),
            new Chapter("第四章：精灵圣地",
                new[] {"精灵射手", "古木守卫", "毒叶法师", "迷途精灵"},
                "古树长老", "圣地看守者", "精灵祭司", "阿格尼"),
            new Chapter("第五章：撒冷",
                new[] {"圣光侍从", "审判者", "守护天使", "圣河少女"},
                "六翼候补", "天使裁决官", "圣河守门人", "米凯尔", "", "加百列"),
            new Chapter("第六章：魔境",
                new[] {"小恶魔", "魔族战士", "火焰术士", "深渊刺客"},
                "魔境猎手", "深渊领主", "旧王亲卫", "伊维尔"),
            new Chapter("第七章：终焉",
                new[] {"终焉魔兵", "虚空术士", "异界刺客", "黑翼守卫"},
                "终焉看门人", "伊维尔残影", "邪神使徒", "莱索恩", "邪神赐福"),
            new Chapter("第八章：世界",
                new[] {"世界回声", "旧日生命", "白色梦境"},
                "永恒残响", "米凯尔与阿格尼", "世界门扉", "最终问题")
        };
    }

    private void ResetRun()
    {
        chapterIndex = 0;
        levelIndex = 1;
        round = 1;
        gold = 20;
        heroHpGrowth = 0;
        fateBladeHealBonus = 0;
        heroPermanentAtkBonus = 0;
        heroPermanentHpBonus = 0;
        goldAltarPurchases = 0;
        skillsUsedThisTurn = 0;
        inBattle = false;
        victoryPending = false;
        actionAnimating = false;
        processingAttackVisuals = false;
        attackVisualQueue.Clear();
        pendingAllyStoryKey = "";
        logs.Clear();
        shownStories.Clear();
        roster.Clear();
        relics.Clear();
        relicUses.Clear();
        skillSlots.Clear();
        selectedSkillIndexes.Clear();
        pendingDeadRows.Clear();
        pendingRefillRows.Clear();
        Array.Clear(playerBoard, 0, playerBoard.Length);
        Array.Clear(enemyBoard, 0, enemyBoard.Length);

        roster.Add(new Unit("阿拉贡", "人族", "前排", "鼓舞", 70, 10));
        roster.Add(new Unit("见习剑士", "人族", "前排", "斩击", 42, 7));
        roster.Add(new Unit("弓箭手", "人族", "后排", "箭雨", 28, 11));
        roster.Add(new Unit("牧师", "人族", "中排", "治疗术", 32, 5));
        AddLog("基础战斗、补员与遗物规则已启用。");
    }

    private void ShowTitle()
    {
        StopBgm();
        ClearCanvas();
        var bg = Panel(canvas.transform, "Title", Color.black);
        Stretch(bg);
        Picture(bg.transform, "assets/story/prologue/title.png", new Vector2(0f, 0f), new Vector2(1f, 1f), true);
        var veil = Panel(bg.transform, "TitleVeil", new Color32(0, 0, 0, 35));
        Stretch(veil);
        veil.GetComponent<Image>().raycastTarget = false;
        Text(bg.transform, "真神之死：终焉", 52, FontStyle.Bold, Color.white, new Vector2(0.10f, 0.66f), new Vector2(0.90f, 0.80f), TextAnchor.MiddleCenter);
        Text(bg.transform, "真神  祝福  诅咒  邪神", 20, FontStyle.Bold, new Color32(234, 208, 142, 255), new Vector2(0.25f, 0.58f), new Vector2(0.75f, 0.65f), TextAnchor.MiddleCenter);
        Text(bg.transform, "WORLD Alpha 0.9 PVE", 15, FontStyle.Normal, new Color32(230, 225, 210, 255), new Vector2(0.25f, 0.53f), new Vector2(0.75f, 0.58f), TextAnchor.MiddleCenter);
        Button(bg.transform, "开始游戏", new Vector2(0.42f, 0.25f), new Vector2(0.58f, 0.33f), () =>
        {
            ResetRun();
            ShowStoryKey("prologue", () => ShowMap(StartBattle));
        });
    }

    private void ShowMap(Action after)
    {
        PlayBgm(ChapterBgmKey());
        ClearCanvas();
        var bg = Panel(canvas.transform, "Map", new Color32(35, 28, 20, 255));
        Stretch(bg);
        Picture(bg.transform, "assets/maps/world_map_fantasy_wide_ui_v1.png", new Vector2(0.02f, 0.05f), new Vector2(0.98f, 0.96f), true);
        var veil = Panel(bg.transform, "MapVeil", new Color32(0, 0, 0, 35));
        Stretch(veil);
        veil.GetComponent<Image>().raycastTarget = false;
        Text(bg.transform, chapters[Mathf.Clamp(chapterIndex, 0, chapters.Length - 1)].Title, 28, FontStyle.Bold, new Color32(255, 238, 185, 255), new Vector2(0.06f, 0.88f), new Vector2(0.50f, 0.95f), TextAnchor.MiddleLeft);
        int currentPoint = MapPointForChapter(chapterIndex);
        var litPoints = new HashSet<int>();
        for (int i = 0; i <= chapterIndex; ++i) litPoints.Add(MapPointForChapter(i));
        for (int i = 0; i < 10; ++i)
        {
            bool outsideMap = i == 9;
            bool active = i == currentPoint;
            bool lit = litPoints.Contains(i);
            if (outsideMap && !lit && !active) continue;

            Vector2 p = MapDisplayPosition(i);
            Vector2 outerHalf = active ? new Vector2(0.0068f, 0.0113f) : new Vector2(0.0055f, 0.0092f);
            var outer = Panel(bg.transform, "MapNodeOuter", active ? new Color32(32, 24, 14, 255) : new Color32(12, 10, 8, 245));
            Anchor(outer, p - outerHalf, p + outerHalf);
            var outerImage = outer.GetComponent<Image>();
            outerImage.preserveAspect = false;
            outerImage.raycastTarget = false;
            AddOutline(outer, active ? new Color32(255, 244, 194, 245) : new Color32(235, 206, 145, 210), new Vector2(1f, -1f));

            var inner = Panel(bg.transform, "MapNodeInner", active ? new Color32(255, 255, 255, 0) : lit ? new Color32(255, 255, 255, 235) : new Color32(45, 34, 22, 210));
            Anchor(inner, p - outerHalf * 0.58f, p + outerHalf * 0.58f);
            var innerImage = inner.GetComponent<Image>();
            innerImage.preserveAspect = false;
            innerImage.raycastTarget = false;
            AddOutline(inner, new Color32(40, 30, 18, 120), new Vector2(0.5f, -0.5f));
            if (active) StartCoroutine(FadeMapNode(innerImage, new Color32(255, 255, 255, 255)));
        }
        Text(bg.transform, "点击任意位置继续", 16, FontStyle.Bold, new Color32(255, 238, 190, 255), new Vector2(0.40f, 0.06f), new Vector2(0.60f, 0.12f), TextAnchor.MiddleCenter);
        TransparentButton(bg.transform, new Vector2(0f, 0f), new Vector2(1f, 1f), after);
    }

    private int MapPointForChapter(int index)
    {
        int[] chapterPoints = { 0, 1, 2, 3, 4, 5, 6, 7, 9 };
        if (index >= 0 && index < chapterPoints.Length) return chapterPoints[index];
        return 9;
    }

    private Vector2 MapDisplayPosition(int pointIndex)
    {
        Vector2[] qtTopLeftRatios =
        {
            new Vector2(0.190f, 0.690f),
            new Vector2(0.255f, 0.515f),
            new Vector2(0.352f, 0.475f),
            new Vector2(0.318f, 0.388f),
            new Vector2(0.364f, 0.263f),
            new Vector2(0.530f, 0.407f),
            new Vector2(0.848f, 0.402f),
            new Vector2(0.790f, 0.522f),
            new Vector2(0.095f, 0.474f),
            new Vector2(0.500f, 0.898f)
        };
        int safe = Mathf.Clamp(pointIndex, 0, qtTopLeftRatios.Length - 1);
        Vector2 ratio = qtTopLeftRatios[safe];
        Vector2 imageMin = new Vector2(0.02f, 0.05f);
        Vector2 imageMax = new Vector2(0.98f, 0.96f);
        return new Vector2(
            Mathf.Lerp(imageMin.x, imageMax.x, ratio.x),
            Mathf.Lerp(imageMin.y, imageMax.y, 1f - ratio.y));
    }

    private IEnumerator FadeMapNode(Image image, Color target)
    {
        if (image == null) yield break;
        for (float t = 0f; t < 1.2f; t += Time.deltaTime)
        {
            if (image == null) yield break;
            float k = Mathf.SmoothStep(0f, 1f, t / 1.2f);
            image.color = new Color(target.r, target.g, target.b, k);
            yield return null;
        }
        if (image != null) image.color = target;
    }

    private void StartBattle()
    {
        if (chapterIndex == chapters.Length - 1 && TryShowWorldEvent()) return;
        PlayBgm(BattleBgmKey());
        inBattle = true;
        victoryPending = false;
        actionAnimating = false;
        processingAttackVisuals = false;
        attackVisualQueue.Clear();
        pendingAllyStoryKey = "";
        round = 1;
        skillsUsedThisTurn = 0;
        skillSlots.Clear();
        selectedSkillIndexes.Clear();
        pendingDeadRows.Clear();
        Array.Clear(enemyBoard, 0, enemyBoard.Length);

        PreparePlayerForBattle();
        FillEnemies();
        GenerateSkills(true);
        ApplyRelicsAtBattleStart();
        ApplyRelicsPerRound();
        AddLog("进入战斗：" + chapters[chapterIndex].Title + " 关卡 " + levelIndex + "/10");
        string storyKey = BattleStoryKey();
        if (!shownStories.Contains(storyKey) && HasStoryKey(storyKey))
        {
            shownStories.Add(storyKey);
            ShowStoryKey(storyKey, ContinueBattleStart);
            return;
        }
        ContinueBattleStart();
    }

    private void ContinueBattleStart()
    {
        bool goldAltarLevel = levelIndex == 10 || (chapterIndex == chapters.Length - 1 && levelIndex == 2);
        if (goldAltarLevel && HasRelic("黄金祭坛"))
        {
            ShowGoldAltar();
            return;
        }
        RenderGame();
    }

    private bool TryShowWorldEvent()
    {
        if (levelIndex == 1)
        {
            ClearCanvas();
            var bg = ScreenBackdrop("WorldQuestion");
            Stretch(bg);
            var panel = CenterPanel(bg.transform, "QuestionPanel", new Vector2(0.24f, 0.30f), new Vector2(0.76f, 0.72f));
            Text(panel.transform, "世界：第一题", 28, FontStyle.Bold, new Color32(45, 24, 10, 255), new Vector2(0.08f, 0.72f), new Vector2(0.92f, 0.88f), TextAnchor.MiddleCenter);
            Text(panel.transform, "选择两名 Boss。\n正确答案：米凯尔、阿格尼。\n无论答案如何，世界继续向前。", 20, FontStyle.Normal, new Color32(55, 34, 16, 255), new Vector2(0.10f, 0.36f), new Vector2(0.90f, 0.66f), TextAnchor.MiddleCenter);
            Button(panel.transform, "米凯尔 与 阿格尼", new Vector2(0.30f, 0.14f), new Vector2(0.70f, 0.28f), () =>
            {
                AddLog("第一题结束。无论答案如何，世界继续向前。");
                levelIndex = 2;
                StartBattle();
            });
            return true;
        }
        if (levelIndex == 3)
        {
            TryFuseWorld();
            ++levelIndex;
        }
        if (levelIndex == 9)
        {
            ShowFinalQuestion();
            return true;
        }
        return false;
    }

    private void ShowFinalQuestion()
    {
        PlayBgm("boss_8");
        ClearCanvas();
        var bg = ScreenBackdrop("FinalQuestion");
        Stretch(bg);
        var panel = CenterPanel(bg.transform, "FinalQuestionPanel", new Vector2(0.24f, 0.30f), new Vector2(0.76f, 0.72f));
        Text(panel.transform, "最终问题", 30, FontStyle.Bold, new Color32(45, 24, 10, 255), new Vector2(0.10f, 0.72f), new Vector2(0.90f, 0.88f), TextAnchor.MiddleCenter);
        Text(panel.transform, "什么让我们不再永恒。\n这里先用简化确认推进，答案会进入最后的打字机真相。", 21, FontStyle.Normal, new Color32(55, 34, 16, 255), new Vector2(0.10f, 0.38f), new Vector2(0.90f, 0.66f), TextAnchor.MiddleCenter);
        Button(panel.transform, "我已经明白", new Vector2(0.35f, 0.14f), new Vector2(0.65f, 0.28f), () =>
        {
            AddLog("最终问题通过。");
            ShowVictoryStories(chapterIndex, levelIndex, "", () => StartCoroutine(EndingTypewriter()));
        });
    }

    private string BattleStoryKey()
    {
        if (chapterIndex == chapters.Length - 1 && levelIndex == 2) return "boss_米凯尔与阿格尼";
        if (levelIndex == 5 || levelIndex == 10)
        {
            Unit boss = Alive(enemyBoard).FirstOrDefault(u => u.Boss);
            if (boss != null) return "boss_" + boss.Name;
        }
        return "c" + chapterIndex + "_l" + levelIndex;
    }

    private void DeployHero()
    {
        Unit template = HeroTemplate();
        playerBoard[HeroSlot] = template.Fresh(false, true);
    }

    private Unit HeroTemplate()
    {
        int[] hp = { 30, 30, 45, 60, 80, 110, 140, 180, 250 };
        int[] atk = { 5, 5, 8, 12, 16, 24, 35, 50, 70 };
        int index = Mathf.Min(chapterIndex, hp.Length - 1);
        int bonus = chapterIndex * 2;
        int worldHp = HasRelic("世界") ? 100 : 0;
        int worldAtk = HasRelic("世界") ? 50 : 0;
        return new Unit("主角", "命运", "中排", "命运之刃",
            Mathf.Max(1, hp[index] + bonus + worldHp + heroHpGrowth + heroPermanentHpBonus),
            atk[index] + bonus + worldAtk + heroPermanentAtkBonus);
    }

    private void PreparePlayerForBattle()
    {
        bool hasInheritedUnit = playerBoard.Any(u => u != null && u.Hp > 0);
        if (!hasInheritedUnit)
        {
            Array.Clear(playerBoard, 0, playerBoard.Length);
            AutoDeployPlayer();
            return;
        }

        for (int i = 0; i < playerBoard.Length; ++i)
        {
            Unit unit = playerBoard[i];
            if (unit == null) continue;
            if (unit.Hp <= 0)
            {
                playerBoard[i] = null;
                continue;
            }

            int inheritedHp = unit.Hp;
            bool inheritedHero = unit.Hero;
            Unit freshBase = inheritedHero ? HeroTemplate() : TemplateByName(unit.Name);
            if (freshBase != null)
            {
                unit.Faction = freshBase.Faction;
                unit.Row = freshBase.Row;
                unit.Skill = freshBase.Skill;
                unit.MaxHp = freshBase.MaxHp;
                unit.Atk = freshBase.Atk;
            }
            unit.Hp = Mathf.Clamp(inheritedHp, 1, unit.MaxHp);
            unit.Enemy = false;
            unit.Hero = inheritedHero;
            unit.Boss = false;
            unit.Shield = 0;
            unit.AliveRounds = 0;
            unit.Vulnerable = 0;
            unit.Petrify = 0;
            unit.Thorn = 0;
            unit.Echo = 0;
            unit.Forge = 0;
            unit.ProtectedDeath = false;
            unit.Revived = false;
        }
        NormalizeHeroSlot();
    }

    private void AutoDeployPlayer()
    {
        DeployHero();
        foreach (Unit template in roster.OrderByDescending(u => u.Atk).ThenByDescending(u => u.MaxHp).ToArray())
        {
            int slot = SlotForRow(template.Row, playerBoard);
            if (slot < 0) continue;
            if (playerBoard[slot] != null) continue;
            playerBoard[slot] = template.Fresh();
        }
    }

    private void FillEnemies()
    {
        Chapter chapter = chapters[chapterIndex];
        if (chapterIndex == chapters.Length - 1 && levelIndex == 2)
        {
            enemyBoard[2] = MakeEnemy("米凯尔", true);
            enemyBoard[3] = MakeEnemy("阿格尼", true);
            AddLog("世界复战：米凯尔与阿格尼同时出现。");
            return;
        }

        string mainName = EnemyNameForLevel(chapter, levelIndex);
        bool bossLevel = levelIndex == 5 || levelIndex == 10 || IsMechanicBossName(mainName);
        Unit main = MakeEnemy(mainName, bossLevel);
        if (!bossLevel && chapterIndex >= 3)
        {
            main.MaxHp += chapterIndex * 3 + levelIndex * 2;
            main.Hp = main.MaxHp;
            main.Atk += chapterIndex / 2;
        }
        if (!bossLevel) RandomizeEnemyStats(main);
        int mainSlot = SlotForRow(main.Row, enemyBoard);
        if (mainSlot >= 0) enemyBoard[mainSlot] = main;

        int roll = UnityEngine.Random.Range(0, 10);
        int count = levelIndex == 10 ? 4 : roll < 3 ? 3 : roll < 8 ? 4 : 5;
        for (int i = 1; i < count; ++i)
        {
            Unit extra = MakeEnemy(chapter.Enemies[(levelIndex + i) % chapter.Enemies.Length], false);
            if (levelIndex != 5 && levelIndex != 10 && chapterIndex >= 3)
            {
                extra.MaxHp += chapterIndex * 3 + levelIndex * 2;
                extra.Hp = extra.MaxHp;
                extra.Atk += chapterIndex / 2;
            }
            RandomizeEnemyStats(extra);
            int slot = SlotForRow(extra.Row, enemyBoard);
            if (slot >= 0) enemyBoard[slot] = extra;
        }
    }

    private void RandomizeEnemyStats(Unit unit)
    {
        int hpDelta = UnityEngine.Random.Range(-3, 5);
        int atkDelta = UnityEngine.Random.Range(0, 2);
        unit.MaxHp = Mathf.Max(1, unit.MaxHp + hpDelta);
        unit.Hp = unit.MaxHp;
        unit.Atk = Mathf.Max(0, unit.Atk + atkDelta);
    }

    private string EnemyNameForLevel(Chapter chapter, int level)
    {
        if (level == 4) return chapter.Elite4;
        if (level == 5) return chapter.Boss5;
        if (level == 9) return chapter.Elite9;
        if (level == 10) return chapter.Boss10;
        return chapter.Enemies[(level + chapterIndex) % chapter.Enemies.Length];
    }

    private bool IsMechanicBossName(string name)
    {
        return name == "魔王？？？" || name == "阿拉贡" || name == "偷窃者米格" || name == "艾琳" ||
               name == "阿格尼" || name == "米凯尔" || name == "伊维尔" || name == "莱索恩" ||
               name == "米凯尔与阿格尼";
    }

    private Unit MakeEnemy(string name, bool boss)
    {
        int scale = chapterIndex * 8 + levelIndex * 3;
        int hp = (boss ? 90 : 34) + scale * (boss ? 3 : 1);
        int atk = (boss ? 12 : 5) + chapterIndex * 3 + levelIndex;
        string row = boss ? "中排" : "前排";
        if (name.Contains("弓") || name.Contains("术") || name.Contains("刺")) row = "后排";
        if (name == "偷窃者米格" && Hero() != null) row = Hero().Row;
        if (name == "艾琳") row = "前排";
        if (name == "莱索恩") { hp += 10; atk += 10; }

        string skill = "攻击";
        if (name == "阿拉贡") skill = "炬火·耀";
        else if (name == "偷窃者米格") skill = "偷窃 / 一无所有";
        else if (name == "吸血鬼伯爵") skill = "吸血";
        else if (name == "阿格尼") skill = "森海永恒 / 永恒毒恶";
        else if (name == "艾琳") skill = "始祖 / 血魔 / 不灭";
        else if (name == "米凯尔") skill = "神御 / 号角 / 六翼制裁";
        else if (name == "伊维尔") skill = "业火 / 魔主 / 炽焰 / 熔岩";
        else if (name == "莱索恩") skill = "终焉 / 四象 / 真·魔主 / 灭尽";
        return new Unit(name, "敌人", row, skill, hp, atk, true, false, boss);
    }

    private void RenderGame()
    {
        ClearCanvas();
        var bg = Panel(canvas.transform, "Game", new Color32(18, 14, 12, 255));
        Stretch(bg);
        Picture(bg.transform, "generated/ui/hearth_battle_table_open.png", new Vector2(0f, 0f), new Vector2(1f, 1f), true);
        var veil = Panel(bg.transform, "BoardVeil", new Color32(0, 0, 0, 18));
        Stretch(veil);
        veil.GetComponent<Image>().raycastTarget = false;
        RenderStatus(bg.transform);
        RenderBoard(bg.transform);
        RenderSkills(bg.transform);
        RenderRelics(bg.transform);
        RenderCommandPanel(bg.transform);
    }

    private void RenderStatus(Transform root)
    {
        var box = Panel(root, "StatusScroll", new Color32(0, 0, 0, 0));
        Anchor(box, new Vector2(0.010f, 0.020f), new Vector2(0.210f, 0.980f));
        Picture(box.transform, "generated/ui/ui_scroll_log.png", Vector2.zero, Vector2.one, false);
        Text(box.transform, "冒险日志", 17, FontStyle.Bold, new Color32(72, 39, 15, 255), new Vector2(0.15f, 0.880f), new Vector2(0.85f, 0.925f), TextAnchor.MiddleCenter);
        Text(box.transform, chapters[chapterIndex].Title, 21, FontStyle.Bold, new Color32(45, 24, 10, 255), new Vector2(0.11f, 0.805f), new Vector2(0.89f, 0.865f), TextAnchor.MiddleCenter);
        Text(box.transform, "章节 " + (chapterIndex + 1) + "/9   关卡 " + levelIndex + "/10   回合 " + round, 12, FontStyle.Bold, new Color32(70, 43, 18, 255), new Vector2(0.10f, 0.765f), new Vector2(0.90f, 0.800f), TextAnchor.MiddleCenter);
        Text(box.transform, "金币 " + gold + "   牌库 " + roster.Count + "   在场 " + Alive(playerBoard).Count, 12, FontStyle.Normal, new Color32(70, 43, 18, 255), new Vector2(0.10f, 0.732f), new Vector2(0.90f, 0.763f), TextAnchor.MiddleCenter);
        Unit hero = Hero();
        if (hero != null)
        {
            Text(box.transform, "主角", 13, FontStyle.Bold, new Color32(50, 27, 10, 255), new Vector2(0.165f, 0.665f), new Vector2(0.345f, 0.700f), TextAnchor.MiddleLeft);
            DrawBar(box.transform, new Vector2(0.350f, 0.670f), new Vector2(0.835f, 0.697f), hero.Hp, hero.MaxHp, new Color32(190, 45, 40, 255), hero.Hp + " / " + hero.MaxHp, 9);
            DrawBar(box.transform, new Vector2(0.350f, 0.635f), new Vector2(0.835f, 0.660f), hero.Shield, Mathf.Max(10, hero.MaxHp / 2), new Color32(78, 152, 196, 255), "护盾 " + hero.Shield, 8);
        }
        Text(box.transform, string.Join("\n", logs.Skip(Math.Max(0, logs.Count - 24)).ToArray()), 11, FontStyle.Normal, new Color32(42, 27, 12, 255), new Vector2(0.185f, 0.170f), new Vector2(0.885f, 0.600f), TextAnchor.LowerLeft);
    }

    private void RenderBoard(Transform root)
    {
        unitCardRects.Clear();
        var board = new GameObject("CardTableBoard", typeof(RectTransform));
        board.transform.SetParent(root, false);
        Anchor(board, new Vector2(0.218f, 0.255f), new Vector2(0.835f, 0.965f));

        var enemyTint = Panel(board.transform, "EnemyHalfTint", new Color32(70, 31, 18, 18));
        Anchor(enemyTint, new Vector2(0.03f, 0.51f), new Vector2(0.97f, 0.96f));
        enemyTint.GetComponent<Image>().raycastTarget = false;
        var playerTint = Panel(board.transform, "PlayerHalfTint", new Color32(34, 24, 14, 16));
        Anchor(playerTint, new Vector2(0.03f, 0.04f), new Vector2(0.97f, 0.49f));
        playerTint.GetComponent<Image>().raycastTarget = false;
        var centerLine = Panel(board.transform, "CenterTableLine", new Color32(255, 218, 120, 38));
        Anchor(centerLine, new Vector2(0.10f, 0.494f), new Vector2(0.90f, 0.506f));
        centerLine.GetComponent<Image>().raycastTarget = false;

        for (int i = 0; i < 5; ++i)
        {
            DrawSlot(board.transform, enemyBoard[i], i, true);
            DrawSlot(board.transform, playerBoard[i], i, false);
        }
    }

    private void DrawSlot(Transform parent, Unit unit, int index, bool enemy)
    {
        Vector2 p = BoardSlotPosition(index, enemy);
        Vector2 half = new Vector2(0.066f, 0.115f);
        if (unit == null)
        {
            DrawEmptySlot(parent, p, half);
            return;
        }

        var shadow = Panel(parent, "SlotShadow", new Color32(0, 0, 0, 72));
        Anchor(shadow, p - half + new Vector2(0.007f, -0.009f), p + half + new Vector2(0.007f, -0.009f));
        shadow.GetComponent<Image>().raycastTarget = false;
        var pedestal = Panel(parent, "SlotPedestal", new Color32(255, 209, 106, 34));
        Anchor(pedestal, p - half + new Vector2(-0.010f, -0.014f), p + half + new Vector2(0.010f, 0.014f));
        pedestal.GetComponent<Image>().raycastTarget = false;
        AddOutline(pedestal, new Color32(255, 230, 150, 58), new Vector2(1.0f, -1.0f));
        var card = DrawUnitCard(parent, unit, enemy, p - half, p + half);
        card.GetComponent<RectTransform>().localEulerAngles = new Vector3(0f, 0f, CardTilt(index, enemy, unit));
        unitCardRects[unit] = card.GetComponent<RectTransform>();
    }

    private void DrawEmptySlot(Transform parent, Vector2 p, Vector2 half)
    {
        Vector2 inset = new Vector2(0.012f, 0.020f);
        var slot = Panel(parent, "EmptySlotHint", new Color32(255, 217, 130, 12));
        Anchor(slot, p - half + inset, p + half - inset);
        slot.GetComponent<Image>().raycastTarget = false;
        AddOutline(slot, new Color32(255, 220, 140, 54), new Vector2(1f, -1f));
        var glow = Panel(slot.transform, "EmptySlotInnerGlow", new Color32(255, 235, 170, 8));
        Anchor(glow, new Vector2(0.08f, 0.08f), new Vector2(0.92f, 0.92f));
        glow.GetComponent<Image>().raycastTarget = false;
        Text(slot.transform, "待部署", 9, FontStyle.Bold, new Color32(255, 230, 170, 92), new Vector2(0.05f, 0.40f), new Vector2(0.95f, 0.60f), TextAnchor.MiddleCenter);
    }

    private float CardTilt(int index, bool enemy, Unit unit)
    {
        string seed = (enemy ? "enemy" : "player") + "|" + index + "|" + (unit != null ? unit.Name : "") + "|" + (unit != null ? unit.Row : "");
        return StableTilt(seed, 3.2f);
    }

    private float StableTilt(string seed, float maxAbs)
    {
        float value = StableRange(seed, -maxAbs, maxAbs);
        if (Mathf.Abs(value) < 0.45f) value += value < 0f ? -0.75f : 0.75f;
        return Mathf.Clamp(value, -maxAbs, maxAbs);
    }

    private float StableRange(string seed, float min, float max)
    {
        unchecked
        {
            uint hash = 2166136261u;
            for (int i = 0; i < seed.Length; ++i)
            {
                hash ^= seed[i];
                hash *= 16777619u;
            }
            float t = (hash & 0x00ffffff) / 16777215f;
            return Mathf.Lerp(min, max, t);
        }
    }

    private GameObject DrawUnitCard(Transform parent, Unit unit, bool enemy, Vector2 min, Vector2 max)
    {
        Color32 baseColor = unit == null ? new Color32(190, 145, 76, 220) : UnitCardColor(unit, enemy);
        var card = Panel(parent, "HearthUnitCard", new Color32(52, 29, 15, 255));
        Anchor(card, min, max);
        AddOutline(card, unit == null ? new Color32(135, 96, 46, 230) : UnitBorderColor(unit, enemy), new Vector2(2.2f, -2.2f));

        var trim = Panel(card.transform, "GoldTrim", new Color32(207, 160, 72, 255));
        Anchor(trim, new Vector2(0.035f, 0.028f), new Vector2(0.965f, 0.972f));
        trim.GetComponent<Image>().raycastTarget = false;
        var inner = Panel(card.transform, "ParchmentFace", baseColor);
        Anchor(inner, new Vector2(0.065f, 0.055f), new Vector2(0.935f, 0.945f));
        inner.GetComponent<Image>().raycastTarget = false;
        AddOutline(inner, new Color32(72, 42, 20, 210), new Vector2(1f, -1f));

        var artWell = Panel(card.transform, "ArtWell", new Color32(34, 22, 14, 255));
        Anchor(artWell, new Vector2(0.105f, 0.365f), new Vector2(0.895f, 0.765f));
        artWell.GetComponent<Image>().raycastTarget = false;
        AddOutline(artWell, new Color32(230, 186, 92, 220), new Vector2(1.2f, -1.2f));
        if (unit == null)
        {
            var back = Panel(card.transform, "EmptyCardBack", new Color32(171, 124, 58, 225));
            Anchor(back, new Vector2(0.13f, 0.13f), new Vector2(0.87f, 0.83f));
            back.GetComponent<Image>().raycastTarget = false;
            AddOutline(back, new Color32(86, 49, 22, 190), new Vector2(1f, -1f));
            return card;
        }

        DrawUnitPortrait(card.transform, unit, new Vector2(0.12f, 0.375f), new Vector2(0.88f, 0.755f));

        var rowGem = Panel(card.transform, "RowGem", new Color32(116, 96, 76, 255));
        Anchor(rowGem, new Vector2(0.040f, 0.790f), new Vector2(0.245f, 0.965f));
        AddOutline(rowGem, new Color32(210, 180, 105, 255), new Vector2(1.5f, -1.5f));
        var redCore = Panel(rowGem.transform, "RowCore", new Color32(178, 38, 34, 255));
        Anchor(redCore, new Vector2(0.18f, 0.16f), new Vector2(0.82f, 0.84f));
        Text(rowGem.transform, RowChinese(unit.Row), 12, FontStyle.Bold, Color.white, new Vector2(0f, 0f), new Vector2(1f, 1f), TextAnchor.MiddleCenter);

        DrawCardHealth(card.transform, unit, new Vector2(0.255f, 0.850f), new Vector2(0.745f, 0.930f));
        var atk = Panel(card.transform, "AttackMetal", new Color32(120, 105, 82, 255));
        Anchor(atk, new Vector2(0.750f, 0.812f), new Vector2(0.952f, 0.958f));
        AddOutline(atk, new Color32(220, 190, 110, 255), new Vector2(1f, -1f));
        Text(atk.transform, unit.Atk.ToString(), 16, FontStyle.Bold, new Color32(40, 22, 10, 255), Vector2.zero, Vector2.one, TextAnchor.MiddleCenter);

        var nameRibbon = Panel(card.transform, "NameRibbon", new Color32(92, 47, 21, 242));
        Anchor(nameRibbon, new Vector2(0.075f, 0.270f), new Vector2(0.925f, 0.360f));
        AddOutline(nameRibbon, new Color32(226, 180, 86, 220), new Vector2(1f, -1f));
        Text(nameRibbon.transform, unit.Name, 11, FontStyle.Bold, new Color32(255, 238, 190, 255), Vector2.zero, Vector2.one, TextAnchor.MiddleCenter);

        var textBox = Panel(card.transform, "SkillTextBox", new Color32(240, 207, 137, 238));
        Anchor(textBox, new Vector2(0.105f, 0.075f), new Vector2(0.895f, 0.260f));
        textBox.GetComponent<Image>().raycastTarget = false;
        AddOutline(textBox, new Color32(110, 70, 32, 170), new Vector2(1f, -1f));
        Text(textBox.transform, unit.Skill + "：" + SkillType(unit.Skill), 8, FontStyle.Bold, new Color32(44, 28, 14, 255), new Vector2(0.05f, 0.52f), new Vector2(0.95f, 0.94f), TextAnchor.MiddleCenter);
        string status = StatusLine(unit);
        if (status != unit.Row)
        {
            Text(textBox.transform, status, 8, FontStyle.Bold, new Color32(90, 34, 20, 255), new Vector2(0.05f, 0.08f), new Vector2(0.95f, 0.50f), TextAnchor.MiddleCenter);
        }
        return card;
    }

    private string RowChinese(string row)
    {
        if (row == "前排") return "前";
        if (row == "中排") return "中";
        if (row == "后排") return "后";
        return "?";
    }

    private string StatusLine(Unit u)
    {
        var parts = new List<string>();
        if (u.Vulnerable > 0) parts.Add("易伤" + u.Vulnerable);
        if (u.Petrify > 0) parts.Add("石化" + u.Petrify);
        if (u.Thorn > 0) parts.Add("荆棘" + u.Thorn);
        if (u.Echo > 0) parts.Add("回响" + u.Echo);
        return parts.Count == 0 ? u.Row : string.Join(" ", parts.ToArray());
    }

    private void DrawCardHealth(Transform parent, Unit unit, Vector2 min, Vector2 max)
    {
        var bg = Panel(parent, "CardHpBarBg", new Color32(48, 25, 18, 245));
        Anchor(bg, min, max);
        AddOutline(bg, new Color32(34, 18, 10, 210), new Vector2(1f, -1f));
        float ratio = unit.MaxHp <= 0 ? 0f : Mathf.Clamp01((float)unit.Hp / unit.MaxHp);
        var fill = Panel(bg.transform, "CardHpFill", new Color32(198, 38, 36, 255));
        Anchor(fill, Vector2.zero, new Vector2(ratio, 1f));
        fill.GetComponent<Image>().raycastTarget = false;
        Text bgText = Text(bg.transform, unit.Hp + "/" + unit.MaxHp, 10, FontStyle.Bold, Color.white, Vector2.zero, Vector2.one, TextAnchor.MiddleCenter);
        AddOutline(bgText.gameObject, new Color32(35, 12, 8, 240), new Vector2(1.1f, -1.1f));

        if (unit.Shield <= 0) return;
        Color32 shieldColor = new Color32(68, 178, 255, 230);
        var top = Panel(parent, "ShieldTopLine", shieldColor);
        Anchor(top, new Vector2(min.x, max.y + 0.010f), new Vector2(max.x, max.y + 0.035f));
        top.GetComponent<Image>().raycastTarget = false;
        AddOutline(top, new Color32(180, 232, 255, 180), new Vector2(0.8f, -0.8f));
        var bottom = Panel(parent, "ShieldBottomLine", shieldColor);
        Anchor(bottom, new Vector2(min.x, min.y - 0.025f), new Vector2(max.x, min.y - 0.006f));
        bottom.GetComponent<Image>().raycastTarget = false;
        AddOutline(bottom, new Color32(180, 232, 255, 150), new Vector2(0.7f, -0.7f));
        Text shieldText = Text(parent, unit.Shield.ToString(), 8, FontStyle.Bold, new Color32(205, 238, 255, 255), new Vector2(min.x, min.y - 0.061f), new Vector2(max.x, min.y - 0.026f), TextAnchor.MiddleCenter);
        AddOutline(shieldText.gameObject, new Color32(20, 65, 105, 240), new Vector2(1f, -1f));
    }

    private void RenderSkills(Transform root)
    {
        var box = Panel(root, "SkillHandTray", new Color32(0, 0, 0, 0));
        Anchor(box, new Vector2(0.220f, 0.006f), new Vector2(0.724f, 0.286f));
        Picture(box.transform, "generated/ui/ui_skill_table_long.png", new Vector2(0.000f, 0.020f), new Vector2(1.000f, 0.890f), false);
        Text(box.transform, "技能手牌  每回合最多2张", 13, FontStyle.Bold, new Color32(255, 236, 178, 235), new Vector2(0.08f, 0.755f), new Vector2(0.52f, 0.865f), TextAnchor.MiddleCenter);
        for (int i = 0; i < 5; ++i)
        {
            int index = i;
            bool selected = selectedSkillIndexes.Contains(index);
            float lift = selected ? 0.060f : 0f;
            DrawSkillCardButton(box.transform, i < skillSlots.Count ? skillSlots[i] : "", new Vector2(0.112f + i * 0.157f, 0.155f + lift), new Vector2(0.222f + i * 0.157f, 0.730f + lift), () => ToggleSkillSelection(index), selected);
        }
    }

    private void RenderRelics(Transform root)
    {
        var box = new GameObject("RelicLooseArea", typeof(RectTransform));
        box.transform.SetParent(root, false);
        Anchor(box, new Vector2(0.842f, 0.020f), new Vector2(0.992f, 0.985f));
        Picture(box.transform, "generated/ui/ui_relic_platform.png", new Vector2(-0.08f, -0.010f), new Vector2(1.04f, 1.000f), false);
        string[] displayRelics = DisplayRelicSlots();
        for (int i = 0; i < 7; ++i)
        {
            string relic = displayRelics[i];
            float offset = StableRange("relic-x|" + i + "|" + relic, -0.010f, 0.010f);
            GameObject card = DrawRelicCard(box.transform, relic, new Vector2(0.125f + offset, 0.815f - i * 0.118f), new Vector2(0.875f + offset, 0.908f - i * 0.118f));
            card.GetComponent<RectTransform>().localEulerAngles = new Vector3(0f, 0f, StableTilt("relic-tilt|" + i + "|" + relic, 2.4f));
        }
    }

    private string[] DisplayRelicSlots()
    {
        string[] slots = new string[MaxOrdinaryRelics];
        foreach (string relic in relics.Where(IsStoryRelic))
        {
            int slot = StoryRelicSlot(relic);
            if (slot >= 0 && slot < slots.Length) slots[slot] = relic;
        }

        int cursor = 0;
        foreach (string relic in relics.Where(r => !IsInstantRelic(r) && !IsStoryRelic(r)))
        {
            while (cursor < slots.Length && !string.IsNullOrEmpty(slots[cursor])) ++cursor;
            if (cursor >= slots.Length) break;
            slots[cursor] = relic;
            ++cursor;
        }
        return slots;
    }

    private int StoryRelicSlot(string relic)
    {
        if (relic == "跃动赤心") return 0;
        if (relic == "邪神赐福") return 2;
        if (relic == "六翼庇护") return 4;
        if (relic == "邪神诅咒") return 6;
        if (relic == "世界") return 3;
        return -1;
    }

    private void RenderCommandPanel(Transform root)
    {
        var box = Panel(root, "CommandPanel", new Color32(0, 0, 0, 0));
        Anchor(box, new Vector2(0.724f, 0.018f), new Vector2(0.840f, 0.272f));
        Picture(box.transform, "generated/ui/ui_command_buttons.png", Vector2.zero, Vector2.one, false);
        if (victoryPending)
        {
            CommandButton(box.transform, "战斗胜利", new Vector2(0.150f, 0.565f), new Vector2(0.850f, 0.735f), ContinueAfterVictory, true);
            CommandButton(box.transform, "下一关", new Vector2(0.150f, 0.240f), new Vector2(0.850f, 0.410f), ContinueAfterVictory, true);
            return;
        }
        CommandButton(box.transform, "释放技能", new Vector2(0.150f, 0.565f), new Vector2(0.850f, 0.735f), CastSelectedSkills, false);
        Text(box.transform, skillsUsedThisTurn + "/2", 13, FontStyle.Bold, new Color32(255, 232, 180, 255), new Vector2(0.150f, 0.492f), new Vector2(0.850f, 0.558f), TextAnchor.MiddleCenter);
        CommandButton(box.transform, "结算回合", new Vector2(0.150f, 0.240f), new Vector2(0.850f, 0.410f), RunRound, true);
    }

    private void DrawSkillCardButton(Transform parent, string skill, Vector2 min, Vector2 max, Action onClick, bool highlighted)
    {
        if (string.IsNullOrEmpty(skill)) return;
        var card = Panel(parent, "SkillCard", highlighted ? new Color32(232, 178, 62, 255) : new Color32(58, 32, 17, 255));
        Anchor(card, min, max);
        AddOutline(card, highlighted ? new Color32(255, 246, 178, 255) : new Color32(198, 150, 68, 230), new Vector2(1.8f, -1.8f));
        var face = Panel(card.transform, "SkillCardFace", highlighted ? new Color32(245, 206, 116, 245) : new Color32(206, 157, 82, 245));
        Anchor(face, new Vector2(0.055f, 0.04f), new Vector2(0.945f, 0.96f));
        face.GetComponent<Image>().raycastTarget = false;
        var button = card.AddComponent<Button>();
        button.targetGraphic = card.GetComponent<Image>();
        button.onClick.AddListener(() => onClick?.Invoke());
        Unit source = SkillSource(skill);
        DrawSkillIllustration(card.transform, skill, source, new Vector2(0.09f, 0.54f), new Vector2(0.91f, 0.94f));
        var info = Panel(card.transform, "SkillInfo", new Color32(242, 210, 142, 238));
        Anchor(info, new Vector2(0.08f, 0.06f), new Vector2(0.92f, 0.54f));
        info.GetComponent<Image>().raycastTarget = false;
        AddOutline(info, new Color32(90, 55, 24, 160), new Vector2(1f, -1f));
        Text(card.transform, skill, 12, FontStyle.Bold, Color.black, new Vector2(0.12f, 0.415f), new Vector2(0.88f, 0.525f), TextAnchor.MiddleCenter);
        Text(card.transform, (source != null ? source.Name : "来源") + " · " + SkillType(skill), 8, FontStyle.Bold, new Color32(70, 43, 18, 255), new Vector2(0.12f, 0.32f), new Vector2(0.88f, 0.42f), TextAnchor.MiddleCenter);
        Text(card.transform, SkillDescription(skill), 7, FontStyle.Normal, new Color32(55, 34, 16, 255), new Vector2(0.10f, 0.085f), new Vector2(0.90f, 0.320f), TextAnchor.UpperCenter);
    }

    private GameObject DrawRelicCard(Transform parent, string relic, Vector2 min, Vector2 max)
    {
        bool empty = string.IsNullOrEmpty(relic);
        var card = Panel(parent, empty ? "RelicSlotHint" : "RelicCard", empty ? new Color32(255, 217, 130, 8) : new Color32(52, 29, 15, 255));
        Anchor(card, min, max);
        AddOutline(card, empty ? new Color32(255, 220, 140, 34) : new Color32(224, 178, 84, 235), new Vector2(1.7f, -1.7f));
        if (empty)
        {
            return card;
        }
        var trim = Panel(card.transform, "RelicGoldTrim", new Color32(207, 160, 72, 255));
        Anchor(trim, new Vector2(0.025f, 0.055f), new Vector2(0.975f, 0.945f));
        trim.GetComponent<Image>().raycastTarget = false;
        var face = Panel(card.transform, "RelicFace", new Color32(222, 172, 88, 245));
        Anchor(face, new Vector2(0.055f, 0.095f), new Vector2(0.945f, 0.905f));
        face.GetComponent<Image>().raycastTarget = false;
        AddOutline(face, new Color32(95, 55, 22, 190), new Vector2(0.8f, -0.8f));
        var artWell = Panel(card.transform, "RelicArtWell", new Color32(34, 22, 14, 255));
        Anchor(artWell, new Vector2(0.075f, 0.20f), new Vector2(0.345f, 0.80f));
        artWell.GetComponent<Image>().raycastTarget = false;
        AddOutline(artWell, new Color32(230, 186, 92, 210), new Vector2(0.9f, -0.9f));
        DrawRelicIllustration(card.transform, relic, new Vector2(0.085f, 0.22f), new Vector2(0.335f, 0.78f));
        Text(card.transform, RelicTitle(relic), 9, FontStyle.Bold, Color.black, new Vector2(0.38f, 0.54f), new Vector2(0.92f, 0.83f), TextAnchor.MiddleCenter);
        Text(card.transform, RewardDescription(relic), 7, FontStyle.Normal, new Color32(55, 35, 16, 255), new Vector2(0.38f, 0.18f), new Vector2(0.92f, 0.55f), TextAnchor.UpperCenter);
        return card;
    }

    private void DrawRewardCardButton(Transform parent, string reward, Vector2 min, Vector2 max, Action onClick)
    {
        GameObject card = DrawLargeRelicCard(parent, reward, min, max);
        var button = card.AddComponent<Button>();
        button.targetGraphic = card.GetComponent<Image>();
        button.onClick.AddListener(() => onClick?.Invoke());
    }

    private void DrawUnitChoiceButton(Transform parent, Unit unit, string footer, Vector2 min, Vector2 max, Action onClick)
    {
        GameObject card = DrawUnitCard(parent, unit, false, min, max);
        var badge = Panel(parent, "UnitChoiceFooter", new Color32(218, 167, 78, 242));
        Anchor(badge, new Vector2(min.x + 0.010f, min.y - 0.070f), new Vector2(max.x - 0.010f, min.y - 0.012f));
        AddOutline(badge, new Color32(76, 42, 18, 210), new Vector2(1f, -1f));
        Text(badge.transform, footer, 10, FontStyle.Bold, new Color32(45, 24, 10, 255), Vector2.zero, Vector2.one, TextAnchor.MiddleCenter);
        var badgeButton = badge.AddComponent<Button>();
        badgeButton.targetGraphic = badge.GetComponent<Image>();
        badgeButton.onClick.AddListener(() => onClick?.Invoke());
        var button = card.AddComponent<Button>();
        button.targetGraphic = card.GetComponent<Image>();
        button.onClick.AddListener(() => onClick?.Invoke());
    }

    private GameObject DrawLargeRelicCard(Transform parent, string relic, Vector2 min, Vector2 max)
    {
        return DrawRelicCard(parent, relic, min, max);
    }

    private string RelicTitle(string relic)
    {
        int uses;
        string usesText = relicUses.TryGetValue(relic, out uses) && uses >= 0 ? " ×" + uses : " ∞";
        return relic + usesText;
    }

    private void DrawRelicIllustration(Transform parent, string relic, Vector2 min, Vector2 max)
    {
        int uniqueIndex = UniqueRelicArtIndex(relic);
        if (uniqueIndex >= 0)
        {
            AtlasPicture(parent, "generated/unique/unique_relics_atlas.png", 4, 4, uniqueIndex, min, max, true);
            return;
        }
        int ordinaryIndex = OrdinaryRelicArtIndex(relic);
        if (ordinaryIndex >= 0)
        {
            AtlasPicture(parent, "generated/relics/ordinary_relics_atlas_01.png", 4, 4, ordinaryIndex, min, max, true);
            return;
        }
        AtlasPicture(parent, "generated/cards/card_art_atlas.png", 4, 4, 14, min, max, true);
    }

    private int UniqueRelicArtIndex(string relic)
    {
        if (string.IsNullOrEmpty(relic)) return -1;
        if (relic == "\u8dc3\u52a8\u8d64\u5fc3") return 0;
        if (relic == "\u7cbe\u7075\u738b\u51a0") return 1;
        if (relic == "\u5723\u6d01\u516d\u7ffc") return 2;
        if (relic == "六翼庇护") return 2;
        if (relic == "\u90aa\u795e\u8d50\u798f") return 3;
        if (relic == "\u4e16\u754c") return 4;
        if (relic == "\u9b54\u738b\u6b8b\u89d2") return 5;
        if (relic == "\u9ec4\u91d1\u796d\u575b") return 6;
        if (relic == "\u7834\u788e\u62a4\u7b26") return 7;
        if (relic == "\u67d3\u8840\u7b26\u5492") return 8;
        if (relic == "\u6b7b\u4ea1\u5723\u5951") return 9;
        if (relic == "\u771f\u795e\u795d\u798f") return 10;
        if (relic == "\u771f\u795e\u8bc5\u5492") return 11;
        if (relic == "\u90aa\u795e\u8bc5\u5492") return 12;
        if (relic == "邪神诅咒") return 12;
        if (relic == "\u7329\u7ea2\u9152\u676f") return 13;
        if (relic == "\u5723\u6cb3\u6c34\u6ef4") return 14;
        if (relic == "\u9ad8\u5854\u77f3\u7891") return 15;
        return -1;
    }

    private int OrdinaryRelicArtIndex(string relic)
    {
        if (string.IsNullOrEmpty(relic)) return -1;
        if (relic == "铁剑") return 0;
        if (relic == "旧盾") return 1;
        if (relic == "战鼓") return 2;
        if (relic == "铜钱袋") return 3;
        if (relic == "医疗包") return 4;
        if (relic == "魔法书") return 5;
        if (relic == "人王徽记") return 6;
        if (relic == "精灵树枝") return 7;
        if (relic == "锈蚀胸甲") return 8;
        if (relic == "幸运骰子") return 9;
        if (relic == "断爪") return 10;
        if (relic == "铁玫瑰") return 11;
        if (relic == "石像鬼雕像") return 12;
        if (relic == "许愿骨") return 13;
        if (relic == "鎏金坩埚") return 14;
        return -1;
    }

    private string SkillCardLabel(string skill)
    {
        Unit source = SkillSource(skill);
        string sourceText = source != null ? source.Name : "技能";
        return sourceText + "\n" + skill + "\n\n" + SkillDescription(skill);
    }

    private string SkillType(string skill)
    {
        if (skill == "治疗术" || skill == "圣光" || skill == "永生之血") return "治疗";
        if (skill == "守护" || skill == "古树根须" || skill == "六翼庇护" || skill == "命运改写" || skill == "铸剑") return "防御";
        if (skill == "鼓舞" || skill == "血雾" || skill == "森语祝福" || skill == "藤蔓缠绕" || skill == "狂暴" || skill == "圣河回响") return "增益";
        if (skill == "吸血" || skill == "命运之刃" || skill == "古木再生") return "综合";
        return "伤害";
    }

    private int BoardAtlasIndex()
    {
        if (chapterIndex <= 1 || chapterIndex >= 8) return 0;
        if (chapterIndex == 2) return 1;
        if (chapterIndex == 3) return 2;
        if (chapterIndex == 4) return 3;
        if (chapterIndex == 5) return 4;
        if (chapterIndex == 6) return 5;
        if (chapterIndex == 7) return 6;
        return 7;
    }

    private void DrawUnitPortrait(Transform parent, Unit unit, Vector2 min, Vector2 max)
    {
        if (unit != null && unit.Enemy && !unit.Boss)
        {
            int enemyIndex = EnemyGroupArtIndex(unit.Name);
            AtlasPicture(parent, "generated/enemies/enemy_groups_atlas_01.png", 5, 4, enemyIndex, min, max, true);
            return;
        }

        string originalPath = OriginalPortraitPath(unit);
        if (!string.IsNullOrEmpty(originalPath))
        {
            Picture(parent, originalPath, min, max, true);
            return;
        }

        int playerIndex = PlayerUnitArtIndex(unit);
        if (playerIndex >= 0)
        {
            AtlasPicture(parent, "generated/units/player_units_atlas_01.png", 5, 4, playerIndex, min, max, true);
            return;
        }

        int uniqueIndex = UniqueUnitArtIndex(unit);
        if (uniqueIndex >= 0)
        {
            AtlasPicture(parent, "generated/unique/unique_portraits_atlas.png", 4, 4, uniqueIndex, min, max, true);
            return;
        }
        AtlasPicture(parent, "generated/cards/card_art_atlas.png", 4, 4, UnitArtIndex(unit), min, max, true);
    }

    private int UniqueUnitArtIndex(Unit unit)
    {
        if (unit == null) return -1;
        string name = unit.Name ?? "";
        if (unit.Hero || name == "\u4e3b\u89d2") return 0;
        if (name == "\u963f\u62c9\u8d21") return 1;
        if (name == "\u827e\u7433") return 2;
        if (name.Contains("\u4f2f\u7235")) return 3;
        if (name == "\u963f\u683c\u5c3c") return 4;
        if (name == "\u7c73\u51ef\u5c14") return 5;
        if (name == "\u52a0\u767e\u5217") return 6;
        if (name == "\u4f0a\u7ef4\u5c14") return 7;
        if (name == "\u83b1\u7d22\u6069") return 8;
        if (name.Contains("\u9b54\u738b")) return 9;
        if (name.Contains("\u7c73\u683c")) return 10;
        if (name.Contains("\u771f\u795e")) return 11;
        if (name.Contains("\u90aa\u795e")) return 12;
        if (name == "\u7cbe\u7075\u738b\u51a0" || name == "\u53e4\u6811\u957f\u8001") return 13;
        if (name.Contains("\u7ec8\u7109")) return 14;
        if (name.Contains("\u4e16\u754c") || name.Contains("\u6700\u7ec8\u95ee\u9898") || name.Contains("\u6c38\u6052\u6b8b\u54cd")) return 15;
        return -1;
    }

    private string OriginalPortraitPath(Unit unit)
    {
        if (unit == null) return "";
        string name = unit.Name ?? "";
        if (name == "阿拉贡") return unit.Boss || unit.Enemy ? "assets/story/chapter1/boss.png" : "assets/story/chapter1/ally_aragorn.png";
        if (name == "吸血鬼伯爵") return "assets/story/chapter2/boss_count.png";
        if (name == "偷窃者米格" || name.Contains("米格")) return "assets/story/chapter2/boss_mig.png";
        if (name == "游侠") return "assets/story/chapter3/ally_ranger.png";
        if (name == "艾琳") return "assets/story/chapter3/boss.png";
        if (name == "阿格尼") return "assets/story/chapter4/boss.png";
        if (name == "加百列") return "assets/story/chapter5/ally_gabriel.png";
        if (name == "米凯尔") return "assets/story/chapter5/boss.png";
        if (name == "米凯尔与阿格尼") return "assets/story/chapter8/world_rematch.png";
        if (name == "伊维尔" || name == "伊维尔残影") return "assets/story/chapter6/boss.png";
        if (name == "莱索恩") return "assets/story/chapter7/boss.png";
        if (name == "魔王？？？") return "assets/story/prologue/boss.png";
        return "";
    }

    private int PlayerUnitArtIndex(Unit unit)
    {
        if (unit == null) return -1;
        string name = unit.Name ?? "";
        if (unit.Hero || name == "主角") return 0;
        if (name == "见习剑士") return 1;
        if (name == "盾卫") return 2;
        if (name == "弓箭手") return 3;
        if (name == "牧师") return 4;
        if (name == "血仆") return 5;
        if (name == "血术师") return 6;
        if (name == "夜行者") return 7;
        if (name == "血裔") return 8;
        if (name == "精灵射手") return 9;
        if (name == "古木守卫") return 10;
        if (name == "毒叶法师") return 11;
        if (name == "守护天使") return 12;
        if (name == "圣光侍从") return 13;
        if (name == "圣河少女") return 14;
        if (name == "审判者") return 15;
        if (name == "小恶魔") return 16;
        if (name == "魔族战士") return 17;
        if (name == "火焰术士") return 18;
        if (name == "深渊刺客") return 19;
        return -1;
    }

    private int UnitArtIndex(Unit unit)
    {
        if (unit == null) return 14;
        if (unit.Hero || unit.Name == "主角") return 13;
        if (unit.Boss)
        {
            if (unit.Name == "艾琳" || unit.Name == "吸血鬼伯爵" || unit.Name == "偷窃者米格") return 5;
            if (unit.Name == "阿格尼") return 7;
            if (unit.Name == "米凯尔") return 8;
            if (unit.Name == "伊维尔" || unit.Name == "莱索恩") return 10;
            if (unit.Name == "阿拉贡") return 0;
        }
        if (unit.Faction == "人族")
        {
            if (unit.Skill == "箭雨") return 1;
            if (unit.Skill == "治疗术" || unit.Skill == "圣光") return 2;
            if (unit.Skill == "守护") return 3;
            return 0;
        }
        if (unit.Faction == "吸血鬼" || unit.Name.Contains("血") || unit.Name.Contains("蝠")) return unit.Skill.Contains("爆发") || unit.Skill.Contains("雾") ? 5 : 4;
        if (unit.Faction == "精灵族" || unit.Name.Contains("精灵") || unit.Name.Contains("古木")) return unit.Name.Contains("古") ? 7 : 6;
        if (unit.Faction == "天使" || unit.Name.Contains("圣") || unit.Name.Contains("审判") || unit.Name.Contains("天使")) return unit.Name.Contains("审判") ? 9 : 8;
        if (unit.Faction == "魔族" || unit.Name.Contains("魔") || unit.Name.Contains("深渊") || unit.Name.Contains("火")) return unit.Name.Contains("深渊") ? 12 : unit.Name.Contains("火") ? 11 : 10;
        if (unit.Name.Contains("终焉") || unit.Name.Contains("世界") || unit.Name.Contains("虚空")) return 15;
        return unit.Enemy ? 15 : 0;
    }

    private int SkillArtIndex(string skill, Unit source)
    {
        if (skill == "圣光" || skill == "六翼庇护" || skill == "审判") return 8;
        if (skill == "火球" || skill == "魔焰") return 11;
        if (skill == "深渊爪击") return 12;
        if (skill == "毒雾" || skill == "森语祝福" || skill == "古木再生" || skill == "古树根须" || skill == "藤蔓缠绕") return 7;
        if (skill == "吸血" || skill == "血雾" || skill == "赤心爆发" || skill == "永生之血") return 5;
        if (skill == "命运之刃" || skill == "命运改写" || skill == "铸剑") return 13;
        if (skill == "箭雨" || skill == "精灵箭") return 1;
        if (source != null) return UnitArtIndex(source);
        return 15;
    }

    private void DrawSkillIllustration(Transform parent, string skill, Unit source, Vector2 min, Vector2 max)
    {
        int skillIndex = SkillDedicatedIndex(skill);
        if (skillIndex >= 0)
        {
            AtlasPicture(parent, "generated/skills/skills_atlas_01.png", 6, 5, skillIndex, min, max, true);
            return;
        }
        if (source != null)
        {
            DrawUnitPortrait(parent, source, min, max);
            return;
        }
        AtlasPicture(parent, "generated/cards/card_art_atlas.png", 4, 4, SkillArtIndex(skill, source), min, max, true);
    }

    private int SkillDedicatedIndex(string skill)
    {
        if (string.IsNullOrEmpty(skill)) return -1;
        if (skill == "斩击") return 0;
        if (skill == "箭雨") return 1;
        if (skill == "鼓舞") return 2;
        if (skill == "治疗术") return 3;
        if (skill == "守护") return 4;
        if (skill == "命运之刃") return 5;
        if (skill == "吸血") return 6;
        if (skill == "血雾") return 7;
        if (skill == "赤心爆发") return 8;
        if (skill == "永生之血") return 9;
        if (skill == "精灵箭") return 10;
        if (skill == "森语祝福") return 11;
        if (skill == "毒雾") return 12;
        if (skill == "古木再生") return 13;
        if (skill == "古树根须") return 14;
        if (skill == "藤蔓缠绕") return 15;
        if (skill == "火球") return 16;
        if (skill == "深渊爪击") return 17;
        if (skill == "圣光") return 18;
        if (skill == "审判") return 19;
        if (skill == "六翼庇护") return 20;
        if (skill == "命运改写") return 21;
        if (skill == "圣河回响") return 22;
        if (skill == "狂暴") return 23;
        if (skill == "铸剑") return 24;
        if (skill == "攻击") return 25;
        if (skill.Contains("偷窃") || skill.Contains("一无所有")) return 26;
        if (skill.Contains("永恒毒恶")) return 27;
        if (skill.Contains("终焉") || skill.Contains("灭尽")) return 28;
        if (skill.Contains("四象")) return 29;
        if (skill == "魔焰") return 16;
        return -1;
    }

    private int EnemyGroupArtIndex(string name)
    {
        if (string.IsNullOrEmpty(name)) return 19;
        if (name.Contains("偷来的")) return 18;
        if (name == "梦影" || name == "异变魔影" || name == "破碎信徒" || name == "梦境守卫" || name == "梦境裂隙") return 0;
        if (name == "野狼" || name == "山贼" || name == "山贼头目") return 1;
        if (name == "森林弓手" || name == "流浪剑士" || name == "迷失骑士" || name == "王城禁卫") return 2;
        if (name == "夜蝠" || name == "猩红猎犬" || name == "血仆" || name == "吸血鬼侍从") return 3;
        if (name == "血骑士" || name == "远古守卫") return 4;
        if (name == "血术师" || name == "夜行者" || name == "血卫") return 5;
        if (name == "城堡守卫" || name == "赤心骑士" || name == "伯爵残影" || name == "王座守卫") return 6;
        if (name == "精灵射手" || name == "迷途精灵" || name == "毒叶法师") return 7;
        if (name == "古木守卫" || name == "古树长老" || name == "圣地看守者" || name == "精灵祭司") return 8;
        if (name == "圣光侍从" || name == "圣河少女" || name == "圣河守门人") return 9;
        if (name == "审判者" || name == "守护天使" || name == "六翼候补" || name == "天使裁决官") return 10;
        if (name == "小恶魔" || name == "魔族战士" || name == "火焰术士") return 11;
        if (name == "深渊刺客" || name == "魔境猎手" || name == "深渊领主" || name == "旧王亲卫") return 12;
        if (name == "终焉魔兵" || name == "黑翼守卫" || name == "终焉看门人") return 13;
        if (name == "虚空术士" || name == "异界刺客" || name == "伊维尔残影" || name == "邪神使徒") return 14;
        if (name == "世界回声" || name == "旧日生命" || name == "白色梦境") return 15;
        if (name == "永恒残响" || name == "世界门扉" || name == "最终问题" || name == "真神残响") return 16;
        if (name.Contains("石像鬼")) return 17;
        return 19;
    }

    private void ToggleSkillSelection(int index)
    {
        if (index < 0 || index >= skillSlots.Count) return;
        if (selectedSkillIndexes.Contains(index)) selectedSkillIndexes.Remove(index);
        else if (selectedSkillIndexes.Count < 2) selectedSkillIndexes.Add(index);
        else AddLog("每回合最多选择2张技能。");
        RenderGame();
    }

    private void CastSelectedSkills()
    {
        if (actionAnimating)
        {
            AddLog("动画结算中，请稍候。");
            return;
        }
        if (selectedSkillIndexes.Count == 0)
        {
            AddLog("尚未选择技能。");
            RenderGame();
            return;
        }
        StartCoroutine(CastSelectedSkillsSequence());
    }

    private IEnumerator CastSelectedSkillsSequence()
    {
        actionAnimating = true;
        int[] indexes = selectedSkillIndexes.OrderByDescending(i => i).ToArray();
        selectedSkillIndexes.Clear();
        foreach (int index in indexes)
        {
            CastSkill(index);
            yield return WaitForAttackVisuals();
            if (!inBattle)
            {
                actionAnimating = false;
                yield break;
            }
            if (CheckBattleEnd())
            {
                actionAnimating = false;
                yield break;
            }
        }
        if (CheckBattleEnd())
        {
            actionAnimating = false;
            yield break;
        }
        actionAnimating = false;
        RenderGame();
    }

    private void CastSkill(int index)
    {
        if (index < 0 || index >= skillSlots.Count) return;
        if (skillsUsedThisTurn >= 2)
        {
            AddLog("本回合最多释放2张技能。");
            return;
        }
        string skill = skillSlots[index];
        skillSlots.RemoveAt(index);
        ++skillsUsedThisTurn;
        AddLog("释放技能：" + skill);

        int repeats = 1 + Mathf.Min(2, Alive(playerBoard).Sum(u => u.Echo));
        for (int i = 0; i < repeats; ++i)
        {
            if (i > 0) AddLog("回响追加释放：" + skill);
            ResolveSkill(skill);
        }

        CleanupDead();
    }

    private void ResolveSkill(string skill)
    {
        int bonus = HasRelic("魔法书") ? 3 : 0;
        Unit source = SkillSource(skill) ?? Hero();
        int healBonus = HasRelic("圣河水滴") && source != null && source.Faction == "天使" ? 4 : 0;
        if (skill == "斩击") DealDamage(FirstAlive(enemyBoard), 8 + bonus, skill, source);
        else if (skill == "箭雨" || skill == "魔焰")
        {
            int amount = skill == "箭雨" ? 4 : 10;
            foreach (Unit e in Alive(enemyBoard)) DealDamage(e, amount + bonus, skill, source);
        }
        else if (skill == "鼓舞")
        {
            foreach (Unit u in Alive(playerBoard)) u.Atk += 2;
            AddLog("所有友军攻击+2。");
        }
        else if (skill == "守护") AddShield(LowestHp(playerBoard), 10);
        else if (skill == "治疗术") Heal(MostWounded(playerBoard), 12 + healBonus);
        else if (skill == "吸血")
        {
            DealDamage(FirstAlive(enemyBoard), 10 + bonus, skill, source);
            Heal(Hero(), 10);
        }
        else if (skill == "血雾")
        {
            foreach (Unit e in Alive(enemyBoard)) e.Atk = Mathf.Max(0, e.Atk - 2);
            AddLog("血雾：敌方全体攻击-2。");
        }
        else if (skill == "赤心爆发") DealDamage(FirstAlive(enemyBoard), 20 + bonus, skill, source);
        else if (skill == "永生之血") Heal(Hero(), 10);
        else if (skill == "精灵箭") DealDamage(FirstAlive(enemyBoard), 8 + bonus, skill, source);
        else if (skill == "森语祝福")
        {
            foreach (Unit u in Alive(playerBoard))
            {
                u.Atk += 2;
                Heal(u, 4);
            }
        }
        else if (skill == "毒雾") foreach (Unit e in Alive(enemyBoard)) DealDamage(e, 3 + bonus, skill, source);
        else if (skill == "古木再生")
        {
            Heal(MostWounded(playerBoard), 25);
            Heal(MostWounded(enemyBoard), 25);
        }
        else if (skill == "古树根须") AddShield(LowestHp(playerBoard), 15);
        else if (skill == "藤蔓缠绕")
        {
            foreach (Unit e in Alive(enemyBoard)) e.Atk = Mathf.Max(0, e.Atk - 1);
            AddLog("藤蔓缠绕：敌方全体攻击-1。");
        }
        else if (skill == "火球") DealDamage(FirstAlive(enemyBoard), 18 + bonus, skill, source);
        else if (skill == "深渊爪击") DealDamage(enemyBoard[4] ?? FirstAlive(enemyBoard), 22 + bonus, skill, source);
        else if (skill == "圣光")
        {
            foreach (Unit u in Alive(playerBoard)) Heal(u, 8 + healBonus);
        }
        else if (skill == "审判") DealDamage(HighestAtk(enemyBoard), 25 + bonus, skill, source);
        else if (skill == "六翼庇护")
        {
            foreach (Unit u in Alive(playerBoard)) AddShield(u, 12);
        }
        else if (skill == "命运改写")
        {
            if (Hero() != null) Hero().ProtectedDeath = true;
            AddLog("命运改写：主角下一次致死伤害会被保留。");
        }
        else if (skill == "圣河回响" && skillSlots.Count > 0 && skillSlots.Count < 5)
        {
            skillSlots.Add(skillSlots[skillSlots.Count - 1]);
            AddLog("圣河回响：复制最后一张技能。");
        }
        else if (skill == "狂暴")
        {
            Unit u = HighestAtk(playerBoard);
            if (u != null) { u.Atk += 10; u.Hp -= 5; AddLog(u.Name + " 狂暴：攻击+10，生命-5。"); }
        }
        else if (skill == "命运之刃")
        {
            Unit hero = Hero();
            DealDamage(FirstAlive(enemyBoard), hero != null ? Mathf.CeilToInt(hero.Atk * (1.0f + chapterIndex * 0.1f)) : 0, skill, hero);
            foreach (Unit u in Alive(playerBoard)) Heal(u, 5 + fateBladeHealBonus);
        }
        else if (skill == "铸剑")
        {
            if (Hero() != null)
            {
                Hero().Forge += 1;
                AddLog("铸剑：主角获得一层铸剑。");
            }
        }
        else DealDamage(FirstAlive(enemyBoard), 6 + bonus, skill, source);
    }

    private void RunRound()
    {
        if (!inBattle || actionAnimating) return;
        StartCoroutine(RunRoundSequence());
    }

    private IEnumerator RunRoundSequence()
    {
        actionAnimating = true;
        skillsUsedThisTurn = 0;
        AddLog("第 " + round + " 回合结算。");
        BossMechanics();
        yield return WaitForAttackVisuals();
        CleanupDead();
        if (CheckBattleEnd())
        {
            actionAnimating = false;
            yield break;
        }
        GenerateSkills(false);
        yield return AllAttack(playerBoard, enemyBoard);
        CleanupDead();
        if (CheckBattleEnd())
        {
            actionAnimating = false;
            yield break;
        }
        yield return AllAttack(enemyBoard, playerBoard);
        CleanupDead();
        if (CheckBattleEnd())
        {
            actionAnimating = false;
            yield break;
        }
        if (TryShowRefill(() =>
        {
            actionAnimating = false;
            FinishRound();
        })) yield break;
        FinishRound();
        actionAnimating = false;
    }

    private void FinishRound()
    {
        GrowAllUnitsMaxHp();
        ++round;
        TickStatuses();
        ApplyRelicsPerRound();
        RenderGame();
    }

    private IEnumerator AllAttack(Unit[] attackers, Unit[] defenders)
    {
        foreach (Unit attacker in Alive(attackers))
        {
            if (attacker.Petrify > 0)
            {
                AddLog(attacker.Name + " 被石化，无法行动。");
                continue;
            }
            int targetSlot = PreferredAttackTargetSlot(attacker, defenders);
            Unit target = targetSlot >= 0 ? defenders[targetSlot] : null;
            if (target == null) yield break;
            int damage = attacker.Atk;
            if (ReferenceEquals(attackers, playerBoard) && HasRelic("世界")) damage += 50;
            if (RowShort(attacker.Row) == "M" && targetSlot == 1 && defenders.Length > 4 && defenders[4] != null && defenders[4].Hp > 0)
            {
                int rearShare = Mathf.Max(1, damage / 3);
                int frontShare = Mathf.Max(0, damage - rearShare);
                DealDamage(target, frontShare, attacker.Name, attacker);
                yield return WaitForAttackVisuals();
                DealDamage(defenders[4], rearShare, attacker.Name + " 分击", attacker);
                yield return WaitForAttackVisuals();
            }
            else
            {
                DealDamage(target, damage, attacker.Name, attacker);
                yield return WaitForAttackVisuals();
            }
        }
    }

    private bool CheckBattleEnd()
    {
        if (FirstAlive(enemyBoard) == null)
        {
            inBattle = false;
            AddLog("胜利！获得金币。");
            gold += 5 + chapterIndex;
            if (HasRelic("铜钱袋")) gold += 5;
            Chapter chapter = chapters[chapterIndex];
            int finishedChapter = chapterIndex;
            int finishedLevel = levelIndex;
            string allyStoryKey = "";
            bool bossRewardLevel = levelIndex == 10 || (chapterIndex == chapters.Length - 1 && levelIndex == 2);
            if (bossRewardLevel)
            {
                GrantBossStoryRelics(finishedChapter, finishedLevel, chapter);
                if (!string.IsNullOrEmpty(chapter.Ally))
                {
                    Unit ally = TemplateByName(chapter.Ally);
                    if (ally != null) roster.Add(ally);
                    allyStoryKey = "ally_" + chapter.Ally;
                }
                GrowHeroAfterChapterBoss();
                if (levelIndex == 10 && chapterIndex < chapters.Length - 1 && Hero() != null)
                {
                    int halfHp = Hero().MaxHp / 2;
                    if (Hero().Hp < halfHp)
                    {
                        Hero().Hp = halfHp;
                        AddLog("Boss战胜利：主角生命回复至一半。");
                    }
                }
                if (levelIndex == 10)
                {
                    ++chapterIndex;
                    levelIndex = 1;
                    if (chapterIndex >= chapters.Length)
                    {
                        StartCoroutine(EndingTypewriter());
                        return true;
                    }
                }
                else
                {
                    ++levelIndex;
                }
            }
            else
            {
                ++levelIndex;
            }

            TryFuseWorld();
            pendingFinishedChapter = finishedChapter;
            pendingFinishedLevel = finishedLevel;
            pendingAllyStoryKey = allyStoryKey;
            victoryPending = true;
            RenderGame();
            return true;
        }

        if (Hero() == null || Hero().Hp <= 0)
        {
            inBattle = false;
            ShowDefeat();
            return true;
        }
        return false;
    }

    private void GrantBossStoryRelics(int finishedChapter, int finishedLevel, Chapter chapter)
    {
        if (finishedChapter == chapters.Length - 1 && finishedLevel == 2)
        {
            AddRelic("六翼庇护");
            AddRelic("邪神诅咒");
            TryFuseWorld();
            return;
        }

        string bossName = EnemyNameForLevel(chapter, finishedLevel);
        if (bossName == "艾琳") AddRelic("跃动赤心");
        else if (bossName == "莱索恩") AddRelic("邪神赐福");
        else if (!string.IsNullOrEmpty(chapter.Relic)) AddRelic(chapter.Relic);
        TryFuseWorld();
    }

    private void ContinueAfterVictory()
    {
        if (!victoryPending) return;
        victoryPending = false;
        int finishedChapter = pendingFinishedChapter;
        int finishedLevel = pendingFinishedLevel;
        string allyStoryKey = pendingAllyStoryKey;
        pendingAllyStoryKey = "";
        Action next = () =>
        {
            if (ShouldOfferReward(finishedLevel)) ShowUnitReward();
            else StartBattle();
        };
        ShowVictoryStories(finishedChapter, finishedLevel, allyStoryKey, next);
    }

    private void ShowVictoryStories(int finishedChapter, int finishedLevel, string allyStoryKey, Action after)
    {
        var keys = new List<string>();
        string afterKey = "c" + finishedChapter + "_l" + finishedLevel + "_after";
        if (HasStoryKey(afterKey)) keys.Add(afterKey);
        if (!string.IsNullOrEmpty(allyStoryKey) && HasStoryKey(allyStoryKey)) keys.Add(allyStoryKey);
        ShowStorySequence(keys, 0, after);
    }

    private void ShowStorySequence(List<string> keys, int index, Action after)
    {
        if (index >= keys.Count)
        {
            after?.Invoke();
            return;
        }
        ShowStoryKey(keys[index], () => ShowStorySequence(keys, index + 1, after));
    }

    private void ShowReward()
    {
        ClearCanvas();
        var bg = ScreenBackdrop("Reward");
        Stretch(bg);
        Picture(bg.transform, "generated/ui/ui_skill_table_long.png", new Vector2(0.030f, 0.145f), new Vector2(0.970f, 0.850f), false);
        var panel = Panel(bg.transform, "RewardTableSurface", new Color32(0, 0, 0, 0));
        Anchor(panel, new Vector2(0.08f, 0.18f), new Vector2(0.92f, 0.82f));
        Text(panel.transform, "遗物奖励", 28, FontStyle.Bold, new Color32(255, 236, 178, 240), new Vector2(0.10f, 0.82f), new Vector2(0.90f, 0.94f), TextAnchor.MiddleCenter);
        Text(panel.transform, "选择一个遗物进入下一关", 15, FontStyle.Normal, new Color32(245, 223, 166, 235), new Vector2(0.10f, 0.75f), new Vector2(0.90f, 0.82f), TextAnchor.MiddleCenter);
        string[] offers = MakeRewardOffers();
        for (int i = 0; i < offers.Length; ++i)
        {
            string offer = offers[i];
            DrawRewardCardButton(panel.transform, offer, new Vector2(0.145f + i * 0.265f, 0.31f), new Vector2(0.335f + i * 0.265f, 0.70f), () =>
            {
                ClaimReward(offer, StartBattle);
            });
        }
        Button(panel.transform, "跳过", new Vector2(0.42f, 0.12f), new Vector2(0.58f, 0.23f), StartBattle);
    }

    private void ShowUnitReward()
    {
        ClearCanvas();
        var bg = ScreenBackdrop("UnitReward");
        Stretch(bg);
        Picture(bg.transform, "generated/ui/ui_skill_table_long.png", new Vector2(0.020f, 0.115f), new Vector2(0.980f, 0.880f), false);
        var panel = Panel(bg.transform, "UnitRewardTableSurface", new Color32(0, 0, 0, 0));
        Anchor(panel, new Vector2(0.04f, 0.16f), new Vector2(0.96f, 0.84f));
        Text(panel.transform, "补员奖励", 28, FontStyle.Bold, new Color32(255, 236, 178, 240), new Vector2(0.10f, 0.84f), new Vector2(0.90f, 0.95f), TextAnchor.MiddleCenter);
        Text(panel.transform, "购买一名棋子加入牌库", 15, FontStyle.Normal, new Color32(245, 223, 166, 235), new Vector2(0.10f, 0.78f), new Vector2(0.90f, 0.84f), TextAnchor.MiddleCenter);
        List<Unit> offers = MakeUnitRewardOffers();
        for (int i = 0; i < offers.Count; ++i)
        {
            Unit offer = offers[i];
            int price = RewardPrice(i);
            DrawUnitChoiceButton(panel.transform, offer, "价格：" + price, new Vector2(0.055f + i * 0.185f, 0.28f), new Vector2(0.205f + i * 0.185f, 0.76f), () =>
            {
                if (gold >= price)
                {
                    gold -= price;
                    roster.Add(offer);
                    TryAutoFillPendingRefill(offer);
                    AddLog("加入牌库：" + offer.Name);
                }
                ShowReward();
            });
        }
        Button(panel.transform, "跳过棋子", new Vector2(0.42f, 0.12f), new Vector2(0.58f, 0.23f), ShowReward);
    }

    private void ShowDefeat()
    {
        ClearCanvas();
        var bg = ScreenBackdrop("Defeat");
        Stretch(bg);
        var panel = CenterPanel(bg.transform, "DefeatPanel", new Vector2(0.30f, 0.24f), new Vector2(0.70f, 0.68f));
        Text(panel.transform, "旅程失败", 34, FontStyle.Bold, new Color32(45, 24, 10, 255), new Vector2(0.14f, 0.70f), new Vector2(0.86f, 0.88f), TextAnchor.MiddleCenter);
        Text(panel.transform, "主角倒下了。", 18, FontStyle.Normal, new Color32(65, 40, 20, 255), new Vector2(0.14f, 0.55f), new Vector2(0.86f, 0.68f), TextAnchor.MiddleCenter);
        Button(panel.transform, "从本章节开始", new Vector2(0.12f, 0.35f), new Vector2(0.48f, 0.48f), () =>
        {
            levelIndex = 1;
            StartBattle();
        });
        Button(panel.transform, "从头挑战", new Vector2(0.52f, 0.35f), new Vector2(0.88f, 0.48f), () =>
        {
            ResetRun();
            StartBattle();
        });
        Button(panel.transform, "返回标题", new Vector2(0.32f, 0.15f), new Vector2(0.68f, 0.28f), ShowTitle);
    }

    private void LoadStoryJson()
    {
        string path = Path.Combine(Application.streamingAssetsPath, "data/story.json");
        if (File.Exists(path)) storyJson = File.ReadAllText(path);
    }

    private bool HasStoryKey(string key)
    {
        return !string.IsNullOrEmpty(key) && !string.IsNullOrEmpty(storyJson) && storyJson.Contains("\"" + key + "\"");
    }

    private void ShowStoryKey(string key, Action after)
    {
        List<StoryLine> lines = LoadStoryLines(key);
        if (lines.Count == 0)
        {
            after?.Invoke();
            return;
        }
        ShowStoryPage(lines, 0, after);
    }

    private List<StoryLine> LoadStoryLines(string key)
    {
        var result = new List<StoryLine>();
        if (!HasStoryKey(key)) return result;

        Match scene = Regex.Match(storyJson, "\"" + Regex.Escape(key) + "\"\\s*:\\s*\\[(.*?)\\]\\s*(,|})", RegexOptions.Singleline);
        if (!scene.Success) return result;

        foreach (Match item in Regex.Matches(scene.Groups[1].Value, "\\{(.*?)\\}", RegexOptions.Singleline))
        {
            string block = item.Groups[1].Value;
            string speaker = JsonField(block, "speaker");
            string text = JsonField(block, "text");
            string image = JsonField(block, "image");
            if (!string.IsNullOrEmpty(text)) result.Add(new StoryLine { Speaker = speaker, Text = text, Image = image });
        }
        return result;
    }

    private string JsonField(string block, string field)
    {
        Match match = Regex.Match(block, "\"" + field + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"", RegexOptions.Singleline);
        if (!match.Success) return "";
        return Regex.Unescape(match.Groups[1].Value);
    }

    private void ShowStoryPage(List<StoryLine> lines, int index, Action after)
    {
        if (index >= lines.Count)
        {
            after?.Invoke();
            return;
        }

        ClearCanvas();
        var bg = Panel(canvas.transform, "Story", Color.black);
        Stretch(bg);
        StoryLine line = lines[index];
        Picture(bg.transform, line.Image, new Vector2(0f, 0.10f), new Vector2(1f, 1f), true);
        TransparentButton(bg.transform, new Vector2(0f, 0f), new Vector2(1f, 0.18f), () =>
        {
            if (!activeTypewriterDone) CompleteActiveTypewriter();
            else ShowStoryPage(lines, index + 1, after);
        });

        var textBox = Panel(bg.transform, "StoryTextBox", new Color32(4, 4, 4, 232));
        Anchor(textBox, new Vector2(0f, 0f), new Vector2(1f, 0.18f));
        textBox.GetComponent<Image>().raycastTarget = false;
        AddOutline(textBox, new Color32(205, 163, 82, 170), new Vector2(0f, 2f));
        string speaker = string.IsNullOrWhiteSpace(line.Speaker) ? "" : line.Speaker;
        Text(textBox.transform, speaker, 22, FontStyle.Bold, new Color32(244, 206, 119, 255), new Vector2(0.055f, 0.58f), new Vector2(0.72f, 0.88f), TextAnchor.MiddleLeft);
        activeTypewriterText = Text(textBox.transform, "", 20, FontStyle.Normal, Color.white, new Vector2(0.055f, 0.16f), new Vector2(0.91f, 0.58f), TextAnchor.UpperLeft);
        Text(textBox.transform, (index + 1) + " / " + lines.Count, 13, FontStyle.Normal, new Color32(210, 190, 160, 255), new Vector2(0.82f, 0.05f), new Vector2(0.94f, 0.18f), TextAnchor.MiddleRight);

        Button(textBox.transform, "继续", new Vector2(0.88f, 0.31f), new Vector2(0.97f, 0.56f), () =>
        {
            if (!activeTypewriterDone) CompleteActiveTypewriter();
            else ShowStoryPage(lines, index + 1, after);
        });
        activeTypewriter = StartCoroutine(Typewriter(activeTypewriterText, line.Text, 0.025f));
    }

    private IEnumerator EndingTypewriter()
    {
        PlayBgm("ending");
        ClearCanvas();
        var bg = Panel(canvas.transform, "Ending", Color.white);
        Stretch(bg);
        var pages = MakeEndingPages();
        for (int pageIndex = 0; pageIndex < pages.Length; ++pageIndex)
        {
            ClearCanvas();
            bg = Panel(canvas.transform, "Ending", Color.white);
            Stretch(bg);
            Text(bg.transform, "终焉之后", 22, FontStyle.Bold, Color.black, new Vector2(0.05f, 0.92f), new Vector2(0.95f, 0.97f), TextAnchor.MiddleLeft);
            Text(bg.transform, (pageIndex + 1) + " / " + pages.Length, 14, FontStyle.Normal, new Color32(45, 45, 45, 255), new Vector2(0.83f, 0.925f), new Vector2(0.95f, 0.97f), TextAnchor.MiddleRight);
            Text[] lineTexts = RenderEndingRows(bg.transform, pages[pageIndex], pageIndex);
            for (int lineIndex = 0; lineIndex < pages[pageIndex].Length; ++lineIndex)
            {
                string full = pages[pageIndex][lineIndex].Text;
                for (int i = 0; i <= full.Length; ++i)
                {
                    lineTexts[lineIndex].text = full.Substring(0, i);
                    yield return new WaitForSeconds(0.006f);
                }
            }
            if (pageIndex < pages.Length - 1)
            {
                bool next = false;
                Button(bg.transform, "下一页", new Vector2(0.43f, 0.025f), new Vector2(0.57f, 0.075f), () => next = true);
                while (!next) yield return null;
            }
        }
        Button(bg.transform, "返回标题", new Vector2(0.43f, 0.025f), new Vector2(0.57f, 0.075f), ShowTitle);
    }

    private Text[] RenderEndingRows(Transform parent, EndingLine[] lines, int pageIndex)
    {
        Text[] texts = new Text[lines.Length];
        float top = 0.90f;
        float bottom = 0.10f;
        float row = (top - bottom) / lines.Length;
        int fontSize = pageIndex == 0 ? 13 : 20;
        for (int i = 0; i < lines.Length; ++i)
        {
            float maxY = top - i * row;
            float minY = top - (i + 1) * row;
            if (!string.IsNullOrEmpty(lines[i].Speaker))
            {
                Text(parent, lines[i].Speaker, fontSize, FontStyle.Bold, Color.black, new Vector2(0.055f, minY), new Vector2(0.145f, maxY), TextAnchor.UpperLeft);
                texts[i] = Text(parent, "", fontSize, FontStyle.Normal, Color.black, new Vector2(0.165f, minY), new Vector2(0.95f, maxY), TextAnchor.UpperLeft);
            }
            else
            {
                texts[i] = Text(parent, "", fontSize + 1, FontStyle.Bold, Color.black, new Vector2(0.055f, minY), new Vector2(0.95f, maxY), TextAnchor.UpperLeft);
            }
        }
        return texts;
    }

    private IEnumerator Typewriter(Text target, string fullText, float delay)
    {
        if (activeTypewriter != null && !ReferenceEquals(activeTypewriter, null))
        {
            activeTypewriter = null;
        }
        activeTypewriterText = target;
        activeTypewriterFullText = fullText ?? "";
        activeTypewriterDone = false;
        target.text = "";
        for (int i = 0; i <= activeTypewriterFullText.Length; ++i)
        {
            target.text = activeTypewriterFullText.Substring(0, i);
            yield return new WaitForSeconds(delay);
        }
        activeTypewriterDone = true;
    }

    private void CompleteActiveTypewriter()
    {
        if (activeTypewriter != null) StopCoroutine(activeTypewriter);
        activeTypewriter = null;
        activeTypewriterDone = true;
        if (activeTypewriterText != null) activeTypewriterText.text = activeTypewriterFullText;
    }

    private EndingLine[][] MakeEndingPages()
    {
        return new[]
        {
            new[]
            {
                new EndingLine("", "由开发者hhhh大概聊一下剧情脉络"),
                new EndingLine("hhhh", "终焉之地是一片存在大量能量的空间，在无数年前那里什么都没有，整个世界也只有终焉之地"),
                new EndingLine("hhhh", "之后在那里出现了一位纯真而永恒的生命体，不会死亡，也没有情感，不需要进食，也不需要繁衍，终焉之地的能力使得他变得全能，他利用能量创造了世界，他被称为真神"),
                new EndingLine("hhhh", "而之后出现的神明邪神觊觎真神的位置，他希望霸占终焉之地，但他无法击败处于终焉之地的邪神，于是他寄希望于进行时空穿越，而时空穿越本质是对世界的修改，而这需要祝福诅咒与真神邪神的四种组合（也就是游戏开头出现的关键词）"),
                new EndingLine("hhhh", "但邪神也无法击败大天使获得真正的真神祝福，这使得他的计划濒临破产"),
                new EndingLine("hhhh", "这时他发现，大天使米凯尔的妹妹居然爱上了魔王，并且诞生了孩子莱索恩，那么这个孩子意外地具备了邪神渴求的真神祝福"),
                new EndingLine("hhhh", "因此他为莱索恩赐予了邪神力量，并且让他代行自己的意志，从而凑齐了时空穿越的条件"),
                new EndingLine("hhhh", "邪神获得其他三个条件是很容易的，而玩家结局也是获得了四个条件，也就是“回到了一切的开始”"),
                new EndingLine("hhhh", "米凯尔从真神（也就是金色身影）获得了天使最强的六翼，因此击败她获得的要素为“真神祝福”"),
                new EndingLine("hhhh", "艾琳由于父亲挑战生命本源被真神（还是身影）诅咒成为吸血鬼，因此击败她获得的要素为“真神诅咒”"),
                new EndingLine("hhhh", "莱索恩被邪神赐予了力量，因此击败他获得的要素为“真神赐福”"),
                new EndingLine("hhhh", "这四个要素决定了世界复战主角选择的答案"),
                new EndingLine("hhhh", "顺带一提伯爵在被变成吸血鬼后也尝试时空穿越拯救儿子，莱索恩（注意莱索恩是魔王，邪神是邪神）派遣米格偷走了他的关键要素，因为邪神无法接受有人再次时空穿越"),
                new EndingLine("hhhh", "阿格尼接受了邪神名为永生的诅咒，因此击败他获得的要素为“邪神诅咒”"),
                new EndingLine("hhhh", "那么既然真神是全能的，那真神为什么不阻止邪神呢"),
                new EndingLine("hhhh", "答案是，真神向往着有限生命才能拥有的东西，哪怕拥有一点或者曾经体验也行，于是真神接受被邪神击败，他的力量残存世间，指引着某些人进行旅程，凑齐时空穿越的要素取代邪神，而第一位完成的人将被定义为真神，他将再次创世，而这个真神已经拥有过有限生命才能拥有的东西了，而完成这趟旅程的就是主角"),
                new EndingLine("hhhh", "因此主角没有名字"),
                new EndingLine("hhhh", "而有限生命才能拥有的东西，正是最后谜题的答案"),
                new EndingLine("hhhh", "世界有太多永恒的事情和永恒的种族，这来自于魔法，魔法是真神永恒的体现，也就是魔法让我们永恒"),
                new EndingLine("hhhh", "那什么让我们特别是人类无法永恒，甚至放弃永恒呢，这也就是真神渴求的有限生命才能拥有的东西，纵观整个故事，我们体会到的可能不同，爱、情感，关怀、生命甚至是有限性本身"),
                new EndingLine("hhhh", "所以如果那个原初的生命体被称作真神，那么真神确实是死了，真神迎来了终焉"),
                new EndingLine("hhhh", "而如果主角也是真神，那么真神也没死，但莱索恩也说过了，终焉就是开始，而真神也迎来了开始"),
                new EndingLine("hhhh", "而这，就是题目的来源")
            },
            new[]
            {
                new EndingLine("hhhh", "我是个没接触过什么代码的人，真正意义上的熟练使用也许才一年"),
                new EndingLine("hhhh", "但我是个很喜欢讲故事的人，上面就是我很喜欢，也精心雕琢了很久的故事，久到比我熟练使用代码的时间还长"),
                new EndingLine("hhhh", "所以自私一点说，这个游戏对于我就是讲一个我爱故事"),
                new EndingLine("hhhh", "因此开发的所有困难，对我都是值得的"),
                new EndingLine("hhhh", "也许报告里需要我报告开发过程的感想，但这里我只想说"),
                new EndingLine("hhhh", "很开心能讲一个很好的故事"),
                new EndingLine("hhhh", "也很感谢主角您，能听我讲完一个故事")
            },
            new[]
            {
                new EndingLine("Recoleta37", "在梦中，我走进一个故事。"),
                new EndingLine("Recoleta37", "那是一方生机盎然的天地。"),
                new EndingLine("Recoleta37", "顺着无尽的木栈，走向分支的深处。"),
                new EndingLine("Recoleta37", "我辨识万物色彩的多态，我见证思绪灵光的继承。"),
                new EndingLine("Recoleta37", "我乘坐重载马车，驶进森林的闭包。"),
                new EndingLine("Recoleta37", "坐在一棵虚函树下，让自己静态变凉。"),
                new EndingLine("Recoleta37", "我拾起故事的碎片，"),
                new EndingLine("Recoleta37", "随心所欲地组合，"),
                new EndingLine("Recoleta37", "拼成一个又一个世界。"),
                new EndingLine("Recoleta37", "最后，在析构之前，"),
                new EndingLine("Recoleta37", "故事终于算出了返回值："),
                new EndingLine("Recoleta37", "一个指针"),
                new EndingLine("Recoleta37", "指向"),
                new EndingLine("Recoleta37", "......"),
                new EndingLine("Recoleta37", "故事的起点。")
            }
        };
    }

    private void DealDamage(Unit target, int amount, string reason, Unit source = null)
    {
        if (target == null || amount <= 0) return;
        if (target.Boss && target.Name == "莱索恩" && round <= 6)
        {
            AddLog("终焉：莱索恩在前六回合没有受到伤害。");
            return;
        }
        if (target.Boss && target.Name == "米凯尔" && round % 6 != 0)
        {
            AddLog("绝对存续的圣洁神御：米凯尔没有受到伤害。");
            return;
        }
        if (source != null && source.Hero && source.Forge > 0)
        {
            amount *= 3;
            --source.Forge;
            AddLog("铸剑：本次伤害变为3倍。");
        }
        if (target.Vulnerable > 0) amount = Mathf.CeilToInt(amount * 1.5f);
        int finalAmount = amount;
        Action impact = () => ApplyDamageImpact(target, finalAmount, reason, source);
        if (!ShowAttackVisual(source, target, reason, impact)) impact();
    }

    private void ApplyDamageImpact(Unit target, int amount, string reason, Unit source)
    {
        if (target == null || target.Hp <= 0) return;
        int shieldHit = Mathf.Min(target.Shield, amount);
        target.Shield -= shieldHit;
        int hpDamage = amount - shieldHit;
        target.Hp -= hpDamage;
        AddLog(reason + " 造成 " + amount + " 伤害 -> " + target.Name);
        bool lifesteal = hpDamage > 0 && source != null && source.Faction == "吸血鬼" && HasRelic("猩红酒杯");
        int thorn = target.Thorn > 0 && source != null && source.Hp > 0 ? target.Thorn : 0;
        if (lifesteal) source.Hp = Mathf.Min(source.MaxHp, source.Hp + 3);
        if (thorn > 0)
        {
            source.Hp -= thorn;
            AddLog(target.Name + " 的荆棘反伤 " + source.Name + " " + thorn + " 点。");
        }
        RenderGame();
        ShowCombatFeedback(target, "-" + amount, new Color32(225, 55, 45, 255));
        if (lifesteal) ShowCombatFeedback(source, "+3", new Color32(70, 210, 115, 255));
        if (thorn > 0) ShowCombatFeedback(source, "-" + thorn, new Color32(225, 55, 45, 255));
    }

    private void Heal(Unit target, int amount)
    {
        if (target == null || amount <= 0) return;
        int before = target.Hp;
        target.Hp = Mathf.Min(target.MaxHp, target.Hp + amount);
        int gained = target.Hp - before;
        AddLog(target.Name + " 回复 " + (target.Hp - before) + " 生命。");
        if (inBattle) RenderGame();
        ShowStatusParticles(target, new Color32(72, 224, 124, 255), false);
        if (gained > 0) ShowCombatFeedback(target, "+" + gained, new Color32(70, 210, 115, 255));
    }

    private void AddShield(Unit target, int amount)
    {
        if (target == null || amount <= 0) return;
        target.Shield += amount;
        if (inBattle) RenderGame();
        ShowStatusParticles(target, new Color32(77, 170, 245, 255), true);
        AddLog(target.Name + " 获得 " + amount + " 护盾。");
        ShowCombatFeedback(target, "+" + amount + "盾", new Color32(70, 150, 230, 255));
    }

    private void CleanupDead()
    {
        for (int i = 0; i < playerBoard.Length; ++i)
        {
            Unit unit = playerBoard[i];
            if (unit != null && unit.Hp <= 0)
            {
                AddLog(unit.Name + " 倒下。");
                if (unit.Hero && (HasRelic("破碎护符") || unit.ProtectedDeath) && !unit.Revived)
                {
                    unit.Hp = 1;
                    unit.Revived = true;
                    ConsumeRelic("破碎护符");
                    AddLog("主角被保留在1点生命。");
                    continue;
                }
                if (HasRelic("精灵王冠") && !unit.Hero && !unit.Revived)
                {
                    unit.Hp = 8;
                    unit.Revived = true;
                    AddLog(unit.Name + " 因精灵王冠复苏。");
                    continue;
                }
                if (!unit.Hero && HasRelic("精灵树枝") && unit.Faction == "精灵族")
                {
                    foreach (Unit e in Alive(enemyBoard)) DealDamage(e, 5, "精灵树枝");
                }
                if (!unit.Hero) pendingDeadRows.Add(unit.Row);
                playerBoard[i] = null;
            }
        }
        for (int i = 0; i < enemyBoard.Length; ++i)
        {
            Unit unit = enemyBoard[i];
            if (unit != null && unit.Hp <= 0)
            {
                if (unit.Boss && unit.Name == "艾琳" && !unit.Revived)
                {
                    unit.Hp = unit.MaxHp;
                    unit.Shield = 0;
                    unit.Revived = true;
                    AddLog("不灭：艾琳满血复活。");
                    continue;
                }
                if (unit.Boss && !unit.Revived && (unit.Name == "阿格尼" || unit.Name == "米凯尔"))
                {
                    unit.Hp = unit.Name == "阿格尼" ? unit.MaxHp : Mathf.Max(20, unit.MaxHp / 2);
                    if (unit.Name == "阿格尼") unit.Atk *= 2;
                    unit.Revived = true;
                    AddLog(unit.Name + " 触发Boss复活机制。");
                    continue;
                }
                AddLog(unit.Name + " 被击败。");
                enemyBoard[i] = null;
            }
        }
    }

    private bool TryShowRefill(Action after)
    {
        foreach (string row in pendingDeadRows.ToArray())
        {
            if (HasEmptySlot(row) && RefillCandidates(row).Count == 0)
            {
                pendingRefillRows.Add(row);
                AddLog("补员暂缺：" + row + "没有合适棋子，之后买入同排棋子会自动上场。");
            }
        }
        pendingDeadRows.RemoveAll(row => !HasEmptySlot(row) || RefillCandidates(row).Count == 0);
        if (pendingDeadRows.Count == 0) return false;
        ShowRefillChoice(after);
        return true;
    }

    private void ShowRefillChoice(Action after)
    {
        string row = pendingDeadRows[0];
        List<Unit> offers = RefillCandidates(row).Take(4).ToList();
        if (offers.Count == 0)
        {
            pendingDeadRows.RemoveAt(0);
            if (!TryShowRefill(after)) after();
            return;
        }

        ClearCanvas();
        var bg = ScreenBackdrop("Refill");
        Stretch(bg);
        Picture(bg.transform, "generated/ui/ui_skill_table_long.png", new Vector2(0.070f, 0.190f), new Vector2(0.930f, 0.805f), false);
        var panel = Panel(bg.transform, "RefillTableSurface", new Color32(0, 0, 0, 0));
        Anchor(panel, new Vector2(0.12f, 0.25f), new Vector2(0.88f, 0.78f));
        Text(panel.transform, "牌库补员", 28, FontStyle.Bold, new Color32(255, 236, 178, 240), new Vector2(0.20f, 0.80f), new Vector2(0.80f, 0.92f), TextAnchor.MiddleCenter);
        Text(panel.transform, "有 " + row + " 单位倒下。选择一名同排伙伴上场。", 18, FontStyle.Normal, new Color32(245, 223, 166, 235), new Vector2(0.20f, 0.70f), new Vector2(0.80f, 0.78f), TextAnchor.MiddleCenter);
        for (int i = 0; i < offers.Count; ++i)
        {
            Unit offer = offers[i];
            DrawUnitChoiceButton(panel.transform, offer, "补员上场", new Vector2(0.10f + i * 0.20f, 0.28f), new Vector2(0.27f + i * 0.20f, 0.66f), () =>
            {
                int slot = SlotForRow(offer.Row, playerBoard);
                if (slot >= 0)
                {
                    playerBoard[slot] = offer.Fresh();
                    pendingDeadRows.Remove(row);
                    AddLog("补员上场：" + offer.Name);
                }
                if (!TryShowRefill(after)) after();
            });
        }
        Button(panel.transform, "跳过补员", new Vector2(0.42f, 0.12f), new Vector2(0.58f, 0.24f), () =>
        {
            pendingDeadRows.Remove(row);
            AddLog("跳过 " + row + " 补员。");
            if (!TryShowRefill(after)) after();
        });
    }

    private List<Unit> RefillCandidates(string row)
    {
        var remaining = new List<Unit>(roster);
        foreach (Unit unit in Alive(playerBoard).Where(u => !u.Hero))
        {
            int index = remaining.FindIndex(t => t.Name == unit.Name);
            if (index >= 0) remaining.RemoveAt(index);
        }
        return remaining.Where(u => u.Row == row && HasEmptySlot(row)).OrderByDescending(u => u.Atk).ThenByDescending(u => u.MaxHp).ToList();
    }

    private bool HasEmptySlot(string row)
    {
        return SlotForRow(row, playerBoard) >= 0;
    }

    private bool TryAutoFillPendingRefill(Unit unit)
    {
        int pendingIndex = pendingRefillRows.IndexOf(unit.Row);
        if (pendingIndex < 0) return false;
        int slot = SlotForRow(unit.Row, playerBoard);
        if (slot < 0) return false;
        playerBoard[slot] = unit.Fresh();
        pendingRefillRows.RemoveAt(pendingIndex);
        AddLog("自动补员：" + unit.Name + "填补" + unit.Row + "空位。");
        return true;
    }

    private void ApplyRelicsAtBattleStart()
    {
        if (HasRelic("旧盾")) foreach (Unit u in Alive(playerBoard)) AddShield(u, 5);
        if (HasRelic("战鼓")) foreach (Unit u in Alive(playerBoard)) u.Atk += 5;
        if (HasRelic("六翼庇护")) foreach (Unit u in Alive(playerBoard)) AddShield(u, 20);
        if (HasRelic("魔王残角"))
        {
            foreach (Unit u in Alive(playerBoard).Where(u => u.Faction == "魔族"))
            {
                u.Hp -= 5;
                u.Atk += 6;
            }
            AddLog("魔王残角：魔族友军生命-5，攻击+6。");
        }
        if (HasRelic("幸运骰子"))
        {
            Unit lucky = Alive(playerBoard).OrderBy(_ => UnityEngine.Random.value).FirstOrDefault();
            if (lucky != null)
            {
                lucky.Atk += 10;
                AddLog("幸运骰子：" + lucky.Name + " 攻击+10。");
            }
        }
        if (UseLimitedRelic("断爪"))
        {
            foreach (Unit e in Alive(enemyBoard)) e.Vulnerable += 2;
            AddLog("断爪：敌方全体获得2层易伤。");
        }
        if (HasRelic("铁玫瑰"))
        {
            foreach (Unit u in Alive(playerBoard)) u.Thorn += 1;
            AddLog("铁玫瑰：友军获得荆棘。");
        }
        if (UseLimitedRelic("高塔石碑"))
        {
            foreach (Unit u in Alive(playerBoard)) u.Echo += 1;
            AddLog("高塔石碑：友军获得回响。");
        }
        if (UseLimitedRelic("石像鬼雕像"))
        {
            foreach (Unit e in Alive(enemyBoard)) e.Petrify += 1;
            AddLog("石像鬼雕像：敌方全体获得石化。");
        }
    }

    private void ApplyRelicsPerRound()
    {
        if (HasRelic("铁剑")) foreach (Unit u in Alive(playerBoard)) u.Atk += 2;
        if (HasRelic("人王徽记")) foreach (Unit u in Alive(playerBoard).Where(u => u.Faction == "人族")) u.Atk += 4;
        if (HasRelic("邪神赐福"))
        {
            foreach (Unit u in Alive(playerBoard))
            {
                u.Atk = Mathf.CeilToInt(u.Atk * 1.5f);
                u.Hp -= 3;
            }
            AddLog("邪神赐福：友军攻击提升，但生命流失。");
            CleanupDead();
        }
        if (HasRelic("邪神诅咒"))
        {
            Unit cursed = HighestAtk(enemyBoard);
            if (cursed != null) cursed.Atk = Mathf.Max(0, cursed.Atk - 2);
        }
        if (HasRelic("医疗包")) Heal(Hero(), 3);
        if (HasRelic("锈蚀胸甲")) AddShield(Hero(), 5);
        if (HasRelic("跃动赤心")) foreach (Unit u in Alive(playerBoard)) Heal(u, 10);
        if (HasRelic("世界") && skillSlots.Count < 5) skillSlots.Add("命运之刃");
    }

    private void GrowHeroAfterChapterBoss()
    {
        heroHpGrowth += 10;
        fateBladeHealBonus += 2;
        if (Hero() != null)
        {
            Hero().MaxHp += 10;
            Hero().Hp += 10;
            AddLog("章节Boss战结束：主角生命上限与生命+10，命运之刃治疗量+2。");
        }
    }

    private void TryFuseWorld()
    {
        string[] parts = { "跃动赤心", "邪神赐福", "六翼庇护", "邪神诅咒" };
        if (HasRelic("世界")) return;
        foreach (string part in parts) if (!HasRelic(part)) return;
        foreach (string part in parts)
        {
            relics.Remove(part);
            relicUses.Remove(part);
        }
        AddRelic("世界");
        AddLog("四件剧情遗物融合为：世界。");
    }

    private void TickStatuses()
    {
        foreach (Unit u in Alive(playerBoard).Concat(Alive(enemyBoard)))
        {
            if (u.Vulnerable > 0) --u.Vulnerable;
            if (u.Petrify > 0) --u.Petrify;
        }
    }

    private void GrowAllUnitsMaxHp()
    {
        foreach (Unit u in Alive(playerBoard).Concat(Alive(enemyBoard)))
        {
            u.MaxHp += 1;
            u.Hp += 1;
        }
        AddLog("回合结算：所有在场棋子生命上限+1。");
    }

    private void BossMechanics()
    {
        foreach (Unit boss in Alive(enemyBoard).Where(u => u.Boss).ToArray())
        {
            if (boss.Name == "阿拉贡")
            {
                boss.Atk += 2;
                if (round % 3 == 0)
                {
                    foreach (Unit e in Alive(enemyBoard)) e.Atk += 3;
                    AddLog("炬火·耀：敌方全体攻击+3。");
                }
            }
            else if (boss.Name == "吸血鬼伯爵")
            {
                Unit target = FirstAlive(playerBoard);
                int before = target != null ? target.Hp : 0;
                DealDamage(target, 10, "吸血", boss);
                if (target != null && target.Hp < before) Heal(boss, before - target.Hp);
            }
            else if (boss.Name == "偷窃者米格")
            {
                if (round % 3 == 0)
                {
                    Unit stolen = Alive(playerBoard).Where(u => !u.Hero && SlotForRow(u.Row, enemyBoard) >= 0).OrderBy(_ => UnityEngine.Random.value).FirstOrDefault();
                    if (stolen != null)
                    {
                        Unit copy = new Unit("偷来的" + stolen.Name, "敌人", stolen.Row, stolen.Skill, stolen.MaxHp, stolen.Atk + 2, true);
                        int slot = SlotForRow(copy.Row, enemyBoard);
                        if (slot >= 0)
                        {
                            enemyBoard[slot] = copy;
                            RemovePlayerUnit(stolen);
                            AddLog("偷窃：米格偷来了 " + stolen.Name + "。");
                        }
                    }
                }
                if (skillSlots.Count > 0)
                {
                    ResolveEnemySkill(skillSlots[UnityEngine.Random.Range(0, skillSlots.Count)], boss);
                }
            }
            else if (boss.Name == "艾琳")
            {
                if (round % 4 == 0) SummonVampires(2);
                Heal(boss, 12);
                for (int i = 0; i <= 1; ++i) DealDamage(playerBoard[i], 12, "血魔", boss);
            }
            else if (boss.Name == "阿格尼")
            {
                int poison = 2 + (round - 1) * 2;
                foreach (Unit p in Alive(playerBoard)) DealDamage(p, poison, "永恒毒恶", boss);
            }
            else if (boss.Name == "米凯尔" && round % 3 == 0)
            {
                DealDamage(HighestAtk(playerBoard), 20, "永恒燃烧的六翼制裁", boss);
            }
            else if (boss.Name == "伊维尔")
            {
                foreach (Unit p in Alive(playerBoard)) DealDamage(p, 2, "业火", boss);
                if (round % 4 == 0) SummonDemons(2);
                foreach (Unit p in Alive(playerBoard)) p.Atk = Mathf.Max(0, p.Atk - 1);
                boss.Atk += 3;
                AddLog("熔岩：伊维尔攻击+3。");
            }
            else if (boss.Name == "莱索恩")
            {
                int roll = UnityEngine.Random.Range(0, 4);
                if (roll == 0) AddShield(boss, 20);
                if (roll == 1) Heal(boss, 10);
                if (roll == 2) boss.Atk *= 2;
                if (roll == 3) foreach (Unit p in Alive(playerBoard)) DealDamage(p, 4, "四象", boss);
                if (round % 4 == 0) SummonDemons(3);
                foreach (Unit p in Alive(playerBoard).Where(u => !u.Hero && u.AliveRounds >= 12)) DealDamage(p, p.Hp + p.Shield, "灭尽", boss);
            }
        }
    }

    private void ResolveEnemySkill(string skill, Unit source)
    {
        if (skill == "斩击" || skill == "精灵箭") DealDamage(FirstAlive(playerBoard), 8, skill, source);
        else if (skill == "箭雨" || skill == "魔焰")
        {
            int amount = skill == "箭雨" ? 4 : 10;
            foreach (Unit p in Alive(playerBoard)) DealDamage(p, amount, skill, source);
        }
        else if (skill == "火球") DealDamage(FirstAlive(playerBoard), 18, skill, source);
        else if (skill == "深渊爪击") DealDamage(playerBoard[4] ?? FirstAlive(playerBoard), 22, skill, source);
        else if (skill == "审判") DealDamage(HighestAtk(playerBoard), 25, skill, source);
        else if (skill == "圣光") foreach (Unit e in Alive(enemyBoard)) Heal(e, 8);
        else foreach (Unit e in Alive(enemyBoard)) e.Atk += 1;
        AddLog("一无所有：米格使用了 " + skill + "。");
    }

    private void SummonEnemy(string name, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            Unit unit = TemplateByName(name) ?? MakeEnemy(name, false);
            int slot = SlotForRow(unit.Row, enemyBoard);
            if (slot >= 0) enemyBoard[slot] = unit.Fresh(true);
        }
    }

    private void SummonDemons(int count)
    {
        string[] pool = { "小恶魔", "魔族战士", "火焰术士", "深渊刺客" };
        for (int i = 0; i < count; ++i) SummonEnemy(pool[UnityEngine.Random.Range(0, pool.Length)], 1);
        AddLog("魔主：召唤魔族棋子。");
    }

    private void SummonVampires(int count)
    {
        string[] pool = { "血仆", "血术师", "夜行者", "血裔" };
        for (int i = 0; i < count; ++i) SummonEnemy(pool[UnityEngine.Random.Range(0, pool.Length)], 1);
        AddLog("始祖：召唤吸血鬼眷属。");
    }

    private void RemovePlayerUnit(Unit target)
    {
        for (int i = 0; i < playerBoard.Length; ++i)
        {
            if (playerBoard[i] == target)
            {
                playerBoard[i] = null;
                pendingDeadRows.Add(target.Row);
                return;
            }
        }
    }

    private void GenerateSkills(bool initial)
    {
        foreach (Unit u in Alive(playerBoard))
        {
            ++u.AliveRounds;
            if (skillSlots.Count >= 5) break;
            if (initial || u.AliveRounds % 2 == 0)
            {
                skillSlots.Add(u.Skill);
                AddLog(u.Name + " 生成技能：" + u.Skill);
            }
        }
        if (HasRelic("世界") && skillSlots.Count < 5) skillSlots.Add("命运之刃");
    }

    private void ShowGoldAltar()
    {
        ClearCanvas();
        var bg = ScreenBackdrop("GoldAltar");
        Stretch(bg);
        int cost = 20 + goldAltarPurchases * 10;
        var panel = CenterPanel(bg.transform, "GoldAltarPanel", new Vector2(0.30f, 0.30f), new Vector2(0.70f, 0.70f));
        Text(panel.transform, "黄金祭坛", 28, FontStyle.Bold, new Color32(45, 24, 10, 255), new Vector2(0.20f, 0.72f), new Vector2(0.80f, 0.88f), TextAnchor.MiddleCenter);
        Text(panel.transform, "当前金币：" + gold + "\n本次购买需要：" + cost + " 金币", 18, FontStyle.Normal, new Color32(55, 34, 16, 255), new Vector2(0.20f, 0.44f), new Vector2(0.80f, 0.66f), TextAnchor.MiddleCenter);
        Button(panel.transform, "购买遗物", new Vector2(0.16f, 0.20f), new Vector2(0.46f, 0.34f), () =>
        {
            if (gold >= cost)
            {
                gold -= cost;
                ++goldAltarPurchases;
                ClaimReward(MakeRewardOffers()[0]);
                AddLog("黄金祭坛：花费金币换取遗物奖励。");
            }
            RenderGame();
        });
        Button(panel.transform, "进入战斗", new Vector2(0.54f, 0.20f), new Vector2(0.84f, 0.34f), RenderGame);
    }

    private Unit FirstAlive(Unit[] board)
    {
        foreach (Unit u in board) if (u != null && u.Hp > 0) return u;
        return null;
    }

    private Unit PreferredAttackTarget(Unit attacker, Unit[] defenders)
    {
        int slot = PreferredAttackTargetSlot(attacker, defenders);
        return slot >= 0 ? defenders[slot] : FirstAlive(defenders);
    }

    private int PreferredAttackTargetSlot(Unit attacker, Unit[] defenders)
    {
        if (attacker == null)
        {
            Unit first = FirstAlive(defenders);
            if (first == null) return -1;
            for (int i = 0; i < defenders.Length; ++i) if (defenders[i] == first) return i;
            return -1;
        }
        foreach (int slot in AttackTargetSlots(attacker.Row))
        {
            if (slot < 0 || slot >= defenders.Length) continue;
            Unit target = defenders[slot];
            if (target != null && target.Hp > 0) return slot;
        }
        Unit fallback = FirstAlive(defenders);
        if (fallback == null) return -1;
        for (int i = 0; i < defenders.Length; ++i) if (defenders[i] == fallback) return i;
        return -1;
    }

    private int[] AttackTargetSlots(string attackerRow)
    {
        string row = RowShort(attackerRow);
        if (row == "F") return new[] { 0, 1, 2, 3, 4 };
        if (row == "M") return new[] { 0, 1, 4, 2, 3 };
        if (row == "B") return new[] { 2, 3, 4, 0, 1 };
        return new[] { 0, 1, 2, 3, 4 };
    }

    private Unit Hero()
    {
        Unit hero = playerBoard[HeroSlot];
        if (hero != null && hero.Hero) return hero;
        foreach (Unit u in playerBoard)
        {
            if (u != null && u.Hero) return u;
        }
        return null;
    }

    private Unit SkillSource(string skill)
    {
        foreach (Unit u in Alive(playerBoard))
        {
            if (u.Skill == skill) return u;
        }
        return null;
    }

    private Unit MostWounded(Unit[] board)
    {
        Unit best = null;
        int bestLoss = 0;
        foreach (Unit u in board)
        {
            if (u == null || u.Hp <= 0) continue;
            int loss = u.MaxHp - u.Hp;
            if (loss > bestLoss) { bestLoss = loss; best = u; }
        }
        return best ?? FirstAlive(board);
    }

    private Unit LowestHp(Unit[] board)
    {
        Unit best = null;
        foreach (Unit u in board)
        {
            if (u == null || u.Hp <= 0) continue;
            if (best == null || u.Hp < best.Hp) best = u;
        }
        return best;
    }

    private Unit HighestAtk(Unit[] board)
    {
        Unit best = null;
        foreach (Unit u in board)
        {
            if (u == null || u.Hp <= 0) continue;
            if (best == null || u.Atk > best.Atk) best = u;
        }
        return best;
    }

    private List<Unit> Alive(Unit[] board)
    {
        var list = new List<Unit>();
        foreach (Unit u in board) if (u != null && u.Hp > 0) list.Add(u);
        return list;
    }

    private bool HasRelic(string name) => relics.Contains(name);

    private void AddRelic(string name)
    {
        AddRelic(name, null);
    }

    private void AddRelic(string name, Action after)
    {
        if (string.IsNullOrEmpty(name)) return;
        bool instant = IsInstantRelic(name);
        if (!instant && relics.Contains(name))
        {
            AddLog("已拥有遗物：" + name);
            after?.Invoke();
            return;
        }
        if (!instant && !IsStoryRelic(name) && OrdinaryRelicCount() >= OrdinaryRelicCapacity())
        {
            if (OrdinaryRelicCapacity() <= 0)
            {
                AddLog("遗物槽已满，剧情遗物已占满全部槽位。未获得：" + name);
                after?.Invoke();
                return;
            }
            ShowRelicReplaceWindow(name, after);
            return;
        }
        relics.Add(name);
        relicUses[name] = InitialRelicUses(name);
        AddLog("获得遗物：" + name);
        ResolveInstantRelic(name);
        if (IsStoryRelic(name)) TrimOrdinaryRelicsToCapacity();
        after?.Invoke();
    }

    private int ActiveRelicCount()
    {
        int count = 0;
        foreach (string relic in relics)
        {
            int uses;
            if (relicUses.TryGetValue(relic, out uses) && uses == 0) continue;
            if (IsInstantRelic(relic)) continue;
            ++count;
        }
        return count;
    }

    private int OrdinaryRelicCount()
    {
        return relics.Count(r => !IsInstantRelic(r) && !IsStoryRelic(r));
    }

    private int OrdinaryRelicCapacity()
    {
        int storyCount = relics.Count(r => IsStoryRelic(r));
        return Mathf.Max(0, MaxOrdinaryRelics - storyCount);
    }

    private void TrimOrdinaryRelicsToCapacity()
    {
        while (OrdinaryRelicCount() > OrdinaryRelicCapacity())
        {
            int index = relics.FindIndex(r => !IsInstantRelic(r) && !IsStoryRelic(r));
            if (index < 0) return;
            string removed = relics[index];
            relics.RemoveAt(index);
            relicUses.Remove(removed);
            AddLog("剧情遗物占据固定槽位，移出普通遗物：" + removed);
        }
    }

    private void ReplaceRelic(string oldRelic, string newRelic, Action after)
    {
        relics.Remove(oldRelic);
        relicUses.Remove(oldRelic);
        relics.Add(newRelic);
        relicUses[newRelic] = InitialRelicUses(newRelic);
        AddLog("遗物替换：" + oldRelic + " -> " + newRelic);
        ResolveInstantRelic(newRelic);
        after?.Invoke();
    }

    private void ShowRelicReplaceWindow(string newRelic, Action after)
    {
        ClearCanvas();
        var bg = ScreenBackdrop("RelicReplace");
        Stretch(bg);
        var panel = CenterPanel(bg.transform, "RelicReplacePanel", new Vector2(0.08f, 0.18f), new Vector2(0.92f, 0.82f));
        Text(panel.transform, "遗物槽已满", 28, FontStyle.Bold, new Color32(45, 24, 10, 255), new Vector2(0.10f, 0.84f), new Vector2(0.90f, 0.94f), TextAnchor.MiddleCenter);
        Text(panel.transform, "选择一件普通遗物替换为：" + newRelic, 16, FontStyle.Normal, new Color32(65, 40, 20, 255), new Vector2(0.10f, 0.76f), new Vector2(0.90f, 0.84f), TextAnchor.MiddleCenter);

        List<string> ordinary = relics.Where(r => !IsInstantRelic(r) && !IsStoryRelic(r)).ToList();
        int count = Mathf.Min(ordinary.Count, 7);
        for (int i = 0; i < count; ++i)
        {
            string oldRelic = ordinary[i];
            float x = 0.055f + i * 0.128f;
            DrawRewardCardButton(panel.transform, oldRelic, new Vector2(x, 0.36f), new Vector2(x + 0.105f, 0.68f), () =>
            {
                ReplaceRelic(oldRelic, newRelic, after);
            });
        }
        Button(panel.transform, "放弃新遗物", new Vector2(0.40f, 0.12f), new Vector2(0.60f, 0.24f), () =>
        {
            AddLog("放弃遗物：" + newRelic);
            after?.Invoke();
        });
    }

    private bool IsInstantRelic(string name)
    {
        return name == "许愿骨" || name == "染血符咒" || name == "死亡圣契" || name == "鎏金坩埚";
    }

    private bool IsStoryRelic(string name)
    {
        return name == "跃动赤心" || name == "邪神赐福" || name == "六翼庇护" || name == "邪神诅咒" || name == "世界";
    }

    private int InitialRelicUses(string name)
    {
        if (name == "许愿骨") return 1;
        if (name == "染血符咒") return 1;
        if (name == "死亡圣契") return 1;
        if (name == "鎏金坩埚") return 1;
        if (name == "破碎护符") return 1;
        if (name == "断爪") return 3;
        if (name == "高塔石碑") return 1;
        if (name == "石像鬼雕像") return 3;
        return -1;
    }

    private void ResolveInstantRelic(string name)
    {
        if (name == "许愿骨")
        {
            AddLog("许愿骨：额外获得两件随机遗物。");
            string[] pool = { "铁剑", "旧盾", "战鼓", "医疗包", "魔法书", "破碎护符", "锈蚀胸甲", "幸运骰子" };
            AddRelic(pool[(chapterIndex + levelIndex + relics.Count) % pool.Length]);
            AddRelic(pool[(chapterIndex * 3 + levelIndex + relics.Count + 2) % pool.Length]);
            ConsumeRelic(name);
        }
        else if (name == "染血符咒")
        {
            if (Hero() != null) Hero().Hp -= 5;
            heroPermanentAtkBonus += 1;
            AddLog("染血符咒：主角失去5点生命，永久攻击+1。");
            ConsumeRelic(name);
            CleanupDead();
        }
        else if (name == "死亡圣契")
        {
            heroPermanentHpBonus -= 5;
            foreach (Unit u in Alive(playerBoard)) u.Hp = u.MaxHp;
            AddLog("死亡圣契：主角永久生命上限-5，友军全体回满。");
            ConsumeRelic(name);
        }
        else if (name == "鎏金坩埚")
        {
            if (gold >= 20 && skillSlots.Count < 5)
            {
                gold -= 20;
                skillSlots.Add("铸剑");
                AddLog("鎏金坩埚：消耗20金币，获得铸剑技能。");
            }
            else
            {
                AddLog("鎏金坩埚：金币不足或技能槽已满。");
            }
            ConsumeRelic(name);
        }
    }

    private bool UseLimitedRelic(string name)
    {
        if (!HasRelic(name)) return false;
        int uses;
        if (!relicUses.TryGetValue(name, out uses) || uses < 0) return true;
        if (uses <= 0) return false;
        --uses;
        relicUses[name] = uses;
        if (uses == 0)
        {
            relics.Remove(name);
            relicUses.Remove(name);
            AddLog(name + " 已耗尽。");
        }
        return true;
    }

    private bool ConsumeRelic(string name)
    {
        if (!HasRelic(name)) return false;
        int uses;
        if (!relicUses.TryGetValue(name, out uses) || uses < 0) return false;
        --uses;
        relicUses[name] = uses;
        if (uses <= 0)
        {
            relics.Remove(name);
            relicUses.Remove(name);
            AddLog("遗物次数耗尽：" + name);
            return true;
        }
        return false;
    }

    private int SlotForRow(string row, Unit[] board)
    {
        int[] slots = row == "前排" ? new[] { 0, 1 } : row == "中排" ? new[] { 2, 3 } : new[] { 4 };
        foreach (int slot in slots)
        {
            if (ReferenceEquals(board, playerBoard) && slot == HeroSlot) continue;
            if (board[slot] == null) return slot;
        }
        return -1;
    }

    private void NormalizeHeroSlot()
    {
        Unit hero = null;
        int currentSlot = -1;
        for (int i = 0; i < playerBoard.Length; ++i)
        {
            Unit unit = playerBoard[i];
            if (unit == null || !unit.Hero) continue;
            if (hero == null)
            {
                hero = unit;
                currentSlot = i;
            }
            else
            {
                playerBoard[i] = null;
            }
        }
        if (hero == null || currentSlot == HeroSlot) return;

        Unit displaced = playerBoard[HeroSlot];
        playerBoard[HeroSlot] = hero;
        playerBoard[currentSlot] = null;
        if (displaced == null || displaced.Hero) return;

        int replacementSlot = SlotForRow(displaced.Row, playerBoard);
        if (replacementSlot >= 0)
        {
            playerBoard[replacementSlot] = displaced;
        }
        else
        {
            playerBoard[currentSlot] = displaced;
        }
    }

    private bool ShouldOfferReward(int level) => level == 3 || level == 6 || level == 9 || level == 10;

    private int RewardPrice(int index)
    {
        int[] weighted = { 3, 4, 4, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 10 };
        int seed = chapterIndex * 17 + levelIndex * 11 + index * 7 + roster.Count;
        return weighted[Mathf.Abs(seed) % weighted.Length];
    }

    private List<Unit> MakeUnitRewardOffers()
    {
        string[] rows = { "前排", "前排", "中排", UnityEngine.Random.value < 0.5f ? "中排" : "后排", "后排" };
        var offers = new List<Unit>();
        foreach (string row in rows)
        {
            List<Unit> pool = RewardPoolForRow(row);
            Unit unit = pool[UnityEngine.Random.Range(0, pool.Count)];
            offers.Add(ScaledRewardUnit(unit));
        }
        return offers;
    }

    private Unit ScaledRewardUnit(Unit unit)
    {
        Unit scaled = unit.Fresh();
        scaled.MaxHp += chapterIndex * 12 + Mathf.Max(0, levelIndex - 1) * 3;
        scaled.Hp = scaled.MaxHp;
        scaled.Atk += chapterIndex * 3 + Mathf.Max(0, levelIndex - 1) / 2;
        if (levelIndex >= 9) { scaled.MaxHp += 8; scaled.Hp += 8; scaled.Atk += 2; }
        if (chapterIndex >= 6) { scaled.MaxHp += 10; scaled.Hp += 10; scaled.Atk += 2; }
        return scaled;
    }

    private List<Unit> RewardPoolForRow(string row)
    {
        var all = new List<Unit>
        {
            new Unit("见习剑士", "人族", "前排", "斩击", 42, 7),
            new Unit("盾卫", "人族", "前排", "守护", 58, 4),
            new Unit("血仆", "吸血鬼", "前排", "吸血", 40, 8),
            new Unit("守护天使", "天使", "前排", "六翼庇护", 60, 6),
            new Unit("魔族战士", "魔族", "前排", "狂暴", 54, 9),
            new Unit("古木守卫", "精灵族", "前排", "古木再生", 62, 5),
            new Unit("牧师", "人族", "中排", "治疗术", 32, 5),
            new Unit("血术师", "吸血鬼", "中排", "赤心爆发", 34, 10),
            new Unit("毒叶法师", "精灵族", "中排", "毒雾", 35, 9),
            new Unit("圣光侍从", "天使", "中排", "圣光", 38, 6),
            new Unit("圣河少女", "天使", "中排", "圣河回响", 36, 7),
            new Unit("火焰术士", "魔族", "中排", "魔焰", 36, 13),
            new Unit("弓箭手", "人族", "后排", "箭雨", 28, 11),
            new Unit("夜行者", "吸血鬼", "后排", "吸血", 30, 13),
            new Unit("精灵射手", "精灵族", "后排", "精灵箭", 32, 12),
            new Unit("审判者", "天使", "后排", "审判", 34, 14),
            new Unit("小恶魔", "魔族", "后排", "火球", 30, 15),
            new Unit("深渊刺客", "魔族", "后排", "深渊爪击", 32, 16)
        };
        string preferred = "";
        if (chapterIndex <= 1) preferred = "人族";
        else if (chapterIndex <= 3) preferred = "吸血鬼";
        else if (chapterIndex == 4) preferred = "精灵族";
        else if (chapterIndex == 5) preferred = "天使";
        else if (chapterIndex <= 7) preferred = "魔族";

        var pool = new List<Unit>();
        foreach (Unit unit in all)
        {
            if (unit.Row != row) continue;
            pool.Add(unit);
            if (unit.Faction == preferred)
            {
                pool.Add(unit);
                pool.Add(unit);
            }
        }
        return pool;
    }

    private string[] MakeRewardOffers()
    {
        string[] pool =
        {
            "铁剑", "旧盾", "战鼓", "铜钱袋", "医疗包", "魔法书", "人王徽记", "猩红酒杯", "精灵树枝", "圣河水滴", "魔王残角", "黄金祭坛", "锈蚀胸甲", "幸运骰子", "破碎护符", "断爪", "铁玫瑰", "高塔石碑", "石像鬼雕像",
            "许愿骨", "染血符咒", "死亡圣契", "鎏金坩埚"
        };
        int seed = chapterIndex * 31 + levelIndex * 7 + relics.Count + roster.Count;
        return new[] { pool[seed % pool.Length], pool[(seed + 5) % pool.Length], pool[(seed + 10) % pool.Length] };
    }

    private void ClaimReward(string reward, Action after = null)
    {
        Unit unit = TemplateByName(reward);
        if (unit != null)
        {
            roster.Add(unit);
            AddLog("加入牌库：" + reward);
            after?.Invoke();
            return;
        }
        AddRelic(reward, after);
    }

    private Unit TemplateByName(string name)
    {
        Unit bestRosterMatch = null;
        foreach (Unit unit in roster)
        {
            if (unit.Name != name) continue;
            if (bestRosterMatch == null || unit.Atk > bestRosterMatch.Atk ||
                (unit.Atk == bestRosterMatch.Atk && unit.MaxHp > bestRosterMatch.MaxHp))
            {
                bestRosterMatch = unit;
            }
        }
        if (bestRosterMatch != null) return bestRosterMatch.Fresh();

        if (name == "主角") return new Unit("主角", "命运", "中排", "命运改写", 60, 8);
        if (name == "阿拉贡") return new Unit("阿拉贡", "人族", "前排", "鼓舞", 70, 10);
        if (name == "见习剑士") return new Unit("见习剑士", "人族", "前排", "斩击", 42, 7);
        if (name == "盾卫") return new Unit("盾卫", "人族", "前排", "守护", 58, 4);
        if (name == "弓箭手") return new Unit("弓箭手", "人族", "后排", "箭雨", 28, 11);
        if (name == "牧师") return new Unit("牧师", "人族", "中排", "治疗术", 32, 5);
        if (name == "游侠") return new Unit("游侠", "人族", "后排", "箭雨", 44, 16);
        if (name == "血仆") return new Unit("血仆", "吸血鬼", "前排", "吸血", 38, 8);
        if (name == "血术师") return new Unit("血术师", "吸血鬼", "中排", "血雾", 34, 7);
        if (name == "夜行者") return new Unit("夜行者", "吸血鬼", "后排", "吸血", 30, 13);
        if (name == "血裔") return new Unit("血裔", "吸血鬼", "前排", "永生之血", 52, 6);
        if (name == "艾琳") return new Unit("艾琳", "吸血鬼", "中排", "赤心爆发", 64, 14);
        if (name == "精灵射手") return new Unit("精灵射手", "精灵族", "后排", "精灵箭", 30, 12);
        if (name == "古木守卫") return new Unit("古木守卫", "精灵族", "前排", "古树根须", 60, 5);
        if (name == "毒叶法师") return new Unit("毒叶法师", "精灵族", "中排", "毒雾", 36, 9);
        if (name == "阿格尼") return new Unit("阿格尼", "精灵族", "中排", "森语祝福", 66, 12);
        if (name == "加百列") return new Unit("加百列", "天使", "中排", "圣光", 70, 14);
        if (name == "守护天使") return new Unit("守护天使", "天使", "前排", "六翼庇护", 52, 7);
        if (name == "圣河少女") return new Unit("圣河少女", "天使", "后排", "圣河回响", 36, 8);
        if (name == "小恶魔") return new Unit("小恶魔", "魔族", "后排", "火球", 30, 15);
        if (name == "魔族战士") return new Unit("魔族战士", "魔族", "前排", "狂暴", 48, 10);
        if (name == "火焰术士") return new Unit("火焰术士", "魔族", "中排", "魔焰", 36, 13);
        if (name == "深渊刺客") return new Unit("深渊刺客", "魔族", "后排", "深渊爪击", 34, 16);
        return null;
    }

    private string SkillDescription(string skill)
    {
        string expanded = ExpandedSkillDescription(skill);
        if (!string.IsNullOrEmpty(expanded)) return expanded;
        if (skill == "斩击") return "对首个敌人造成8伤害";
        if (skill == "箭雨") return "敌方全体4伤害";
        if (skill == "鼓舞") return "友军攻击+2";
        if (skill == "治疗术") return "治疗失血友军12";
        if (skill == "守护") return "最低血友军10护盾";
        if (skill == "命运之刃") return "伤害并治疗全体";
        if (skill == "魔焰") return "敌方全体10伤害";
        if (skill == "圣光") return "友军全体治疗8";
        if (skill == "血雾") return "敌方全体攻击-2";
        if (skill == "赤心爆发") return "首个敌人20伤害";
        if (skill == "精灵箭") return "首个敌人8伤害";
        if (skill == "森语祝福") return "友军攻击+2并治疗4";
        if (skill == "毒雾") return "敌方全体3伤害";
        if (skill == "古树根须") return "最低血友军15护盾";
        if (skill == "六翼庇护") return "友军全体12护盾";
        if (skill == "命运改写") return "主角保留一次致死伤害";
        if (skill == "铸剑") return "主角下一次伤害x3";
        return "基础效果";
    }

    private string ExpandedSkillDescription(string skill)
    {
        if (skill == "\u65a9\u51fb") return "\u5bf9\u9996\u4e2a\u654c\u4eba\u9020\u62108\u70b9\u7269\u7406\u4f24\u5bb3\u3002";
        if (skill == "\u7bad\u96e8") return "\u5bf9\u654c\u65b9\u5168\u4f53\u9020\u62104\u70b9\u7fa4\u4f53\u4f24\u5bb3\u3002";
        if (skill == "\u9f13\u821e") return "\u6240\u6709\u53cb\u519b\u653b\u51fb+2\uff0c\u5f53\u573a\u751f\u6548\u3002";
        if (skill == "\u6cbb\u7597\u672f") return "\u6cbb\u7597\u5931\u8840\u6700\u591a\u7684\u53cb\u519b12\u70b9\u751f\u547d\u3002";
        if (skill == "\u5b88\u62a4") return "\u4e3a\u751f\u547d\u6700\u4f4e\u7684\u53cb\u519b\u589e\u52a010\u70b9\u62a4\u76fe\u3002";
        if (skill == "\u547d\u8fd0\u4e4b\u5203")
        {
            Unit hero = Hero();
            int damage = hero != null ? Mathf.CeilToInt(hero.Atk * (1.0f + chapterIndex * 0.1f)) : 0;
            int heal = 5 + fateBladeHealBonus;
            return "\u4e3b\u89d2\u5bf9\u9996\u4e2a\u654c\u4eba\u9020\u6210" + damage + "\u70b9\u4f24\u5bb3\uff1b\u5168\u4f53\u53cb\u519b\u6062\u590d" + heal + "\u70b9\u751f\u547d\u3002";
        }
        if (skill == "\u9b54\u7130") return "\u5bf9\u654c\u65b9\u5168\u4f53\u9020\u621010\u70b9\u9b54\u7130\u4f24\u5bb3\u3002";
        if (skill == "\u5723\u5149") return "\u6cbb\u7597\u6240\u6709\u53cb\u519b8\u70b9\u751f\u547d\u3002";
        if (skill == "\u8840\u96fe") return "\u654c\u65b9\u5168\u4f53\u653b\u51fb-2\u3002";
        if (skill == "\u8d64\u5fc3\u7206\u53d1") return "\u5bf9\u9996\u4e2a\u654c\u4eba\u9020\u621020\u70b9\u7206\u53d1\u4f24\u5bb3\u3002";
        if (skill == "\u6c38\u751f\u4e4b\u8840") return "\u6cbb\u7597\u4e3b\u89d210\u70b9\u751f\u547d\u3002";
        if (skill == "\u7cbe\u7075\u7bad") return "\u5bf9\u9996\u4e2a\u654c\u4eba\u9020\u62108\u70b9\u7cbe\u7075\u4f24\u5bb3\u3002";
        if (skill == "\u68ee\u8bed\u795d\u798f") return "\u6240\u6709\u53cb\u519b\u653b\u51fb+2\uff0c\u5e76\u56de\u590d4\u70b9\u751f\u547d\u3002";
        if (skill == "\u6bd2\u96fe") return "\u5bf9\u654c\u65b9\u5168\u4f53\u9020\u62103\u70b9\u6301\u7eed\u6027\u4f24\u5bb3\u3002";
        if (skill == "\u53e4\u6728\u518d\u751f") return "\u53cc\u65b9\u5931\u8840\u5355\u4f4d\u5404\u56de\u590d25\u70b9\u751f\u547d\u3002";
        if (skill == "\u53e4\u6811\u6839\u987b") return "\u4e3a\u751f\u547d\u6700\u4f4e\u7684\u53cb\u519b\u589e\u52a015\u70b9\u62a4\u76fe\u3002";
        if (skill == "\u85e4\u8513\u7f20\u7ed5") return "\u654c\u65b9\u5168\u4f53\u653b\u51fb-1\u3002";
        if (skill == "\u706b\u7403") return "\u5bf9\u9996\u4e2a\u654c\u4eba\u9020\u621018\u70b9\u706b\u7130\u4f24\u5bb3\u3002";
        if (skill == "\u6df1\u6e0a\u722a\u51fb") return "\u4f18\u5148\u653b\u51fb\u654c\u65b9\u540e\u6392\uff0c\u9020\u621022\u70b9\u4f24\u5bb3\u3002";
        if (skill == "\u5ba1\u5224") return "\u5bf9\u653b\u51fb\u6700\u9ad8\u7684\u654c\u4eba\u9020\u621025\u70b9\u4f24\u5bb3\u3002";
        if (skill == "\u516d\u7ffc\u5e87\u62a4") return "\u4e3a\u6240\u6709\u53cb\u519b\u589e\u52a012\u70b9\u62a4\u76fe\u3002";
        if (skill == "\u547d\u8fd0\u6539\u5199") return "\u4e3b\u89d2\u4e0b\u6b21\u81f4\u6b7b\u4f24\u5bb3\u4fdd\u75591\u70b9\u751f\u547d\u3002";
        if (skill == "\u5723\u6cb3\u56de\u54cd") return "\u590d\u5236\u6280\u80fd\u624b\u724c\u4e2d\u6700\u540e\u4e00\u5f20\u6280\u80fd\u3002";
        if (skill == "\u72c2\u66b4") return "\u6700\u9ad8\u653b\u51fb\u53cb\u519b\u653b\u51fb+10\uff0c\u5931\u53bb5\u70b9\u751f\u547d\u3002";
        if (skill == "\u94f8\u5251") return "\u4e3b\u89d2\u83b7\u5f971\u5c42\u94f8\u5251\uff0c\u4e0b\u6b21\u4f24\u5bb3\u63d0\u5347\u3002";
        if (skill == "\u5438\u8840") return "\u5bf9\u9996\u4e2a\u654c\u4eba\u9020\u621010\u70b9\u4f24\u5bb3\uff0c\u5e76\u6cbb\u7597\u4e3b\u89d2\u3002";
        return "";
    }

    private string RewardDescription(string reward)
    {
        if (TemplateByName(reward) != null) return "单位牌";
        if (reward == "铁剑") return "每回合友军攻击+2";
        if (reward == "人王徽记") return "每回合人族友军攻击+4";
        if (reward == "旧盾") return "战斗开始友军护盾+5";
        if (reward == "战鼓") return "战斗开始友军攻击+5";
        if (reward == "铜钱袋") return "胜利后金币+5";
        if (reward == "医疗包") return "每回合治疗主角3";
        if (reward == "锈蚀胸甲") return "每回合主角护盾+5";
        if (reward == "魔法书") return "技能伤害+3";
        if (reward == "猩红酒杯") return "吸血鬼造成生命伤害后回复3";
        if (reward == "精灵树枝") return "精灵族死亡时敌方全体5伤害";
        if (reward == "圣河水滴") return "天使治疗技能额外+4";
        if (reward == "魔王残角") return "魔族开局生命-5，攻击+6";
        if (reward == "黄金祭坛") return "Boss关前可花金币换遗物";
        if (reward == "幸运骰子") return "战斗开始随机友军攻击+10";
        if (reward == "破碎护符") return "1次：主角致死时保留1血";
        if (reward == "断爪") return "3次：敌方全体易伤";
        if (reward == "铁玫瑰") return "友军获得荆棘";
        if (reward == "高塔石碑") return "1次：友军获得回响";
        if (reward == "石像鬼雕像") return "3次：敌方全体石化";
        if (reward == "许愿骨") return "一次性：额外获得遗物";
        if (reward == "染血符咒") return "一次性：主角失血，永久攻击+1";
        if (reward == "死亡圣契") return "一次性：生命上限-5，全体回满";
        if (reward == "鎏金坩埚") return "一次性：花20金币获得铸剑";
        if (reward == "跃动赤心") return "剧情遗物：每回合友军全体回复10生命";
        if (reward == "邪神赐福") return "剧情遗物：每回合攻击提升但生命流失";
        if (reward == "六翼庇护") return "剧情遗物：战斗开始友军全体获得20护盾";
        if (reward == "邪神诅咒") return "剧情遗物：每回合压低敌方最高攻击";
        if (reward == "世界") return "四件剧情遗物合成，主角大幅强化";
        return "遗物";
    }

    private void AddLog(string line)
    {
        logs.Add(line);
        while (logs.Count > 80) logs.RemoveAt(0);
    }

    private void ClearCanvas()
    {
        if (activeTypewriter != null)
        {
            StopCoroutine(activeTypewriter);
            activeTypewriter = null;
        }
        activeTypewriterDone = true;
        if (canvas != null) Destroy(canvas.gameObject);
        var root = new GameObject("Canvas");
        canvas = root.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        root.AddComponent<CanvasScaler>().uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        root.GetComponent<CanvasScaler>().referenceResolution = new Vector2(1500, 900);
        root.AddComponent<GraphicRaycaster>();
        RenderBgmToggle();
    }

    private void RenderBgmToggle()
    {
        if (canvas == null) return;
        Transform existing = canvas.transform.Find("BgmToggle");
        if (existing != null) Destroy(existing.gameObject);

        var go = Panel(canvas.transform, "BgmToggle", new Color32(28, 18, 10, 175));
        Anchor(go, new Vector2(0.946f, 0.910f), new Vector2(0.986f, 0.976f));
        AddOutline(go, bgmMuted ? new Color32(120, 78, 48, 210) : new Color32(235, 190, 95, 235), new Vector2(1.4f, -1.4f));
        var button = go.AddComponent<Button>();
        button.targetGraphic = go.GetComponent<Image>();
        var colors = button.colors;
        colors.normalColor = Color.white;
        colors.highlightedColor = new Color32(255, 235, 165, 255);
        colors.pressedColor = new Color32(120, 72, 32, 255);
        colors.selectedColor = colors.highlightedColor;
        button.colors = colors;
        button.onClick.AddListener(ToggleBgmMuted);

        var icon = Panel(go.transform, "BgmSpeakerIcon", Color.white);
        Anchor(icon, new Vector2(0.16f, 0.16f), new Vector2(0.84f, 0.84f));
        var iconImage = icon.GetComponent<Image>();
        iconImage.sprite = bgmMuted ? bgmOffSprite : bgmOnSprite;
        iconImage.color = bgmMuted ? new Color32(150, 118, 90, 255) : new Color32(255, 230, 170, 255);
        iconImage.preserveAspect = true;
        iconImage.raycastTarget = false;
        go.transform.SetAsLastSibling();
    }

    private GameObject ScreenBackdrop(string name)
    {
        var bg = Panel(canvas.transform, name, new Color32(52, 42, 34, 255));
        Stretch(bg);
        var vignette = Panel(bg.transform, "Vignette", new Color32(0, 0, 0, 70));
        Stretch(vignette);
        vignette.GetComponent<Image>().raycastTarget = false;
        return bg;
    }

    private GameObject CenterPanel(Transform parent, string name, Vector2 min, Vector2 max)
    {
        var panel = Panel(parent, name, new Color32(231, 204, 143, 248));
        Anchor(panel, min, max);
        AddOutline(panel, new Color32(72, 42, 20, 230), new Vector2(2f, -2f));
        return panel;
    }

    private GameObject FramedPanel(Transform parent, string name, Vector2 min, Vector2 max, Color faceColor, Color frameColor)
    {
        var outer = Panel(parent, name, frameColor);
        Anchor(outer, min, max);
        AddOutline(outer, new Color32(226, 178, 82, 220), new Vector2(2.2f, -2.2f));
        var middle = Panel(outer.transform, name + "Middle", new Color32(184, 124, 55, 255));
        Anchor(middle, new Vector2(0.018f, 0.018f), new Vector2(0.982f, 0.982f));
        middle.GetComponent<Image>().raycastTarget = false;
        var face = Panel(outer.transform, name + "Face", faceColor);
        Anchor(face, new Vector2(0.035f, 0.035f), new Vector2(0.965f, 0.965f));
        face.GetComponent<Image>().raycastTarget = false;
        AddOutline(face, new Color32(74, 43, 20, 160), new Vector2(1f, -1f));
        DrawCornerStuds(outer.transform);
        return outer;
    }

    private void DrawCornerStuds(Transform parent)
    {
        Vector2 size = new Vector2(0.032f, 0.032f);
        Vector2[] points =
        {
            new Vector2(0.018f, 0.018f), new Vector2(0.950f, 0.018f),
            new Vector2(0.018f, 0.950f), new Vector2(0.950f, 0.950f)
        };
        foreach (Vector2 p in points)
        {
            var stud = Panel(parent, "CornerStud", new Color32(228, 176, 80, 230));
            Anchor(stud, p, p + size);
            stud.GetComponent<Image>().raycastTarget = false;
            AddOutline(stud, new Color32(72, 42, 20, 160), new Vector2(1f, -1f));
        }
    }

    private GameObject Panel(Transform parent, string name, Color color)
    {
        var go = new GameObject(name);
        go.transform.SetParent(parent, false);
        var image = go.AddComponent<Image>();
        image.color = color;
        if (canvas != null && name != "BgmToggle")
        {
            Transform toggle = canvas.transform.Find("BgmToggle");
            if (toggle != null) toggle.SetAsLastSibling();
        }
        return go;
    }

    private void AddOutline(GameObject go, Color color, Vector2 distance)
    {
        var outline = go.AddComponent<Outline>();
        outline.effectColor = color;
        outline.effectDistance = distance;
    }

    private void BuildEffectSprites()
    {
        softCircleSprite = MakeSoftCircleSprite("SoftCircleEffect", 64);
        sparkSprite = MakeSparkSprite("SparkEffect", 64);
        bgmOnSprite = MakeSpeakerSprite("BgmSpeakerOn", 64, false);
        bgmOffSprite = MakeSpeakerSprite("BgmSpeakerOff", 64, true);
    }

    private Sprite MakeSpeakerSprite(string name, int size, bool muted)
    {
        var tex = new Texture2D(size, size, TextureFormat.RGBA32, false);
        tex.wrapMode = TextureWrapMode.Clamp;
        tex.filterMode = FilterMode.Bilinear;
        Color32 clear = new Color32(0, 0, 0, 0);
        Color32 solid = new Color32(255, 255, 255, 255);
        Color32[] pixels = new Color32[size * size];
        for (int i = 0; i < pixels.Length; ++i) pixels[i] = clear;

        for (int y = 24; y <= 40; ++y)
        {
            for (int x = 10; x <= 22; ++x) pixels[y * size + x] = solid;
        }
        for (int y = 18; y <= 46; ++y)
        {
            float t = Mathf.Abs(y - 32f) / 14f;
            int right = Mathf.RoundToInt(Mathf.Lerp(42f, 28f, t));
            for (int x = 22; x <= right; ++x) pixels[y * size + x] = solid;
        }

        DrawArc(pixels, size, 34, 32, 13, -48, 48, 3, solid);
        DrawArc(pixels, size, 34, 32, 22, -48, 48, 3, solid);
        if (muted)
        {
            DrawLine(pixels, size, 47, 20, 57, 44, 4, solid);
            DrawLine(pixels, size, 57, 20, 47, 44, 4, solid);
        }

        tex.SetPixels32(pixels);
        tex.Apply();
        tex.name = name;
        return Sprite.Create(tex, new Rect(0, 0, size, size), new Vector2(0.5f, 0.5f), 100f);
    }

    private void DrawArc(Color32[] pixels, int size, int cx, int cy, int radius, float minAngle, float maxAngle, int thickness, Color32 color)
    {
        float inner = radius - thickness;
        float outer = radius + thickness;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                float dx = x - cx;
                float dy = y - cy;
                float dist = Mathf.Sqrt(dx * dx + dy * dy);
                if (dist < inner || dist > outer) continue;
                float angle = Mathf.Atan2(dy, dx) * Mathf.Rad2Deg;
                if (angle >= minAngle && angle <= maxAngle) pixels[y * size + x] = color;
            }
        }
    }

    private void DrawLine(Color32[] pixels, int size, int x0, int y0, int x1, int y1, int thickness, Color32 color)
    {
        Vector2 a = new Vector2(x0, y0);
        Vector2 b = new Vector2(x1, y1);
        Vector2 ab = b - a;
        float lenSq = Mathf.Max(1f, ab.sqrMagnitude);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                Vector2 p = new Vector2(x, y);
                float t = Mathf.Clamp01(Vector2.Dot(p - a, ab) / lenSq);
                if (Vector2.Distance(p, a + ab * t) <= thickness) pixels[y * size + x] = color;
            }
        }
    }

    private Sprite MakeSoftCircleSprite(string name, int size)
    {
        var tex = new Texture2D(size, size, TextureFormat.RGBA32, false);
        tex.wrapMode = TextureWrapMode.Clamp;
        tex.filterMode = FilterMode.Bilinear;
        Color[] pixels = new Color[size * size];
        float center = (size - 1) * 0.5f;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                float dx = (x - center) / center;
                float dy = (y - center) / center;
                float d = Mathf.Sqrt(dx * dx + dy * dy);
                float alpha = Mathf.Clamp01(1.0f - Mathf.SmoothStep(0.20f, 1.0f, d));
                pixels[y * size + x] = new Color(1f, 1f, 1f, alpha);
            }
        }
        tex.SetPixels(pixels);
        tex.Apply();
        tex.name = name;
        return Sprite.Create(tex, new Rect(0, 0, size, size), new Vector2(0.5f, 0.5f), 100f);
    }

    private Sprite MakeSparkSprite(string name, int size)
    {
        var tex = new Texture2D(size, size, TextureFormat.RGBA32, false);
        tex.wrapMode = TextureWrapMode.Clamp;
        tex.filterMode = FilterMode.Bilinear;
        Color[] pixels = new Color[size * size];
        float center = (size - 1) * 0.5f;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                float dx = Mathf.Abs((x - center) / center);
                float dy = Mathf.Abs((y - center) / center);
                float diamond = dx + dy * 1.8f;
                float core = Mathf.Clamp01(1.0f - Mathf.SmoothStep(0.04f, 0.85f, diamond));
                pixels[y * size + x] = new Color(1f, 1f, 1f, core);
            }
        }
        tex.SetPixels(pixels);
        tex.Apply();
        tex.name = name;
        return Sprite.Create(tex, new Rect(0, 0, size, size), new Vector2(0.5f, 0.5f), 100f);
    }

    private void SetEffectSprite(GameObject go, Sprite sprite)
    {
        if (go == null || sprite == null) return;
        var image = go.GetComponent<Image>();
        image.sprite = sprite;
        image.type = Image.Type.Simple;
        image.preserveAspect = false;
    }

    private void TransparentButton(Transform parent, Vector2 min, Vector2 max, Action onClick)
    {
        var go = Panel(parent, "ClickLayer", new Color32(0, 0, 0, 0));
        Anchor(go, min, max);
        var image = go.GetComponent<Image>();
        image.raycastTarget = true;
        var button = go.AddComponent<Button>();
        button.transition = Selectable.Transition.None;
        button.onClick.AddListener(() => onClick?.Invoke());
    }

    private void DrawBar(Transform parent, Vector2 min, Vector2 max, int value, int maxValue, Color fillColor, string label, int fontSize)
    {
        var bg = Panel(parent, "BarBg", new Color32(55, 37, 25, 230));
        Anchor(bg, min, max);
        AddOutline(bg, new Color32(40, 24, 12, 180), new Vector2(1f, -1f));
        float ratio = maxValue <= 0 ? 0f : Mathf.Clamp01((float)value / maxValue);
        var fill = Panel(bg.transform, "BarFill", fillColor);
        Anchor(fill, Vector2.zero, new Vector2(ratio, 1f));
        fill.GetComponent<Image>().raycastTarget = false;
        Text(bg.transform, label, fontSize, FontStyle.Bold, Color.white, Vector2.zero, Vector2.one, TextAnchor.MiddleCenter);
    }

    private bool ShowAttackVisual(Unit source, Unit target, string reason, Action onImpact)
    {
        if (canvas == null || source == null || target == null || !inBattle) return false;
        Vector2 start;
        Vector2 end;
        if (!TryUnitScreenAnchor(source, out start) || !TryUnitScreenAnchor(target, out end)) return false;
        attackVisualQueue.Enqueue(new AttackVisualRequest(source, target, reason, onImpact));
        if (!processingAttackVisuals) StartCoroutine(ProcessAttackVisualQueue());
        return true;
    }

    private IEnumerator ProcessAttackVisualQueue()
    {
        processingAttackVisuals = true;
        while (attackVisualQueue.Count > 0)
        {
            AttackVisualRequest request = attackVisualQueue.Dequeue();
            yield return PlayAttackVisual(request);
            yield return new WaitForSeconds(0.06f);
        }
        processingAttackVisuals = false;
    }

    private IEnumerator WaitForAttackVisuals()
    {
        while (processingAttackVisuals || attackVisualQueue.Count > 0)
        {
            yield return null;
        }
    }

    private IEnumerator PlayAttackVisual(AttackVisualRequest request)
    {
        Unit source = request.Source;
        Unit target = request.Target;
        if (canvas == null || source == null || target == null || !inBattle) yield break;
        Vector2 start;
        Vector2 end;
        if (!TryUnitScreenAnchor(source, out start) || !TryUnitScreenAnchor(target, out end)) yield break;

        Color32 color = FactionEffectColor(source);
        string row = RowShort(source.Row);
        if (row == "F")
        {
            yield return BumpUnitCard(source, start, end);
            StartCoroutine(HitFlash(end, color));
        }
        else if (row == "B")
        {
            yield return ProjectileAttackEffect(start, end, color);
        }
        else
        {
            yield return ParticleAttackEffect(start, end, color);
        }
        request.OnImpact?.Invoke();
        yield return new WaitForSeconds(0.16f);
    }

    private IEnumerator BumpUnitCard(Unit source, Vector2 start, Vector2 end)
    {
        RectTransform rect;
        if (!unitCardRects.TryGetValue(source, out rect) || rect == null) yield break;
        Vector2 delta = new Vector2((end.x - start.x) * 1500f, (end.y - start.y) * 900f);
        if (delta.sqrMagnitude < 0.01f) yield break;
        Vector2 dir = delta.normalized;
        Vector2 origin = rect.anchoredPosition;
        Vector3 originScale = rect.localScale;
        Vector2 bump = dir * 58f;
        for (float t = 0f; t < 0.11f; t += Time.deltaTime)
        {
            if (rect == null) yield break;
            float k = Mathf.SmoothStep(0f, 1f, t / 0.11f);
            rect.anchoredPosition = Vector2.Lerp(origin, origin + bump, k);
            rect.localScale = originScale * (1f + 0.045f * k);
            yield return null;
        }
        for (float t = 0f; t < 0.15f; t += Time.deltaTime)
        {
            if (rect == null) yield break;
            float k = Mathf.SmoothStep(0f, 1f, t / 0.15f);
            rect.anchoredPosition = Vector2.Lerp(origin + bump, origin, k);
            rect.localScale = originScale * (1.045f - 0.045f * k);
            yield return null;
        }
        if (rect != null)
        {
            rect.anchoredPosition = origin;
            rect.localScale = originScale;
        }
    }

    private IEnumerator ChargeAttackEffect(Vector2 start, Vector2 end, Color color)
    {
        StartCoroutine(FlashLine(start, end, color, 22f, 0.20f));
        StartCoroutine(PathEmbers(start, end, color, 12, 0.17f));
        GameObject marker = Panel(canvas.transform, "ChargeEffect", color);
        SetEffectSprite(marker, softCircleSprite);
        marker.GetComponent<Image>().raycastTarget = false;
        AddOutline(marker, Color.white, new Vector2(1.2f, -1.2f));
        yield return MoveEffect(marker, start, Vector2.Lerp(start, end, 0.92f), color, 0.18f, new Vector2(0.026f, 0.043f));
        StartCoroutine(SlashBurst(end, color));
        StartCoroutine(ImpactBurst(end, color, 14));
        yield return MoveEffect(marker, Vector2.Lerp(start, end, 0.86f), start, color, 0.09f, new Vector2(0.013f, 0.023f));
        if (marker != null) Destroy(marker);
        StartCoroutine(HitFlash(end, color));
    }

    private IEnumerator ProjectileAttackEffect(Vector2 start, Vector2 end, Color color)
    {
        StartCoroutine(FlashLine(start, end, color, 12f, 0.32f));
        StartCoroutine(PathEmbers(start, end, color, 16, 0.28f));
        GameObject bolt = Panel(canvas.transform, "ProjectileEffect", color);
        SetEffectSprite(bolt, softCircleSprite);
        bolt.GetComponent<Image>().raycastTarget = false;
        AddOutline(bolt, Color.white, new Vector2(1f, -1f));
        yield return MoveEffect(bolt, start, end, color, 0.30f, new Vector2(0.020f, 0.034f));
        if (bolt != null) Destroy(bolt);
        StartCoroutine(ImpactBurst(end, color, 11));
        StartCoroutine(HitFlash(end, color));
    }

    private IEnumerator ParticleAttackEffect(Vector2 start, Vector2 end, Color color)
    {
        StartCoroutine(FlashLine(start, end, color, 8f, 0.28f));
        StartCoroutine(PathEmbers(start, end, color, 18, 0.26f));
        for (int i = 0; i < 16; ++i)
        {
            StartCoroutine(DelayedParticle(start, end, color, i * 0.018f));
        }
        yield return new WaitForSeconds(0.31f);
        StartCoroutine(ImpactBurst(end, color, 13));
        StartCoroutine(HitFlash(end, color));
    }

    private IEnumerator DelayedParticle(Vector2 start, Vector2 end, Color color, float delay)
    {
        yield return new WaitForSeconds(delay);
        Vector2 jitter = new Vector2(UnityEngine.Random.Range(-0.028f, 0.028f), UnityEngine.Random.Range(-0.020f, 0.020f));
        GameObject particle = Panel(canvas.transform, "ParticleEffect", color);
        SetEffectSprite(particle, softCircleSprite);
        particle.GetComponent<Image>().raycastTarget = false;
        AddOutline(particle, Color.white, new Vector2(0.8f, -0.8f));
        yield return MoveEffect(particle, start + jitter, end - jitter * 0.35f, color, 0.25f, new Vector2(0.007f, 0.014f));
        if (particle != null) Destroy(particle);
    }

    private IEnumerator PathEmbers(Vector2 start, Vector2 end, Color color, int count, float span)
    {
        Vector2 delta = end - start;
        Vector2 normal = new Vector2(-delta.y, delta.x).normalized;
        for (int i = 0; i < count; ++i)
        {
            float k = count <= 1 ? 0.5f : (float)i / (count - 1);
            Vector2 jitter = normal * UnityEngine.Random.Range(-0.020f, 0.020f) + delta.normalized * UnityEngine.Random.Range(-0.010f, 0.010f);
            Vector2 pos = Vector2.Lerp(start, end, k) + jitter;
            float delay = span * k * 0.75f;
            StartCoroutine(EmberFade(pos, color, delay));
        }
        yield return null;
    }

    private IEnumerator EmberFade(Vector2 center, Color color, float delay)
    {
        yield return new WaitForSeconds(delay);
        GameObject ember = EffectRect("TrailEmber", new Color(color.r, color.g, color.b, 0.58f), center, new Vector2(UnityEngine.Random.Range(12f, 28f), UnityEngine.Random.Range(10f, 24f)), UnityEngine.Random.Range(0f, 180f));
        SetEffectSprite(ember, softCircleSprite);
        yield return FadeEffect(ember, UnityEngine.Random.Range(0.16f, 0.28f));
    }

    private IEnumerator HitFlash(Vector2 center, Color color)
    {
        GameObject flash = Panel(canvas.transform, "HitFlash", new Color(color.r, color.g, color.b, 0.45f));
        SetEffectSprite(flash, softCircleSprite);
        flash.GetComponent<Image>().raycastTarget = false;
        AddOutline(flash, Color.white, new Vector2(1.4f, -1.4f));
        GameObject ring = Panel(canvas.transform, "HitRing", new Color(1f, 1f, 1f, 0.55f));
        SetEffectSprite(ring, softCircleSprite);
        ring.GetComponent<Image>().raycastTarget = false;
        Vector2 size = new Vector2(0.038f, 0.064f);
        for (float t = 0f; t < 0.24f; t += Time.deltaTime)
        {
            if (flash == null) yield break;
            float k = t / 0.24f;
            Vector2 grow = size * (1f + k * 1.35f);
            Anchor(flash, center - grow, center + grow);
            Image image = flash.GetComponent<Image>();
            image.color = new Color(color.r, color.g, color.b, 0.58f * (1f - k));
            if (ring != null)
            {
                Vector2 ringGrow = size * (1.3f + k * 2.4f);
                Anchor(ring, center - ringGrow, center + ringGrow);
                ring.GetComponent<Image>().color = new Color(1f, 1f, 1f, 0.42f * (1f - k));
            }
            yield return null;
        }
        if (flash != null) Destroy(flash);
        if (ring != null) Destroy(ring);
    }

    private IEnumerator SlashBurst(Vector2 center, Color color)
    {
        for (int i = 0; i < 4; ++i)
        {
            float angle = -42f + i * 28f;
            GameObject slash = EffectRect("SlashBurst", new Color(color.r, color.g, color.b, 0.82f), center, new Vector2(185f - i * 22f, 14f), angle);
            SetEffectSprite(slash, sparkSprite);
            StartCoroutine(FadeEffect(slash, 0.20f));
            yield return new WaitForSeconds(0.025f);
        }
    }

    private IEnumerator ImpactBurst(Vector2 center, Color color, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            float angle = 360f * i / Mathf.Max(1, count) + UnityEngine.Random.Range(-12f, 12f);
            float distance = UnityEngine.Random.Range(38f, 88f);
            StartCoroutine(SparkFly(center, angle, distance, color, i * 0.006f));
        }
        yield return null;
    }

    private IEnumerator SparkFly(Vector2 center, float angle, float distancePx, Color color, float delay)
    {
        yield return new WaitForSeconds(delay);
        GameObject spark = EffectRect("ImpactSpark", new Color(color.r, color.g, color.b, 0.86f), center, new Vector2(UnityEngine.Random.Range(22f, 46f), UnityEngine.Random.Range(4f, 8f)), angle);
        SetEffectSprite(spark, sparkSprite);
        Image image = spark.GetComponent<Image>();
        Vector2 dir = new Vector2(Mathf.Cos(angle * Mathf.Deg2Rad), Mathf.Sin(angle * Mathf.Deg2Rad));
        for (float t = 0f; t < 0.34f; t += Time.deltaTime)
        {
            if (spark == null) yield break;
            float k = Mathf.Clamp01(t / 0.34f);
            Vector2 offset = new Vector2(dir.x * distancePx * k / 1500f, dir.y * distancePx * k / 900f);
            Vector2 size = new Vector2(Mathf.Lerp(0.016f, 0.004f, k), Mathf.Lerp(0.008f, 0.002f, k));
            Anchor(spark, center + offset - size, center + offset + size);
            image.color = new Color(color.r, color.g, color.b, 0.86f * (1f - k));
            yield return null;
        }
        if (spark != null) Destroy(spark);
    }

    private IEnumerator FlashLine(Vector2 start, Vector2 end, Color color, float thickness, float duration)
    {
        Vector2 center = Vector2.Lerp(start, end, 0.5f);
        GameObject line = EffectRect("AttackLine", new Color(color.r, color.g, color.b, 0.55f), center, new Vector2(EffectPixelLength(start, end), thickness), EffectAngle(start, end));
        AddOutline(line, Color.white, new Vector2(1f, -1f));
        yield return FadeEffect(line, duration);
    }

    private void ShowStatusParticles(Unit unit, Color color, bool shield)
    {
        if (canvas == null || unit == null || !inBattle) return;
        Vector2 center;
        if (!TryUnitScreenAnchor(unit, out center)) return;
        StartCoroutine(StatusParticleBurst(center, color, shield));
    }

    private IEnumerator StatusParticleBurst(Vector2 center, Color color, bool shield)
    {
        int count = shield ? 18 : 14;
        for (int i = 0; i < count; ++i)
        {
            float angle = 360f * i / count + UnityEngine.Random.Range(-8f, 8f);
            float radius = UnityEngine.Random.Range(shield ? 48f : 28f, shield ? 86f : 66f);
            Vector2 dir = new Vector2(Mathf.Cos(angle * Mathf.Deg2Rad), Mathf.Sin(angle * Mathf.Deg2Rad));
            Vector2 start = shield ? center + new Vector2(dir.x * radius / 1500f, dir.y * radius / 900f) : center;
            Vector2 end = shield ? center + new Vector2(dir.x * (radius + 14f) / 1500f, dir.y * (radius + 14f) / 900f) : center + new Vector2(dir.x * radius / 1500f, dir.y * radius / 900f);
            StartCoroutine(StatusParticle(start, end, color, i * 0.010f, shield));
        }
        if (shield) StartCoroutine(HitFlash(center, color));
        yield return null;
    }

    private IEnumerator StatusParticle(Vector2 start, Vector2 end, Color color, float delay, bool shield)
    {
        yield return new WaitForSeconds(delay);
        GameObject particle = EffectRect(shield ? "ShieldParticle" : "HealParticle", new Color(color.r, color.g, color.b, 0.82f), start, shield ? new Vector2(10f, 22f) : new Vector2(16f, 16f), UnityEngine.Random.Range(0f, 180f));
        SetEffectSprite(particle, shield ? sparkSprite : softCircleSprite);
        Image image = particle.GetComponent<Image>();
        float duration = shield ? 0.34f : 0.42f;
        for (float t = 0f; t < duration; t += Time.deltaTime)
        {
            if (particle == null) yield break;
            float k = Mathf.Clamp01(t / duration);
            Vector2 pos = shield ? Vector2.Lerp(start, end, k) : Vector2.Lerp(start, end, Mathf.SmoothStep(0f, 1f, k));
            Vector2 size = shield ? new Vector2(Mathf.Lerp(0.010f, 0.004f, k), Mathf.Lerp(0.022f, 0.006f, k)) : new Vector2(Mathf.Lerp(0.010f, 0.022f, k), Mathf.Lerp(0.017f, 0.030f, k));
            Anchor(particle, pos - size, pos + size);
            image.color = new Color(color.r, color.g, color.b, 0.82f * (1f - k));
            yield return null;
        }
        if (particle != null) Destroy(particle);
    }

    private GameObject EffectRect(string name, Color color, Vector2 normalizedCenter, Vector2 sizePx, float angle)
    {
        var go = Panel(canvas.transform, name, color);
        SetEffectSprite(go, softCircleSprite);
        var rect = go.GetComponent<RectTransform>();
        rect.anchorMin = normalizedCenter;
        rect.anchorMax = normalizedCenter;
        rect.sizeDelta = sizePx;
        rect.anchoredPosition = Vector2.zero;
        rect.localEulerAngles = new Vector3(0f, 0f, angle);
        go.GetComponent<Image>().raycastTarget = false;
        return go;
    }

    private IEnumerator FadeEffect(GameObject go, float duration)
    {
        if (go == null) yield break;
        Image image = go.GetComponent<Image>();
        Color start = image.color;
        for (float t = 0f; t < duration; t += Time.deltaTime)
        {
            if (go == null) yield break;
            float k = Mathf.Clamp01(t / duration);
            image.color = new Color(start.r, start.g, start.b, start.a * (1f - k));
            yield return null;
        }
        if (go != null) Destroy(go);
    }

    private float EffectPixelLength(Vector2 start, Vector2 end)
    {
        Vector2 delta = new Vector2((end.x - start.x) * 1500f, (end.y - start.y) * 900f);
        return Mathf.Max(30f, delta.magnitude);
    }

    private float EffectAngle(Vector2 start, Vector2 end)
    {
        Vector2 delta = new Vector2((end.x - start.x) * 1500f, (end.y - start.y) * 900f);
        return Mathf.Atan2(delta.y, delta.x) * Mathf.Rad2Deg;
    }

    private IEnumerator MoveEffect(GameObject go, Vector2 start, Vector2 end, Color color, float duration, Vector2 size)
    {
        if (go == null) yield break;
        Image image = go.GetComponent<Image>();
        for (float t = 0f; t < duration; t += Time.deltaTime)
        {
            if (go == null) yield break;
            float k = Mathf.Clamp01(t / duration);
            Vector2 center = Vector2.Lerp(start, end, Mathf.SmoothStep(0f, 1f, k));
            Anchor(go, center - size, center + size);
            image.color = new Color(color.r, color.g, color.b, 0.95f * (1f - k * 0.25f));
            yield return null;
        }
        if (go != null) Anchor(go, end - size, end + size);
    }

    private Color32 FactionEffectColor(Unit unit)
    {
        if (unit == null) return new Color32(255, 255, 255, 255);
        if (unit.Enemy && chapterIndex >= chapters.Length - 1) return new Color32(248, 248, 238, 255);
        if (unit.Faction == "人族") return new Color32(64, 146, 255, 255);
        if (unit.Faction == "魔族") return new Color32(148, 72, 222, 255);
        if (unit.Faction == "精灵族") return new Color32(64, 190, 95, 255);
        if (unit.Faction == "天使") return new Color32(242, 202, 70, 255);
        if (unit.Faction == "吸血鬼") return new Color32(218, 48, 54, 255);
        if (unit.Boss) return new Color32(230, 70, 52, 255);
        return new Color32(245, 226, 150, 255);
    }

    private void ShowCombatFeedback(Unit unit, string label, Color color)
    {
        if (canvas == null || unit == null || !inBattle) return;
        Vector2 center;
        if (!TryUnitScreenAnchor(unit, out center)) return;
        var text = Text(canvas.transform, label, 30, FontStyle.Bold, color,
            center + new Vector2(-0.060f, -0.020f), center + new Vector2(0.060f, 0.055f), TextAnchor.MiddleCenter);
        var outline = text.gameObject.AddComponent<Outline>();
        outline.effectColor = Color.black;
        outline.effectDistance = new Vector2(2.5f, -2.5f);
        StartCoroutine(FloatAndFade(text));
    }

    private IEnumerator FloatAndFade(Text text)
    {
        if (text == null) yield break;
        RectTransform rect = text.rectTransform;
        Color start = text.color;
        for (float t = 0f; t < 0.75f; t += Time.deltaTime)
        {
            if (text == null) yield break;
            float k = t / 0.75f;
            rect.anchoredPosition += new Vector2(0f, 38f * Time.deltaTime);
            rect.localScale = Vector3.one * (1.12f - 0.18f * k);
            text.color = new Color(start.r, start.g, start.b, 1f - k);
            yield return null;
        }
        if (text != null) Destroy(text.gameObject);
    }

    private bool TryUnitScreenAnchor(Unit unit, out Vector2 center)
    {
        center = new Vector2(0.5f, 0.5f);
        for (int i = 0; i < 5; ++i)
        {
            if (enemyBoard[i] == unit)
            {
                center = BoardToScreen(BoardSlotPosition(i, true));
                return true;
            }
            if (playerBoard[i] == unit)
            {
                center = BoardToScreen(BoardSlotPosition(i, false));
                return true;
            }
        }
        return false;
    }

    private Vector2 BoardSlotPosition(int index, bool enemy)
    {
        Vector2[] enemyPos =
        {
            new Vector2(0.18f, 0.585f), new Vector2(0.82f, 0.585f),
            new Vector2(0.34f, 0.735f), new Vector2(0.66f, 0.735f),
            new Vector2(0.50f, 0.875f)
        };
        Vector2[] playerPos =
        {
            new Vector2(0.18f, 0.415f), new Vector2(0.82f, 0.415f),
            new Vector2(0.34f, 0.265f), new Vector2(0.66f, 0.265f),
            new Vector2(0.50f, 0.125f)
        };
        return enemy ? enemyPos[index] : playerPos[index];
    }

    private Vector2 BoardToScreen(Vector2 boardLocal)
    {
        Vector2 boardMin = new Vector2(0.218f, 0.255f);
        Vector2 boardMax = new Vector2(0.835f, 0.965f);
        return new Vector2(Mathf.Lerp(boardMin.x, boardMax.x, boardLocal.x), Mathf.Lerp(boardMin.y, boardMax.y, boardLocal.y));
    }

    private Color32 UnitCardColor(Unit unit, bool enemy)
    {
        if (unit.Hero) return new Color32(224, 207, 142, 255);
        if (enemy) return new Color32(222, 160, 103, 255);
        if (unit.Faction == "人族") return new Color32(224, 198, 136, 255);
        if (unit.Faction == "吸血鬼") return new Color32(210, 162, 160, 255);
        if (unit.Faction == "精灵族") return new Color32(174, 205, 142, 255);
        if (unit.Faction == "天使") return new Color32(230, 218, 160, 255);
        if (unit.Faction == "魔族") return new Color32(190, 160, 198, 255);
        return new Color32(210, 188, 128, 255);
    }

    private Color32 UnitBorderColor(Unit unit, bool enemy)
    {
        if (unit.Boss) return new Color32(170, 42, 28, 255);
        if (unit.Hero) return new Color32(238, 202, 64, 255);
        return enemy ? new Color32(116, 58, 32, 255) : new Color32(84, 52, 24, 255);
    }

    private string RowShort(string row)
    {
        if (row == "前排") return "F";
        if (row == "中排") return "M";
        if (row == "后排") return "B";
        return row;
    }

    private GameObject AtlasPicture(Transform parent, string relativePath, int cols, int rows, int index, Vector2 min, Vector2 max, bool preserveAspect)
    {
        var go = new GameObject("AtlasPicture");
        go.transform.SetParent(parent, false);
        var image = go.AddComponent<Image>();
        image.color = new Color32(24, 18, 12, 255);
        Sprite sprite = LoadAtlasSprite(relativePath, cols, rows, index);
        if (sprite != null)
        {
            image.sprite = sprite;
            image.color = Color.white;
            image.preserveAspect = preserveAspect;
        }
        image.raycastTarget = false;
        Anchor(go, min, max);
        return go;
    }

    private GameObject Picture(Transform parent, string relativePath, Vector2 min, Vector2 max, bool preserveAspect)
    {
        var go = new GameObject("Picture");
        go.transform.SetParent(parent, false);
        var image = go.AddComponent<Image>();
        image.color = new Color32(20, 18, 16, 255);
        Sprite sprite = LoadSprite(relativePath);
        if (sprite != null)
        {
            image.sprite = sprite;
            image.color = Color.white;
            image.preserveAspect = preserveAspect;
        }
        image.raycastTarget = false;
        Anchor(go, min, max);
        return go;
    }

    private Sprite LoadSprite(string relativePath)
    {
        if (string.IsNullOrEmpty(relativePath)) return null;
        if (spriteCache.TryGetValue(relativePath, out Sprite cached)) return cached;

        string normalized = relativePath.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
        string path = Path.Combine(Application.streamingAssetsPath, normalized);
        if (!File.Exists(path)) return null;

        byte[] bytes = File.ReadAllBytes(path);
        var texture = new Texture2D(2, 2);
        if (!texture.LoadImage(bytes)) return null;
        texture.name = Path.GetFileNameWithoutExtension(path);
        Sprite sprite = Sprite.Create(texture, new Rect(0, 0, texture.width, texture.height), new Vector2(0.5f, 0.5f));
        spriteCache[relativePath] = sprite;
        return sprite;
    }

    private Sprite LoadAtlasSprite(string relativePath, int cols, int rows, int index)
    {
        string key = relativePath + "#" + cols + "x" + rows + ":" + index;
        if (spriteCache.TryGetValue(key, out Sprite cached)) return cached;
        Texture2D texture = LoadTexture(relativePath);
        if (texture == null) return null;
        int safeIndex = Mathf.Clamp(index, 0, cols * rows - 1);
        int col = safeIndex % cols;
        int rowFromTop = safeIndex / cols;
        float cellW = texture.width / (float)cols;
        float cellH = texture.height / (float)rows;
        Rect rect = new Rect(col * cellW, texture.height - (rowFromTop + 1) * cellH, cellW, cellH);
        Sprite sprite = Sprite.Create(texture, rect, new Vector2(0.5f, 0.5f));
        spriteCache[key] = sprite;
        return sprite;
    }

    private Texture2D LoadTexture(string relativePath)
    {
        if (textureCache.TryGetValue(relativePath, out Texture2D cached)) return cached;
        string normalized = relativePath.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
        string path = Path.Combine(Application.streamingAssetsPath, normalized);
        if (!File.Exists(path)) return null;
        byte[] bytes = File.ReadAllBytes(path);
        var texture = new Texture2D(2, 2);
        if (!texture.LoadImage(bytes)) return null;
        texture.name = Path.GetFileNameWithoutExtension(path);
        textureCache[relativePath] = texture;
        return texture;
    }

    private Text Text(Transform parent, string content, int size, FontStyle style, Color color, Vector2 min, Vector2 max, TextAnchor anchor)
    {
        if (!string.IsNullOrEmpty(content) && content.Contains("\u6280\u80fd\u624b\u724c") && content.Contains("\u6bcf\u56de\u5408"))
        {
            content = "";
        }
        var go = new GameObject("Text");
        go.transform.SetParent(parent, false);
        var text = go.AddComponent<Text>();
        text.font = font;
        text.text = content;
        text.fontSize = size;
        text.fontStyle = style;
        text.color = color;
        text.alignment = anchor;
        text.horizontalOverflow = HorizontalWrapMode.Wrap;
        text.verticalOverflow = VerticalWrapMode.Truncate;
        text.raycastTarget = false;
        Anchor(go, min, max);
        return text;
    }

    private void Button(Transform parent, string label, Vector2 min, Vector2 max, Action onClick, bool highlighted = false)
    {
        if (label == "\u7ee7\u7eed")
        {
            TransparentButton(parent, min, max, onClick);
            return;
        }
        var go = Panel(parent, "Button", highlighted ? new Color32(238, 202, 82, 255) : new Color32(112, 66, 30, 255));
        Anchor(go, min, max);
        AddOutline(go, highlighted ? new Color32(255, 244, 170, 255) : new Color32(55, 30, 12, 240), new Vector2(1.5f, -1.5f));
        var button = go.AddComponent<Button>();
        button.targetGraphic = go.GetComponent<Image>();
        var colors = button.colors;
        colors.normalColor = Color.white;
        colors.highlightedColor = highlighted ? new Color32(255, 222, 100, 255) : new Color32(136, 82, 40, 255);
        colors.pressedColor = new Color32(80, 45, 20, 255);
        colors.selectedColor = colors.highlightedColor;
        button.colors = colors;
        button.onClick.AddListener(() => onClick?.Invoke());
        int fontSize = label.Length > 38 ? 11 : label.Length > 22 ? 13 : 15;
        Text(go.transform, label, fontSize, FontStyle.Bold, highlighted ? Color.black : new Color32(255, 232, 180, 255), new Vector2(0.04f, 0.04f), new Vector2(0.96f, 0.96f), TextAnchor.MiddleCenter);
    }

    private void CommandButton(Transform parent, string label, Vector2 min, Vector2 max, Action onClick, bool highlighted)
    {
        var hit = Panel(parent, "CommandHitArea", new Color32(255, 255, 255, 1));
        Anchor(hit, min, max);
        var image = hit.GetComponent<Image>();
        image.raycastTarget = true;
        var button = hit.AddComponent<Button>();
        button.targetGraphic = image;
        var colors = button.colors;
        colors.normalColor = new Color32(255, 255, 255, 1);
        colors.highlightedColor = new Color32(255, 236, 160, 45);
        colors.pressedColor = new Color32(90, 45, 20, 80);
        colors.selectedColor = colors.highlightedColor;
        button.colors = colors;
        button.onClick.AddListener(() => onClick?.Invoke());
        Text(parent, label, label.Length > 8 ? 13 : 15, FontStyle.Bold, highlighted ? new Color32(255, 245, 190, 255) : new Color32(255, 230, 178, 255), min, max, TextAnchor.MiddleCenter);
    }

    private static void Stretch(GameObject go)
    {
        Anchor(go, Vector2.zero, Vector2.one);
    }

    private static void Anchor(GameObject go, Vector2 min, Vector2 max)
    {
        var rect = go.GetComponent<RectTransform>();
        if (rect == null) rect = go.AddComponent<RectTransform>();
        rect.anchorMin = min;
        rect.anchorMax = max;
        rect.offsetMin = Vector2.zero;
        rect.offsetMax = Vector2.zero;
    }
}
