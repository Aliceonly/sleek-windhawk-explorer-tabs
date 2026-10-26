---
title: Sleek WinTabs：五项功能的修改方案
subtitle: 只做调研与设计；未修改 mod，未在 Windows 上运行验证
lang: zh
template: doc
theme: paper
---

> 历史设计记录（2026-10-26）。本页说明当时的方案，不代表当前功能已经完成。
> 最新实现状态见 [implementation-status.md](implementation-status.md)；设置 key 以 [配置设计](configuration-design.md) 为准。

建议扩展现有 `sleekwintabs.wh.cpp`，不拆成五个 mod。共用标签定位与选择策略，各项保留独立开关。

## A 功能范围与配置

| 需求 | 建议行为 | 实现重点 |
|---|---|---|
| ① 地址栏中键 | 点击面包屑目录，直接打开该目录的新标签 | 用真实 Shell 目录标识，不模拟键盘输入 |
| ② 拖文件到标签 | 在目标标签标题上松手，即移动文件 | 可先悬停切到目标标签；由 Shell 处理移动 |
| ③ 关闭后选择 | 配置为最左标签、关闭项的左邻或系统默认 | 使用当前标签顺序，覆盖关闭后台标签 |
| ④ 减少中键延迟 | 删除人为等待，尽早完成一次有效选择 | 替换 150 ms 定时器，消除重复调度 |
| ⑤ Ctrl + 中键 | 强制后台打开，不切过去再切回来 | 点击时记录 Ctrl，贯穿本次开标签流程 |

建议保留单文件安装方式。拖放模块可以最后交付，不必另建一套选择逻辑。

```yaml title="拟议配置 · 不是完整的 Windhawk settings 声明"
fixTabTitles: true                 # 保留现有标题修复
selectNewTabs: true                # 保留现有自动切换
breadcrumbMiddleClick: true        # 新增地址栏中键
ctrlMiddleClickInBackground: true  # 新增 Ctrl + 中键后台打开
closeTabSelection: left            # native / leftmost / left
dragFilesToTabs: true              # 新增标签标题接收文件移动
```

`closeTabSelection` 在 Windhawk 中显示为下拉框。建议默认选“左邻”；想每次回到第一个标签时，改为“最左”。这些只是建议默认值，本轮没有写入配置。

整合后，不应同时启用官方地址栏中键 mod。否则同一次点击可能被处理两次。

## B 延迟与卡顿：已经核实的原因

| 位置 | 已核实事实 | 修改方向 |
|---|---|---|
| 当前 mod，`:133` | `kSelectTabDelayMs = 150` | 删除固定等待，不只把数值调小 |
| 当前 mod，`:169` / `:191` | 两个 AddTab hook 都会安排选择 | 加调用嵌套保护和标签去重 |
| 当前 mod，`:131` / `:148` | 全局队列依次选择所有待处理标签 | 改为按窗口管理，只保留最新有效选择 |
| 当前 mod，`:260` | 卸载未取消 timer 或清理 pending | 补窗口销毁、卸载和取消流程 |
| 官方地址栏 mod | 同步 UIA 查询与多段 Sleep 位于鼠标钩子调用链 | 输入回调立即返回，识别工作异步执行 |

行号均指 `sleekwintabs.wh.cpp`。指定版本的反汇编还证实：`AddTab` 会转调 `AddTabAtIndex`。因此当前双 hook 会重复安排同一标签的选择，不只是理论风险。

官方 `explorer-breadcrumb-mclick-newtab` v1.2.0 先读取地址栏，再模拟新建和输入路径。普通祖先目录路径上，固定等待为 **150 + 50 + 450 + 150 + 50 = 850 ms**。直接识别盘符时省去前两段，为 650 ms。UIA 和导航耗时另计。这是源码累计值，不是端到端实测值。[官方源码](https://github.com/ramensoftware/windhawk-mods/blob/ecb4b86d0e225b72c8b04ba636c260554129fefa/mods/explorer-breadcrumb-mclick-newtab.wh.cpp)

该实现还会从窗口根节点搜索后代元素。低级鼠标钩子需快速返回；超时可导致钩子被系统移除。这解释了卡顿风险，但尚未在用户机器上复现。[Microsoft：LowLevelMouseProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc)

当前代码注释提到：在 `AddTab` 内立即选择，焦点会被后续初始化夺回。所以不能简单删除 timer 后同步调用 `SelectTab`。需要改为“退出创建调用栈，再按就绪状态选择”。该现象来自原代码说明，尚无本轮运行验证。

## C 先统一选择策略：需求④与⑤共用

```flow
输入事件 -> 意图记录: 捕获按键与窗口
意图记录 -> 原生适配层: 创建或关闭标签
原生适配层 -> 选择策略: 提交一次结果
选择策略 -> 窗口消息队列: 退出当前调用栈
窗口消息队列 -> 就绪检查: 检查目标与操作代次
就绪检查 -> 原生选择: 目标仍有效才执行
```

每次操作携带 `OpenIntent`：来源、窗口身份、序号、点击时的 Ctrl 状态和是否前台打开。每个窗口独立保存状态，不跨线程保存无所有权的 frame 指针。

| 操作 | 选择行为 |
|---|---|
| 普通中键打开文件夹或面包屑 | 遵循 `selectNewTabs` |
| Ctrl + 中键，后台选项开启 | 始终后台，优先于 `selectNewTabs` |
| Ctrl + 中键，后台选项关闭 | 沿用原有 mod 规则 |
| 右键“在新标签页中打开” | 保留当前 `selectNewTabs` 行为 |
| “+”按钮或 Ctrl + T | 不额外覆盖系统前台行为 |
| 用户随后主动切换或关闭窗口 | 取消旧选择，禁止迟到抢焦点 |

现有文件列表的中键开标签，仍交给 Explorer 创建；只决定是否安排前台选择。Ctrl 必须在触发点击时记录，不能在回调时读取，因为用户可能已经松手。

同步调用路径可用线程局部的输入上下文。异步路径需将操作序号绑定到实际开标签事件。**这条关联需要实机探针确认**，不能用宽泛时间窗口猜测，误伤紧接着的 Ctrl + T。

`AddTab` 与 `AddTabAtIndex` 共用嵌套计数，最外层只提交一次。每个窗口按 GUID 去重；连续打开多个标签时，保留全部创建，只合并多余的前台选择。

先用 UI 线程消息排队，解除固定 150 ms。`PostMessage` 不保证导航已就绪；候选通知是 `OnNavigatedViewStartedToShow`。需验证哪个时点能稳定选择，不能等网络目录加载完才显示标签。未就绪时使用事件重试，并设取消期限，不循环 Sleep。

## D 地址栏中键：只借鉴功能，不搬运模拟按键流程

1. 在输入入口记录点击位置、窗口和修饰键。
2. 在独立 UIA 线程命中面包屑，不扫描整个窗口。
3. 从当前 Shell 目录解析被点中的真实目标。
4. 把目标 PIDL 与操作序号投递到所属 UI 线程。
5. 调用原生 `AddTab`，复用 C 节的前后台策略。

PIDL 是 Shell 的目录标识。普通路径可用祖先 PIDL 定位，而不是按显示名称拼字符串。这样可避开同名目录、本地化名称和固定 `MAX_PATH` 缓冲区的问题。

现代地址栏使用 `FileExplorerExtensions.BreadcrumbBarItemControl`。旧版 `CBreadcrumbBar::_NavigateToIDList` 符号确实存在，但不能假定它覆盖现代 XAML 地址栏。先探测原生路径；无法直接取得目标时，用 UIA 做命中识别。

UIA 实例由专用 MTA 线程复用。只查询面包屑范围，并在导航、布局变化时使缓存失效。过期结果直接丢弃，不对新窗口执行旧点击。[Microsoft：UI Automation 线程要求](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-threading)

地址栏下拉菜单也纳入兼容测试。折叠祖先、虚拟目录和菜单项需解析到真实 Shell 项；不能只按“右侧还有几个文字”推算。无法可靠解析时保留原行为，不打开猜测目录。

这条链路不使用 `Alt+D`、`Ctrl+T`、`SendInput` 或剪贴板，也不添加固定等待。可确定删除人为延迟，但不能保证 Shell 和 UIA 本身零耗时。

## E 拖文件到 tab：扩展原生悬停，补上直接投递

**建议第一版允许悬停激活目标 tab，然后在标题上松手移动。** 用户不需要再拖进文件列表区域。不承诺“完全不激活后台 tab 就完成投递”。

指定版本的 `FileExplorerExtensions.dll` 中，`FileExplorerTabItemControlVM::TabViewItem_DragEnter` 已检查文件拖放数据，并请求选中命中的 tab。它证明存在悬停选择路径，不证明松手会移动文件。[Microsoft DLL](https://msdl.microsoft.com/download/symbols/FileExplorerExtensions.dll/6A6A604C61c000/FileExplorerExtensions.dll)、[匹配 PDB](https://msdl.microsoft.com/download/symbols/FileExplorerExtensions.pdb/2A014EE6463D4D41A6D116BED57D99B51/FileExplorerExtensions.pdb)

| 阶段 | 处理方式 |
|---|---|
| 进入标签标题 | 区分文件拖放和标签重排；保存命中 item 身份 |
| 悬停 | 沿用原生激活，确认选中项确实是命中项 |
| 准备目标 | 获取该 tab 当前目录；绑定目录的 `IDropTarget` |
| 移过其他标签 | 对旧目标调用 DragLeave，再进入新目标 |
| 在标题上松手 | 向目标转发原始数据对象，调用 Drop |
| 结束或取消 | 正确反馈结果，释放数据、目标和事件订阅 |

原生代码中存在 `DataPackageView` 查询 `IDataObject` 的使用证据。可将其作为数据桥的候选，但不是所有来源都支持的公开保证。桥接失败时不伪造数据，也不按文本路径自行复制。

目录目标可经 `IShellItem::BindToHandler` 获取；也可取得 `IShellFolder` 后调用 `CreateViewObject(IID_IDropTarget)`。必须转发完整拖放会话，而不是只调用一次 `Drop`。[BindToHandler](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitem-bindtohandler)、[CreateViewObject](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-createviewobject)、[IDropTarget](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nn-oleidl-idroptarget)

按你的要求，标题区域提供“移动”而非按跨盘默认值变成“复制”。只有源和目标均允许 `DROPEFFECT_MOVE` 才接受；保留修饰键，若最终协商不是移动则拒绝。不能假报成功，也不能手工删除源文件。[Microsoft：Drop 的 effect 契约](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idroptarget-drop)

冲突、权限、进度和撤销沿用 Shell 能力，不自行实现文件搬运。目录尚未对应目标、目标已关闭或不支持写入时，显示禁止投递；绝不能误投到旧的活动目录。

最关键的实机验证是 XAML 的 Drop 事件顺序，以及结果如何反馈给拖放源。仅 hook `CExplorerFrame::Drop` 不足以证明覆盖 tab 标题。先验证 XAML tab 区域的接入，不接管整个 Explorer 窗口的拖放。

## F 关闭标签：按实时顺序选，不按创建顺序猜

指定版本的 `FileExplorerTabControlVM::OnTabRemoved` 有原生左邻逻辑：关闭活动项时选索引 `i-1`；关闭首项时选后继；关闭后台项不切换。这是静态代码证据，不代表用户机器上的最终可见行为。

因此“左邻”模式还需覆盖后台项关闭，并避免旧的开标签任务把焦点抢回。“最左”则是另一条明确策略。

| 配置 | 关闭完成后的目标 |
|---|---|
| `native` | 不干预系统选择 |
| `leftmost` | 选当前存活标签中最左的一项 |
| `left` | 选被关闭项关闭前的左邻；没有左邻则选最左存活项 |

“任意标签”按字面设计：关闭后台标签也执行所选策略。关闭最后一个标签仍由 Explorer 关闭窗口，不自动补建空标签。关闭失败、整窗退出或标签迁移到新窗口，不执行此规则。

关闭前读取真实 `TabView.TabItems()`，保存目标 item 的对象身份。关闭成功且集合已更新后，再确认目标仍存在，提交一次选择。用户拖动重排后重新读取集合，不靠标题、路径或创建顺序匹配。[TabItems 契约](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.controls.tabview.tabitems)、[Microsoft TabView 源码](https://github.com/microsoft/microsoft-ui-xaml/blob/f980ab26e378db4afe4264c4f00c5bf6cc11ec34/controls/dev/TabView/TabView.cpp#L545)

候选入口是 `OnTabRemoved`、`TabView_TabItemsChanged` 与 `CExplorerFrame::RemoveTab`。不能只监听关闭按钮；还需覆盖标签中键、Ctrl + W 和菜单关闭，并区分“关闭”与“移到新窗口”。

已有标签可用 XAML item 身份选择，不强求导出未知 GUID。新创建标签继续使用 `AddTab` 返回的 GUID。两条通道封装到同一适配层；绑定回写与焦点稳定性仍需验证。RTL 布局按实际左右方向处理，不能把索引 0 永久等同于物理最左。

## G 具体改哪些代码，以及兼容性边界

| 代码位置 | 拟议修改 |
|---|---|
| settings、`LoadSettings` | 增加四个设置项；保留现有两个设置 |
| `PendingSelect` / `ScheduleSelectTab` | 替换为按窗口管理的意图与待选状态 |
| `SelectTabTimerProc` | 移除固定等待，改为 UI 消息与就绪事件 |
| 两个 AddTab hook | 捕获来源、处理 Ctrl、嵌套去重与失败路径 |
| `HookExplorerFrameSymbols` | 增加选择观察、关闭、就绪和生命周期适配 |
| 新增 Breadcrumb / TabDrop 模块 | 分离命中识别、目标解析和 Shell 投递 |
| `Wh_ModUninit` | 取消消息、解除事件、停止 worker、释放 COM 引用 |
| `README.md` | 写明行为、兼容版本、冲突 mod 与测量方法 |

逻辑模块仍可放在当前 `.cpp` 内，无需为此引入新运行时依赖。关闭与拖放需要增加 `FileExplorerExtensions.dll` 的适配，不只改 `explorerframe.dll`。

原生适配层是最重要的隔离点。匹配版本中，`AddTab` / `SelectTab` 使用接口子对象的 `this`；其他 frame 方法使用完整对象或另一接口。不能把同一个 `void*` 传给所有方法，也不能硬编码字段偏移或私有结构布局。

已确认的 frame 符号包括 `RemoveTab`、`OnNavigatedViewStartedToShow`、`GetSelectedTabShellItem` 和 `_OnDestroy`。符号存在不等于调用条件、对象寿命和跨版本 ABI 已得到保证。[Microsoft ExplorerFrame PDB](https://msdl.microsoft.com/download/symbols/ExplorerFrame.pdb/E4B44D51365E4FA3AA9BBAD5CA3BB93A1/ExplorerFrame.pdb)

每项功能独立探测依赖。缺少可靠对象映射或 ABI 不匹配时，只禁用对应功能并记录原因。保留标题修复，不以键盘模拟作为无声降级。

参考库 `ramensoftware/windhawk-mods` 调研时有 1,056 stars，最近推送为 2026-10-25。其规则规定未另行声明的 mod 按 MIT 提交；目标地址栏 mod 没有另行声明。若实际复用代码，需保留来源与署名。本方案不复制其他 GPLv3 mod 的代码。[官方仓库及许可规则](https://github.com/ramensoftware/windhawk-mods/blob/ecb4b86d0e225b72c8b04ba636c260554129fefa/README.md)

## H 实施顺序与验收标准

| 阶段 | 交付与通过条件 |
|---|---|
| 1：④ + ⑤ | 替换 timer；Ctrl 后台正确；重复 hook 不重复切换 |
| 2：① | 面包屑原生开标签；不抢地址栏焦点；没有 SendInput |
| 3：③ | 当前与后台标签关闭均符合设置；重排后仍选对目标 |
| 4：② | 标题直接接收移动；目标不明时拒绝；不破坏普通拖放 |

各阶段先验证依赖的原生入口，再实现功能。现代 XAML 输入关联、选择稳定时点、关闭对象身份、Drop 数据桥是实机探针的四个重点。

**性能目标，不是实测成绩：**

| 指标 | 测量与目标 |
|---|---|
| 输入回调自身耗时 | 用高精度计时；p95 小于 1 ms；不运行 UIA 或 Sleep |
| 人为固定等待 | 删除当前 150 ms 与参考方案的 Sleep 链 |
| 中键到标签可见选中 | 本地缓存目录目标 p95 不高于 100 ms |
| 重复选择 | 每次开标签最多一次有效的前台提交 |
| 文件夹加载时间 | 单独记录，不混入“标签选择完成”指标 |

在同一 Windows build、同一机器上比较原版与新版。文件列表与地址栏分别测量；每种暖启动场景至少 100 次，记录 p50、p95 和失败数。冷启动、UNC 与离线目录单列，不承诺同一阈值。

| 测试组 | 必须覆盖 |
|---|---|
| 中键与 Ctrl | 提前松开 Ctrl；连续点击；随后按 Ctrl + T；手动切换 |
| 地址栏 | 中文、重复名称、长路径、UNC、折叠菜单、虚拟目录 |
| 标签关闭 | 首项、中间项、末项、后台项、最后一项、重排后关闭 |
| 文件移动 | 同盘、跨盘、重名冲突、取消、只允许复制的数据源 |
| 生命周期 | 多窗口、关闭目标窗口、动态禁用 mod、卸载时仍有任务 |
| 无回归 | 文件拖到正文区域、标签重排、标签拖到新窗口、标题修复 |

验收时需确认文件只到达目标目录；取消或失败不误删源。每次失败都能定位到输入、解析、创建、就绪、选择或投递阶段。

本轮核验范围是源码与静态符号。frame 样本为 `10.0.26100.9278 x64`，扩展样本文件版本为 `2607.29001.0.0`。现有 README 的“已测试”仅适用于原版；新增功能尚未编译，也未运行。下一步实施前需读取实际机器的两个 DLL 版本，不能把单一版本证据当作全部 Win11 的保证。
