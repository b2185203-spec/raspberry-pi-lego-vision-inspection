#pragma once

#include <opencv2/opencv.hpp>

#include <array>
#include <cmath>
#include <string>
#include <vector>

/*
 * 图像预处理只做两件事：
 * 1. 找到灰色底板。
 * 2. 把底板透视校正为 800 x 800。
 *
 * 保持摄像头画面中的方向，不额外旋转透视结果。
 */
class ImagePreprocessor
{
public:
    bool process(
        const cv::Mat& original,
        cv::Mat& rectified,
        std::string& message
    ) const
    {
        std::array<cv::Point2f, 4> corners;

        if (!findBoardCorners(original, corners))
        {
            message = "图像处理失败：没有找到灰色底板";
            return false;
        }

        rectified = rectifyBoard(original, corners);
        message =
            "图像处理完成\n"
            "灰色底板：已裁切并透视校正为 800 x 800\n"
            "方向处理：保持透视后的画面方向";

        return true;
    }

private:
    static std::array<cv::Point2f, 4> orderCorners(
        const std::vector<cv::Point2f>& points
    )
    {
        std::array<cv::Point2f, 4> ordered;

        float smallest_sum = 1e9F;
        float largest_sum = -1e9F;
        float smallest_difference = 1e9F;
        float largest_difference = -1e9F;

        for (const auto& point : points)
        {
            float sum = point.x + point.y;
            float difference = point.x - point.y;

            if (sum < smallest_sum)
            {
                smallest_sum = sum;
                ordered[0] = point;
            }

            if (difference > largest_difference)
            {
                largest_difference = difference;
                ordered[1] = point;
            }

            if (sum > largest_sum)
            {
                largest_sum = sum;
                ordered[2] = point;
            }

            if (difference < smallest_difference)
            {
                smallest_difference = difference;
                ordered[3] = point;
            }
        }

        return ordered;
    }

    static cv::Mat makeBoardMask(const cv::Mat& image)
    {
        cv::Mat hsv;
        cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

        cv::Mat mask;
        cv::inRange(
            hsv,
            cv::Scalar(0, 0, 35),
            cv::Scalar(179, 255, 170),
            mask
        );

        cv::Mat open_kernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE,
            cv::Size(31, 31)
        );

        cv::Mat close_kernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE,
            cv::Size(49, 49)
        );

        cv::morphologyEx(mask, mask, cv::MORPH_OPEN, open_kernel);
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, close_kernel);

        return mask;
    }

    static bool scoreBoardContour(
        const std::vector<cv::Point>& contour,
        const std::array<cv::Point2f, 4>& corners,
        const cv::Size& image_size,
        double& score
    )
    {
        double area = cv::contourArea(contour);
        double image_area = image_size.width * image_size.height;
        double area_ratio = area / image_area;

        if (area_ratio < 0.15 || area_ratio > 0.90)
        {
            return false;
        }

        cv::RotatedRect rectangle = cv::minAreaRect(contour);
        double width = rectangle.size.width;
        double height = rectangle.size.height;

        if (std::min(width, height) < 2.0)
        {
            return false;
        }

        double aspect_ratio =
            std::max(width, height) / std::min(width, height);

        if (aspect_ratio > 1.43)
        {
            return false;
        }

        double rectangularity = area / std::max(width * height, 1.0);

        if (rectangularity < 0.70)
        {
            return false;
        }

        cv::Point2f center;

        for (const auto& point : corners)
        {
            center += point;
        }

        center *= 0.25F;

        cv::Point2f image_center(
            image_size.width / 2.0F,
            image_size.height / 2.0F
        );

        double distance = cv::norm(center - image_center);
        double diagonal = std::hypot(
            image_size.width,
            image_size.height
        );

        double center_score = std::max(
            0.0,
            1.0 - distance / diagonal
        );

        double square_score = std::max(
            0.0,
            1.0 - std::abs(aspect_ratio - 1.0)
        );

        score = area_ratio * 0.35
            + rectangularity * 0.35
            + square_score * 0.20
            + center_score * 0.10;

        return true;
    }

    static bool findBoardCorners(
        const cv::Mat& image,
        std::array<cv::Point2f, 4>& best_corners
    )
    {
        cv::Mat mask = makeBoardMask(image);
        std::vector<std::vector<cv::Point>> contours;

        cv::findContours(
            mask,
            contours,
            cv::RETR_EXTERNAL,
            cv::CHAIN_APPROX_SIMPLE
        );

        double best_score = -1.0;

        for (const auto& contour : contours)
        {
            double perimeter = cv::arcLength(contour, true);
            std::vector<cv::Point> polygon;

            cv::approxPolyDP(
                contour,
                polygon,
                0.02 * perimeter,
                true
            );

            if (polygon.size() != 4 || !cv::isContourConvex(polygon))
            {
                continue;
            }

            std::vector<cv::Point2f> points(
                polygon.begin(),
                polygon.end()
            );

            auto corners = orderCorners(points);
            double score = 0.0;

            if (
                scoreBoardContour(
                    contour,
                    corners,
                    image.size(),
                    score
                )
                && score > best_score
            )
            {
                best_score = score;
                best_corners = corners;
            }
        }

        // 四边形检测失败时，使用轮廓的最小外接矩形。
        if (best_score < 0.0)
        {
            for (const auto& contour : contours)
            {
                cv::Point2f box[4];
                cv::minAreaRect(contour).points(box);

                std::vector<cv::Point2f> points(box, box + 4);
                auto corners = orderCorners(points);
                double score = 0.0;

                if (
                    scoreBoardContour(
                        contour,
                        corners,
                        image.size(),
                        score
                    )
                    && score > best_score
                )
                {
                    best_score = score;
                    best_corners = corners;
                }
            }
        }

        return best_score >= 0.0;
    }

    static cv::Mat rectifyBoard(
        const cv::Mat& image,
        const std::array<cv::Point2f, 4>& corners
    )
    {
        std::vector<cv::Point2f> source(
            corners.begin(),
            corners.end()
        );

        std::vector<cv::Point2f> target{
            {0.0F, 0.0F},
            {799.0F, 0.0F},
            {799.0F, 799.0F},
            {0.0F, 799.0F}
        };

        cv::Mat matrix = cv::getPerspectiveTransform(source, target);
        cv::Mat rectified;

        cv::warpPerspective(
            image,
            rectified,
            matrix,
            cv::Size(800, 800),
            cv::INTER_LINEAR,
            cv::BORDER_REPLICATE
        );

        return rectified;
    }
};
