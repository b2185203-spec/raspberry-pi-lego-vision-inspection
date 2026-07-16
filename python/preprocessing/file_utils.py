"""简单的图片和输出目录操作。"""

import shutil
from pathlib import Path

import cv2
import numpy as np

from config import FOLDERS, IMAGE_EXTENSIONS, PROJECT_ROOT


def get_image_files(folder):
    """只读取指定文件夹的第一层图片，不递归读取任何子目录。"""
    folder = Path(folder)
    files = []
    for path in folder.iterdir():
        if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS:
            files.append(path)
    return sorted(files, key=lambda path: path.name)


def read_image(path):
    """读取图片；使用 imdecode 支持 Windows 中文路径，失败返回 None。"""
    try:
        data = np.fromfile(str(path), dtype=np.uint8)
        if data.size == 0:
            return None
        return cv2.imdecode(data, cv2.IMREAD_COLOR)
    except OSError:
        return None


def save_image(path, image):
    """保存图片到指定路径，自动创建父目录，成功返回 True。"""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    success, data = cv2.imencode(path.suffix or ".png", image)
    if not success:
        return False
    try:
        data.tofile(str(path))
        return True
    except OSError:
        return False


def create_output_folders():
    """创建两个处理阶段、预览和报告的输出目录。"""
    for folder_name in FOLDERS:
        (PROJECT_ROOT / "images_rectified" / folder_name).mkdir(parents=True, exist_ok=True)
        (PROJECT_ROOT / "images_aligned" / folder_name).mkdir(parents=True, exist_ok=True)
        (PROJECT_ROOT / "pipeline_previews" / folder_name).mkdir(parents=True, exist_ok=True)
    (PROJECT_ROOT / "reports").mkdir(parents=True, exist_ok=True)


def clean_old_results():
    """只清理允许的自动结果目录，永远不触碰 images。"""
    allowed_paths = [
        PROJECT_ROOT / "images_rectified",
        PROJECT_ROOT / "images_aligned",
        PROJECT_ROOT / "pipeline_previews",
        PROJECT_ROOT / "reports",
    ]
    for path in allowed_paths:
        resolved = path.resolve()
        if PROJECT_ROOT.resolve() not in resolved.parents:
            raise RuntimeError(f"拒绝清理项目外路径：{resolved}")
        if path.exists():
            shutil.rmtree(path)


def count_images_by_folder(base_folder):
    """返回五个状态文件夹各自的图片数量。"""
    counts = {}
    for folder_name in FOLDERS:
        counts[folder_name] = len(get_image_files(Path(base_folder) / folder_name))
    return counts
