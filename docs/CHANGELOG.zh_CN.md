<p align="right">
  <strong>简体中文</strong> · <a href="CHANGELOG.md">English</a>
</p>

# Changelog

## Unreleased

- 更新夜间主题与新粗野主义主题示例，使其使用当前稀疏 `styles` Manifest，并补充系统“主题”App 生命周期说明。已安装主题现在拥有详情页和两步确认的异步卸载流程；内置默认主题受保护，删除当前主题前会先切回默认主题。
- 将固定 Factory App 分区从 3 MiB 缩小到 2 MiB，并把释放的 1 MiB 全部分配给 `appfs`。当前应用仍保留约 39% 的 Factory 余量，插件、主题、staging 和私有数据分区则扩大到 5.94 MiB。
- 移除仓库内置的计数器示例插件、生成的包、专用主机测试、验证入口和过时文档链接；通用 API 文档现改用中性的示例 Demo。
- 为 PAP App 新增私有持久化存储。已安装 App 把只读 `bundle` 与系统管理的 `data` 子树分开；同 ID 更新保留数据，卸载原子移除整个容器。异步 `passport.storage` 提供有界读取、原子写入、删除、列表、用量查询、整数错误枚举、路径隔离、64 KiB/16 文件配额和两个未完成请求，不会在 LVGL 锁内执行 Flash I/O；非空文件系统损坏时也不再静默格式化。
- 围绕系统统一导航、24 种继承式公共样式和整数输入枚举重做 PAP 运行时。原生页面与 PAP 路由使用有界八层栈；长按确定在二级页面返回上一页，只在根页退出到桌面。PAP 现在可通过受保护封装使用 LVGL View、Text、Button、Image、List/ListItem、Bar、Arc、Slider、Switch、Spinner、Line、Checkbox 与 Canvas，而不会拿到裸指针。Image、Line、Canvas 共用每页 32 KiB 预算，内存中只保留当前可见树，每页最多 48 个 PAP 底层 LVGL 对象。上/下键双击使用明确事件枚举，当前共用 ADC 硬件会报告不支持组合键。
- 新增所有 PAP 共用、基于 cJSON 的有界 `passport.json` 系统 API。插件可以把 JSON 解码为 Lua 值、把 Lua 值编码为紧凑 JSON、保留嵌套 null 与空数组，并通过可恢复的 `nil, error` 处理失败；主机测试覆盖往返转换、UTF-8、重复键、深度/节点/大小限制、不安全数字、稀疏 table、循环引用和不支持类型。
- 移除未发布阶段的 Manifest 与主题兼容路径：包必须使用当前完整 Schema 和 API `1`，会拒绝包括无效 `permissions` 占位字段在内的未知字段；主题使用有界稀疏 `styles` 对象，不保留旧 `tokens` 回退。打包器与固件共用一致的解析规则和主机测试。
- 修复较大 PAP 偶发命令行安装失败。根因是无响应 BLE 写入会冲满设备端固定的 8 项接收队列；CLI 现在先等待设备确认接收，使用有响应分片形成背压，等待最终设备结果，并在失败时返回错误，不再固定等待两秒后乐观退出。新增 Fake BLE 主机测试锁定传输行为。
- 新增可安装的 Agent 授权面板 PAP 与可编辑的 Web Bluetooth Demo。面板会显示一条紧凑 JSON 请求，允许用户在最多三个选项中选择，通过 Passport Link 回传结果，并按请求 ID 重放重复请求以应对传输重试；Demo 可连接已打开的授权面板，预览实际的 200 字节 payload，发送请求或取消消息，并显示设备响应。
- 修复所有 PAP 在运行时初始化阶段都无法启动。托管 Lua 核心按预期使用 32 位数值 ABI，但 Passport API 桥接层按宿主默认 ABI 编译，导致 `luaL_newlib` 拒绝不匹配，且未受保护的初始化异常会让设备复位。桥接层现在与核心使用相同 ABI、在构建时断言数值宽度、在 Lua 保护调用内初始化，并由主机测试使用同一数值模型。JSON 现在会拒绝超出文档所述 32 位运行时范围的值，不再静默丢失范围；Agent 授权面板已在真机以 33,941 / 81,920 字节 Lua 堆成功启动。
- 扩充可安装主题的有界视觉属性，覆盖表面、选中文字、边框、圆角、间距、不透明度、对齐和阴影几何。共享列表组件会为绘制范围安全留边，且不增加对象或任务；新粗野主义示例使用 2 px 边框与偏移 4 px 的硬阴影。
- 将原有的简短插件说明扩展为详细的双语插件开发指南，覆盖 Manifest 字段、生命周期、完整 Lua API、运行时限制、Link 行为、打包、安装、测试、排障和发布检查。
- 修复安装 `.pap` 导致设备复位。除原 4 KiB 安装任务不足以承载 FATFS 与 cJSON 调用链外，主题校验还会在任务栈中嵌套多份完整主题定义，因此即使扩大到 6 KiB 仍可能溢出。仅校验时现在不再构造主题对象，加载已安装主题时直接写入目标缓冲，Registry 与主题 Manifest 缓冲区继续留在系统任务栈之外，同时安装任务会报告实测最小剩余量。
- 将按键多击判断窗口从 180 ms 缩短到 100 ms，将系统返回桌面的按住时间从 1.5 秒缩短到 800 ms；原生页面的 UP/DOWN 双击现在明确移动两行，不再丢失。设置页把公开设备码移入独立“设备信息”二级页并移除主题 footer；插件管理页在已安装插件列表上方显示设备码。新增无外部依赖的 Web Bluetooth `.pap` 安装器，支持输入配对码、按 Service UUID 发现设备、连接后复核设备码、显示进度/错误状态、有响应写入和有界重试，并补充协议主机测试与可供浏览器发现的 Service UUID 广播。
- 重做原生设置 App，加入四项可直接调整并持久化的功能：默认 50% 的屏幕亮度、默认 30% 且异步试听的系统音量、默认 30 秒的息屏时间，以及默认关闭的按键音开关。单一有界工作任务负责合并 NVS 写入并按需初始化音频；息屏后的第一次按键序列只唤醒屏幕，不会误触发隐藏界面。接入真实 ES8311/I2S 路径后，最终应用镜像为 1203424 字节，Factory 分区仍剩余 62%。
- 修复 UI 文字全空：启用生成的 RLE 压缩字库所必需的 LVGL 解码器；静态验证与组件配置现在都会拒绝“压缩字库与解码开关不一致”的构建。
- 用可复现的 Noto Sans SC 14 px / 4 bpp 字库替换覆盖不全的内置 CJK 子集，覆盖全部 3755 个 GB2312 一级常用汉字和两个 Font Awesome 导航图标；16 级 alpha 消除了 2 bpp 边缘量化造成的明显颗粒感，静态检查会锁定源码图、字体 profile、图标范围和解码器。
- 按真实输入模型重做底部动作提示：系统统一管理上/下键选择和长按确定的返回/主页行为，PAP 只通过 `passport.ui.action` 提供短按确定动作词；超长文案显示省略号。原生页面与 PAP 路由使用相同导航语义。
- 在扩充并平滑中文覆盖的同时，将加入设置功能前的应用镜像从 1346800 字节降至 1146528 字节，减少 200272 字节（14.9%）。4 bpp 方案比中间版本的 16 px / 2 bpp 构建增加 101936 字节，但没有新增 LVGL buffer、task、字体 fallback 或 kerning table。该阶段只保留 RGB565 / Label / Flex 路径；后续 PAP 控件支持会按上述有界设计启用额外 LVGL 对象。
- 精简仓库根目录：将 GitHub 可识别的社区治理文档迁入 `.github/`，将变更记录迁入 `docs/`，同步全部引用，并在仓库检查中加入根目录文档白名单。
- 全仓库文档语言规范：所有维护中的 Markdown 默认 `.md` 文件使用英文，简体中文使用配对的 `.zh_CN.md`，双方提供语言切换；静态检查会阻止缺失配对、缺失切换链接或英文默认页混入中文正文。
- AI 开发流程一期：精简按任务加载的上下文入口，统一本地/CI 验证脚本，新增 PR 自动构建与模板，并提交依赖锁文件以提高构建可复现性。
- PR 审查修复：GitHub Actions 固定到完整 commit SHA，构建与发布 job 按最小权限拆分，同步 checkout 关闭凭证持久化；补充 Feature Request / Usage Question issue 表单；启用并修正私密安全报告兜底说明；清理 README 路径、CI 触发条件与历史分支描述漂移。
- 语言规范变更：commit 标题、PR 标题与 body 由"默认中文"改为**使用英文**（`docs/contribution/commit-and-pr.md` 更新）；中文写作规范（全角标点）适用范围剔除 PR/MR 描述（`doc-conventions.md` 更新）。
- CI 构建改造：`build-firmware.yml` 显式传入 `SDKCONFIG_DEFAULTS=sdkconfig.defaults` 再 `idf.py build`，由 defaults 启用自定义分区表（`CONFIG_PARTITION_TABLE_CUSTOM=y`，文件名为 `partitions.csv`）；`CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE` 改为 `n`，再用 `idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin` 合并可直刷完整固件；产物精简为仅 full.bin；`actions/cache` 升级到 v5 以消除 GitHub Actions Node.js 20 弃用警告；CI 文档同步更新。
- 合并上游 PR #6（wireless-low-power-demos）以解决 PR #4 冲突：引入无线/低功耗 demo（`main/demo_wifi.c`、`demo_ble.c`、`demo_radio.c`、`demo_low_power.c`）、`partitions.csv`（NVS/PHY/3 MB factory-app 分区）、`main/CMakeLists.txt`/`main.c`/`demo.h`/`sdkconfig.defaults` 更新；同步硬件指南的 Wi-Fi/BLE/低功耗章节；README 能力契约表补充 Wi-Fi/Bluetooth LE/Low power 三项（中英双语）。
- 提交规范补充：`docs/contribution/commit-and-pr.md` 明确 PR 标题与 commit 标题使用相同的 Conventional Commit 格式和英文祈使句，不用名词短语当标题。
- CI 与文档清理：`sync-main.yml` 移除 `test_mode` 残留模板注释；`docs/development/coding-conventions.md` 将「Redis TTL」条目泛化为「缓存组件」条目（当前固件无 TTL 约束需求，消除从模板带入的无关约定）。
- 补充通用规范（借鉴 Shinku）：`docs/contribution/doc-conventions.md` 新增中文全角标点规范（正文 `，`；`（`）`，代码/命令/路径保留英文原样）、凭证不入仓规范（token/密钥/私钥绝不入仓，提交前 git diff 扫描敏感前缀）、文件删除安全规范（删除走系统回收站，不用 rm -rf/git clean -fd）。
- 代码注释规范强化：`docs/development/coding-conventions.md` 补充完善注释要求——函数说明（用途/参数/返回值/副作用/线程上下文/内存所有权/初始化顺序）、变量说明（语义/取值范围/生命周期/同步要求）、逻辑注释（状态机/时序/寄存器/魔数依据），覆盖范围宁多勿少，中文注释保留英文技术术语。
- 文档去 AI 化：`docs/README.md` / `docs/README.zh_CN.md` 移除 AI 专属章节（Entry point、Source-of-truth、提需求格式、BSP 边界、Runtime invariants、验收交付格式、构建命令），README 只保留给人看的项目介绍、硬件能力契约、demo 案例与项目结构；构建命令章节删除（与 `docs/development/build-and-test.md` 重复）。
- 新增 `docs/development/agent-guide.md`：集中承载"AI 如何在本仓库工作"（上下文建立顺序、事实来源优先级、提需求格式、BSP 边界、运行时规则、交付格式），并链接 build-and-test 与硬件指南，不重复构建命令与验收矩阵。
- 同步更新索引：`AGENTS.md` 规则索引新增 agent-guide 条目；`docs/INDEX.md` 与 `docs/development/README.md` 新增 agent-guide 索引行。
- 文档补充：`docs/fork-guide.md` 说明「为什么根目录不放置 README」——根目录 README 预留给 fork 开发者自行放置（上游留空），fork 后可将自己的内容写入根目录 `README.md` 介绍 fork 后的项目；GitHub 显示优先级（根 README > docs/README.md）契合该预留意图。
- 分支合并：创建 `main-update` 分支（基于与上游一致的 main），将 `feature/repo-structure`、`ci/build-firmware`、`ci/sync-main` 三个分支合并进来，统一 docs 结构（CI 文档归入 `docs/development/`，workflow 文件随 ci 分支引入 `.github/workflows/`）；解决 development/software-design README 的 add/add 冲突。
- 合并后审查修复：`docs/INDEX.md` 补充 CI 文档索引；`docs/fork-guide.md` 修正 workflow 引用为 `.github/workflows/sync-main.yml`；`docs/README` 双语项目结构块补充 `.github/workflows/` 与 CI 文档说明。
- ci 分支 CI 文档路径调整：`ci/build-firmware` 的 `docs/software-design/CI-build-and-release.md` 与 `ci/sync-main` 的 `docs/software-design/CI-sync-main.md` 均移入各分支的 `docs/development/`（CI 属工程规范）；`docs/software-design/README.md` 保留为软件设计索引；feature 分支的 software-design 索引同步更新引用。
- fork 补充文档目录迁移：`assets/docs/` 移至 `docs/assets/`（文档素材归入 docs/ 更合理），新增 `docs/assets/.gitkeep` 空目录占位；同步更新 AGENTS.md / INDEX / doc-conventions / fork-guide 的路径引用。
- 文档结构调整：根目录不再放 README——上游英文 README 移入 `docs/README.md`、中文移入 `docs/README.zh_CN.md`（GitHub 从 docs/ 识别主 README）；原 `docs/README.md` 根总索引更名为 `docs/INDEX.md`；同步更新 AGENTS.md / CONTRIBUTING / SUPPORT / fork-guide / doc-conventions 的路径引用。
- 初始化项目文档：新增 `AGENTS.md`、`CLAUDE.md` 和 `CHANGELOG.md`。
- 仓库结构规整：上游英文 `README.md` 更名为 `README.en_US.md`，保留 `README.zh_CN.md`。
- 新增目录骨架：`docs/`（software-design / hardware-design）、`assets/`（fonts / images / music，各含 `README.md`）、`skills/`。
- 将上游硬件开发指南归位到 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`。
- 文档规范：子目录 readme 统一为大写 `README.md`；补充 fork 用户约定（main 只动根 README）。
- 扩展 fork 用户约定：`main` 分支允许修改根目录 `README.md` 和 `assets/docs/`（README 不足以说明项目时存放补充文档与素材）。
- 新增 `assets/docs/` 目录约定：上游 main 只保留空目录 `.gitkeep`，内容文件仅存在于 fork；使用方法规范写入 AGENTS.md「给 fork 用户」约定。
- CI 文档迁移：`docs/software-design/CI.md` 从本分支移除，迁至 `ci/build-firmware` 分支并改名为 `docs/software-design/CI-build-and-release.md`。
- 补充 `main` 分支策略说明：解释 `main` 保持干净的两大原因（与上游同步无冲突 + 多小项目按分支整理）；例外——执意 main 开发需停用 CI 自动同步；提醒 fork 用户默认 action 关闭需手动启用（此条为整个 CI 的通用要求，统一写入 AGENTS.md）。
- 文档拆分：将 `AGENTS.md` 按主题拆为公共文档——新增 `docs/contribution/`（doc-conventions.md、commit-and-pr.md）与 `docs/development/`（build-and-test.md、coding-conventions.md），新增 `docs/fork-guide.md`；`AGENTS.md` 精简为简介 + 项目概述 + 必读文档索引。
- 同步更新索引：`docs/software-design/README.md`、`README.en_US.md` / `README.zh_CN.md` 的 `docs/` 目录说明。
- 参考 cindy 仓库文档组织完善索引：新增 `docs/README.md` 根总索引；AGENTS.md 规则索引按触发场景改写（附触发条件）；`docs/contribution/` 与 `docs/development/` 的 README 补充收录标准。
- 引入社区治理文档（参照 cindy 改写，放仓库根目录）：新增 `CONTRIBUTING.md` / `.zh_CN.md`（贡献指南，针对 ESP-IDF/AI agent/fork 场景改写）、`CODE_OF_CONDUCT.md` / `.zh_CN.md`（贡献者公约）、`SECURITY.md` / `.zh_CN.md`（安全报告流程）、`SUPPORT.md` / `.zh_CN.md`（支持渠道）；AGENTS.md 与 docs/README.md 同步引用。

## Passport Platform v1（本次改造）

- 将硬编码 Demo 菜单改造成中文 Launcher + 单前台 App 管理模型。
- 新增 `.pap` 可安装 Lua 插件、插件管理、BLE 无系统配对安装与公开设备码目标校验。
- 新增统一中文页面容器、状态栏、语义化动作提示栏和共享 14 px / 4 bpp 中文字体。
- 新增轻量继承式公共样式，可通过同一 `.pap` / BLE 链路安装。
- 新增授权面板插件示例、夜间主题示例、打包/检查/BLE 安装工具和完整平台文档。
