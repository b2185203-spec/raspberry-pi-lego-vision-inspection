#pragma once

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct DetectedObject
{
    int class_id = -1;
    cv::Rect box;
    cv::Point center;
    float score = 0.0F;
};

class YoloDetector
{
public:
    explicit YoloDetector(const std::string& model_path)
        : environment_(ORT_LOGGING_LEVEL_WARNING, "lego_yolo")
    {
        session_options_.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_EXTENDED
        );

        session_options_.SetIntraOpNumThreads(2);

        session_ = std::make_unique<Ort::Session>(
            environment_,
            model_path.c_str(),
            session_options_
        );

        Ort::AllocatorWithDefaultOptions allocator;

        auto input_name = session_->GetInputNameAllocated(
            0,
            allocator
        );
        input_name_ = input_name.get();

        auto output_name = session_->GetOutputNameAllocated(
            0,
            allocator
        );
        output_name_ = output_name.get();
    }

    std::string detect(
        const cv::Mat& image,
        cv::Mat& preview,
        std::vector<DetectedObject>& detections
    )
    {
        detections.clear();

        if (image.empty())
        {
            return "YOLO：截图为空，无法检测";
        }

        preview = image.clone();

        float scale = std::min(
            640.0F / image.cols,
            640.0F / image.rows
        );

        int resized_width = static_cast<int>(
            image.cols * scale
        );
        int resized_height = static_cast<int>(
            image.rows * scale
        );

        int pad_x = (640 - resized_width) / 2;
        int pad_y = (640 - resized_height) / 2;

        cv::Mat resized;
        cv::resize(
            image,
            resized,
            cv::Size(resized_width, resized_height)
        );

        cv::Mat letterbox;
        cv::copyMakeBorder(
            resized,
            letterbox,
            pad_y,
            640 - resized_height - pad_y,
            pad_x,
            640 - resized_width - pad_x,
            cv::BORDER_CONSTANT,
            cv::Scalar(114, 114, 114)
        );

        cv::Mat blob = cv::dnn::blobFromImage(
            letterbox,
            1.0 / 255.0,
            cv::Size(640, 640),
            cv::Scalar(),
            true,
            false
        );

        std::array<int64_t, 4> input_shape{
            1,
            3,
            640,
            640
        };

        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault
        );

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory,
            blob.ptr<float>(),
            blob.total(),
            input_shape.data(),
            input_shape.size()
        );

        const char* input_names[] = {input_name_.c_str()};
        const char* output_names[] = {output_name_.c_str()};

        auto output_tensors = session_->Run(
            Ort::RunOptions{nullptr},
            input_names,
            &input_tensor,
            1,
            output_names,
            1
        );

        const float* output = output_tensors[0].GetTensorData<float>();
        constexpr int class_count = 5;
        constexpr int candidate_count = 8400;

        std::vector<cv::Rect> boxes;
        std::vector<float> scores;
        std::vector<int> class_ids;

        for (int index = 0; index < candidate_count; ++index)
        {
            int best_class = 0;
            float best_score = 0.0F;

            for (int class_id = 0; class_id < class_count; ++class_id)
            {
                float score = output[
                    (4 + class_id) * candidate_count + index
                ];

                if (score > best_score)
                {
                    best_score = score;
                    best_class = class_id;
                }
            }

            if (best_score < 0.40F)
            {
                continue;
            }

            float center_x = output[index];
            float center_y = output[candidate_count + index];
            float width = output[2 * candidate_count + index];
            float height = output[3 * candidate_count + index];

            int left = static_cast<int>(
                (center_x - width / 2.0F - pad_x) / scale
            );
            int top = static_cast<int>(
                (center_y - height / 2.0F - pad_y) / scale
            );
            int right = static_cast<int>(
                (center_x + width / 2.0F - pad_x) / scale
            );
            int bottom = static_cast<int>(
                (center_y + height / 2.0F - pad_y) / scale
            );

            left = std::clamp(left, 0, image.cols - 1);
            top = std::clamp(top, 0, image.rows - 1);
            right = std::clamp(right, 0, image.cols - 1);
            bottom = std::clamp(bottom, 0, image.rows - 1);

            if (right <= left || bottom <= top)
            {
                continue;
            }

            boxes.emplace_back(
                left,
                top,
                right - left,
                bottom - top
            );
            scores.push_back(best_score);
            class_ids.push_back(best_class);
        }

        std::vector<int> kept_indices;
        cv::dnn::NMSBoxes(
            boxes,
            scores,
            0.40F,
            0.45F,
            kept_indices
        );

        if (kept_indices.empty())
        {
            return "YOLO：未检测到积木";
        }

        const std::vector<std::string> class_names{
            "black_reference",
            "beige",
            "blue",
            "brown",
            "red"
        };

        std::ostringstream result;
        result << "YOLO 检测到 " << kept_indices.size() << " 个积木\n";

        for (int kept_index : kept_indices)
        {
            const cv::Scalar colors[]{
                cv::Scalar(0, 255, 255),
                cv::Scalar(40, 210, 255),
                cv::Scalar(255, 140, 30),
                cv::Scalar(50, 120, 170),
                cv::Scalar(40, 40, 255)
            };

            const cv::Rect& box = boxes[kept_index];
            int class_id = class_ids[kept_index];
            cv::Point center(
                box.x + box.width / 2,
                box.y + box.height / 2
            );

            detections.push_back({
                class_id,
                box,
                center,
                scores[kept_index]
            });

            cv::rectangle(
                preview,
                box,
                colors[class_id],
                3
            );
            cv::circle(
                preview,
                center,
                5,
                colors[class_id],
                -1
            );
            cv::putText(
                preview,
                class_names[class_id]
                    + " "
                    + std::to_string(
                        static_cast<int>(scores[kept_index] * 100.0F)
                    )
                    + "%",
                cv::Point(box.x, std::max(24, box.y - 8)),
                cv::FONT_HERSHEY_SIMPLEX,
                0.6,
                colors[class_id],
                2,
                cv::LINE_AA
            );

            result
                << class_names[class_id]
                << "："
                << static_cast<int>(scores[kept_index] * 100.0F)
                << "%\n";
        }

        return result.str();
    }

private:
    Ort::Env environment_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::string output_name_;
};
