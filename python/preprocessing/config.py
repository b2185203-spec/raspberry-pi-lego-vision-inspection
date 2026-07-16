"""数据预处理的集中配置。修改参数只需要改这里。"""

from pathlib import Path

# 仓库根目录下的 dataset 就是数据集目录。
PROJECT_ROOT = Path(__file__).resolve().parents[2] / "dataset"

FOLDERS = [
    "all_correct",
    "missing_beige",
    "missing_blue",
    "missing_brown",
    "missing_red",
]

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}

OUTPUT_WIDTH = 800
OUTPUT_HEIGHT = 800

CLEAN_OLD_RESULTS = True
SAVE_PREVIEWS = True

# 灰色底板参数：沿用当前项目中已调过的 HSV 和形态学参数。
BOARD_HSV_LOWER = (0, 0, 35)
BOARD_HSV_UPPER = (179, 255, 170)
BOARD_OPEN_KERNEL = 31
BOARD_CLOSE_KERNEL = 49
BOARD_MIN_AREA_RATIO = 0.15
BOARD_MAX_AREA_RATIO = 0.90
BOARD_POLYGON_EPSILON_RATIO = 0.02
BOARD_MIN_RECTANGULARITY = 0.70
BOARD_MIN_ASPECT_RATIO = 0.70
BOARD_MAX_ASPECT_RATIO = 1.30
ALLOW_MIN_AREA_RECT_FALLBACK = True
