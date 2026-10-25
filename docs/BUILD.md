# thyme 构建与验证路线

## 当前构建边界

主仓使用 Coryyi/UniXNU 的 umi-pin 分支，设备仓为 Coryyi/UniXNU-thyme。
2026-10-25 的 Actions run 40202591498 成功完成 kernel；userdata、payload 均未执行。
本次追踪修复尚未完成新的完整 kernel CI 或真机验证。

source-lock.json 固定的是原 umi 工作区。旧 check-umi-repository.py 还要求
board.name=umi 和固定上游提交，不能拿它给当前 thyme fork 出具通过证明。
不要只改锁文件使检查变绿；应建立实际固件、内核、ADT、镜像的 SHA-256 清单。

## 按验证门槛推进

1. 固定并纳管本机实际使用的 thymePkg、BootShim、DarwinBoot 和构建脚本，保留当前可回 Android 的备份。
2. 使用 console-only 追踪重编译内核；检查追踪开关与最终 kernel 哈希，重新组装 payload 和 boot.img。
3. 根据本机 FDT、XBL 配置、保留区及 UEFI 实际分配记录核对内存。Recovery 顶部 reserved 区说明不能仅凭 DRAM bank 覆盖就声称整段可分配。
4. 建立可读的 XNU 阶段证据。若需画屏，使用 BootArgs 的已验证 scanout，限定写入边界，建立正确页表并处理 MMU 生命周期；与 watchdog 分开实验。
5. 先验证入口、MMU、arm_vm_init、timer/GIC，再逐项启用 SMP。交接前色带和复位时长不能替代内核阶段记录。
6. 单独验证 UFS 读取、OS/SystemOS、root、launchd，再考虑 Hello。默认 umi_defconfig 不包含完整用户态配置。
7. 在测试区验证 Data 和 FTS 触摸，之后分别评估 GPU、网络、音频、USB 和电源管理。它们各自需要驱动与真机验证，无可靠完成日期。

临时 fastboot boot 不刷写 boot 分区，但启动的固件仍执行真实硬件操作；
所有测试镜像都应先检查其存储写入路径、设备身份和内存范围。

## 上游资料

[原 Mi 10 构建说明](BUILD-umi-upstream.md)记录原始本地 ABI、seed、策略等输入依赖；
它不是本机 thyme 干净克隆后的一键构建指南。
