<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# 2FA 验证器

这是一个可安装的 Lua PAP。它通过 Passport Link 接收统一 Passport 网页工具发送的
TOTP 账号，写入当前 App 的私有持久化存储，并在设备上生成 RFC 6238 验证码。
Base32 解码、HMAC-SHA1、计数器构造和动态截断全部由 PAP 实现；固件只提供易失性
墙上时间和页面级定时回调。

网页发送账号前，必须先在 PAP 中打开“接收密钥”页面。v1 载荷支持 SHA-1 TOTP、
6 位或 8 位验证码，以及 15～120 秒周期。再次发送相同签发方和账号会替换已保存
密钥；最多保存 12 个账号。

当前开发板没有电池供电的墙上时钟。重启或断电后，需要打开 PAP，并在网页中点击
“同步时间”，之后才能读取验证码。固件保持供电时，即使退出并重新打开 PAP，时间
仍然有效。

现有 Passport Link v1 链路按需求保持不加密、不认证。公开设备码只用于检查目标
设备，不能防止附近观察者或写入者获取或替换 TOTP 密钥。PAP 不会记录或回传已保存
密钥，但其私有 FAT 存储也不是硬件安全存储。

打包并检查：

~~~bash
python3 tools/pack_pap.py examples/totp-authenticator examples/totp-authenticator/dist/totp-authenticator.pap
python3 tools/inspect_pap.py examples/totp-authenticator/dist/totp-authenticator.pap
~~~
