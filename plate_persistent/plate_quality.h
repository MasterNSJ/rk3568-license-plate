#pragma once
#include "plate_algorithms.h"
#include <cstdlib>

namespace plate {
// Thresholds are experiment parameters, not calibrated correctness probabilities.
struct QualityConfig {
    bool enabled = true;
    float min_score = .30f;
    float min_width = 16.f, min_height = 6.f;
};
inline double environment_number(const char* name, double fallback, double low, double high)
{
    const char* raw = std::getenv(name);
    if (!raw || !*raw) return fallback;
    char* end = nullptr;
    const double value = std::strtod(raw, &end);
    if (end == raw || *end || !std::isfinite(value) || value < low || value > high)
        throw std::runtime_error(std::string("Invalid environment option: ") + name);
    return value;
}
inline QualityConfig quality_from_environment()
{
    QualityConfig config;
    config.enabled = environment_number("PLATE_FILTER_ENABLED", 1, 0, 1) != 0;
    config.min_score = static_cast<float>(environment_number("PLATE_MIN_SCORE", .30, 0, 1));
    config.min_width = static_cast<float>(environment_number("PLATE_MIN_WIDTH", 16, 0, 4096));
    config.min_height = static_cast<float>(environment_number("PLATE_MIN_HEIGHT", 6, 0, 4096));
    return config;
}
inline std::string quality_reason(const Detection& d, int width, int height,
                                  const QualityConfig& config)
{
    if (!config.enabled) return "";
    if (!std::isfinite(d.confidence) || d.confidence < config.min_score)
        return "score_below_threshold";
    for (float v : d.box) if (!std::isfinite(v)) return "nonfinite_box";
    if (d.box[2] - d.box[0] < config.min_width || d.box[3] - d.box[1] < config.min_height)
        return "crop_below_minimum_size";
    float area = 0, sign = 0;
    for (int i = 0; i < 4; ++i) {
        const auto a = d.corners[i], b = d.corners[(i + 1) % 4], c = d.corners[(i + 2) % 4];
        if (!std::isfinite(a.x) || !std::isfinite(a.y)) return "nonfinite_corner";
        if (a.x < 0 || a.y < 0 || a.x > width - 1 || a.y > height - 1)
            return "corner_outside_image";
        const float cross = (b.x-a.x)*(c.y-b.y) - (b.y-a.y)*(c.x-b.x);
        if (std::abs(cross) < .001f || (sign != 0 && cross * sign <= 0))
            return "invalid_corner_order_or_nonconvex";
        sign = cross;
        area += a.x*b.y - b.x*a.y;
    }
    return std::abs(area) < 4 ? "degenerate_quadrilateral" : "";
}
} // namespace plate
