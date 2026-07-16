#pragma once

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "yolo_detector.hpp"

struct PositionRule
{
    const char* name;
    int class_id;
    int expected_dx;
    int expected_dy;
};

/*
 * 简单位置专家系统：
 * 1. YOLO 检测 black_reference 和四种彩色元件。
 * 2. 使用 black_reference 检测框中心作为参考点。
 * 3. 根据参考框位于底板哪一侧，把相对坐标换算到统一方向。
 * 4. 使用普通 IF 判断偏差是否不超过 100 像素。
 *
 * 相对坐标会根据黑色参考块所在边换算到统一方向。
 */
class PositionRules
{
public:
    std::string check(
        cv::Mat& preview,
        const std::vector<DetectedObject>& detections
    ) const
    {
        const DetectedObject* reference = findBest(0, detections);

        if (reference == nullptr)
        {
            return
                "位置判断失败：YOLO 没有检测到 black_reference";
        }

        std::string direction = findDirection(reference->center);

        cv::putText(
            preview,
            "REFERENCE",
            cv::Point(
                reference->box.x,
                std::min(
                    790,
                    reference->box.y + reference->box.height + 24
                )
            ),
            cv::FONT_HERSHEY_SIMPLEX,
            0.65,
            cv::Scalar(0, 255, 255),
            2,
            cv::LINE_AA
        );

        std::ostringstream report;
        report
            << "位置参考：black_reference 框中心 ("
            << reference->center.x
            << ", "
            << reference->center.y
            << ")\n"
            << "底板方向："
            << direction
            << "\n";

        bool all_correct = true;

        for (const auto& rule : rules_)
        {
            const DetectedObject* object = findBest(
                rule.class_id,
                detections
            );

            if (object == nullptr)
            {
                report << rule.name << "：缺失\n";
                all_correct = false;
                continue;
            }

            cv::Point difference =
                object->center - reference->center;

            cv::Point normalized = normalizeDirection(
                difference,
                direction
            );

            double error = std::hypot(
                normalized.x - rule.expected_dx,
                normalized.y - rule.expected_dy
            );

            bool correct = error <= position_tolerance_;

            cv::Scalar color = correct
                ? cv::Scalar(0, 220, 0)
                : cv::Scalar(0, 0, 255);

            cv::putText(
                preview,
                correct ? "OK" : "WRONG",
                cv::Point(
                    object->box.x,
                    std::min(
                        790,
                        object->box.y + object->box.height + 24
                    )
                ),
                cv::FONT_HERSHEY_SIMPLEX,
                0.7,
                color,
                2,
                cv::LINE_AA
            );

            report
                << rule.name
                << "："
                << (correct ? "位置正确" : "位置错误")
                << "，偏差="
                << static_cast<int>(error)
                << " px\n";

            if (!correct)
            {
                all_correct = false;
            }
        }

        report << (
            all_correct
                ? "总判定：所有元件正确"
                : "总判定：存在缺失或位置错误"
        );

        return report.str();
    }

private:
    const DetectedObject* findBest(
        int class_id,
        const std::vector<DetectedObject>& detections
    ) const
    {
        const DetectedObject* best = nullptr;

        for (const auto& object : detections)
        {
            if (object.class_id != class_id)
            {
                continue;
            }

            if (best == nullptr || object.score > best->score)
            {
                best = &object;
            }
        }

        return best;
    }

    static std::string findDirection(const cv::Point& reference)
    {
        int distance_top = reference.y;
        int distance_bottom = 800 - reference.y;
        int distance_left = reference.x;
        int distance_right = 800 - reference.x;

        int smallest = std::min({
            distance_top,
            distance_bottom,
            distance_left,
            distance_right
        });

        if (smallest == distance_bottom)
        {
            return "下";
        }

        if (smallest == distance_right)
        {
            return "右";
        }

        if (smallest == distance_top)
        {
            return "上";
        }

        return "左";
    }

    static cv::Point normalizeDirection(
        const cv::Point& difference,
        const std::string& direction
    )
    {
        if (direction == "下")
        {
            return difference;
        }

        if (direction == "右")
        {
            return cv::Point(-difference.y, difference.x);
        }

        if (direction == "上")
        {
            return cv::Point(-difference.x, -difference.y);
        }

        return cv::Point(difference.y, -difference.x);
    }

    // 由20张 all_correct 人工标签统计得到，单位为800x800像素。
    const std::array<PositionRule, 4> rules_{
        PositionRule{"beige", 1, 149, -513},
        PositionRule{"blue", 2, 252, -104},
        PositionRule{"brown", 3, -258, -64},
        PositionRule{"red", 4, -232, -448}
    };

    const double position_tolerance_ = 100.0;
};
