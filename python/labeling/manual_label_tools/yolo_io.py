from pathlib import Path


def pixel_box_to_yolo(class_id, box, image_width, image_height):
    """把像素框转换成 YOLO 所需的归一化格式。"""
    x1, y1, x2, y2 = box
    center_x = (x1 + x2) / 2 / image_width
    center_y = (y1 + y2) / 2 / image_height
    box_width = (x2 - x1) / image_width
    box_height = (y2 - y1) / image_height
    return class_id, center_x, center_y, box_width, box_height


def yolo_box_to_pixel(values, image_width, image_height):
    """把 YOLO 归一化框转换成界面使用的像素框。"""
    class_id, center_x, center_y, box_width, box_height = values
    x1 = (center_x - box_width / 2) * image_width
    y1 = (center_y - box_height / 2) * image_height
    x2 = (center_x + box_width / 2) * image_width
    y2 = (center_y + box_height / 2) * image_height
    return [int(class_id), x1, y1, x2, y2]


def load_yolo_labels(path, image_width, image_height):
    """读取一个标签文件；不存在时返回空框列表。"""
    path = Path(path)
    boxes = []
    if not path.exists():
        return boxes

    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) != 5:
            continue
        try:
            values = [float(value) for value in parts]
        except ValueError:
            continue
        boxes.append(yolo_box_to_pixel(values, image_width, image_height))
    return boxes


def save_yolo_labels(path, boxes, image_width, image_height):
    """保存一个标签文件，小于3像素的无效框不会写入。"""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = []

    for class_id, x1, y1, x2, y2 in boxes:
        if x2 - x1 < 3 or y2 - y1 < 3:
            continue

        values = pixel_box_to_yolo(
            class_id,
            [x1, y1, x2, y2],
            image_width,
            image_height,
        )
        line = (
            f"{values[0]} {values[1]:.6f} {values[2]:.6f} "
            f"{values[3]:.6f} {values[4]:.6f}"
        )
        lines.append(line)

    text = "\n".join(lines)
    if lines:
        text += "\n"
    path.write_text(text, encoding="utf-8")
