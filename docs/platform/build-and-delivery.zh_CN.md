<p align="right"><a href="build-and-delivery.md">English</a> · <strong>简体中文</strong></p>

# 构建、测试与交付状态

## 所需环境

项目锁定 ESP-IDF 5.5.3。新增 Lua 依赖为 `espressif/lua 5.5.0`，由 Component Manager 下载。

```bash
idf.py set-target esp32c3
idf.py build
idf.py size
idf.py size-components
idf.py size-files
idf.py merge-bin -o build/ai-passport-platform-full.bin
```

## 2026-08-26 验证结果

```text
Build: PASS（ESP-IDF 5.5.3、ESP32-C3 干净构建）
Host tests: PASS
  - 仓库与双语文档检查：PASS
  - UI 字体 profile、覆盖与解码器测试：PASS
  - Passport Link 协议测试：PASS
  - 设置值、息屏计时与唤醒抑制模型测试：PASS
  - PAP 打包测试：PASS
Device tests: NOT RUN（用户明确要求不烧入，并已关闭设备）
Unverified: 实机亮度曲线、精确 30 秒息屏与唤醒序列消费、音量/按键音响度、重启持久化、按需初始化 I2S 后的空闲堆、屏幕字形清晰度与对比度、按键手感和 BLE 安装
```

产物：

```text
应用镜像：1203424 字节
合并镜像：1268960 字节
Factory 分区：3145728 字节（剩余 62%）
固件：build/FoloToy-AI-Passport-full.bin
SHA-256：13a8229df2b0732a79f957c422a316b53b9cad892e698b09461412cd99028c18
```

本次只生成离线产物，没有打开串口、调用 `idf.py flash` 或访问设备。
