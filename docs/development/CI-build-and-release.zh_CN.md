<p align="right">
  <strong>简体中文</strong> · <a href="CI-build-and-release.md">English</a>
</p>

# 自动构建、打包、发布与 Pages 部署

`.github/workflows/pipeline.yml` 是唯一的发布工作流。它负责检查 Pull
Request、重新打包仓库内所有 PAP、在 `main` 提交生成的 `dist/*.pap`、构建
ESP32-C3 固件、发布插件/主题与固件，并部署静态 GitHub Pages 网站。

## 触发条件

- **Pull Request**：校验当前 PAP、网站目录、网页安装器、仓库规则和主机测试；不授予写入或发布权限。
- **Push 到 `main`**：重新生成 PAP，只更新对应目录中发生变化的生成文件，发布滚动的 `latest` 预发布版本并部署 Pages。
- **Push `v*` Tag**：重新生成 PAP，发布不可变的版本 Release，包含固件、全部插件/主题包、目录和校验和。
- **手动触发**：对选择的 ref 执行同样的发布流程。

## 流水线

1. 打包任务扫描 `examples/**/manifest.json`，校验当前完整 Schema，使用
   `tools/pack_pap.py` 重新生成每个包，拒绝过期或重复输出，并写出确定性的
   目录。`main` 构建中，独立的最小权限任务会在静态检查和固件检查通过后，
   只提交生成的 `dist/*.pap`，使用 `github-actions[bot]` 推回 `main`。该
   Token 推送不会递归启动同一工作流。
2. 静态任务把生成的 PAP 恢复到检出目录，运行
   `./tools/validate.sh --static`，在临时目录构建网站，并校验包、文档和安装器链接。
3. 固件任务使用 ESP-IDF 5.5.3 针对 ESP32-C3 运行
   `./tools/validate.sh --firmware`，上传经过校验的合并镜像。
4. 发布任务为分支构建创建或刷新 `latest`，为 Tag 创建版本 Release，上传
   `FoloToy-AI-Passport-full.bin`、所有 `.pap` 资源、`catalog.json` 和 `SHA256SUMS`。
5. 同一构建会把生成的插件包复制到 Pages 产物。网站读取同源的
   `data/catalog.json` 和包文件，因此安装链接不依赖 GitHub API 目录列表或跨域跳转。

所有 Action 都固定到完整 commit SHA。只读检查使用 `contents: read`；只有生成文件提交和
Release 任务获得 `contents: write`；Pages 部署使用独立的 `pages` 与 OIDC 权限。首次部署前，
请在仓库设置中把 Pages → Build and deployment → Source 设置为 **GitHub Actions**。

## 生成文件

已提交的 `examples/**/dist/*.pap` 是由同目录源码生成的发布产物，不要手工编辑。运行
`python3 tools/build_pap_catalog.py --check` 可以验证它们与当前 Manifest 和 payload 一致；
工作流在提交变更前使用 `--write --prune`。

启用 Pages 后，网站地址为
`https://rvaim.github.io/ai-passport/`。插件/主题卡片读取生成的目录，并打开现有 Web Bluetooth
安装器，同时预选对应的安装包。浏览器安装仍要求安全上下文和桌面版 Chrome 或 Edge。

硬件与烧录细节见[硬件开发指南](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)。
