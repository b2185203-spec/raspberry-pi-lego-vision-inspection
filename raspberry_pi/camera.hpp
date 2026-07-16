#pragma once

#include <opencv2/opencv.hpp>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

class Camera
{
public:
    Camera(
        const std::string& device,
        int width,
        int height,
        int fps
    )
        : device_(device),
          width_(width),
          height_(height),
          fps_(fps)
    {
    }

    bool open()
    {
        if (!capture_.open(device_, cv::CAP_V4L2))
        {
            std::cerr << "摄像头打开失败\n";
            return false;
        }

        capture_.set(
            cv::CAP_PROP_FOURCC,
            cv::VideoWriter::fourcc('M', 'J', 'P', 'G')
        );
        capture_.set(cv::CAP_PROP_FRAME_WIDTH, width_);
        capture_.set(cv::CAP_PROP_FRAME_HEIGHT, height_);
        capture_.set(cv::CAP_PROP_FPS, fps_);

        std::cout << "摄像头已经打开\n";
        return true;
    }

    bool read(cv::Mat& frame)
    {
        if (!capture_.read(frame) || frame.empty())
        {
            return false;
        }

        // 摄像头画面需要顺时针旋转 90 度。
        cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);

        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            latest_frame_ = frame.clone();
        }

        if (snapshot_requested_.exchange(false))
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            snapshot_ = frame.clone();
            snapshot_version_.fetch_add(1);
        }

        return true;
    }

    void snapshotWhen(bool condition)
    {
        if (condition)
        {
            snapshot_requested_.store(true);
        }
    }

    bool getLatestFrame(cv::Mat& output)
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (latest_frame_.empty())
        {
            return false;
        }

        output = latest_frame_.clone();
        return true;
    }

    bool getSnapshot(cv::Mat& output)
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        if (snapshot_.empty())
        {
            return false;
        }

        output = snapshot_.clone();
        return true;
    }

    std::uint64_t getSnapshotVersion() const
    {
        return snapshot_version_.load();
    }

    void close()
    {
        capture_.release();
        std::cout << "摄像头已经关闭\n";
    }

private:
    std::string device_;
    int width_;
    int height_;
    int fps_;
    cv::VideoCapture capture_;

    cv::Mat latest_frame_;
    cv::Mat snapshot_;
    std::mutex frame_mutex_;
    std::mutex snapshot_mutex_;
    std::atomic<bool> snapshot_requested_{false};
    std::atomic<std::uint64_t> snapshot_version_{0};
};
