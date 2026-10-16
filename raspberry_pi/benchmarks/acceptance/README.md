# 独立离线验收入口

此入口在2026-10-16的Pi 4B正式配置验收中使用。复用原C++底板预处理、YOLO解码/NMS及PositionRules；不运行相机、超声波或Web服务。正式源码只改动ORT默认线程设置。

`prepare.py`在构建目录生成带计时和验证钩子的源码副本，SessionOptions取自传入的正式源码。原文件不会被改写。输入模型检查为FP32 `[1,3,640,640]`、输出 `[1,9,8400]`。

## 编译和运行

在仓库根目录执行，`ONNXRUNTIME_ROOT`指向设备上现有的AArch64 ORT安装目录：

```bash
export ONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake -S raspberry_pi -B build-production \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"
cmake --build build-production -j2
cmake -S raspberry_pi/benchmarks/acceptance -B build-acceptance \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"
cmake --build build-acceptance -j2

# 校验并复制固定15张图片；输出目录必须不存在
run="acceptance-output-$(date +%Y%m%d_%H%M%S)"
python3 raspberry_pi/benchmarks/acceptance/prepare_inputs.py --output "$run/inputs"
model="$PWD/models/lego_yolov8n_manual.onnx"
images="$PWD/$run/inputs"

# 普通延迟：Session一次，预热20次，正式总计100次
./build-acceptance/acceptance_benchmark latency \
  "$model" "$images" "$images/images.txt" "$run/latency"

# 资源监控：另一个进程，同样20+100次；不要同时运行
python3 raspberry_pi/benchmarks/acceptance/monitor.py "$run/monitor" -- \
  ./build-acceptance/acceptance_benchmark monitor \
  "$model" "$images" "$images/images.txt" "$run/monitor"

# 原类与测量副本各自导出15张完整检测结果，独立于计时
./build-acceptance/acceptance_verify \
  "$model" "$images" "$images/images.txt" "$run/verify_direct"
./build-acceptance/acceptance_verify_instrumented \
  "$model" "$images" "$images/images.txt" "$run/verify_instrumented"
```

固定输入的相对仓库位置、顺序及SHA256见 `image_manifest.csv`。若图片已在一个平铺目录，准备输入时可添加 `--flat-input /path/to/images`；仍逐文件核对同一SHA256。源码在其他目录时，可给CMake传入 `-DSOURCE_ROOT=/path/to/source`，不修改本仓库源码。

正式复测分别使用已保存的设备/GitHub改动前后源码快照，显式传入SOURCE_ROOT；四组正式目标均编译成功，原类与测量副本输出均通过比较。设备完整实验目录保留这些快照和实际命令。构建未添加Release或额外优化选项。

## 测量范围与验证方法

- `steady_clock`计时；普通模式不读取proc内存接口，不运行外部采样器。每轮不打印检测日志。
- 完整流程：图片读取、原底板定位/校正、YOLO预处理、`Ort::Session::Run`、后处理/NMS、位置规则与结果绘制。模型/Session初始化单独记录。
- 固定图片按索引循环，预热20次之后继续，正式100次，不是每张100次。普通/监控各自创建一次Session。
- `stage_timing.csv`保留全部100次；`warmup_timing.csv`保留20次；底板内部步骤、检测和业务字符串记录保留全部120次。
- NMS属于后处理，底板内部计时有嵌套，不能重复累加。统计采用总体标准差与线性插值分位数 `(n-1)*q`。
- 监控请求间隔100ms；proc提供RSS/PSS和系统可用内存，sysfs提供温度/频率，vcgencmd记录降频状态；CPU使用proc计数差值，单核100%。监控覆盖目标进程整个生命周期，其延迟与普通模式分开报告。
- 原类与插桩副本对比透视图、绘制图、检测类别/框/中心及业务字符串。改动前后再比较底板Mask、四角点及原始输出Tensor。Tensor容差 `atol=1e-5, rtol=1e-4`，置信度容差 `1e-5`；类别、整数框、业务判断要求一致。
- 验证导出像素、Mask和Tensor只在独立验证入口启用，计时中不启用这些导出。

[正式验收数据与统计](../results/20261016_final_acceptance/README.md)保留100次原始延迟、资源序列、全部逐图验证结果及实际配置；[性能文档](../../docs/performance_optimization.md)记录正式前后对照。
