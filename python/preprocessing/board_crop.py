"""阶段一：检测灰色底板并透视校正为 800×800。"""

import cv2
import numpy as np

from config import (
    ALLOW_MIN_AREA_RECT_FALLBACK, BOARD_CLOSE_KERNEL, BOARD_HSV_LOWER,
    BOARD_HSV_UPPER, BOARD_MAX_AREA_RATIO, BOARD_MAX_ASPECT_RATIO,
    BOARD_MIN_AREA_RATIO, BOARD_MIN_ASPECT_RATIO, BOARD_MIN_RECTANGULARITY,
    BOARD_OPEN_KERNEL, BOARD_POLYGON_EPSILON_RATIO, OUTPUT_HEIGHT, OUTPUT_WIDTH,
)


def make_odd(value):
    return max(1, int(value) // 2 * 2 + 1)


def make_board_mask(image):
    """输入原图，输出灰色底板候选掩膜。"""
    hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, np.array(BOARD_HSV_LOWER), np.array(BOARD_HSV_UPPER))
    open_kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (make_odd(BOARD_OPEN_KERNEL),) * 2)
    close_kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (make_odd(BOARD_CLOSE_KERNEL),) * 2)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, open_kernel)
    return cv2.morphologyEx(mask, cv2.MORPH_CLOSE, close_kernel)


def order_corners(points):
    """把四个点排序为左上、右上、右下、左下。"""
    points = np.asarray(points, dtype=np.float32).reshape(4, 2)
    point_sum = points.sum(axis=1)
    point_diff = np.diff(points, axis=1).reshape(-1)
    return np.array([
        points[np.argmin(point_sum)], points[np.argmin(point_diff)],
        points[np.argmax(point_sum)], points[np.argmax(point_diff)],
    ], dtype=np.float32)


def score_contour(contour, corners, image):
    """综合面积、方形程度、矩形度和中心位置评分。"""
    height, width = image.shape[:2]
    area = cv2.contourArea(contour)
    area_ratio = area / (height * width)
    if not BOARD_MIN_AREA_RATIO <= area_ratio <= BOARD_MAX_AREA_RATIO:
        return None
    rectangle = cv2.minAreaRect(contour)
    rect_width, rect_height = rectangle[1]
    if min(rect_width, rect_height) < 2:
        return None
    aspect_ratio = max(rect_width, rect_height) / min(rect_width, rect_height)
    if not BOARD_MIN_ASPECT_RATIO <= 1 / aspect_ratio <= BOARD_MAX_ASPECT_RATIO:
        return None
    rectangularity = area / max(rect_width * rect_height, 1)
    if rectangularity < BOARD_MIN_RECTANGULARITY:
        return None
    center = corners.mean(axis=0)
    image_center = np.array([width / 2, height / 2])
    center_score = max(0, 1 - np.linalg.norm(center - image_center) / np.hypot(width, height))
    square_score = max(0, 1 - abs(aspect_ratio - 1))
    return area_ratio * 0.35 + rectangularity * 0.35 + square_score * 0.20 + center_score * 0.10


def find_board_contour(mask, image):
    """从候选掩膜中选择可信的底板轮廓，不直接选最大轮廓。"""
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    best = None
    for contour in contours:
        perimeter = cv2.arcLength(contour, True)
        polygon = cv2.approxPolyDP(contour, BOARD_POLYGON_EPSILON_RATIO * perimeter, True)
        if len(polygon) == 4 and cv2.isContourConvex(polygon):
            corners = order_corners(polygon.reshape(4, 2))
            score = score_contour(contour, corners, image)
            if score is not None and (best is None or score > best["score"]):
                best = {"corners": corners, "score": score, "method": "polygon", "contour": contour}
    if best is None and ALLOW_MIN_AREA_RECT_FALLBACK:
        for contour in contours:
            corners = order_corners(cv2.boxPoints(cv2.minAreaRect(contour)))
            score = score_contour(contour, corners, image)
            if score is not None and (best is None or score > best["score"]):
                best = {"corners": corners, "score": score, "method": "min_area_rect", "contour": contour}
    return best


def detect_board_corners(image):
    """返回 success, corners, message。失败时不生成伪造角点。"""
    mask = make_board_mask(image)
    result = find_board_contour(mask, image)
    if result is None:
        return False, None, "没有找到可靠底板四角"
    return True, result["corners"], result


def rectify_board(image, corners):
    """把四角透视变换为固定 800×800 底板图。"""
    target = np.array([[0, 0], [OUTPUT_WIDTH - 1, 0], [OUTPUT_WIDTH - 1, OUTPUT_HEIGHT - 1], [0, OUTPUT_HEIGHT - 1]], dtype=np.float32)
    matrix = cv2.getPerspectiveTransform(corners, target)
    return cv2.warpPerspective(image, matrix, (OUTPUT_WIDTH, OUTPUT_HEIGHT), borderMode=cv2.BORDER_REPLICATE)


def process_board_image(image):
    """阶段一入口：返回 success、裁切图和普通信息字典。"""
    success, corners, result = detect_board_corners(image)
    if not success:
        return False, None, {"status": "failed", "message": result, "corner_method": "", "board_area_ratio": ""}
    rectified = rectify_board(image, corners)
    area_ratio = cv2.contourArea(result["contour"]) / (image.shape[0] * image.shape[1])
    return True, rectified, {"status": "success", "message": "", "corner_method": result["method"], "board_area_ratio": area_ratio}
