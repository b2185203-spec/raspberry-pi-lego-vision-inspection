# 数据说明

- `images`：100张原始图片，五个文件夹各20张。
- `images_rectified`：当前实际使用的94张灰板透视裁切图，尺寸为800×800。
- `labels_rectified`：与这94张裁切图一一对应的YOLO五类标签。
- `data_manual.yaml`：类别顺序和训练数据配置示例。

下面6张原图目前没有进入训练集，因为它们没有对应的有效裁切图和标签：

- `missing_beige_002`
- `missing_beige_007`
- `missing_beige_008`
- `missing_blue_018`
- `missing_blue_019`
- `missing_brown_017`

运行预处理脚本时使用 `--clean` 会重新生成 `images_rectified`。重新训练前，需要确认新生成图片都有正确标签。
