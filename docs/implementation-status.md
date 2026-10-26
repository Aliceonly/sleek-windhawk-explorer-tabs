---
title: Sleek WinTabs：首批代码已落地
subtitle: 1.2.0-alpha.1；基础调度与配置模型，不是七项功能完成版
lang: zh
template: doc
theme: paper
---

已开始修改 `sleekwintabs.wh.cpp`，保持单文件安装。当前交付覆盖配置模型、选择调度和同步输入规则。七项需求尚未全部实现，也未通过 Windows 实机验收。

源码与全部设计、实现文档统一维护在 [Aliceonly/sleek-windhawk-explorer-tabs](https://github.com/Aliceonly/sleek-windhawk-explorer-tabs)。
文档入口见 [索引](README.md)。

## A 本批实际改动

| 部分 | 已写入的代码 | 验证边界 |
| --- | --- | --- |
| 原有标题修复 | 保留路径末段规则和旧设置键 | 纯逻辑回归通过 |
| 选择延迟 | 删除固定 150 ms；默认投递 owner-window 消息 | 稳定选中和界面耗时未实测 |
| 创建去重 | 按窗口实例及返回 GUID 合并转调 | 不合并同窗口的不同 GUID |
| 窗口与寿命 | 通过 IOleWindow 查询窗口，持有 COM 引用 | 不猜 private this 偏移 |
| 取消与卸载 | 输入 epoch、配置身份、销毁清理、回调栈排空 | 需 Windows 重入与卸载测试 |
| Ctrl+中键 | 保存 DOWN 修饰键，配对 UP 不取消自身请求 | 只覆盖同步输入调用链 |
| 配置 | 45 项契约、范围、枚举和快捷键解析 | 当前面板只展示 8 项 |

同步输入入口为 `DispatchMessageA/W`，并尝试接入可选的 `TranslateAcceleratorGlobal`。没有注册快捷键，也没有接管 Ctrl+D。后台规则仅省略 mod 的选择，不撤销 Explorer 自身的激活。

原生 `SelectTab` 只解析地址，不把其每次调用都误当成用户操作。已加入选择执行门，避免消息泵重入时递归提交。配置变更会使旧创建快照失效。

## B 七项功能的真实状态

| 需求 | 当前状态 | 仍需完成 |
| --- | --- | --- |
| ① 地址栏中键 | 未接入 | 现代 XAML 命中、真实 Shell 目标、异步查询 |
| ② 文件拖入 tab | 未接入 | 标题 Drop 路由、数据桥、完整 Shell 会话 |
| ③ 关闭后选左邻或最左 | 未接入 | 真实标签顺序、关闭事件和迁移区分 |
| ④ 中键延迟优化 | 基础代码已改 | 稳定就绪时点、同机性能对照 |
| ⑤ Ctrl+中键后台 | 同步路径代码已改 | XAML 异步创建与输入的因果关联 |
| ⑥ Ctrl+Q 复制 | 未接入 | 当前视图快照、原生创建、选项恢复 |
| ⑦ 多目录 Enter | 未接入 | 非编辑焦点、新窗口身份、有序批次 |

缺少能力时跳过选择，不代表对应功能完成。当前没有移动文件，也没有用模拟按键、剪贴板或 Sleep 补齐未接入功能。

完整选项仍以 `docs/implementation-plan.md` 为准。37 个后续选项只存在于契约和测试，不伪装成可用控件。

## C 本地验证结果

| 检查 | 结果 | 不代表什么 |
| --- | --- | --- |
| 生产 core 普通构建 | 10,612 项断言通过 | 不是 Explorer GUI 测试 |
| ASan 与 UBSan | 10,612 项断言通过 | 不覆盖 Windows 消息与 COM |
| 设置一致性 | 45 项契约；8 项展示并消费；37 项待接入 | 不是 45 项功能通过 |
| Windows x64 交叉编译 | 已生成 AMD64 COFF object | 未链接 Windhawk DLL |
| Windhawk 本机编译及运行 | 未执行 | 无兼容性结论 |
| 延迟、卡顿、焦点回跳 | 未实测 | 无 p95 优化成绩 |
| PowerShell 5.1 原生解析 | 未执行 | 采集脚本仍需目标机解析 |

测试直接编译生产单文件中的 core，没有重写一套算法。覆盖所有配置条目和枚举、范围、冲突、焦点优先级、去重、过期、取消、重入门和回调计数。

新增了确定性寿命测试：停止后仍计入销毁回调。嵌套 shutdown 返回时，外层销毁栈未退出则不能回收状态。实际 Windows 消息顺序仍须实测。

编译环境为 macOS arm64、Apple clang 17.0.0 和 Zig 0.17.0。Windows 检查采用 `x86_64-windows-gnu`、C++23、`-Wall -Wextra -Werror`。

## D 文件与复现入口

| 文件 | 用途 |
| --- | --- |
| `sleekwintabs.wh.cpp` | 单文件 mod、契约、纯策略、原生调度 |
| `tests/core-tests.cpp` | 编译生产 core 的断言测试 |
| `tools/run-core-tests.py` | 普通及 sanitizer 测试入口 |
| `tools/check-settings.py` | 契约、设置面板和消费者检查 |
| `tools/check-windows-compile.py` | 生成并丢弃 AMD64 COFF object |
| `tools/collect-environment.ps1` | Windows 只读环境采集 |
| `docs/windows-acceptance.md` | Windows 交互、性能和卸载检查 |
| `docs/settings-contract.md` | 45 项状态、迁移及错误处理 |

```sh title="本地测试"
python3 tools/check-settings.py
python3 tools/run-core-tests.py
python3 tools/run-core-tests.py --sanitize
python3 tools/check-windows-compile.py --sdk /tmp/sleekwintabs-sdk
```

最后一条使用本次保留在临时目录的官方头文件。其他机器需先准备同版本 SDK 头文件。测试二进制放在临时目录，不成为安装依赖。

SDK 来自 [Windhawk 官方 mods 仓库的固定版本](https://github.com/ramensoftware/windhawk-mods/tree/6774848bf06023622d0918a8b2d2110e315a7c9e/.vscode/windhawk_headers_1.7.3)。三份头文件的 Git blob 身份已核对；未把外部头文件提交进项目。

可复现下载与校验：

```sh title="准备官方头文件"
SDK=/tmp/sleekwintabs-sdk
REF=6774848bf06023622d0918a8b2d2110e315a7c9e
mkdir -p "$SDK"
for name in windhawk_api.h windhawk_api_internal.h windhawk_utils.h; do
  curl -fsSL "https://raw.githubusercontent.com/ramensoftware/windhawk-mods/$REF/.vscode/windhawk_headers_1.7.3/$name" -o "$SDK/$name"
done
shasum -a 256 "$SDK"/windhawk*.h
```

| 头文件 | 本次 SHA256 |
| --- | --- |
| `windhawk_api.h` | `e9c4ed4a484dd8de31abccbdfb974c6a7f236e9d210ab69b30d45e8935f1d40c` |
| `windhawk_api_internal.h` | `540ea496ca3f015fe2ade232ca212a3a1cc704b5a5b6a27f429b7ac521a5de23` |
| `windhawk_utils.h` | `37761200fd6c45735101ac98d3395e97013ded1337b6b5462a943acba793315f` |

## E 继续实现所需的证据

下一步应在目标 Windows 上编译候选版本并运行验收。首先验证 `IOleWindow` 窗口关联、稳定选中点，以及 Ctrl 输入与创建的实际调用链。随后继续 P4–P9，而非暴露未接通的配置。

```powershell title="只读采集目标环境"
.\tools\collect-environment.ps1 -ProbeWindhawk
```

脚本默认输出 JSON。`-OutFile` 仅创建新文件，不覆盖旧文件。不会重启 Explorer、修改注册表、启用 mod 或读取凭据。原生解析命令和详细步骤见 `docs/windows-acceptance.md`。

当前卸载会等待 owner 线程清理和现有调用栈退出。若 Explorer 线程卡住，卸载也会等待；不能超时后强行卸载仍有回调的 DLL。这一风险属于实机验收项。

本报告记录首批实现结果；代码与文档随仓库提交同步维护，尚未发布正式版本。
当前版本不应当作七项功能完成版安装到日常工作环境。
