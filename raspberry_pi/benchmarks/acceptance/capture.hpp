#pragma once
#include <array>
#include <vector>
#include <opencv2/opencv.hpp>
inline bool acceptance_capture_enabled=false;
inline std::array<cv::Point2f,4> acceptance_corners;
inline cv::Mat acceptance_mask;
inline std::vector<float> acceptance_tensor;
