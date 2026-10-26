---
title: Sleek WinTabs：七项功能方案
subtitle: 新增 Ctrl+Q 复制标签与 Enter 批量打开；仅设计，未修改 mod
lang: zh
template: doc
theme: paper
---

> 历史设计记录（2026-10-26）。本页说明当时的方案，不代表当前功能已经完成。
> 最新实现状态见 [implementation-status.md](implementation-status.md)；设置 key 以 [配置设计](configuration-design.md) 为准。

复制当前标签的快捷键已改为 **Ctrl+Q**。`Ctrl+D` 保留 Windows 原生行为。本页补充第⑥、⑦项，前五项方案不变。

## A 七项功能与配置入口

| 编号 | 功能 | 默认行为 |
|---|---|---|
| ① | 地址栏中键开标签 | 原生打开目标目录，删除模拟按键与固定等待 |
| ② | 文件拖到标签标题 | 在标题上松手移动，先确认目标目录 |
| ③ | 关闭后选择 | 选被关闭项的左邻，支持改为最左或系统默认 |
| ④ | 中键延迟优化 | 替换 150 ms timer，合并重复选择 |
| ⑤ | Ctrl + 中键 | 后台打开，保持原标签激活 |
| ⑥ | Ctrl+Q 复制当前标签 | 在当前窗口新建副本，并切到副本 |
| ⑦ | 多选文件夹后按 Enter | 按当前视图顺序，打开到一个新窗口的多个 tab |

**最新要求：所有可合理配置的行为都作为 mod 设置。** 开关、快捷键、标签位置、切换策略、排序、批量目标、拖放方式和高级参数不再写死。

[打开完整配置项设计：选项、默认值、范围与生效规则](configuration-design.md)

本页后续章节保留实现研究，其行为说明作为默认值阅读。设置命名与可选值以完整配置报告为准；固定的正确性约束不能关闭。默认仍是 `Ctrl+Q` 复制，`Ctrl+D` 不接管。

[查看前五项的完整设计与证据](five-feature-design.md)
## B 参考项目：借鉴行为，不照搬等待与窗口拦截

已核查 `w4po/ExplorerTabUtility` 的源码。其 `DuplicateActiveTab` 保存当前目录和选中项目，再明确绕过“复用已有标签”，创建一个真正的副本。[复制入口源码](https://github.com/w4po/ExplorerTabUtility/blob/fad736353210d36595c6e6caa77ac7ad9129b3ee/ExplorerTabUtility/Hooks/ExplorerWatcher.cs#L236)

它通过 `WM_COMMAND 0xA21B` 请求新标签，再等待标签出现。随后每 50 ms 轮询对应对象，导航完成后恢复选中项目。本方案只借鉴“位置加选择”的行为，继续使用前版的原生 AddTab 和事件驱动流程。[创建与导航源码](https://github.com/w4po/ExplorerTabUtility/blob/fad736353210d36595c6e6caa77ac7ad9129b3ee/ExplorerTabUtility/Hooks/ExplorerWatcher.cs#L590)

| 调研项 | 核查结果与取舍 |
|---|---|
| 维护状态 | 3,000 stars，未归档；最近推送为 2025-05-04 |
| 许可证 | MIT；复用代码时保留署名，不引入其 .NET 运行时 |
| 兼容性反馈 | 存在窗口隐藏或无法打开的用户报告；不等于已证实根因 |
| 本方案取舍 | 不采用全局“窗口自动合并成 tab”的方式 |
| ExplorerTabPlus | 暂未找到可确认的同名仓库；待补链接后核查，不冒用相似项目 |

维护信息来自[官方仓库元数据](https://api.github.com/repos/w4po/ExplorerTabUtility)，许可见[LICENSE](https://github.com/w4po/ExplorerTabUtility/blob/fad736353210d36595c6e6caa77ac7ad9129b3ee/LICENSE)。窗口问题的参考是[项目 issue #121](https://github.com/w4po/ExplorerTabUtility/issues/121)，本轮没有复现它。

若仍同时运行 ExplorerTabUtility，需关闭重叠快捷键和窗口自动合并。否则本次要求的新窗口可能被它合并回旧窗口。

## C Ctrl+Q：复制的是标签，不是文件

| 细节 | 建议行为 |
|---|---|
| 作用范围 | 仅当前 File Explorer 窗口，不注册系统全局热键 |
| 新标签位置 | 紧接当前标签右侧；已有同目录标签也照常新建 |
| 前后台 | 显式切到副本，不受 Ctrl + 中键的后台规则影响 |
| 复制内容 | 当前 Shell 位置，以及仍存在的选中项目 |
| 不承诺复制 | 完整前进后退历史、临时搜索状态、滚动位置 |
| 输入场景 | 重命名、地址栏编辑、搜索输入、输入法组合中不触发 |
| 长按按键 | 忽略自动重复；松开后再次按下才算下一次操作 |

实现步骤如下。

1. 在所属 UI 线程确认当前窗口、活动 tab 和输入上下文。
2. 快照当前目录 PIDL、所选项目身份和当前 tab 位置。
3. 调用 `AddTabAtIndex`，在源 tab 右侧创建副本。
4. 将新 GUID 标记为 `Duplicate` 意图，安排一次前台选择。
5. 新视图就绪后恢复仍有效的项目选择。

这不会复制、移动或删除磁盘上的文件。恢复选择失败时，保留已经打开的副本，不删除它重试。

选中项目用 Shell 标识保存，不按显示名称重新查找。原项目按名称恢复选择；本方案避免由隐藏扩展名或重名显示造成歧义。[原项目选择恢复代码](https://github.com/w4po/ExplorerTabUtility/blob/fad736353210d36595c6e6caa77ac7ad9129b3ee/ExplorerTabUtility/Hooks/ExplorerWatcher.cs#L746)

特殊 Shell 目录只有在能取得可靠目标时才支持。不能保存完整搜索状态时，不把它静默替换成父目录。整个快照和恢复过程受操作代次约束，用户导航或关闭目标后停止旧任务。

`Ctrl+D` 原生用于删除所选项目；改为 `Ctrl+Q` 后不覆盖它。这里只处理明确的 `Ctrl+Q`，不匹配 Ctrl+Shift+Q 或 Alt 组合。[Microsoft：Explorer 快捷键](https://support.microsoft.com/en-us/accessibility/windows/keyboard-shortcuts-in-windows)

## D Enter：顺序来自视图，一次只创建一个目标窗口

建议仅在文件列表拥有焦点时接管普通 Enter。条件是：至少选中两个文件夹，且没有混选文件。单选、混选、修饰键组合和文本编辑继续沿用原行为。文件夹快捷方式与压缩包不默认当作普通文件夹。

```flow
按下 Enter -> 快照当前选择: 校验焦点与文件夹
快照当前选择 -> 冻结目录顺序: 按视图顺序
冻结目录顺序 -> 创建一个新窗口: 首目录作为第一个 tab
创建一个新窗口 -> 绑定目标窗口: 必须确认窗口身份
绑定目标窗口 -> 依次追加剩余 tab: 原生创建并让出消息循环
依次追加剩余 tab -> 保持首个 tab 激活: 不逐个跳转
```

例如列表当前显示 `C、A、B`，选中这三个目录后按 Enter，新窗口应得到 `[C] [A] [B]`。不再按名称排序，也不将最后点击的文件夹挪到第一位。

有明确的 Shell 契约可用于顺序提取：`IFolderView::Items` 配合 `SVGIO_SELECTION | SVGIO_FLAG_VIEWORDER`，请求 `IShellItemArray`。顺序标志要求返回视图顺序；只调用 `SelectedItems()` 或 `GetSelection()` 不能据此保证顺序。[Microsoft：Items](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview-items)、[SVGIO 标志](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-_svgio)

若视图不支持所需枚举形式，再按全视图顺序与所选对象身份求交。匹配使用 Shell 身份，不使用标题。不同提供者是否正确实现此契约，仍需在 Win11 上验证。

| 情况 | 处理方式 |
|---|---|
| 原窗口已经有多个 tab | 原窗口不变，不向它追加这批目录 |
| 新窗口先生成 Home tab | 将首个槽位导航到第一个目录，不多留空 tab |
| 第一项慢、后续项快 | 创建顺序固定，不按导航完成时间排序 |
| 中间目录无法访问 | 记录失败，继续后续项；成功项保持相对顺序 |
| 用户关闭目标窗口 | 取消尚未创建的项，不重新开一个窗口追着执行 |
| 长按 Enter | 只创建一次批次，不因自动重复再开窗口 |

`BatchOpen` 意图临时跳过 `selectNewTabs` 的逐项切换。新窗口只在建立时激活一次，之后不抢回用户主动切换的 tab。每次创建后让出消息循环，不插固定 Sleep，也不等一个目录加载完才创建下一个。

## E 修改位置与最关键的实机验证点

仍整合到同一个 `.cpp`，共用前版的窗口状态和原生适配层。

| 逻辑模块 | 职责与接入位置 |
|---|---|
| `InputRouter` | 识别 Ctrl+Q / Enter、焦点、修饰键和自动重复 |
| `ViewSnapshot` | 获取活动视图，复制目录与选择的 Shell 标识 |
| `DuplicateController` | 创建副本，绑定新 GUID，恢复选择 |
| `BatchOpenController` | 管理目录队列、目标窗口、失败与取消 |
| `SelectionPolicy` | 新增 `Duplicate`、`BatchOpen`，避免与中键规则混淆 |
| `WindowFactory` | 只创建一个新窗口，并返回已确认的窗口身份 |

输入入口候选是现有符号中的 `TranslateAcceleratorGlobal` 与 `TranslateAcceleratorIO`。需要验证它们相对 XAML 和文件视图处理按键的先后顺序。不能只在顶层窗口监听 `WM_KEYDOWN`，就假定能捕获全部情况。

焦点判断也有验证缺口。窗口拥有焦点不代表文件项拥有逻辑焦点；现代 DirectUI 内部的重命名不能靠标准 ListView 消息排除。不能确认文件列表处于非编辑态时，不吞 Enter。[Microsoft：HasFocusIO 的范围](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iinputobject-hasfocusio)

**批量功能的最大缺口是“精确绑定本次创建的新窗口”。** 不能拿当前前台窗口、最后枚举到的窗口或最新 HWND 当作目标。

有比窗口差集更可靠的公开候选：Microsoft 示例用 `CoCreateInstance(CLSID_ShellBrowserWindow)` 返回 `IWebBrowser2`。随后用首目录的 PIDL 调用 `Navigate2`，再显示该实例。应优先验证这条路线，保留创建返回的对象作为本次批次身份。[Microsoft：创建 Explorer 实例的示例](https://learn.microsoft.com/en-us/windows/win32/shell/developing-with-windows-explorer)

该示例不能证明当前 Win11 上必定创建独立顶层窗口。还需验证返回对象如何关联正确的 frame、tab actions 和就绪事件。若此路线不适用，再研究 `CreateFrameWindow` / `CreateInstance` 的原生创建链，不猜私有参数布局。

外部工具常用“创建前后的窗口差集，再检查目录”来定位。并发打开相同目录时，这仍有歧义。它可以辅助校验，但不能成为唯一保证；无法确认目标时停止追加，不把剩余目录塞进别人的窗口。

跨 Explorer 进程也不能传递裸指针。若当前版本无法建立可靠的跨进程关联，应报告不支持该路径。**单窗口关联探针通过后，才能把第⑦项标为可实施完成。**

## F 验收与交付顺序

| 场景 | 验收条件 |
|---|---|
| Ctrl+Q 基本行为 | 同一窗口只新增一个副本，位置在源 tab 右侧 |
| 已有同目录 tab | 仍创建副本，不跳到已有 tab |
| 选中项目 | 新视图恢复仍存在的选择，不触碰磁盘文件 |
| 快捷键隔离 | Ctrl+D 不被 mod 接管，其他应用中的 Ctrl+Q 不受影响 |
| 编辑与输入法 | 重命名、地址栏、搜索输入不误建 tab 或窗口 |
| 批量数量 | N 个有效目录对应一个新窗口中的 N 个 tab |
| 排序与分组 | 名称倒序、时间排序、分组视图均保持快照顺序 |
| 并发窗口 | 同时打开相同目录时不误绑定；有歧义则停止 |
| 失败和取消 | 已开标签保留，未开任务可取消，不反复重建窗口 |
| 生命周期 | 多窗口、卸载、源窗口关闭、目标窗口关闭不留悬空任务 |

新增性能要求是：键盘入口不 Sleep、不轮询窗口、不等待目录导航。批量创建每次让出 UI 线程；10 个和 50 个目录分别测输入响应、首窗出现时间、全部 tab 创建时间和失败数。暂不承诺新窗口出现的固定毫秒数。

建议顺序：先完成共同的输入与选择逻辑，再实现 **④⑤⑥ → ① → ③ → ⑦ → ②**。第⑥项复用现有开标签能力；第⑦项需要先验证新窗口绑定。拖放仍放在最后。

本轮只更新设计报告。仓库没有改动，也没有编译或 Windows 运行测试。`ExplorerTabPlus` 的准确来源仍待补充；这不妨碍先确定上述行为和验证路线。
