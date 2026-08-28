<p align="right">
  <strong>简体中文</strong> · <a href="CI-build-and-release.md">English</a>
</p>

# 自动构建、固件发布与 Pages 部署

`.github/workflows/pipeline.yml` 是唯一的验证和发布工作流。它负责检查仓库与主机行为、构建 ESP32-C3 固件、发布固件 Release，并把一个统一的 Web Bluetooth 安装器部署到 GitHub Pages。工作流不会重新生成 PAP、构建 PAP 目录、提交 PAP，也不会发布仓库内的 PAP 文件。

## 触发条件

- **Pull Request**：执行仓库、主机、安装器产物和固件检查，不授予写入或发布权限。
- **Push 到 `main`**：执行同样检查，刷新滚动的 `latest` 固件预发布版本并部署 Pages。
- **Push `v*` Tag**：执行同样检查，发布不可变的版本固件 Release，然后部署 Pages。
- **手动触发**：对选择的 ref 执行发布流程。

## 流水线

1. 静态任务先使用 ESP-IDF 5.5.3 在隔离构建目录中生成 `dependencies.lock` 锁定的 Managed Components，再运行 `./tools/validate.sh --static`。共用门禁检查仓库规则、主机行为、`.pap` 打包器、Web Bluetooth 协议 helper，以及只包含安装器的准确 Pages 产物。
2. 固件任务使用 ESP-IDF 5.5.3 针对 ESP32-C3 运行 `./tools/validate.sh --firmware`，验证合并镜像，并把它上传为工作流内部产物。
3. 对非 PR 事件，固件发布任务创建或刷新 `latest`，或者创建版本 Tag 对应的 Release。Release 只包含 `FoloToy-AI-Passport-full.bin` 与 `SHA256SUMS`。
4. Pages 任务与固件发布并行运行：它先使用 GitHub 官方 `actions/configure-pages` 读取已经配置的 Pages 站点，再直接用 `web/installer.html`、`web/installer.mjs` 和 `web/passport-install-protocol.mjs` 构建并上传无依赖产物。
5. Pages 部署任务发布已经上传的产物。Pages 配置与固件发布是独立任务，因此 Pages 配置失败不会阻断 BIN Release。

所有 Action 都固定到完整 commit SHA。验证任务使用 `contents: read`；只有固件 Release 任务获得 `contents: write`；Pages 配置与部署使用独立的 `pages` 与 OIDC 权限。

## Pages 启用

`rvaim/ai-passport` 已把 Pages 构建来源配置为 **GitHub Actions**。工作流使用短期 `GITHUB_TOKEN` 读取配置并部署，不需要 Personal Access Token 或 `PAGES_TOKEN` Secret。全新的 fork 仍需在仓库设置中启用一次 Pages 并选择 **GitHub Actions**，因为 GitHub 不允许工作流自动生成的 Token 自行创建 Pages 站点。

## PAP 与 Pages 边界

仓库示例 PAP 是开发者显式运行 `tools/pack_pap.py` 产生并维护的输出；CI 不再重新生成或提交 `examples/**/dist/*.pap`，Release 也不再包含 PAP 文件或包目录。

启用 Pages 后，公开安装页位于 `https://rvaim.github.io/ai-passport/`。页面只提供统一安装器：输入公开配对码，选择本地 `.pap` 插件或主题，连接匹配的 Passport，然后安装。所选文件只保留在浏览器中，不会上传。Web Bluetooth 要求安全上下文以及桌面版 Chrome 或 Edge。

硬件与烧录细节见[硬件开发指南](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)。
