"""第一步：只检测灰色底板、透视裁切为 800×800，不做 L 对齐或标记。"""

import argparse
import csv
import shutil
import sys
from pathlib import Path

# 允许直接运行这个文件，并导入同目录里的裁切代码。
sys.path.insert(0, str(Path(__file__).resolve().parent))

from board_crop import process_board_image
from file_utils import get_image_files, read_image, save_image


FOLDERS = [
    "all_correct",
    "missing_beige",
    "missing_blue",
    "missing_brown",
    "missing_red",
]


def clean_output(root):
    """只删除本步骤生成的 images_rectified，绝不删除原始 images。"""
    output_root = root / "images_rectified"
    if output_root.exists():
        shutil.rmtree(output_root)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parents[2] / "dataset"),
        help="包含 images 的数据集目录",
    )
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    if not (root / "images").is_dir():
        raise SystemExit(f"没有找到原图目录：{root / 'images'}")

    if args.clean:
        clean_output(root)

    output_root = root / "images_rectified"
    rows = []
    success_count = 0

    for folder in FOLDERS:
        source_files = get_image_files(root / "images" / folder)
        for source_path in source_files:
            image = read_image(source_path)
            if image is None:
                rows.append([folder, source_path.name, "failed", "图片读取失败", ""])
                print(f"失败：{folder}/{source_path.name}，图片读取失败")
                continue

            success, cropped, info = process_board_image(image)
            if not success:
                rows.append([folder, source_path.name, "failed", info["message"], ""])
                print(f"失败：{folder}/{source_path.name}，{info['message']}")
                continue

            output_path = output_root / folder / f"{source_path.stem}.png"
            if not save_image(output_path, cropped):
                rows.append([folder, source_path.name, "failed", "裁切图保存失败", ""])
                print(f"失败：{folder}/{source_path.name}，裁切图保存失败")
                continue

            rows.append([folder, source_path.name, "success", "", str(output_path)])
            success_count += 1

    report_path = root / "reports" / "board_crop_report.csv"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.open("w", newline="", encoding="utf-8-sig") as file:
        writer = csv.writer(file)
        writer.writerow(["folder", "filename", "status", "reason", "output_path"])
        writer.writerows(rows)

    print(f"原图数量：{len(rows)}")
    print(f"裁切成功：{success_count}")
    print(f"裁切失败：{len(rows) - success_count}")
    print(f"报告：{report_path}")

    if success_count != len(rows):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
