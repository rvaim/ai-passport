<p align="right">
  <strong>简体中文</strong> · <a href="CI-validation.md">English</a>
</p>

# PR 自动验证（CI Validation）

`.github/workflows/pipeline.yml` 会在 Pull Request、`main` push、版本 Tag 和手动触发时运行验证任务。它与本地共用 `tools/validate.sh`，只有这些检查通过后才会发布。

## Jobs

- **Repository and host checks**：校验 Markdown 相对链接、GitHub Action 完整 SHA、Issue Form 基本结构、依赖锁文件、冲突标记和疑似敏感信息；随后运行 `actionlint` 与 host tests。
- **ESP-IDF 5.5.3 firmware**：在全新隔离的构建/配置目录中，使用 ESP-IDF 5.5.3 / ESP32-C3 运行 `./tools/validate.sh --firmware`，验证编译、0x0 合并固件及其中三个映像的偏移和内容，并把已验证镜像交给发布任务。

验证任务只有 `contents: read` 权限，不使用仓库 secrets，因此可以安全验证来自 fork 的 PR；只有符合条件的发布任务会获得写入权限。所有 GitHub Actions 固定到完整 commit SHA；升级时必须核对官方版本、更新 SHA 注释并运行 `actionlint`。

## 本地复现

```bash
./tools/validate.sh --static
get_idf553
./tools/validate.sh --firmware
```

CI 失败应先在本地运行相同模式。不要在 workflow 中复制另一套构建或校验命令。
