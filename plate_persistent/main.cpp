#include "plate_engine.h"
#include "stream_protocol.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {
std::string json_string(const std::string& value)
{
    static const char hex[] = "0123456789abcdef";
    std::string output = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { output += '\\'; output += static_cast<char>(c); }
        else if (c < 32) { output += "\\u00"; output += hex[c >> 4]; output += hex[c & 15]; }
        else output += static_cast<char>(c);
    }
    return output + '"';
}
void save(const std::filesystem::path& directory, const std::string& image_path,
          const cv::Mat& image, const plate::FrameResult& frame, double margin)
{
    std::filesystem::create_directories(directory);
    std::ofstream json(directory / "results.json"), text(directory / "results.txt");
    if (!json || !text) throw std::runtime_error("Cannot create result files");
    json << std::fixed << std::setprecision(6)
         << "{\n  \"image\": " << json_string(image_path)
         << ",\n  \"backend\": \"native RKNN C API, no USB inference bridge\","
         << "\n  \"margin_each_side\": " << margin
         << ",\n  \"detection_call_ms\": " << frame.detection_call_ms
         << ",\n  \"preprocess_ms\": " << frame.preprocess_ms
         << ",\n  \"postprocess_ms\": " << frame.postprocess_ms
         << ",\n  \"engine_total_ms\": " << frame.total_ms
         << ",\n  \"timing_scope\": \"input set, model run, output retrieval and copy; not pure NPU time\","
         << "\n  \"detections\": [\n";
    text << "原图：" << image_path << "\n检测数量：" << frame.plates.size() << '\n';
    cv::Mat canvas = image.clone();
    for (std::size_t i = 0; i < frame.plates.size(); ++i) {
        const auto& result = frame.plates[i];
        const auto& detection = result.detection;
        const std::string input_name = "plate_" + std::to_string(i + 1) + "_input.png";
        if (!result.input_bgr.empty() && !cv::imwrite((directory / input_name).string(), result.input_bgr))
            throw std::runtime_error("Could not save recognition input");
        json << "    {\"id\": " << i + 1 << ", \"bbox_xyxy\": [";
        for (int j = 0; j < 4; ++j) json << (j ? ", " : "") << detection.box[j];
        json << "], \"keypoints\": [";
        for (int j = 0; j < 4; ++j)
            json << (j ? ", " : "") << '[' << detection.corners[j].x << ", " << detection.corners[j].y << ']';
        json << "], \"detection_confidence\": " << detection.confidence
             << ", \"layout\": " << detection.layout
             << ", \"raw_plate\": " << json_string(result.recognition.text)
             << ", \"basic_complete\": " << (result.recognition.complete ? "true" : "false")
             << ", \"review_reason\": " << json_string(result.error.empty() ? result.recognition.reason : result.error)
             << ", \"sampling_mode\": " << json_string(result.sampling)
             << ", \"native_crop_size\": [" << result.native_size.width << ", " << result.native_size.height << ']'
             << ", \"recognition_call_ms\": " << result.recognition_call_ms
             << ", \"accepted\": " << (result.accepted ? "true" : "false")
             << ", \"crop_ms\": " << result.crop_ms
             << ", \"decode_ms\": " << result.decode_ms
             << ", \"input_path\": " << (result.input_bgr.empty() ? "null" : json_string((directory / input_name).string()))
             << ", \"ctc_indices\": [";
        for (std::size_t j = 0; j < result.recognition.frame_indices.size(); ++j)
            json << (j ? ", " : "") << result.recognition.frame_indices[j];
        json << "]}" << (i + 1 == frame.plates.size() ? "\n" : ",\n");
        text << "车牌" << i + 1 << "：";
        if (result.recognition.complete && result.error.empty()) text << result.recognition.text;
        else text << "待确认；原始预测=" << result.recognition.text;
        text << '\n';
        // Keep rejected candidates in JSON, but omit them from the accepted-result canvas.
        if (!result.accepted) continue;
        const cv::Scalar color = result.recognition.complete && result.error.empty()
                               ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 165, 255);
        const cv::Point top_left(cvRound(detection.box[0]), cvRound(detection.box[1]));
        const cv::Point bottom_right(cvRound(detection.box[2]), cvRound(detection.box[3]));
        cv::rectangle(canvas, top_left, bottom_right, color, 2);
        cv::putText(canvas, "plate" + std::to_string(i + 1), {top_left.x, std::max(15, top_left.y - 6)},
                    cv::FONT_HERSHEY_SIMPLEX, .6, color, 2);
    }
    json << "  ]\n}\n";
    json.flush(); text.flush();
    if (!json || !text || !cv::imwrite((directory / "detection.jpg").string(), canvas))
        throw std::runtime_error("Could not save complete results");
}
} // namespace

// One process owns both contexts. It accepts exactly one request at a time.
// stdout also contains SDK logs; only lines beginning @PLATE_ are protocol records.
int serve(const char* detector, const char* recognizer)
{
    const auto init_start = std::chrono::steady_clock::now();
    plate::Engine engine(detector, recognizer);
    const double init_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - init_start).count();
    std::size_t request_index = 0;
    return plate_stream::run(std::cin, std::cout, [&](const plate_stream::Request& request) {
            const auto begin = std::chrono::steady_clock::now();
            const auto image = cv::imread(request.image, cv::IMREAD_COLOR);
            const double read_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - begin).count();
            if (image.empty()) throw std::runtime_error("Cannot read image: " + request.image);
            const auto frame = engine.process(image, .05);
            const auto save_start = std::chrono::steady_clock::now();
            save(request.directory, request.image, image, frame, .05);
            const double save_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - save_start).count();
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - begin).count();
            std::ofstream timing(std::filesystem::path(request.directory) / "timing.json");
            timing << "{\"request_index\":" << ++request_index
                   << ",\"model_init_ms\":" << init_ms << ",\"read_ms\":" << read_ms
                   << ",\"save_ms\":" << save_ms << ",\"backend_total_ms\":" << ms << "}\n";
            if (!timing) throw std::runtime_error("Cannot save timing metadata");
            return ms;
    });
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 4 && std::string(argv[3]) == "--stream") {
        try { return serve(argv[1], argv[2]); }
        catch (const std::exception& error) {
            std::cerr << "STREAM ERROR: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 5 && argc != 6) {
        std::cerr << "Usage: " << argv[0] << " detector.rknn recognizer.rknn image.jpg output_dir [margin]\n";
        return 2;
    }
    try {
        double margin = .05;
        if (argc == 6) {
            std::size_t consumed = 0;
            margin = std::stod(argv[5], &consumed);
            if (consumed != std::string(argv[5]).size()) throw std::runtime_error("Invalid margin value");
        }
        if (!std::isfinite(margin) || margin < 0 || margin > .2)
            throw std::runtime_error("Margin must be within [0,0.2]");
        const auto image = cv::imread(argv[3], cv::IMREAD_COLOR);
        if (image.empty()) throw std::runtime_error("Cannot read image: " + std::string(argv[3]));
        std::cout << "1. 原图 " << image.cols << 'x' << image.rows << "；加载板端模型\n";
        plate::Engine engine(argv[1], argv[2]);
        std::cout << "2. 在开发板本地执行检测与识别\n";
        const auto frame = engine.process(image, margin);
        std::cout << "检测数量：" << frame.plates.size() << "；检测调用：" << frame.detection_call_ms << " ms\n";
        for (std::size_t i = 0; i < frame.plates.size(); ++i) {
            const auto& result = frame.plates[i];
            std::cout << "车牌" << i + 1 << "：";
            if (result.error.empty() && result.recognition.complete) std::cout << result.recognition.text;
            else std::cout << "待确认；原始预测=" << result.recognition.text
                           << "；原因=" << (result.error.empty() ? result.recognition.reason : result.error);
            std::cout << "；定位分数=" << result.detection.confidence
                      << "；识别调用=" << result.recognition_call_ms << " ms\n";
        }
        save(argv[4], argv[3], image, frame, margin);
        std::cout << "3. 结果目录：" << argv[4] << "\n"
                  << "完整性检查通过仍需核对真实号码。\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}

