#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// ============================================================================
// 游戏常量
// ============================================================================

/// 主角在玩家棋盘中的固定槽位索引（己方中排左侧，不可被其他单位占用）
inline constexpr int kHeroSlot = 2;

/// 技能槽最大数量（底部面板同时最多持有 5 张技能卡）
inline constexpr int kMaxSkills = 5;

/// 遗物槽最大数量（右侧面板同时最多持有 7 个遗物）
inline constexpr int kMaxRelics = 7;

// ============================================================================
// 核心数据结构
// ============================================================================

/// 单位模板 —— 定义一种单位的静态属性
/// 对应 data/ 中尚未独立化的单位数据库
struct UnitTemplate
{
    QString name;    ///< 单位名称（如"见习剑士"）
    QString faction; ///< 所属阵营：人族 / 吸血鬼 / 精灵族 / 天使 / 魔族 / 命运
    QString row;     ///< 默认站位偏好：前排 / 中排 / 后排
    QString skill;   ///< 携带技能名称（如"斩击"）
    int hp;          ///< 基础生命值
    int atk;         ///< 基础攻击力
};

/// 遗物实例 —— 记录遗物的名称、剩余使用次数与槽位属性
/// uses: -1=永久有效, >0=剩余次数, 0=待清理
/// slotless: true=一次性遗物不占槽，触发后直接丢弃
struct RelicInstance
{
    QString name;
    int uses = -1;         ///< -1=永久, >0=剩余次数
    bool slotless = false; ///< 一次性遗物不占用槽位
};

/// 状态效果 —— 附加在单位上的 buff/debuff
/// layers: 层数（1-N），decays: 是否每回合-1
struct StatusEffect
{
    QString name;     ///< 状态名称："石化"/"脆弱"/"荆棘"/"回响"/"铸剑"
    int layers = 1;   ///< 当前层数
    bool decays = false; ///< 每回合结束时是否-1层
};

/// 单位实例 —— 战斗中某个单位在棋盘上的动态状态
/// 模板提供基线，实例记录当前 HP / 护盾 / 回合计数等
struct UnitInstance
{
    UnitTemplate base;       ///< 原始模板（atk 字段在战斗中被遗物/buff 修改）
    int hp = 0;              ///< 当前生命值
    int shield = 0;          ///< 当前护盾值（优先抵扣伤害）
    int aliveRounds = 0;     ///< 存活回合数（用于技能生成周期，每 2 回合产一张）
    QVector<StatusEffect> statuses; ///< 当前附加的状态效果
    bool hero = false;       ///< 是否为主角（不可移除、不可出售）
    bool boss = false;       ///< 是否为 Boss 单位（触发特殊机制）
    bool revived = false;    ///< 本场战斗是否已触发过复活
    bool protectedDeath = false; ///< 是否被"命运改写"保护（免死一次）
};

/// 技能卡牌 —— 显示在底部技能槽中的可选技能
struct SkillCard
{
    QString name;    ///< 技能名称（如"斩击"、"治疗术"）
    QString source;  ///< 生成此技能的单位名称（如"见习剑士"）
    QString faction; ///< 生成此技能的单位阵营（用于遗物"圣河水滴"按阵营匹配）
};

/// 章节定义 —— 描述一个章节的关卡配置与奖励
/// 每一章固定 10 关，其中第 5 关为小 Boss、第 10 关为章节 Boss
struct ChapterDef
{
    QString title;             ///< 章节标题（中文，如"第一章：微风如诗"）
    QStringList normalEnemies; ///< 普通敌人名称池（非 Boss 关从中随机抽取）
    QString elite4;            ///< 第 4 关精英敌人名称
    QString boss5;             ///< 第 5 关小 Boss / 剧情 Boss 名称
    QString elite9;            ///< 第 9 关精英敌人名称
    QString boss10;            ///< 第 10 关章节 Boss 名称
    QString bossRelic;         ///< 击败章节 Boss 后获得的剧情遗物（可为空）
    QString bossAlly;          ///< 击败章节 Boss 后加入的剧情角色（可为空）
};

// ============================================================================
// 全局数据函数
// ============================================================================

/// 判断是否拥有专属 Boss 机制的 boss 名称
/// 这些 Boss 在 cleanupDeaths() 中有复活逻辑，在 bossMechanics() 中有特殊行为
inline bool isUniqueBossName(const QString& name)
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

/// 返回全部 9 章的静态配置（序章 + 第一章 ~ 第八章）
/// 这是游戏的"关卡数据库"，供 enterCurrentLevel() 和 setupBattle() 查询
inline QVector<ChapterDef> chapters()
{
    return {
        // 序章
        {QStringLiteral("序章：梦境"),
         {QStringLiteral("梦影"), QStringLiteral("异变魔影"), QStringLiteral("破碎信徒")},
         QStringLiteral("梦境守卫"), QStringLiteral("真神残响"), QStringLiteral("梦境裂隙"),
         QStringLiteral("魔王？？？"), QString(), QString()},
        // 第一章
        {QStringLiteral("第一章：微风如诗"),
         {QStringLiteral("野狼"), QStringLiteral("山贼"), QStringLiteral("森林弓手"), QStringLiteral("流浪剑士")},
         QStringLiteral("山贼头目"), QStringLiteral("迷失骑士"), QStringLiteral("王城禁卫"),
         QStringLiteral("阿拉贡"), QString(), QStringLiteral("阿拉贡")},
        // 第二章
        {QStringLiteral("第二章：猩红平原"),
         {QStringLiteral("血仆"), QStringLiteral("夜蝠"), QStringLiteral("吸血鬼侍从"), QStringLiteral("猩红猎犬")},
         QStringLiteral("血骑士"), QStringLiteral("吸血鬼伯爵"), QStringLiteral("远古守卫"),
         QStringLiteral("偷窃者米格"), QString(), QString()},
        // 第三章
        {QStringLiteral("第三章：赤心跃动"),
         {QStringLiteral("血术师"), QStringLiteral("夜行者"), QStringLiteral("血卫"), QStringLiteral("城堡守卫")},
         QStringLiteral("赤心骑士"), QStringLiteral("伯爵残影"), QStringLiteral("王座守卫"),
         QStringLiteral("艾琳"), QStringLiteral("跃动赤心"), QStringLiteral("游侠")},
        // 第四章
        {QStringLiteral("第四章：精灵圣地"),
         {QStringLiteral("精灵射手"), QStringLiteral("古木守卫"), QStringLiteral("毒叶法师"), QStringLiteral("迷途精灵")},
         QStringLiteral("古树长老"), QStringLiteral("圣地看守者"), QStringLiteral("精灵祭司"),
         QStringLiteral("阿格尼"), QStringLiteral("精灵王冠"), QString()},
        // 第五章
        {QStringLiteral("第五章：撒冷"),
         {QStringLiteral("圣光侍从"), QStringLiteral("审判者"), QStringLiteral("守护天使"), QStringLiteral("圣河少女")},
         QStringLiteral("六翼候补"), QStringLiteral("天使裁决官"), QStringLiteral("圣河守门人"),
         QStringLiteral("米凯尔"), QStringLiteral("圣洁六翼"), QStringLiteral("加百列")},
        // 第六章
        {QStringLiteral("第六章：魔境"),
         {QStringLiteral("小恶魔"), QStringLiteral("魔族战士"), QStringLiteral("火焰术士"), QStringLiteral("深渊刺客")},
         QStringLiteral("魔境猎手"), QStringLiteral("深渊领主"), QStringLiteral("旧王亲卫"),
         QStringLiteral("伊维尔"), QString(), QString()},
        // 第七章
        {QStringLiteral("第七章：终焉"),
         {QStringLiteral("终焉魔兵"), QStringLiteral("虚空术士"), QStringLiteral("异界刺客"), QStringLiteral("黑翼守卫")},
         QStringLiteral("终焉看门人"), QStringLiteral("伊维尔残影"), QStringLiteral("邪神使徒"),
         QStringLiteral("莱索恩"), QStringLiteral("邪神赐福"), QString()},
        // 第八章（世界章节，前三关为问答/剧情，后七关含最终 Boss）
        {QStringLiteral("第八章：世界"),
         {QStringLiteral("世界回声"), QStringLiteral("旧日生命"), QStringLiteral("白色梦境")},
         QStringLiteral("永恒残响"), QStringLiteral("米凯尔与阿格尼"), QStringLiteral("世界门扉"),
         QStringLiteral("最终问题"), QStringLiteral("世界"), QString()}
    };
}
