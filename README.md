# 基于树莓派和YOLOv8n的视觉检测系统

运行于Raspberry Pi 4B的积木装配视觉检测项目。通过超声波触发USB摄像头截图，定位并校正灰色底板，识别五类积木，再根据黑色参考块判断彩色积木是否缺失或位置错误，通过Web页面反馈结果。

端侧使用C++完成图像处理、ONNX模型推理、位置规则判断与硬件接口调用；Python端提供数据预处理、标注、训练和ONNX导出工具。

## 效果展示

![检测界面](docs/images/detection_ui_1.png)

网页展示实时画面、底板透视图、检测框、类别及业务判断。[另一组检测界面](docs/images/detection_ui_2.png)

![树莓派部署](docs/images/deployment.jpg)

Raspberry Pi 4B、USB摄像头与超声波测距模块组成的检测装置。

## 技术栈

| 层次 | 实际使用技术 |
|---|---|
| 端侧平台 | Raspberry Pi 4B、AArch64 Linux |
| C++系统 | C++17、CMake、标准库线程、互斥锁与原子变量 |
| 视觉与推理 | OpenCV、YOLOv8n、ONNX Runtime CPU Execution Provider、FP32 ONNX |
| 硬件接口 | USB摄像头 / V4L2、libgpiod GPIO、超声波Trig/Echo测距 |
| 结果展示 | Linux Socket、HTTP、MJPEG视频流 |

## 系统流程

```text
超声波距离进入 (0, 12cm) 范围 → 请求下一帧截图
                                 ↓
USB摄像头 / V4L2 → 灰色底板定位 → 800×800透视校正
                                 ↓
640×640 Letterbox与归一化 → YOLOv8n / ORT CPU推理
                                 ↓
置信度筛选与NMS → 黑色参考块相对位置判断 → Web图像与文字反馈
```

超声波条件从不满足变为满足时触发一次截图；快照版本号用于识别新的截图。GPIO承担测距和触发，检测结果由Web页面展示。

检测类别为 `black_reference`、`beige`、`blue`、`brown`、`red`。位置判断选择各类别置信度最高的目标，以黑色参考块所在边统一方向，在800×800校正图上计算相对坐标偏差；先检查缺件，再使用100像素径向阈值判断位置错误。

## 核心工程实现

- **C++模块划分**：`Camera`、`Ultrasonic`、`ImagePreprocessor`、`YoloDetector`、`PositionRules`和`WebServer`分别封装采集、硬件、视觉、推理、规则与展示，由主程序串联。
- **采集与服务协同**：主线程负责测距、采集和检测，Web服务在独立线程运行；互斥锁保护共享帧和结果，原子标记与快照版本号管理截图请求。
- **底板视觉处理**：HSV阈值分割、椭圆核开闭运算、轮廓与四角点定位、透视变换，将底板统一到800×800，保持校正后的画面方向。
- **推理执行与结果映射**：Session初始化一次并跨帧复用；完成640×640 Letterbox、归一化、ORT Run、输出解码、NMS及检测框坐标还原。
- **业务与硬件衔接**：libgpiod读取超声波Echo脉宽并控制Trig，距离阈值触发检测；位置规则输出缺失/位置错误判断，HTTP接口提供状态、截图和检测结果。
- **独立性能验收**：复用原C++检测类，分开测量普通延迟、资源占用和ORT节点Profiling，并对底板定位、Tensor、检测框和业务结果进行一致性比较。

## 性能优化与离线验收

2026-10-16正式验收中，取消显式ORT 2线程设置，使用默认自动线程配置；保留Extended图优化、默认自旋、Sequential及CPU EP。模型保持FP32和640×640输入，预处理、形态学核及业务规则不变。

测试设备为Raspberry Pi 4B（4核Cortex-A72），Debian 13.4，ORT 1.26.0，OpenCV 4.10.0。固定15张图片，每进程只创建一次Session，预热20次，正式总计100次。

| 平均延迟 | 原显式2线程 | 默认自动线程 | 降低比例 |
|---|---:|---:|---:|
| ORT推理 | 608.09ms | 443.75ms | 27.03% |
| 离线完整流程 | 1080.76ms | 917.52ms | 15.10% |

离线完整流程包含图片读取、底板定位与透视校正、YOLO预处理/推理/后处理、位置规则及结果绘制。上述数据不包含真实相机采集、超声波触发和Web传输，不代表硬件联动时延。

15张图片的底板Mask、角点、透视图、检测框、类别及最终业务判断一致，原始FP32输出Tensor在验收误差阈值内一致。正式C++目标编译成功，离线输出一致性验证通过。

默认自动线程配置降低了本轮延迟，同时增加了CPU占用与最高采样温度。资源指标、参数对照、节点耗时和统计口径保留在详细文档中：

- [性能优化、两轮参数对照与正式复测](raspberry_pi/docs/performance_optimization.md)
- [最终验收报告与优化提交](FINAL_REPORT.md)
- [正式验收原始CSV与一致性记录](raspberry_pi/benchmarks/results/20261016_final_acceptance/README.md)
- [独立离线Benchmark及复现命令](raspberry_pi/benchmarks/acceptance/README.md)

## 编译与运行

树莓派端依赖C++17编译器、CMake、OpenCV、libgpiod 2.x C++库和AArch64版ONNX Runtime。使用现有ONNX模型，无需重新训练。

从仓库根目录执行，将ORT路径替换为设备上的实际安装目录：

```bash
cd raspberry_pi
cmake -S . -B build -DONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake --build build -j2
./build/gpio_test ../models/lego_yolov8n_manual.onnx
```

第一个参数为模型路径；省略时默认读取 `../models/lego_yolov8n_manual.onnx`。
运行用户需要访问 `/dev/video0` 和 `/dev/gpiochip0`。当前源码使用GPIO line offset 23/24作为Trig/Echo，并对摄像头帧顺时针旋转90°。

浏览器访问：

```text
http://树莓派IP:8080
```

离线验收入口与正式硬件程序分开运行，具体命令见上方Benchmark文档。

## 仓库导航

| 目录 | 内容 |
|---|---|
| [raspberry_pi/](raspberry_pi/) | C++正式程序、硬件接口与检测模块 |
| [raspberry_pi/benchmarks/](raspberry_pi/benchmarks/) | 独立测试入口、参数对照和验收数据 |
| [python/](python/) | 数据预处理、人工标注、训练及ONNX导出工具 |
| [models/](models/) | 模型权重与ONNX文件 |
| [dataset/](dataset/) | 项目数据与标注 |
| [results/](results/) | 模型训练结果 |

模型训练记录见[训练曲线](docs/images/training_results.png)和[混淆矩阵](docs/images/confusion_matrix.png)。训练指标与本次15张图片的性能/一致性验收分别记录。

项目当前检测五类固定积木，业务规则针对既定底板布局，判断范围为缺件与位置错误。
