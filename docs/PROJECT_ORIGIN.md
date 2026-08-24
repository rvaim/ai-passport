# 项目来源与上游关系

## 项目身份

本仓库 `rvaim/ai-passport` 是一个社区维护的衍生固件，目标硬件是 FoloToy AI Passport。
项目起点来自 [`folotoy/ai-passport`](https://github.com/folotoy/ai-passport)，但当前仓库有
自己的产品目标、包格式、运行时和发布节奏，不是上游仓库的镜像或官方发行渠道。

仓库曾在开发过程中重新初始化 Git，因此当前提交图不保留上游提交祖先。不能根据本仓库的
root commit 推断某个文件是否为全新实现；判断来源时应结合本文件、MIT License 和实际代码
差异。

## 继承的基础

本项目在上游基础上继续使用或修改了以下部分：

- ESP32-C3 工程与 ESP-IDF Component Manager 配置；
- `components/bsp` 中的屏幕、共享 I2C、三键 ADC、ES8311 音频和 CW2017 电量支持；
- AI Passport 的引脚、显示面板、音频和无 PSRAM 内存约束；
- USB Serial/JTAG、LVGL 与真机验收方法的基础经验；
- 原项目的 MIT License 和 FoloToy 版权声明。

这些内容在当前仓库中可能已经因为产品需求而调整，运行时行为应以当前分支源码和实机结果
为准，不能只参考上游 README。

## 本项目的主要修改

| 范围 | 本仓库实现 |
| --- | --- |
| 产品模型 | 用单一 Registry 取代固定 Demo 菜单，统一系统插件与下载插件生命周期 |
| 下载格式 | Manifest v5、Host API v5、受限字节码 VM、固定资源预算和精确版本拒绝 |
| 信任链 | ECDSA P-256 包签名、固件内置公钥、设备端物理批准 |
| 系统 UI | 公共中文字体、语义组件、动作栏、确认弹窗、Canvas 与主题 Token |
| 设置 | 可替换的 `system.settings`、内置兜底和共享设备信息页 |
| 安装 | 设备码同步后的无配对 BLE GATT Web Bluetooth 安装器 |
| 通信 | 系统 Nearby Runtime Gateway、消息、Blob、摘要 ACK 与半双工语音 |
| 隔离 | 单前台 VM、owner generation、退出/故障强制回收与迟到任务过滤 |
| 工具与测试 | JSON 打包器、字体生成器、BLE 客户端、Host VM/协议/参考插件测试 |

上游的 `demo/*` 分支不是本仓库的发布接口。本仓库 README 不要求用户配置上游 remote，也不
假设这些分支存在于 `origin`。

## 同步上游的原则

上游更新不能直接整分支合并到本仓库。BSP、依赖或硬件文档需要同步时，应先把上游配置成
单独的只读 remote，并按子系统比较：

```bash
git remote add upstream https://github.com/folotoy/ai-passport.git
git fetch upstream main
git diff upstream/main HEAD -- components/bsp sdkconfig.defaults
```

同步时必须逐项确认：

1. 引脚、I2C 地址、LCD 初始化和音频时钟是否仍匹配当前设备；
2. 上游改动是否会破坏 Registry、Host API 或前台资源所有权；
3. 内部 RAM、Flash 分区和 DMA 预算是否仍成立；
4. 主机测试、ESP-IDF 构建和相关真机验收是否分别通过。

不要用兼容分支或静默回退掩盖冲突。若两个实现的数据格式或生命周期不同，应选择一套当前
规范并明确迁移或拒绝，而不是同时保留不稳定的双路径。

## 名称与支持边界

“FoloToy”与“AI Passport”仅用于标识上游来源和兼容硬件。本项目没有声称获得 FoloToy
认证、赞助或维护承诺。

- 插件 VM、Host API、主题、设备码、安装器或 Nearby 的问题应提交到本项目；
- 原理图、板卡批次或上游未修改 BSP 的问题，应先准备硬件证据，再判断应提交到哪里；
- 不要要求上游维护者为本项目自定义的包格式或产品交互提供支持。

## 许可证

上游项目采用 MIT License。本仓库根目录的 [`LICENSE`](../LICENSE) 保留了原始 FoloToy
版权声明和完整许可证文本。分发源码或固件时应一并保留该声明与许可证。

---

## English summary

This repository is an independently maintained derivative firmware for FoloToy
AI Passport hardware. It is based on
[`folotoy/ai-passport`](https://github.com/folotoy/ai-passport), retains the
upstream MIT License and BSP foundation, and adds its own Registry, signed
package format, bounded VM, host UI, themes, device-code installer, and Nearby
runtime. It is not an official FoloToy release or an upstream mirror.

The repository was reinitialized during development and therefore does not
retain upstream Git ancestry. Upstream changes must be reviewed by subsystem;
they must not be merged wholesale across the plugin lifecycle, memory, or Flash
boundaries described above.
