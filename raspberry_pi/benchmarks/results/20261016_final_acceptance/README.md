# 离线验收数据：2026-10-16

原始实验目录：`/home/benben/vision_project/benchmarks/experiments/20261016_165702_final_acceptance`。
此目录发布必要的文本测量记录；原始目录中的完整源码快照、二进制、输入图片、逐图像素/Mask/Tensor和所有日志均保留在设备上。

- `device_before`：原设备源码，ORT显式2线程；`device_after`：同一源码仅取消显式线程设置。
- `repository_before/after`：GitHub源码改动前后的正式 `gpio_test` 编译日志和选项。两套源码均完成原类与插桩副本的离线输出对比。
- `latency`：独立进程，20次预热后100次正式计时；每进程只创建一个Session。
- `monitor`：另一进程，同样20+100次，附加进程内内存检查点和外部100ms间隔采样；其耗时不作为普通延迟。
- `stage_timing.csv`：100次正式记录。`warmup_timing.csv`：20次预热。图片按固定名单循环，预热后不重置图片索引。
- `board_step_timing.csv`、`detections.csv`、`business_results.csv`：完整120次记录，业务字符串以UTF-8字节的十六进制保存。
- `stage_summary.csv`：ms，线性插值分位数，位置 `(n-1)*q`；总体标准差（除以n）。
- `memory_samples.csv`、`memory_checkpoints.csv`：原始内存字段单位KiB；`system_samples.csv`：CPU百分比、温度℃、频率kHz。
- `resource_summary.csv`：原分析器保留了内存指标名的 `_kib` 后缀，但其中内存统计数字已除以1024，实际单位为MiB。原始CSV未改写。
- `monitor_metadata.json`：实际采样方法、范围、退出码和内核峰值RSS。资源均值覆盖整个监控进程（初始化、预检、预热、正式测量、结果写入和退出）。CPU按单核100%计算。
- `correctness.json`、`correctness_images.csv`：全部6组、每组15张比较。`business_consistency.json`：4个测量进程各120次的原业务字符串比较。
- `image_manifest.csv`：原15张图片的固定顺序、仓库位置和SHA256；未复制图片到此目录。
- `original_sha256.json`、`source_change.patch`：设备原源码哈希和唯一正式源码改动。`environment.json`、各组 `config.txt`：环境和实际配置。
- `../parameter_studies/`：前两轮全部配置的汇总，以及第二轮全部实际节点/算子比较，不截取Top10。节点耗时来自独立Profiling进程。

完整流程计时从 `imread` 前至原 `PositionRules::check` 返回后，包含底板检测与800×800透视校正、640×640模型预处理、ORT Run、后处理/NMS及业务结果绘制。初始化、预检、预热、验证数据导出不计入100次延迟。
NMS包含于后处理；`detector_total_ms`包含预处理/Run/后处理；底板内部步骤存在嵌套，不应重复求和。
全部正式验收结论限定为离线C++检测链路。
