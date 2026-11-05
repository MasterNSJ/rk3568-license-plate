#pragma once
#include "plate_algorithms.h"
#include <memory>
#include <opencv2/core.hpp>

namespace plate {
struct PlateResult {
    Detection detection;
    Decoded recognition;
    cv::Mat input_bgr;
    std::string sampling, error;
    cv::Size native_size;
    double recognition_call_ms = 0;
};
struct FrameResult {
    std::vector<PlateResult> plates;
    double detection_call_ms = 0;
};
// Construct and use on one worker thread. Keep alive across frames when adding Qt.
class Engine {
public:
    Engine(const std::string& detector_path, const std::string& recognizer_path);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    FrameResult process(const cv::Mat& image_bgr, double margin = .05);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace plate
