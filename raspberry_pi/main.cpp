#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <thread>

#include <gpiod.hpp>
#include <opencv2/opencv.hpp>

#include "camera.hpp"
#include "image_preprocessor.hpp"
#include "position_rules.hpp"
#include "ultrasonic.hpp"
#include "web_server.hpp"
#include "yolo_detector.hpp"

using namespace std::chrono_literals;

volatile std::sig_atomic_t running = 1;

void handleSignal(int)
{
    running = 0;
}

bool shouldSnapshot(double distance)
{
    return distance > 0.0 && distance < 12.0;
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    try
    {
        gpiod::chip chip("/dev/gpiochip0");

        Camera camera("/dev/video0", 980, 980, 30);
        Ultrasonic ultrasonic(chip, 23, 24, "ultrasonic");
        WebServer web_server(camera, 8080, 30);
        ImagePreprocessor preprocessor;
        PositionRules position_rules;
        // 默认从仓库 models 目录读取，也可以用第一个参数指定模型。
        std::string model_path = "../models/lego_yolov8n_manual.onnx";
        if (argc > 1)
        {
            model_path = argv[1];
        }

        YoloDetector detector(model_path);

        if (!camera.open())
        {
            return 1;
        }

        std::thread web_thread(
            [&]()
            {
                if (!web_server.run(running))
                {
                    running = 0;
                }
            }
        );

        bool previous_condition = false;
        std::uint64_t processed_snapshot_version = 0;

        web_server.setMarkerResult(
            "等待截图并裁切灰色底板"
        );
        web_server.setYoloResult(
            "等待超声波触发截图"
        );

        while (running)
        {
            double distance = ultrasonic.measure_cm();
            bool current_condition = shouldSnapshot(distance);
            bool trigger = current_condition && !previous_condition;

            // 先记录触发，再读取下一帧作为截图。
            camera.snapshotWhen(trigger);

            cv::Mat frame;
            if (!camera.read(frame))
            {
                std::cerr << "读取摄像头失败\n";
                std::this_thread::sleep_for(100ms);
                continue;
            }

            std::uint64_t snapshot_version =
                camera.getSnapshotVersion();

            if (snapshot_version != processed_snapshot_version)
            {
                cv::Mat snapshot;

                if (camera.getSnapshot(snapshot))
                {
                    cv::Mat rectified;
                    std::string preprocess_message;

                    if (preprocessor.process(
                        snapshot,
                        rectified,
                        preprocess_message
                    ))
                    {
                        cv::Mat detection_preview;
                        std::vector<DetectedObject> detections;
                        std::string yolo_message = detector.detect(
                            rectified,
                            detection_preview,
                            detections
                        );
                        std::string position_message = position_rules.check(
                            detection_preview,
                            detections
                        );

                        web_server.setResultImages(
                            rectified,
                            detection_preview
                        );
                        web_server.setMarkerResult(preprocess_message);
                        web_server.setYoloResult(
                            yolo_message + "\n" + position_message
                        );
                    }
                    else
                    {
                        web_server.setMarkerResult(
                            preprocess_message
                        );
                        web_server.setYoloResult(
                            "YOLO：本次截图没有通过图像处理"
                        );
                    }

                    processed_snapshot_version = snapshot_version;
                }
            }

            previous_condition = current_condition;

            if (distance > 0.0)
            {
                std::cout
                    << "\r当前距离："
                    << distance
                    << " cm    "
                    << std::flush;
            }

            std::this_thread::sleep_for(30ms);
        }

        std::cout << "\n正在关闭程序\n";

        if (web_thread.joinable())
        {
            web_thread.join();
        }

        camera.close();
    }
    catch (const std::exception& error)
    {
        std::cerr << "\n程序错误：" << error.what() << '\n';
        return 1;
    }

    return 0;
}
