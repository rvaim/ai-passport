<p align="right">
  <strong>简体中文</strong> · <a href="DESIGN.md">English</a>
</p>

# Passport 网页工具

## 用途

页面用于通过 Web Bluetooth 操作附近的一台 Passport。最上方是共享连接区，下面依次为“Passport 安装器”和“2FA 密钥发送”。安装器发送本地插件或主题 `.pap`，密钥工具向“2FA 验证器”PAP 配置一个 TOTP 账号。所选安装包、账号记录和密钥只在浏览器与设备之间传输，不会上传。用户输入 Passport 上显示的公开设备码；浏览器按主广播中的 Passport Service UUID 发现设备，网页连接后读取 GATT 中公开的 Device Code，设备码不一致时拒绝目标设备。

## 本地运行

Web Bluetooth 要求安全上下文。localhost 会被视为安全环境，因此应启动本地服务，不能直接双击 HTML 文件：

```bash
python3 -m http.server 8000 --directory web
```

使用桌面版 Chrome 或 Edge 打开 `http://localhost:8000/installer.html`。Safari、Firefox 和 iOS 浏览器当前不提供 Web Bluetooth。

## 交互与视觉系统

页面首先展示目标设备与连接状态，再展示两个有顺序的任务区；设备连接验证成功后会被两项工具复用，不要求用户为每个任务重新连接。设备码输入会在键入时规范化为大写 `XXXXX-XXXXX-X`，因此粘贴、带空格或已经带横杠的内容都会走同一套校验。安装器会持续显示文件、就绪状态、进度、设备反馈、失败恢复和完成结果。2FA 工具既可读取 `otpauth://totp` URI，也可手动填写签发方、账号、Base32 密钥、验证码位数和周期；密钥默认隐藏，并提供明确的时间同步与发送操作。两条流程都不使用网页自有弹窗。

Web Bluetooth 首次由该网站访问设备时，浏览器拥有的强制授权窗口仍是必经步骤；之后，`navigator.bluetooth.getDevices()` 会提供已授权设备，网页会让广播名称与 `Passport-<code>` 完全相符的已授权设备直接重连，不再打开设备选择器；否则，浏览器会列出广播 Passport Service 的设备。网页只有通过 GATT 复核完整设备码后，才会启用两个工具。GitHub Pages 直接把同一页面部署为首页，不提供包目录、独立主题入口、远程包预加载或内置 PAP 文件。键盘焦点、拖放、设备码无效、禁用、处理中、成功、失败、断开连接、浏览器不支持和非安全上下文等状态都有明确反馈。

视觉语言延续现有 Passport 安装器：浅蓝背景、深色文字、克制的蓝色操作控件、单一清晰边框容器、12～16 px 圆角和中性偏移阴影。本地配置工具不下载网页字体，使用系统字体以减少等待和外部依赖。SVG 图标采用一致描边，状态不会只靠颜色表达。

## 2FA 密钥发送

发送工具使用固定 Manifest 命名空间 `com.folotoy.totp-authenticator`。添加账号前，设备必须打开“2FA 验证器 → 接收 2FA 密钥”。读取 `otpauth://totp` URI 后会填充可见字段；页面支持 SHA-1、6 位或 8 位验证码，并在任何 BLE 写入前拒绝格式错误或不支持的输入。发送账号时也会附带浏览器当前 Unix 时间；独立的“同步时间”操作只更新时间，不改变已保存账号。

网页会等待 PAP 的 Link 响应，只有插件确认私有存储写入成功后才显示成功；成功后会清空 URI 与密钥字段。当前 Link 传输按需求不加密、不认证，因此页面会在密钥操作附近直接说明这一边界，不会暗示公开设备码能够保护机密性。

## 协议与限制

页面只接受非空且不超过 4 MiB 的 `.pap` 文件，会校验并规范化输入的设备码、按主广播中的自定义 Service UUID 发现设备、复核 GATT 返回的设备码、在本地计算 IEEE CRC-32，并以 180 字节分片进行有响应写入和有界重试。按 Service 发现延续了固件提交 `9d27ed8` 中已经验证的安装器行为，不依赖浏览器先合并主广播与 scan response 再执行筛选。页面会等待设备返回开始接收和最终安装通知。

2FA 应用消息通过现有 Link RX/TX 特征发送完整 Passport Link 帧，并遵守 200 字节 payload 上限。页面为当前连接生成随机公开 48 位浏览器来源 ID，以已复核的 Passport 设备码作为目标，并从固定 PAP Manifest ID 计算 FNV-1a service ID。纯协议 helper 由 `tests/test_web_installer_protocol.mjs`、`tests/test_passport_auth_protocol.mjs` 和 `tests/test_passport_totp_protocol.mjs` 覆盖，并全部纳入仓库静态门禁。
