# UniXNU-thyme — 小米 10S

[English](README.md)

基于小米 10（umi）成果开发的小米 10S（thyme / SM8250-AC）实验移植。
目前没有 thyme 进入 Hello、桌面、XNU 触摸可用或可日用的验证结果。

2026-10-25 实测一台 8GB thyme：Android ADB 可连接，Bootloader 已解锁，
当前槽位 b。当前 Android ADB 为 uid=2000，su 不可用；临时启动 TWRP 后
取得 uid=0，已读取设备树、内存、GPT 和内核日志，没有刷写分区。
Recovery 的 pstore 为空，/dev/mem 不存在，尚未取得 XNU 运行追踪。

此前 README 的 Hello、触摸、第 80 轮结果属于上游 12GB 小米 10。
原始记录保存在[上游历史摘要](docs/validation-umi-upstream.json)，
本机结果单独记录在[thyme 验证摘要](docs/validation.json)。

此次修正移除了追踪里的 framebuffer 和看门狗副作用：原补丁重复检查已占用
L1 槽位而进入死循环，画屏地址不在新增映射内，且 TTBR0 撤销后仍使用物理地址。
恢复的 console 追踪经过 8 组编译后 ARM64 指令仿真，完整内核重编译及真机测试待做。

[构建现状及后续门槛](docs/BUILD.md)；原[umi 构建文档](docs/BUILD-umi-upstream.md)
作为历史参考保留。本机工作区的 thymePkg 固件和 DarwinBoot 改动还须纳入版本管理；
仓内 umiPkg、umi 硬件 JSON 及 source-lock.json 不能作为 thyme 已验证的依据。

源码内核 XNU 10063.141.1；系统输入为 iPad7,1 / iPadOS 17.7.11 / 21H461。
默认配置关闭 native UFS、root、Data、显示等接入，kernel CI 成功不等于完整系统可启动。

原有致谢、来源及许可保留于 [NOTICE.md](NOTICE.md) 和 [LICENSE](LICENSE)。
