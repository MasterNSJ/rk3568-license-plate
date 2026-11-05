#include "plate_engine.h"
#include "rknn_api.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <utility>
#include <opencv2/imgproc.hpp>

namespace plate {
namespace {
using Clock = std::chrono::steady_clock;
void check(int code, const char* operation)
{
    if (code != RKNN_SUCC)
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(code));
}
std::vector<uint8_t> read_model(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open model: " + path);
    const auto length = file.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > UINT32_MAX)
        throw std::runtime_error("Invalid model file size: " + path);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length))
        throw std::runtime_error("Cannot read model: " + path);
    return bytes;
}
struct Tensor { rknn_tensor_attr attr{}; std::vector<float> values; };
class Runtime {
public:
    Runtime(const std::string& path, int width, int height, unsigned output_count)
        : model_(read_model(path)), width_(width), height_(height)
    {
        try {
            check(rknn_init(&context_, model_.data(), static_cast<uint32_t>(model_.size()), 0, nullptr), "rknn_init");
            rknn_input_output_num count{};
            check(rknn_query(context_, RKNN_QUERY_IN_OUT_NUM, &count, sizeof(count)), "query IO counts");
            if (count.n_input != 1 || count.n_output != output_count)
                throw std::runtime_error("Unexpected model input/output count: " + path);
            rknn_tensor_attr input{};
            check(rknn_query(context_, RKNN_QUERY_INPUT_ATTR, &input, sizeof(input)), "query input");
            const bool nchw = input.n_dims == 4 && input.dims[0] == 1 && input.dims[1] == 3
                              && input.dims[2] == static_cast<unsigned>(height) && input.dims[3] == static_cast<unsigned>(width);
            const bool nhwc = input.n_dims == 4 && input.dims[0] == 1 && input.dims[1] == static_cast<unsigned>(height)
                              && input.dims[2] == static_cast<unsigned>(width) && input.dims[3] == 3;
            if (!nchw && !nhwc) throw std::runtime_error("Model image shape differs: " + path);
            attrs_.resize(count.n_output);
            for (unsigned i = 0; i < count.n_output; ++i) {
                attrs_[i].index = i;
                check(rknn_query(context_, RKNN_QUERY_OUTPUT_ATTR, &attrs_[i], sizeof(attrs_[i])), "query output");
                if (attrs_[i].n_dims < 1 || attrs_[i].n_dims > RKNN_MAX_DIMS || attrs_[i].dims[0] != 1
                    || attrs_[i].fmt == RKNN_TENSOR_NC1HWC2 || attrs_[i].n_elems == 0)
                    throw std::runtime_error("Unsupported logical output tensor");
                std::printf("output[%u] %s fmt=%s dims=", i, attrs_[i].name, get_format_string(attrs_[i].fmt));
                for (unsigned d = 0; d < attrs_[i].n_dims; ++d)
                    std::printf("%s%u", d ? "," : "", attrs_[i].dims[d]);
                std::printf("\n");
            }
            rknn_sdk_version version{};
            check(rknn_query(context_, RKNN_QUERY_SDK_VERSION, &version, sizeof(version)), "query version");
            std::printf("Loaded %s; API=%s driver=%s\n", path.c_str(), version.api_version, version.drv_version);
        } catch (...) {
            if (context_) rknn_destroy(context_);
            context_ = 0;
            throw;
        }
    }
    ~Runtime() { if (context_) rknn_destroy(context_); }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    std::vector<Tensor> infer(const cv::Mat& image, double& elapsed)
    {
        if (image.type() != CV_8UC3 || image.cols != width_ || image.rows != height_)
            throw std::runtime_error("RKNN input must match raw uint8 image dimensions");
        cv::Mat pixels = image.isContinuous() ? image : image.clone();
        rknn_input input{};
        input.buf = pixels.data;
        input.size = static_cast<uint32_t>(pixels.total() * pixels.elemSize());
        input.type = RKNN_TENSOR_UINT8;
        input.fmt = RKNN_TENSOR_NHWC;
        input.pass_through = 0; // Mean/std are embedded in the RKNN files.
        const auto start = Clock::now();
        check(rknn_inputs_set(context_, 1, &input), "rknn_inputs_set");
        check(rknn_run(context_, nullptr), "rknn_run");
        std::vector<rknn_output> outputs(attrs_.size());
        for (unsigned i = 0; i < outputs.size(); ++i) {
            outputs[i].index = i;
            outputs[i].want_float = 1;
        }
        check(rknn_outputs_get(context_, static_cast<uint32_t>(outputs.size()), outputs.data(), nullptr), "rknn_outputs_get");
        struct Guard {
            rknn_context context;
            std::vector<rknn_output>& outputs;
            ~Guard() { rknn_outputs_release(context, static_cast<uint32_t>(outputs.size()), outputs.data()); }
        } guard{context_, outputs};
        std::vector<Tensor> tensors;
        for (unsigned i = 0; i < outputs.size(); ++i) {
            const std::size_t count = attrs_[i].n_elems;
            if (!outputs[i].buf || outputs[i].size < count * sizeof(float))
                throw std::runtime_error("RKNN returned a short floating-point output");
            const auto* values = static_cast<const float*>(outputs[i].buf);
            tensors.push_back({attrs_[i], std::vector<float>(values, values + count)});
        }
        elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return tensors; // Guard releases SDK memory after copying logical outputs.
    }
private:
    std::vector<uint8_t> model_;
    rknn_context context_ = 0;
    int width_, height_;
    std::vector<rknn_tensor_attr> attrs_;
};

std::vector<Head> detector_heads(const std::vector<Tensor>& tensors)
{
    std::vector<Head> heads;
    for (const auto& tensor : tensors) {
        const auto& a = tensor.attr;
        Head head;
        if (a.n_dims == 4 && a.dims[1] == 45 && a.dims[2] == a.dims[3]) {
            head.side = static_cast<int>(a.dims[2]); head.channel_major = true;
        } else if (a.n_dims == 4 && a.dims[3] == 45 && a.dims[1] == a.dims[2]) {
            head.side = static_cast<int>(a.dims[1]); head.channel_major = false;
        } else throw std::runtime_error("Unexpected detector head layout");
        head.values = tensor.values;
        heads.push_back(std::move(head));
    }
    return heads;
}
bool class_major(const Tensor& tensor)
{
    const auto& a = tensor.attr;
    if (a.n_elems != 68 * 18) throw std::runtime_error("Recognition output must have 1224 scores");
    if (a.n_dims == 3) {
        if (a.dims[1] == 68 && a.dims[2] == 18) return true;
        if (a.dims[1] == 18 && a.dims[2] == 68) return false;
    }
    if (a.n_dims == 4) {
        if (a.dims[1] == 68 && a.dims[2] * a.dims[3] == 18) return true;
        if (a.dims[3] == 68 && a.dims[1] * a.dims[2] == 18) return false;
    }
    throw std::runtime_error("Unsupported recognition output layout");
}
float length(cv::Point2f a, cv::Point2f b)
{
    const auto d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y);
}
cv::Size native_size(const std::vector<cv::Point2f>& p)
{
    return {static_cast<int>(std::max(length(p[2], p[3]), length(p[1], p[0]))),
            static_cast<int>(std::max(length(p[1], p[2]), length(p[0], p[3])))};
}
cv::Mat warp(const cv::Mat& image, const std::vector<cv::Point2f>& points, cv::Size size, int border)
{
    const std::vector<cv::Point2f> target{{0, 0}, {static_cast<float>(size.width - 1), 0},
        {static_cast<float>(size.width - 1), static_cast<float>(size.height - 1)}, {0, static_cast<float>(size.height - 1)}};
    const auto matrix = cv::getPerspectiveTransform(points, target);
    if (!cv::checkRange(matrix)) throw std::runtime_error("Nonfinite perspective matrix");
    cv::Mat crop;
    cv::warpPerspective(image, crop, matrix, size, cv::INTER_LINEAR, border);
    return crop;
}
cv::Mat recognition_input(const cv::Mat& image, PlateResult& result, double margin)
{
    std::vector<cv::Point2f> points;
    for (const auto& p : result.detection.corners) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.y < 0 || p.x > image.cols - 1 || p.y > image.rows - 1)
            throw std::runtime_error("Keypoints outside the source image");
        points.emplace_back(p.x, p.y);
    }
    auto sum = [](cv::Point2f p) { return p.x + p.y; };
    auto difference = [](cv::Point2f p) { return p.y - p.x; };
    std::vector<cv::Point2f> ordered{
        *std::min_element(points.begin(), points.end(), [&](auto a, auto b) { return sum(a) < sum(b); }),
        *std::min_element(points.begin(), points.end(), [&](auto a, auto b) { return difference(a) < difference(b); }),
        *std::max_element(points.begin(), points.end(), [&](auto a, auto b) { return sum(a) < sum(b); }),
        *std::max_element(points.begin(), points.end(), [&](auto a, auto b) { return difference(a) < difference(b); })};
    for (unsigned i = 0; i < ordered.size(); ++i)
        for (unsigned j = i + 1; j < ordered.size(); ++j)
            if (length(ordered[i], ordered[j]) < .001f) throw std::runtime_error("Repeated or ambiguous corners");
    if (!cv::isContourConvex(ordered) || std::abs(cv::contourArea(ordered)) < 2)
        throw std::runtime_error("Degenerate plate quadrilateral");
    const auto base_size = native_size(ordered);
    if (base_size.width < 2 || base_size.height < 2
        || static_cast<double>(base_size.width) * base_size.height > static_cast<double>(image.cols) * image.rows * 4)
        throw std::runtime_error("Invalid perspective crop size");
    if (result.detection.layout == 1) {
        const cv::Mat native = warp(image, ordered, base_size, cv::BORDER_CONSTANT);
        const int upper_height = static_cast<int>(native.rows * 5.0 / 12.0);
        const int lower_start = static_cast<int>(native.rows / 3.0);
        if (upper_height < 1 || lower_start >= native.rows) throw std::runtime_error("Double-row crop too small");
        cv::Mat upper, merged, output;
        const auto lower = native.rowRange(lower_start, native.rows);
        cv::resize(native.rowRange(0, upper_height), upper, lower.size());
        cv::hconcat(upper, lower, merged);
        cv::resize(merged, output, {94, 24});
        result.sampling = "double_row_merge_unvalidated";
        result.native_size = base_size;
        return output;
    }
    cv::Point2f center;
    for (const auto& p : ordered) center += p;
    center *= .25f;
    const float factor = static_cast<float>(1.0 + 2.0 * margin);
    for (auto& p : ordered) p = center + (p - center) * factor;
    result.native_size = native_size(ordered);
    if (result.native_size.width >= 188 && result.native_size.height >= 48) {
        const cv::Mat native = warp(image, ordered, result.native_size, cv::BORDER_REPLICATE);
        cv::Mat output;
        cv::resize(native, output, {94, 24}, 0, 0, cv::INTER_AREA);
        result.sampling = "native_warp_then_area";
        return output;
    }
    result.sampling = "direct_linear_small_crop";
    return warp(image, ordered, {94, 24}, cv::BORDER_REPLICATE);
}
float round3(double value) { return static_cast<float>(std::nearbyint(value * 1000.0) / 1000.0); }
} // namespace

class Engine::Impl {
public:
    Impl(const std::string& detector, const std::string& recognizer)
        : detection(detector, 640, 640, 3), recognition(recognizer, 94, 24, 1) {}
    Runtime detection, recognition;
};
Engine::Engine(const std::string& detector_path, const std::string& recognizer_path)
    : impl_(new Impl(detector_path, recognizer_path)) {}
Engine::~Engine() = default;

FrameResult Engine::process(const cv::Mat& image_bgr, double margin)
{
    if (image_bgr.empty() || image_bgr.type() != CV_8UC3)
        throw std::runtime_error("Expected a nonempty BGR uint8 source image");
    if (!std::isfinite(margin) || margin < 0 || margin > .2)
        throw std::runtime_error("Margin must be in [0,0.2]");
    const double ratio = std::min(640.0 / image_bgr.rows, 640.0 / image_bgr.cols);
    const int new_height = static_cast<int>(image_bgr.rows * ratio);
    const int new_width = static_cast<int>(image_bgr.cols * ratio);
    if (new_width < 1 || new_height < 1) throw std::runtime_error("Image aspect ratio too extreme");
    const int top = (640 - new_height) / 2, left = (640 - new_width) / 2;
    cv::Mat resized, padded, rgb;
    cv::resize(image_bgr, resized, {new_width, new_height});
    cv::copyMakeBorder(resized, padded, top, 640 - new_height - top,
                       left, 640 - new_width - left, cv::BORDER_CONSTANT, {114, 114, 114});
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    FrameResult frame;
    const auto heads = impl_->detection.infer(rgb, frame.detection_call_ms);
    auto detections = decode_detector(detector_heads(heads));
    for (auto detection : detections) {
        for (int coordinate = 0; coordinate < 4; ++coordinate) {
            const bool x = coordinate % 2 == 0;
            const double value = (static_cast<double>(detection.box[coordinate]) - (x ? left : top)) / ratio;
            detection.box[coordinate] = round3(std::max(0.0, std::min(value, static_cast<double>((x ? image_bgr.cols : image_bgr.rows) - 1))));
        }
        if (detection.box[2] <= detection.box[0] || detection.box[3] <= detection.box[1]) continue;
        for (auto& point : detection.corners) {
            point.x = round3((static_cast<double>(point.x) - left) / ratio);
            point.y = round3((static_cast<double>(point.y) - top) / ratio);
        }
        PlateResult result;
        result.detection = detection;
        try {
            result.input_bgr = recognition_input(image_bgr, result, margin);
        } catch (const cv::Exception& error) {
            result.error = error.what();
        } catch (const std::runtime_error& error) {
            result.error = error.what();
        }
        if (result.error.empty()) {
            const auto output = impl_->recognition.infer(result.input_bgr, result.recognition_call_ms);
            result.recognition = decode_recognizer(output.at(0).values, class_major(output.at(0)));
        }
        frame.plates.push_back(std::move(result));
    }
    return frame;
}
} // namespace plate
