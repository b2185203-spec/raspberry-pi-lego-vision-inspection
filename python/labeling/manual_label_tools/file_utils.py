import json
from pathlib import Path

import cv2
import numpy as np

from manual_label_config import FOLDERS, IMAGE_EXTENSIONS, IMAGE_ROOT, LABEL_ROOT


def get_all_images():
    """只读取 images_rectified 下五个目录中的灰色底板裁切图片。"""
    images = []
    for folder_name in FOLDERS:
        folder_path = IMAGE_ROOT / folder_name
        if not folder_path.exists():
            print(f"图片目录不存在：{folder_path}")
            continue

        folder_images = []
        for path in folder_path.iterdir():
            if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS:
                folder_images.append(path)
        images.extend(sorted(folder_images, key=lambda path: path.name))
    return images


def find_reference_image(images, name):
    """按文件名查找参考图，允许 jpg、png 等扩展名不同。"""
    reference_stem = Path(name).stem
    for path in images:
        if path.parent.name == "all_correct" and path.stem == reference_stem:
            return path
    return None


def read_image(path):
    """使用兼容 Windows 路径的方式读取图片。"""
    data = np.fromfile(str(path), dtype=np.uint8)
    if data.size == 0:
        return None
    return cv2.imdecode(data, cv2.IMREAD_COLOR)


def label_path(image_path):
    """让标签目录结构与图片目录结构保持一致。"""
    return LABEL_ROOT / image_path.parent.name / f"{image_path.stem}.txt"


def load_json(path):
    path = Path(path)
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def save_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(data, ensure_ascii=False, indent=2)
    path.write_text(text, encoding="utf-8")
