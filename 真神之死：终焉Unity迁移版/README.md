# 真神之死：终焉 Unity 迁移版

这是《真神之死：终焉》的 Unity 迁移与美化版本。该版本保留完整 PVE 流程，并重做了标题页、世界地图、剧情演出、战斗牌桌、角色卡、技能卡、遗物槽、攻击/治疗/护盾反馈和 BGM 播放。

## 运行环境

- Unity 2022.3.62f3c1
- Windows 64-bit

## 打开工程

用 Unity Hub 打开本目录即可：

`真神之死：终焉Unity迁移版`

工程源码包含：

- `Assets`
- `Packages`
- `ProjectSettings`
- `tools`

未提交 Unity 本地缓存目录，例如 `Library`、`Logs`、`Temp`、`UserSettings`、`Build`。

## Windows 发布包

本地已生成可运行发布包：

`真神之死-终焉-Unity-Windows.zip`

解压后运行：

`真神之死-终焉.exe`

## 构建

仓库包含构建脚本：

`Assets/Editor/BuildWindows.cs`

可通过 Unity Editor 构建，也可用 batchmode 调用 `BuildWindows.Build` 生成 Windows 版。

## 说明

该版本使用 `StreamingAssets` 存放剧情、美术、BGM 和生成资源。BGM 生成脚本只从环境变量读取 API key，仓库内不包含任何 API key。
