# 最终优化与离线验收报告

2026-10-16完成正式源码配置落地、编译、离线验收和GitHub更新。本次验收范围为固定15张图片的原C++检测链路。

- 正式源码只修改 `raspberry_pi/yolo_detector.hpp` 第31行：取消 `SetIntraOpNumThreads(2)`，使用ORT默认自动线程配置。
- Extended、默认自旋开启、Sequential、CPU EP保持原样；FP32模型、640×640输入、OpenCV参数、31×31与49×49椭圆核、底板定位和业务规则均保留。
- 新增独立离线入口及测量支持文件：`raspberry_pi/benchmarks/acceptance/`；补充性能文档、原始CSV、资源序列和输出一致性结果，保留原README必要内容。
- 设备源码和GitHub源码改动前后的正式 `gpio_test` 目标均编译成功。原C++检测类的独立离线入口运行成功，底板检测、透视校正、YOLO及PositionRules均参与测试。
- 已将验收通过的正式可执行文件部署至 `/home/benben/vision_project/build/gpio_test`，并核对文件SHA256；原可执行文件和原源码快照保留。

设备：Raspberry Pi 4B，4核Cortex-A72，MemTotal 3887804 KiB，Debian 13.4/aarch64，内核6.12.75+rpt-rpi-v8，ORT 1.26.0，OpenCV 4.10.0，GCC 14.2.0。业务程序编译选项为 `-std=gnu++17 -Wall -Wextra`，前后相同。

每个进程只创建一次Session，固定15张图片循环，预热20次，正式总计100次。普通延迟与资源监控分进程采集。

| 指标 | 原显式2线程 | 默认自动线程 | 变化 |
|---|---:|---:|---:|
| ORT Run平均（ms） | 608.09 | 443.75 | 降低27.03% |
| ORT Run P95（ms） | 612.15 | 446.41 | 降低27.07% |
| 离线完整流程平均（ms） | 1080.76 | 917.52 | 降低15.10% |
| 离线完整流程P95（ms） | 1089.00 | 924.92 | 降低15.07% |
| 进程CPU均值（单核100%） | 160.17% | 247.62% | 增加87.44个百分点 |
| RSS均值（MiB） | 154.62 | 153.30 | 减少1.32 |
| PSS均值（MiB） | 146.19 | 144.88 | 减少1.31 |
| 内核峰值RSS（MiB） | 163.86 | 163.77 | 减少0.09 |
| 最高采样温度（℃） | 68.65 | 74.50 | 增加5.84 |

完整流程包含图片读取、底板定位与校正、YOLO预处理/Run/后处理、业务判断与结果绘制。资源均值覆盖独立监控进程的初始化、预检、预热、正式测量及退出，采样请求间隔100ms；不与普通延迟混用。四核监控采样频率均为1.8GHz，降频标志均为 `0x0`。

15张图片的底板Mask、四角点、透视图和最终绘制图一致；检测数量、类别、整数框、中心点、最终业务判断一致。原类与插桩副本共4组对比、源码改动前后共2组对比均通过。原始FP32 Tensor全部满足 `atol=1e-5, rtol=1e-4`，最大绝对差0.00030517578125；检测置信度最大绝对差2.39e-7。普通/监控前后4个进程各120次业务字符串均与原版对应结果一致。

GitHub优化提交：[`96a7f62fe79f1bdd081c6c0739e5101e4c75f257`](https://github.com/b2185203-spec/raspberry-pi-lego-vision-inspection/commit/96a7f62fe79f1bdd081c6c0739e5101e4c75f257)。提交已发布并核对远端文件树；未删除历史提交或原始实验数据。新增发布文件已检查，不含密码、密钥、模型、图片、实验ZIP或编译缓存。

- [性能原理、两轮参数对照与正式复测](raspberry_pi/docs/performance_optimization.md)
- [100次原始延迟、内存/CPU/温度/频率及一致性记录](raspberry_pi/benchmarks/results/20261016_final_acceptance/README.md)
- [离线入口及复现命令](raspberry_pi/benchmarks/acceptance/README.md)
- 设备完整实验及原文件备份：`/home/benben/vision_project/benchmarks/experiments/20261016_165702_final_acceptance`。
