# -*- coding: utf-8 -*-

"""训练 YOLOv8n，并导出供 ONNX Runtime 使用的模型。"""

import random
import shutil
from pathlib import Path

import torch
from ultralytics import YOLO


# 自动找到仓库根目录，所有路径都使用仓库内的相对位置。
ROOT = Path(__file__).resolve().parents[2]

SOURCE_IMAGES = ROOT / "dataset" / "images_rectified"
SOURCE_LABELS = ROOT / "dataset" / "labels_rectified"

DATASET_DIR = ROOT / "dataset" / "yolo_dataset_generated"
DATA_YAML = DATASET_DIR / "data.yaml"

RUN_NAME = "lego_yolov8n_manual"
TRAIN_DIR = ROOT / "runs" / "train" / RUN_NAME
FINAL_ONNX = ROOT / "models" / "lego_yolov8n_manual.onnx"

GROUPS = [
    "all_correct",
    "missing_beige",
    "missing_blue",
    "missing_brown",
    "missing_red",
]

# 类别编号必须与人工标注工具保持一致。
CLASS_NAMES = [
    "black_reference",
    "beige",
    "blue",
    "brown",
    "red",
]

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}

VAL_RATIO = 0.20
RANDOM_SEED = 42

EPOCHS = 100
PATIENCE = 20
IMAGE_SIZE = 640
BATCH_SIZE = 8


def find_image_label_pairs(group):
    """读取一个文件夹中的图片和标签，并检查是否一一对应。"""

    image_dir = SOURCE_IMAGES / group
    label_dir = SOURCE_LABELS / group

    images = {}
    for image_path in image_dir.iterdir():
        if image_path.suffix.lower() in IMAGE_EXTENSIONS:
            images[image_path.stem] = image_path

    labels = {}
    for label_path in label_dir.glob("*.txt"):
        labels[label_path.stem] = label_path

    missing_labels = sorted(set(images) - set(labels))
    extra_labels = sorted(set(labels) - set(images))

    if missing_labels:
        raise RuntimeError(f"{group} 缺少标签：{missing_labels}")

    if extra_labels:
        raise RuntimeError(f"{group} 存在多余标签：{extra_labels}")

    pairs = []
    for name in sorted(images):
        pairs.append((images[name], labels[name]))

    return pairs


def copy_pair(image_path, label_path, split_name, group):
    """把一张图片及其标签复制到训练集或验证集。"""

    output_image_dir = DATASET_DIR / "images" / split_name / group
    output_label_dir = DATASET_DIR / "labels" / split_name / group

    output_image_dir.mkdir(parents=True, exist_ok=True)
    output_label_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(image_path, output_image_dir / image_path.name)
    shutil.copy2(label_path, output_label_dir / label_path.name)


def prepare_dataset():
    """按每个原始类别分别划分80%训练集和20%验证集。"""

    if DATASET_DIR.exists():
        shutil.rmtree(DATASET_DIR)

    total_train = 0
    total_val = 0
    total_source = 0

    print("\n数据集划分：")

    for group_index, group in enumerate(GROUPS):
        pairs = find_image_label_pairs(group)

        randomizer = random.Random(RANDOM_SEED + group_index)
        randomizer.shuffle(pairs)

        val_count = max(1, round(len(pairs) * VAL_RATIO))
        val_pairs = pairs[:val_count]
        train_pairs = pairs[val_count:]

        for image_path, label_path in train_pairs:
            copy_pair(image_path, label_path, "train", group)

        for image_path, label_path in val_pairs:
            copy_pair(image_path, label_path, "val", group)

        total_train += len(train_pairs)
        total_val += len(val_pairs)
        total_source += len(pairs)

        print(
            f"{group:<18} "
            f"train={len(train_pairs):>2} "
            f"val={len(val_pairs):>2}"
        )

    if total_train + total_val != total_source:
        raise RuntimeError(
            "训练集和验证集数量与源数据不一致"
        )

    print(f"可用于训练的已标注图片：{total_source} 张")
    print(f"训练集：{total_train} 张")
    print(f"验证集：{total_val} 张")


def create_data_yaml():
    """生成 Ultralytics 使用的数据集配置。"""

    lines = [
        f"path: {DATASET_DIR.as_posix()}",
        "train: images/train",
        "val: images/val",
        "",
        "names:",
    ]

    for class_id, class_name in enumerate(CLASS_NAMES):
        lines.append(f"  {class_id}: {class_name}")

    DATA_YAML.parent.mkdir(parents=True, exist_ok=True)
    DATA_YAML.write_text("\n".join(lines) + "\n", encoding="utf-8")


def train():
    """使用第一块 NVIDIA 显卡训练 YOLOv8n。"""

    if not torch.cuda.is_available():
        raise RuntimeError("没有检测到可用的 NVIDIA CUDA 显卡")

    print(f"\nGPU：{torch.cuda.get_device_name(0)}")

    # 本地没有基础权重时，Ultralytics 会自动下载 yolov8n.pt。
    model = YOLO("yolov8n.pt")

    model.train(
        data=str(DATA_YAML),
        epochs=EPOCHS,
        patience=PATIENCE,
        imgsz=IMAGE_SIZE,
        batch=BATCH_SIZE,
        device=0,
        workers=0,
        seed=RANDOM_SEED,
        plots=True,
        project=str(ROOT / "runs" / "train"),
        name=RUN_NAME,
        exist_ok=True,
    )

    best_model = TRAIN_DIR / "weights" / "best.pt"

    if not best_model.exists():
        raise FileNotFoundError(f"训练完成后没有找到：{best_model}")

    return best_model


def validate(best_model):
    """在验证集上计算 Precision、Recall 和 mAP。"""

    model = YOLO(str(best_model))
    metrics = model.val(
        data=str(DATA_YAML),
        imgsz=IMAGE_SIZE,
        batch=BATCH_SIZE,
        device=0,
        workers=0,
    )

    print("\n验证指标：")
    print(f"Precision: {metrics.box.mp:.4f}")
    print(f"Recall:    {metrics.box.mr:.4f}")
    print(f"mAP50:     {metrics.box.map50:.4f}")
    print(f"mAP50-95:  {metrics.box.map:.4f}")


def export_onnx(best_model):
    """导出固定输入640x640、FP32、无NMS的 ONNX 模型。"""

    model = YOLO(str(best_model))
    exported_file = model.export(
        format="onnx",
        imgsz=IMAGE_SIZE,
        batch=1,
        dynamic=False,
        simplify=False,
        half=False,
        opset=12,
        nms=False,
    )

    exported_file = Path(exported_file)
    shutil.copy2(exported_file, FINAL_ONNX)

    print(f"\nONNX Runtime 模型：{FINAL_ONNX}")


def main():
    print("YOLOv8n 五类目标检测训练")

    prepare_dataset()
    create_data_yaml()

    best_model = train()
    validate(best_model)
    export_onnx(best_model)

    print("\n全部完成")
    print(f"PyTorch 模型：{best_model}")
    print(f"ONNX 模型：{FINAL_ONNX}")


if __name__ == "__main__":
    main()
