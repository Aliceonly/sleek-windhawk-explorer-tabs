# Sleek WinTabs 文档

项目仓库：[Aliceonly/sleek-windhawk-explorer-tabs](https://github.com/Aliceonly/sleek-windhawk-explorer-tabs)。
克隆地址：`git@github.com:Aliceonly/sleek-windhawk-explorer-tabs.git`。

当前代码为 `1.2.0-alpha.1` 首批实现，不是七项功能完成版。
最新事实以实现状态为准；历史设计说明需求与方案，不表示已经通过验收。

## 设计与实施记录

| 文档 | Markdown | HTML | 用途 |
| --- | --- | --- | --- |
| 五项功能研究 | [源码](five-feature-design.md) | [阅读版](html/five-feature-design.html) | 地址栏中键、拖放、关闭选择、延迟及 Ctrl+中键 |
| 七项功能方案 | [源码](seven-feature-design.md) | [阅读版](html/seven-feature-design.html) | 补充 Ctrl+Q 复制及多目录 Enter |
| 完整配置设计 | [源码](configuration-design.md) | [阅读版](html/configuration-design.html) | 45 项设计选项、默认值与优先级 |
| 代码实施计划 | [源码](implementation-plan.md) | [阅读版](html/implementation-plan.html) | P0–P10 阶段、依赖和验收条件 |
| 当前实现状态 | [源码](implementation-status.md) | [阅读版](html/implementation-status.html) | 已实现、未接入和已运行检查 |
| 设置实现契约 | [源码](settings-contract.md) | [阅读版](html/settings-contract.html) | 8 项已接入与 37 项后续设置 |
| Windows 验收 | [源码](windows-acceptance.md) | [阅读版](html/windows-acceptance.html) | 实机编译、交互、性能和卸载检查 |

## 代码和工具

| 入口 | 内容 |
| --- | --- |
| [sleekwintabs.wh.cpp](../sleekwintabs.wh.cpp) | 单文件 Windhawk mod |
| [core-tests.cpp](../tests/core-tests.cpp) | 直接编译生产 core 的测试 |
| [run-core-tests.py](../tools/run-core-tests.py) | 普通与 sanitizer 测试 |
| [check-settings.py](../tools/check-settings.py) | 配置声明、契约和消费者检查 |
| [check-windows-compile.py](../tools/check-windows-compile.py) | Windows x64 COFF 交叉编译 |
| [collect-environment.ps1](../tools/collect-environment.ps1) | 只读采集 Windows 运行环境 |

HTML 阅读版随 Markdown 一并提交，不依赖本机绝对路径或常驻服务。
修改内容时以 Markdown 为源，再重新生成对应 HTML。
原作者署名及许可见 [LICENSE](../LICENSE)。
