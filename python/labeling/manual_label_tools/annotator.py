import cv2

from manual_label_config import (
    AUTO_SAVE,
    CLASS_NAMES,
    PREVIEW_ROOT,
    REVIEW_STATUS_PATH,
    SOURCE_STATUS_PATH,
    WINDOW_HEIGHT,
    WINDOW_WIDTH,
)
from .file_utils import label_path, load_json, read_image, save_json
from .yolo_io import load_yolo_labels, save_yolo_labels


STATE = {}

# 每个 missing 文件夹里禁止出现的类别编号。
MISSING = {
    "missing_beige": 1,
    "missing_blue": 2,
    "missing_brown": 3,
    "missing_red": 4,
}

COLORS = [
    (30, 30, 30),    # black_reference
    (180, 210, 230), # beige
    (255, 100, 20),  # blue
    (70, 120, 180),  # brown
    (20, 20, 240),   # red
]


def image_key(path):
    return f"{path.parent.name}/{path.name}"


def mark_changed():
    """记录当前图片已被人工修改。"""
    STATE["dirty"] = True
    STATE["saved"] = False


def save_current():
    """保存当前图片的框；只有人工改过时才更新来源状态。"""
    image = STATE["image"]
    path = STATE["images"][STATE["index"]]
    height, width = image.shape[:2]
    forbidden_class = MISSING.get(path.parent.name)
    valid_boxes = []

    for box in STATE["boxes"]:
        if box[0] == forbidden_class:
            print(f"错误：{path.parent.name} 中不能标注类别 {box[0]}")
            continue
        valid_boxes.append(box)

    STATE["boxes"] = valid_boxes
    save_yolo_labels(label_path(path), valid_boxes, width, height)

    if STATE.get("dirty", False):
        STATE["source"][image_key(path)] = "manual"
        save_json(SOURCE_STATUS_PATH, STATE["source"])

    STATE["dirty"] = False
    STATE["saved"] = True


def load_current():
    """读取当前图片及其已有标签。"""
    path = STATE["images"][STATE["index"]]
    image = read_image(path)
    if image is None:
        raise RuntimeError(f"无法读取图片：{path}")

    height, width = image.shape[:2]
    STATE["image"] = image
    STATE["boxes"] = load_yolo_labels(label_path(path), width, height)
    STATE["selected"] = -1
    STATE["undo"] = []
    STATE["drag"] = None
    STATE["dirty"] = False
    STATE["saved"] = label_path(path).exists()


def hit_box(x, y):
    for index in range(len(STATE["boxes"]) - 1, -1, -1):
        _, x1, y1, x2, y2 = STATE["boxes"][index]
        if x1 <= x <= x2 and y1 <= y <= y2:
            return index
    return -1


def hit_corner(x, y):
    """检查鼠标是否靠近某个框的四个角。"""
    radius = 10
    for index, box in enumerate(STATE["boxes"]):
        _, x1, y1, x2, y2 = box
        corners = [(x1, y1), (x2, y1), (x1, y2), (x2, y2)]
        for corner_id, (corner_x, corner_y) in enumerate(corners):
            if abs(x - corner_x) <= radius and abs(y - corner_y) <= radius:
                return index, corner_id
    return -1, -1


def mouse(event, x, y, flags, param):
    """左键拖框；拖框内部可移动，拖四角可缩放。"""
    scale = STATE["scale"]
    x = x / scale
    y = y / scale

    if event == cv2.EVENT_LBUTTONDOWN:
        corner_index, corner_id = hit_corner(x, y)
        selected_index = corner_index
        if selected_index < 0:
            selected_index = hit_box(x, y)

        STATE["selected"] = selected_index
        STATE["undo"].append([box[:] for box in STATE["boxes"]])

        if corner_index >= 0:
            old_box = STATE["boxes"][corner_index][:]
            STATE["drag"] = ("resize", corner_index, corner_id, old_box)
        elif selected_index >= 0:
            old_box = STATE["boxes"][selected_index][:]
            STATE["drag"] = ("move", selected_index, x, y, old_box)
        else:
            STATE["drag"] = ("new", x, y, x, y)

    elif event == cv2.EVENT_MOUSEMOVE and STATE.get("drag"):
        drag = STATE["drag"]

        if drag[0] == "new":
            STATE["drag"] = ("new", drag[1], drag[2], x, y)

        elif drag[0] == "move":
            _, index, start_x, start_y, old_box = drag
            class_id, x1, y1, x2, y2 = old_box
            height, width = STATE["image"].shape[:2]
            box_width = x2 - x1
            box_height = y2 - y1
            new_x1 = max(0, min(width - box_width, x1 + x - start_x))
            new_y1 = max(0, min(height - box_height, y1 + y - start_y))
            STATE["boxes"][index] = [
                class_id,
                new_x1,
                new_y1,
                new_x1 + box_width,
                new_y1 + box_height,
            ]

        elif drag[0] == "resize":
            _, index, corner_id, old_box = drag
            class_id, x1, y1, x2, y2 = old_box
            height, width = STATE["image"].shape[:2]
            x = max(0, min(width, x))
            y = max(0, min(height, y))

            if corner_id == 0:
                x1, y1 = x, y
            elif corner_id == 1:
                x2, y1 = x, y
            elif corner_id == 2:
                x1, y2 = x, y
            else:
                x2, y2 = x, y

            x1, x2 = sorted((x1, x2))
            y1, y2 = sorted((y1, y2))
            STATE["boxes"][index] = [class_id, x1, y1, x2, y2]

    elif event == cv2.EVENT_LBUTTONUP and STATE.get("drag"):
        drag = STATE.pop("drag")
        changed = drag[0] in ("move", "resize")

        if drag[0] == "new":
            _, x1, y1, x2, y2 = drag
            x1, x2 = sorted((x1, x2))
            y1, y2 = sorted((y1, y2))
            if x2 - x1 >= 3 and y2 - y1 >= 3:
                new_box = [STATE["class_id"], x1, y1, x2, y2]
                STATE["boxes"].append(new_box)
                STATE["selected"] = len(STATE["boxes"]) - 1
                changed = True

        if changed:
            mark_changed()


def draw_screen():
    image = STATE["image"].copy()
    path = STATE["images"][STATE["index"]]
    key = image_key(path)
    confirmed = STATE["review"].get(key, False)
    source = STATE["source"].get(key, "not_labeled")

    for index, box in enumerate(STATE["boxes"]):
        class_id, x1, y1, x2, y2 = box
        color = COLORS[class_id]
        thickness = 3 if index == STATE["selected"] else 2
        center_x = int((x1 + x2) / 2)
        center_y = int((y1 + y2) / 2)
        cv2.rectangle(image, (int(x1), int(y1)), (int(x2), int(y2)), color, thickness)
        cv2.circle(image, (center_x, center_y), 4, color, -1)
        text = f"{class_id} {CLASS_NAMES[class_id]} ({center_x},{center_y})"
        cv2.putText(image, text, (int(x1), max(18, int(y1) - 5)), 0, 0.45, color, 1)

    header = (
        f"{path.parent.name}/{path.name}  "
        f"{STATE['index'] + 1}/{len(STATE['images'])}  "
        f"class={STATE['class_id']} {CLASS_NAMES[STATE['class_id']]}  "
        f"boxes={len(STATE['boxes'])}  source={source}  confirmed={confirmed}"
    )
    help_text = (
        "1-4 class | A/D image | S save | X clear page | Enter confirm | "
        "V check | E previews | Q quit"
    )
    cv2.putText(image, header, (8, 22), 0, 0.48, (0, 0, 0), 2)
    cv2.putText(image, help_text, (8, 45), 0, 0.43, (0, 0, 0), 1)

    scale = min(WINDOW_WIDTH / image.shape[1], WINDOW_HEIGHT / image.shape[0])
    STATE["scale"] = scale
    output_width = int(image.shape[1] * scale)
    output_height = int(image.shape[0] * scale)
    return cv2.resize(image, (output_width, output_height))


def validate_label_file(path):
    """严格检查一个 YOLO 标签文件，并返回类别列表和错误。"""
    classes = []
    errors = []

    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except Exception as error:
        return classes, [f"无法读取：{error}"]

    if not lines:
        errors.append("标签文件为空")

    for line_number, line in enumerate(lines, start=1):
        parts = line.split()
        if len(parts) != 5:
            errors.append(f"第 {line_number} 行不是5列")
            continue

        try:
            class_id = int(parts[0])
            values = [float(value) for value in parts[1:]]
        except ValueError:
            errors.append(f"第 {line_number} 行包含非法数字")
            continue

        if class_id < 0 or class_id > 4:
            errors.append(f"第 {line_number} 行类别超出0到4")
        if any(value < 0 or value > 1 for value in values):
            errors.append(f"第 {line_number} 行坐标超出0到1")
        if values[2] <= 0 or values[3] <= 0:
            errors.append(f"第 {line_number} 行宽高必须大于0")
        classes.append(class_id)

    return classes, errors


def check_all_labels(images, review):
    """检查所有标签是否存在、格式正确、类别完整且已人工确认。"""
    errors = []
    warnings = []
    class_counts = [0, 0, 0, 0, 0]
    label_count = 0

    for path in images:
        key = image_key(path)
        current_label_path = label_path(path)
        expected_classes = set(range(5))
        forbidden_class = MISSING.get(path.parent.name)
        if forbidden_class is not None:
            expected_classes.remove(forbidden_class)

        if not current_label_path.exists():
            errors.append(f"缺少标签：{key}")
            continue

        label_count += 1
        classes, file_errors = validate_label_file(current_label_path)
        for error in file_errors:
            errors.append(f"{key}：{error}")

        if set(classes) != expected_classes or len(classes) != len(set(classes)):
            errors.append(f"类别错误：{key}，实际类别={classes}")

        for class_id in classes:
            if 0 <= class_id <= 4:
                class_counts[class_id] += 1

        if not review.get(key, False):
            warnings.append(f"未人工确认：{key}")

    confirmed_count = sum(review.get(image_key(path), False) for path in images)
    print(f"图片数量：{len(images)}")
    print(f"标签数量：{label_count}")
    print(f"人工确认：{confirmed_count}")
    print(f"四类框数量：{class_counts}")
    print(f"错误数量：{len(errors)}")
    for error in errors:
        print(f"  {error}")
    print(f"警告数量：{len(warnings)}")
    for warning in warnings:
        print(f"  {warning}")


def write_preview(path, image):
    """兼容 Windows 路径地保存预览图。"""
    output_path = PREVIEW_ROOT / path.parent.name / f"{path.stem}.png"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    ok, encoded = cv2.imencode(".png", image)
    if ok:
        encoded.tofile(str(output_path))
    else:
        print(f"预览保存失败：{output_path}")


def export_previews(images, review, source):
    """导出全部带框预览，不修改当前浏览位置。"""
    old_state = STATE.copy()

    for path in images:
        image = read_image(path)
        if image is None:
            print(f"预览跳过，无法读取：{path}")
            continue
        boxes = load_yolo_labels(label_path(path), image.shape[1], image.shape[0])
        STATE.update(
            image=image,
            boxes=boxes,
            selected=-1,
            images=[path],
            index=0,
            class_id=0,
            review=review,
            source=source,
            scale=1,
        )
        write_preview(path, draw_screen())

    STATE.clear()
    STATE.update(old_state)
    print("预览导出完成")


def run_annotator(images, reference_path):
    """启动 OpenCV 人工标注窗口。"""
    STATE.update(
        images=images,
        index=images.index(reference_path),
        class_id=0,
        review=load_json(REVIEW_STATUS_PATH),
        source=load_json(SOURCE_STATUS_PATH),
        scale=1,
        drag=None,
        dirty=False,
    )
    load_current()

    window_name = "YOLO Manual Label"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
    cv2.setMouseCallback(window_name, mouse)

    while True:
        cv2.imshow(window_name, draw_screen())
        key = cv2.waitKeyEx(30)

        if ord("1") <= key <= ord("5"):
            STATE["class_id"] = key - ord("1")

        elif key in (ord("s"), ord("S")):
            save_current()

        elif key in (ord("a"), ord("A"), ord("d"), ord("D")):
            if AUTO_SAVE and (STATE["dirty"] or not STATE["saved"]):
                save_current()

            current_path = STATE["images"][STATE["index"]]
            if not STATE["review"].get(image_key(current_path), False):
                print("提醒：当前图片尚未人工确认")

            if key in (ord("a"), ord("A")):
                STATE["index"] = max(0, STATE["index"] - 1)
            else:
                STATE["index"] = min(len(images) - 1, STATE["index"] + 1)
            load_current()

        elif key in (3014656, 46, 127):
            if STATE["selected"] >= 0:
                STATE["undo"].append([box[:] for box in STATE["boxes"]])
                STATE["boxes"].pop(STATE["selected"])
                STATE["selected"] = -1
                mark_changed()

        elif key == 26 and STATE["undo"]:
            STATE["boxes"] = STATE["undo"].pop()
            mark_changed()

        elif key in (ord("r"), ord("R")):
            load_current()

        elif key in (ord("x"), ord("X")):
            if STATE["boxes"]:
                STATE["undo"].append([box[:] for box in STATE["boxes"]])
                STATE["boxes"] = []
                STATE["selected"] = -1
                mark_changed()
                print("当前图片的全部标签已清空，请按 S 保存")

        elif key == 13:
            save_current()
            current_path = STATE["images"][STATE["index"]]
            STATE["review"][image_key(current_path)] = True
            save_json(REVIEW_STATUS_PATH, STATE["review"])

        elif key in (ord("v"), ord("V")):
            check_all_labels(images, STATE["review"])

        elif key in (ord("e"), ord("E")):
            export_previews(images, STATE["review"], STATE["source"])

        elif key in (ord("q"), ord("Q"), 27):
            if STATE["dirty"] or not STATE["saved"]:
                save_current()
            break

    cv2.destroyAllWindows()
