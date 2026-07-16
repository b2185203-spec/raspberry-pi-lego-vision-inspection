from pathlib import Path

# 自动找到仓库中的 dataset，移动仓库后不需要改绝对路径。
PROJECT_ROOT = Path(__file__).resolve().parents[2] / "dataset"
IMAGE_ROOT = PROJECT_ROOT / "images_rectified"
LABEL_ROOT = PROJECT_ROOT / "labels_rectified"
PREVIEW_ROOT = PROJECT_ROOT / "previews" / "manual_label"
REVIEW_STATUS_PATH = PROJECT_ROOT / "labels_rectified_review_status.json"
SOURCE_STATUS_PATH = PROJECT_ROOT / "labels_rectified_source_status.json"
FOLDERS = ["all_correct", "missing_beige", "missing_blue", "missing_brown", "missing_red"]
CLASS_NAMES = ["black_reference", "beige", "blue", "brown", "red"]
IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}
WINDOW_WIDTH = 1200
WINDOW_HEIGHT = 900
AUTO_SAVE = True
