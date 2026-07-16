from manual_label_tools.annotator import run_annotator
from manual_label_tools.file_utils import get_all_images


def main():
    """读取全部灰色底板裁切图，并从第一张开始逐张标注。"""
    images = get_all_images()
    if not images:
        print("images_rectified 中没有可标注图片")
        return

    print(f"图片数量：{len(images)}")
    print(f"从第一张开始：{images[0]}")
    run_annotator(images, images[0])


if __name__ == "__main__":
    main()
