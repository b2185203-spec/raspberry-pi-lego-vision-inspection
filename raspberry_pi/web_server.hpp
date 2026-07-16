#pragma once

#include "camera.hpp"

#include <opencv2/opencv.hpp>

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

class WebServer
{
public:
    WebServer(
        Camera& camera,
        std::uint16_t port,
        int stream_fps
    )
        : camera_{camera},
          port_{port},
          stream_fps_{
              stream_fps > 0
                  ? stream_fps
                  : 15
          }
    {
    }

    /*
     * 后面完成标记识别以后，
     * 调用这个函数更新网页右上角文字。
     */
    void setMarkerResult(
        const std::string& result
    )
    {
        std::lock_guard<std::mutex> lock{
            result_mutex_
        };

        marker_result_ = result;
    }

    /*
     * 后面完成YOLO以后，
     * 调用这个函数更新网页右下角文字。
     */
    void setYoloResult(
        const std::string& result
    )
    {
        std::lock_guard<std::mutex> lock{
            result_mutex_
        };

        yolo_result_ = result;
    }

    // 输入是转正图和已画 YOLO 框的图，输出给网页右上、右下区域。
    void setResultImages(
        const cv::Mat& rectified_image,
        const cv::Mat& detection_image
    )
    {
        std::lock_guard<std::mutex> lock{result_mutex_};
        rectified_image_ = rectified_image.clone();
        detection_image_ = detection_image.clone();
        ++result_image_version_;
    }

    bool run(
        volatile std::sig_atomic_t& running
    )
    {
        int server_socket = ::socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

        if (server_socket < 0)
        {
            std::cerr
                << "错误：创建服务器失败\n";

            return false;
        }

        int reuse_address = 1;

        ::setsockopt(
            server_socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse_address,
            sizeof(reuse_address)
        );

        sockaddr_in server_address{};

        server_address.sin_family = AF_INET;

        server_address.sin_addr.s_addr =
            INADDR_ANY;

        server_address.sin_port =
            htons(port_);

        if (
            ::bind(
                server_socket,
                reinterpret_cast<sockaddr*>(
                    &server_address
                ),
                sizeof(server_address)
            ) < 0
        )
        {
            std::cerr
                << "错误：绑定端口失败："
                << std::strerror(errno)
                << '\n';

            ::close(server_socket);

            return false;
        }

        if (
            ::listen(
                server_socket,
                8
            ) < 0
        )
        {
            std::cerr
                << "错误：监听端口失败\n";

            ::close(server_socket);

            return false;
        }

        std::cout
            << "网页服务器已经启动\n";

        std::cout
            << "浏览器访问：http://树莓派IP:"
            << port_
            << '\n';

        std::cout
            << "按 Ctrl+C 停止程序\n";

        while (running)
        {
            pollfd socket_info{};

            socket_info.fd = server_socket;
            socket_info.events = POLLIN;

            int poll_result = ::poll(
                &socket_info,
                1,
                500
            );

            if (!running)
            {
                break;
            }

            if (poll_result < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                std::cerr
                    << "错误：等待网页连接失败\n";

                break;
            }

            if (poll_result == 0)
            {
                continue;
            }

            int client_socket = ::accept(
                server_socket,
                nullptr,
                nullptr
            );

            if (client_socket < 0)
            {
                continue;
            }

            handleClient(
                client_socket,
                running
            );
        }

        ::close(server_socket);

        if (stream_thread_.joinable())
        {
            stream_thread_.join();
        }

        std::cout
            << "网页服务器已经关闭\n";

        return true;
    }

private:
    void handleClient(
        int client_socket,
        volatile std::sig_atomic_t& running
    )
    {
        char request_buffer[4096]{};

        ssize_t received = ::recv(
            client_socket,
            request_buffer,
            sizeof(request_buffer) - 1,
            0
        );

        if (received <= 0)
        {
            ::close(client_socket);
            return;
        }

        std::string request{
            request_buffer,
            static_cast<std::size_t>(received)
        };

        if (
            request.rfind(
                "GET /stream ",
                0
            ) == 0
        )
        {
            startVideoStream(
                client_socket,
                running
            );

            return;
        }

        if (
            request.rfind(
                "GET /rectified",
                0
            ) == 0
        )
        {
            sendResultImage(client_socket, false);
            ::close(client_socket);

            return;
        }

        if (
            request.rfind(
                "GET /detection",
                0
            ) == 0
        )
        {
            sendResultImage(client_socket, true);
            ::close(client_socket);

            return;
        }

        if (
            request.rfind(
                "GET /snapshot",
                0
            ) == 0
        )
        {
            sendSnapshot(client_socket);
            ::close(client_socket);

            return;
        }

        if (
            request.rfind(
                "GET /status",
                0
            ) == 0
        )
        {
            sendStatus(client_socket);
            ::close(client_socket);

            return;
        }

        sendHomePage(client_socket);
        ::close(client_socket);
    }

    void startVideoStream(
        int client_socket,
        volatile std::sig_atomic_t& running
    )
    {
        /*
         * 当前简单版本只允许一个实时视频连接。
         * 状态和截图请求仍然可以同时正常访问。
         */
        if (stream_active_.exchange(true))
        {
            const std::string response =
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: text/plain; charset=UTF-8\r\n"
                "Connection: close\r\n"
                "\r\n"
                "当前已经存在一个视频连接。";

            sendText(
                client_socket,
                response
            );

            ::close(client_socket);

            return;
        }

        if (stream_thread_.joinable())
        {
            stream_thread_.join();
        }

        stream_thread_ = std::thread(
            [this, client_socket, &running]()
            {
                sendVideoStream(
                    client_socket,
                    running
                );

                ::shutdown(
                    client_socket,
                    SHUT_RDWR
                );

                ::close(client_socket);

                stream_active_.store(false);
            }
        );
    }

    bool sendAll(
        int client_socket,
        const void* data,
        std::size_t size
    )
    {
        const auto* bytes =
            static_cast<const unsigned char*>(
                data
            );

        std::size_t total_sent = 0;

        while (total_sent < size)
        {
            ssize_t sent = ::send(
                client_socket,
                bytes + total_sent,
                size - total_sent,
                MSG_NOSIGNAL
            );

            if (sent < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                return false;
            }

            if (sent == 0)
            {
                return false;
            }

            total_sent +=
                static_cast<std::size_t>(sent);
        }

        return true;
    }

    bool sendText(
        int client_socket,
        const std::string& text
    )
    {
        return sendAll(
            client_socket,
            text.data(),
            text.size()
        );
    }

    void sendHomePage(int client_socket)
    {
        const std::string html = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">

<head>
    <meta charset="UTF-8">

    <meta
        name="viewport"
        content="width=device-width, initial-scale=1.0"
    >

    <title>树莓派视觉检测系统</title>

    <style>
        * {
            box-sizing: border-box;
        }

        html,
        body {
            width: 100%;
            height: 100%;
            margin: 0;
            background: #161616;
            color: #ffffff;
            font-family:
                Arial,
                "Microsoft YaHei",
                sans-serif;
        }

        .layout {
            display: grid;

            grid-template-columns:
                minmax(0, 1fr)
                minmax(0, 1fr);

            grid-template-rows:
                minmax(0, 1fr)
                minmax(0, 1fr);

            width: 100%;
            height: 100%;

            gap: 8px;
            padding: 8px;
        }

        .panel {
            min-width: 0;
            min-height: 0;

            overflow: hidden;

            border: 1px solid #4d4d4d;
            border-radius: 8px;

            background: #222222;
        }

        .panel-title {
            height: 42px;

            display: flex;
            align-items: center;

            padding: 0 14px;

            font-size: 18px;
            font-weight: bold;

            background: #303030;
            border-bottom: 1px solid #4d4d4d;
        }

        .video-panel {
            grid-column: 1;
            grid-row: 1 / 3;

            display: flex;
            flex-direction: column;
        }

        .video-area {
            flex: 1;
            min-height: 0;

            display: flex;
            justify-content: center;
            align-items: center;

            background: #000000;
        }

        .video-area img {
            width: 100%;
            height: 100%;

            object-fit: contain;
        }

        .snapshot-panel {
            grid-column: 2;
            grid-row: 1;

            display: flex;
            flex-direction: column;
        }

        .snapshot-content {
            flex: 1;
            min-height: 0;

            display: grid;

            grid-template-columns:
                minmax(0, 3fr)
                minmax(180px, 2fr);
        }

        .snapshot-image {
            min-width: 0;
            min-height: 0;

            display: flex;
            align-items: center;
            justify-content: center;

            background: #000000;
        }

        .snapshot-image img {
            width: 100%;
            height: 100%;

            object-fit: contain;
        }

        .marker-result {
            padding: 14px;

            overflow: auto;

            white-space: pre-wrap;
            word-break: break-word;

            font-size: 17px;
            line-height: 1.6;

            border-left: 1px solid #4d4d4d;
        }

        .yolo-panel {
            grid-column: 2;
            grid-row: 2;

            display: flex;
            flex-direction: column;
        }

        .yolo-result {
            flex: 1;

            min-width: 0;

            padding: 16px;

            overflow: auto;

            white-space: pre-wrap;
            word-break: break-word;

            font-size: 18px;
            line-height: 1.7;

            border-left: 1px solid #4d4d4d;
        }

        .waiting {
            color: #bdbdbd;
        }

        @media (max-width: 900px) {
            .layout {
                display: flex;
                flex-direction: column;
                height: auto;
                min-height: 100%;
            }

            .video-panel,
            .snapshot-panel,
            .yolo-panel {
                min-height: 500px;
            }

            .snapshot-content {
                grid-template-columns: 1fr;
                grid-template-rows: 3fr 2fr;
            }

            .marker-result {
                border-left: none;
                border-top: 1px solid #4d4d4d;
            }
        }
    </style>
</head>

<body>
    <div class="layout">

        <section class="panel video-panel">
            <div class="panel-title">
                实时视频
            </div>

            <div class="video-area">
                <img
                    src="/stream"
                    alt="实时视频"
                >
            </div>
        </section>

        <section class="panel snapshot-panel">
            <div class="panel-title">
                灰色底板裁切与透视校正
            </div>

            <div class="snapshot-content">
                <div class="snapshot-image">
                    <img
                        id="rectifiedImage"
                        src="/rectified?v=0"
                        alt="转正图"
                    >
                </div>

                <div
                    id="markerResult"
                    class="marker-result waiting"
                >
                    暂未进行标记识别
                </div>
            </div>
        </section>

        <section class="panel yolo-panel">
            <div class="panel-title">
                YOLO 五类检测与位置判断
            </div>

            <div class="snapshot-content">
                <div class="snapshot-image">
                    <img
                        id="detectionImage"
                        src="/detection?v=0"
                        alt="YOLO 检测图"
                    >
                </div>

                <div
                    id="yoloResult"
                    class="yolo-result waiting"
                >
                    暂未进行 YOLO 检测
                </div>
            </div>
        </section>

    </div>

    <script>
        let lastResultVersion = -1;

        async function updateStatus()
        {
            try
            {
                const response = await fetch(
                    "/status?t=" + Date.now(),
                    {
                        cache: "no-store"
                    }
                );

                if (!response.ok)
                {
                    return;
                }

                const data = await response.json();

                const markerElement =
                    document.getElementById(
                        "markerResult"
                    );

                const yoloElement =
                    document.getElementById(
                        "yoloResult"
                    );

                markerElement.textContent =
                    data.markerResult;

                yoloElement.textContent =
                    data.yoloResult;

                if (
                    data.resultVersion
                    !== lastResultVersion
                )
                {
                    lastResultVersion =
                        data.resultVersion;

                    document.getElementById(
                        "rectifiedImage"
                    ).src =
                        "/rectified?v="
                        + data.resultVersion
                        + "&t="
                        + Date.now();

                    document.getElementById(
                        "detectionImage"
                    ).src =
                        "/detection?v="
                        + data.resultVersion
                        + "&t="
                        + Date.now();
                }
            }
            catch (error)
            {
                console.log(
                    "状态更新失败",
                    error
                );
            }
        }

        updateStatus();

        setInterval(
            updateStatus,
            100
        );
    </script>
</body>

</html>
)HTML";

        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Content-Length: "
            + std::to_string(html.size())
            + "\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n"
            "\r\n"
            + html;

        sendText(
            client_socket,
            response
        );
    }

    void sendSnapshot(int client_socket)
    {
        cv::Mat snapshot;

        if (!camera_.getSnapshot(snapshot))
        {
            snapshot = cv::Mat(
                480,
                640,
                CV_8UC3,
                cv::Scalar(
                    35,
                    35,
                    35
                )
            );

            cv::putText(
                snapshot,
                "Waiting for snapshot",
                cv::Point(
                    120,
                    245
                ),
                cv::FONT_HERSHEY_SIMPLEX,
                1.0,
                cv::Scalar(
                    220,
                    220,
                    220
                ),
                2,
                cv::LINE_AA
            );
        }

        std::vector<unsigned char> jpeg_data;

        const std::vector<int> parameters{
            cv::IMWRITE_JPEG_QUALITY,
            80
        };

        if (
            !cv::imencode(
                ".jpg",
                snapshot,
                jpeg_data,
                parameters
            )
        )
        {
            const std::string response =
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";

            sendText(
                client_socket,
                response
            );

            return;
        }

        const std::string header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: "
            + std::to_string(jpeg_data.size())
            + "\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Connection: close\r\n"
            "\r\n";

        if (
            !sendText(
                client_socket,
                header
            )
        )
        {
            return;
        }

        sendAll(
            client_socket,
            jpeg_data.data(),
            jpeg_data.size()
        );
    }

    void sendResultImage(
        int client_socket,
        bool want_detection_image
    )
    {
        cv::Mat image;

        {
            std::lock_guard<std::mutex> lock{result_mutex_};
            image = want_detection_image
                ? detection_image_.clone()
                : rectified_image_.clone();
        }

        if (image.empty())
        {
            image = cv::Mat(
                800,
                800,
                CV_8UC3,
                cv::Scalar(35, 35, 35)
            );
            cv::putText(
                image,
                "Waiting for result",
                cv::Point(220, 410),
                cv::FONT_HERSHEY_SIMPLEX,
                1.0,
                cv::Scalar(220, 220, 220),
                2,
                cv::LINE_AA
            );
        }

        std::vector<unsigned char> jpeg_data;
        const std::vector<int> parameters{
            cv::IMWRITE_JPEG_QUALITY,
            85
        };

        if (!cv::imencode(".jpg", image, jpeg_data, parameters))
        {
            sendText(
                client_socket,
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n\r\n"
            );
            return;
        }

        const std::string header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: "
            + std::to_string(jpeg_data.size())
            + "\r\nCache-Control: no-store, no-cache, must-revalidate\r\n"
            "Connection: close\r\n\r\n";

        if (sendText(client_socket, header))
        {
            sendAll(
                client_socket,
                jpeg_data.data(),
                jpeg_data.size()
            );
        }
    }

    void sendStatus(int client_socket)
    {
        std::string marker_result;
        std::string yolo_result;
        std::uint64_t result_image_version = 0;

        {
            std::lock_guard<std::mutex> lock{
                result_mutex_
            };

            marker_result = marker_result_;
            yolo_result = yolo_result_;
            result_image_version = result_image_version_;
        }

        const std::string json =
            "{"
            "\"snapshotVersion\":"
            + std::to_string(
                camera_.getSnapshotVersion()
            )
            + ","
            "\"resultVersion\":"
            + std::to_string(result_image_version)
            + ","
            "\"markerResult\":\""
            + jsonEscape(marker_result)
            + "\","
            "\"yoloResult\":\""
            + jsonEscape(yolo_result)
            + "\""
            "}";

        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=UTF-8\r\n"
            "Content-Length: "
            + std::to_string(json.size())
            + "\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
            "Connection: close\r\n"
            "\r\n"
            + json;

        sendText(
            client_socket,
            response
        );
    }

    std::string jsonEscape(
        const std::string& input
    )
    {
        std::string output;

        output.reserve(
            input.size()
        );

        for (char character : input)
        {
            switch (character)
            {
                case '\\':
                    output += "\\\\";
                    break;

                case '"':
                    output += "\\\"";
                    break;

                case '\n':
                    output += "\\n";
                    break;

                case '\r':
                    output += "\\r";
                    break;

                case '\t':
                    output += "\\t";
                    break;

                default:
                    output += character;
                    break;
            }
        }

        return output;
    }

    void sendVideoStream(
        int client_socket,
        volatile std::sig_atomic_t& running
    )
    {
        timeval send_timeout{};

        send_timeout.tv_sec = 2;
        send_timeout.tv_usec = 0;

        ::setsockopt(
            client_socket,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &send_timeout,
            sizeof(send_timeout)
        );

        const std::string response_header =
            "HTTP/1.1 200 OK\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Connection: close\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "\r\n";

        if (
            !sendText(
                client_socket,
                response_header
            )
        )
        {
            return;
        }

        cv::Mat frame;

        std::vector<unsigned char> jpeg_data;

        const std::vector<int> jpeg_parameters{
            cv::IMWRITE_JPEG_QUALITY,
            88
        };

        const auto frame_interval =
            std::chrono::milliseconds{
                1000 / stream_fps_
            };

        while (running)
        {
            auto frame_start =
                std::chrono::steady_clock::now();

            if (!camera_.getLatestFrame(frame))
            {
                std::cerr
                    << "错误：读取摄像头失败\n";

                break;
            }

            jpeg_data.clear();

            if (
                !cv::imencode(
                    ".jpg",
                    frame,
                    jpeg_data,
                    jpeg_parameters
                )
            )
            {
                std::cerr
                    << "错误：视频JPEG编码失败\n";

                break;
            }

            const std::string frame_header =
                "--frame\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: "
                + std::to_string(
                    jpeg_data.size()
                )
                + "\r\n"
                "\r\n";

            if (
                !sendText(
                    client_socket,
                    frame_header
                )
            )
            {
                break;
            }

            if (
                !sendAll(
                    client_socket,
                    jpeg_data.data(),
                    jpeg_data.size()
                )
            )
            {
                break;
            }

            if (
                !sendText(
                    client_socket,
                    "\r\n"
                )
            )
            {
                break;
            }

            std::this_thread::sleep_until(
                frame_start
                + frame_interval
            );
        }
    }

private:
    Camera& camera_;

    std::uint16_t port_;

    int stream_fps_;

    std::atomic<bool> stream_active_{false};

    std::thread stream_thread_;

    std::mutex result_mutex_;

    std::string marker_result_{
        "暂未进行标记识别"
    };

    std::string yolo_result_{
        "暂未进行 YOLO 检测"
    };

    cv::Mat rectified_image_;

    cv::Mat detection_image_;

    std::uint64_t result_image_version_ = 0;
};
