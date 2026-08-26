<p align="right">
  <strong>简体中文</strong> · <a href="build-and-test.md">English</a>
</p>

# 构建与验证（Build & Test）

使用 ESP-IDF 5.5.x（已知开发环境 5.5.3）：

```bash
get_idf553                    # 进入仓库的 ESP-IDF 5.5.3 环境
idf.py set-target esp32c3     # 配置目标芯片（fresh checkout 后/换 target 后运行）
idf.py build                  # 编译固件，验证依赖
idf.py flash monitor          # 烧录并打开日志
idf.py fullclean              # 配置过期时清空生成状态（勿用于清理用户源码改动）
```

仓库提交 `dependencies.lock` 以固定 ESP-IDF Managed Components 的解析结果。修改 `idf_component.yml` 后必须使用 ESP-IDF 5.5.3 重新生成锁文件、review 版本变化并与 manifest 一起提交；普通构建不应产生未提交的锁文件差异。

固件门禁使用全新的临时构建目录，并从仓库 `sdkconfig.defaults` 生成隔离的 `sdkconfig`。它不会读取或覆盖开发者根目录的 `sdkconfig`，只把验证通过的合并镜像复制到 `build/FoloToy-AI-Passport-full.bin`。

当前基线包含四类无需硬件即可运行的主机测试：Passport Link 帧编码/校验、设置值与唤醒抑制状态机、`.pap` 打包格式，以及 UI 字库的 CMake 源码图/常用汉字字符集。

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -Itests/host_stubs -Icomponents/passport_core/include -Icomponents/passport_link/include \
  tests/test_passport_link_protocol.c \
  components/passport_core/src/passport_crc32.c \
  components/passport_link/src/passport_link_protocol.c \
  -o /tmp/test_passport_link_protocol
/tmp/test_passport_link_protocol
python3 tests/test_generate_ui_font.py
python3 tests/test_pack_pap.py
python3 tools/generate_ui_font.py --check
```

统一验证入口：

```bash
./tools/validate.sh --static    # 仓库/字库一致性、workflow、文档链接、敏感信息、host tests
./tools/validate.sh --firmware  # ESP-IDF build、merge-bin、固件偏移校验
./tools/validate.sh             # 完整验证
```

完整验证要求预先激活 ESP-IDF 5.5.3。CI 与本地使用同一脚本；若 CI 和本地行为不同，应先修复脚本或环境，而不是维护两份命令。

涉及物理外设的改动必须在真机运行硬件指南验收清单，并把“编译通过”与“硬件验证通过”分开记录。
