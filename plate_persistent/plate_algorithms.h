#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace plate {
struct Point { float x = 0, y = 0; };
struct Detection {
    std::array<float, 4> box{};
    std::array<Point, 4> corners{};
    float confidence = 0, objectness = 0;
    int layout = 0;
    std::size_t source_index = 0;
};
struct Head {
    int side = 0;
    bool channel_major = true;
    std::vector<float> values;
};
inline float sigmoid(float value)
{
    return 1.f / (1.f + std::exp(-std::max(-80.f, std::min(80.f, value))));
}
inline float overlap(const Detection& a, const Detection& b)
{
    const float width = std::max(0.f, std::min(a.box[2], b.box[2]) - std::max(a.box[0], b.box[0]));
    const float height = std::max(0.f, std::min(a.box[3], b.box[3]) - std::max(a.box[1], b.box[1]));
    const float area_a = std::max(0.f, a.box[2] - a.box[0]) * std::max(0.f, a.box[3] - a.box[1]);
    const float area_b = std::max(0.f, b.box[2] - b.box[0]) * std::max(0.f, b.box[3] - b.box[1]);
    return width * height / std::max(1e-12f, area_a + area_b - width * height);
}
inline std::vector<Detection> suppress(std::vector<Detection> candidates)
{
    std::stable_sort(candidates.begin(), candidates.end(), [](const Detection& a, const Detection& b) {
        if (a.confidence != b.confidence) return a.confidence > b.confidence;
        return a.source_index > b.source_index;
    });
    std::vector<Detection> kept;
    for (const auto& candidate : candidates) {
        bool duplicate = false;
        for (const auto& previous : kept)
            if (overlap(candidate, previous) > .5f) { duplicate = true; break; }
        if (!duplicate) kept.push_back(candidate);
    }
    return kept;
}
inline std::vector<Detection> decode_detector(const std::vector<Head>& heads)
{
    if (heads.size() != 3) throw std::runtime_error("Expected three detector heads");
    const int sides[3] = {80, 40, 20};
    const float anchors[3][3][2] = {{{4, 5}, {8, 10}, {13, 16}},
                                  {{23, 29}, {43, 55}, {73, 105}},
                                  {{146, 217}, {231, 300}, {335, 433}}};
    std::vector<Detection> candidates;
    std::size_t source_index = 0;
    for (int level = 0; level < 3; ++level) {
        const auto found = std::find_if(heads.begin(), heads.end(), [&](const Head& h) { return h.side == sides[level]; });
        if (found == heads.end()) throw std::runtime_error("Missing detector head size");
        const Head& head = *found;
        const int side = head.side;
        const float stride = static_cast<float>(640 / side);
        if (head.values.size() != static_cast<std::size_t>(45 * side * side))
            throw std::runtime_error("Detector head element count differs");
        for (float value : head.values)
            if (!std::isfinite(value)) throw std::runtime_error("Nonfinite detector output");
        auto at = [&](int anchor, int feature, int y, int x) {
            const int channel = anchor * 15 + feature;
            const std::size_t index = head.channel_major
                ? static_cast<std::size_t>((channel * side + y) * side + x)
                : static_cast<std::size_t>((y * side + x) * 45 + channel);
            return head.values[index];
        };
        for (int anchor = 0; anchor < 3; ++anchor)
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x, ++source_index) {
                    const float objectness = sigmoid(at(anchor, 4, y, x));
                    if (objectness <= .3f) continue;
                    const float cx = (sigmoid(at(anchor, 0, y, x)) * 2.f - .5f + x) * stride;
                    const float cy = (sigmoid(at(anchor, 1, y, x)) * 2.f - .5f + y) * stride;
                    const float w = sigmoid(at(anchor, 2, y, x)) * 2.f;
                    const float h = sigmoid(at(anchor, 3, y, x)) * 2.f;
                    const float width = w * w * anchors[level][anchor][0];
                    const float height = h * h * anchors[level][anchor][1];
                    const float single = sigmoid(at(anchor, 13, y, x));
                    const float dual = sigmoid(at(anchor, 14, y, x));
                    Detection detection;
                    detection.box = {cx - width / 2.f, cy - height / 2.f,
                                     cx + width / 2.f, cy + height / 2.f};
                    detection.objectness = objectness;
                    detection.confidence = objectness * std::max(single, dual);
                    detection.layout = dual > single ? 1 : 0;
                    detection.source_index = source_index;
                    for (int point = 0; point < 4; ++point) {
                        detection.corners[point].x = at(anchor, 5 + point * 2, y, x) * anchors[level][anchor][0] + x * stride;
                        detection.corners[point].y = at(anchor, 6 + point * 2, y, x) * anchors[level][anchor][1] + y * stride;
                    }
                    candidates.push_back(detection);
                }
    }
    return suppress(std::move(candidates));
}
inline const std::array<const char*, 68>& characters()
{
    static const std::array<const char*, 68> chars = {{
        u8"京",u8"沪",u8"津",u8"渝",u8"冀",u8"晋",u8"蒙",u8"辽",u8"吉",u8"黑",
        u8"苏",u8"浙",u8"皖",u8"闽",u8"赣",u8"鲁",u8"豫",u8"鄂",u8"湘",u8"粤",
        u8"桂",u8"琼",u8"川",u8"贵",u8"云",u8"藏",u8"陕",u8"甘",u8"青",u8"宁",
        u8"新","0","1","2","3","4","5","6","7","8","9",
        "A","B","C","D","E","F","G","H","J","K","L","M","N","P",
        "Q","R","S","T","U","V","W","X","Y","Z","I","O","-"}};
    return chars;
}
struct Decoded {
    std::string text;
    std::vector<int> frame_indices, symbols;
    bool complete = false;
    std::string reason;
};
inline Decoded decode_recognizer(const std::vector<float>& values, bool class_major)
{
    if (values.size() != 68 * 18) throw std::runtime_error("Expected 68x18 recognition scores");
    Decoded result;
    int previous = -1;
    for (int time = 0; time < 18; ++time) {
        int best = 0;
        float score = -std::numeric_limits<float>::infinity();
        for (int cls = 0; cls < 68; ++cls) {
            const float value = values[class_major ? cls * 18 + time : time * 68 + cls];
            if (!std::isfinite(value)) throw std::runtime_error("Nonfinite recognition output");
            if (value > score) { best = cls; score = value; }
        }
        result.frame_indices.push_back(best);
        if (best != 67 && best != previous) {
            result.symbols.push_back(best);
            result.text += characters()[best];
        }
        previous = best;
    }
    if (result.symbols.size() != 7 && result.symbols.size() != 8)
        result.reason = "Expected 7 or 8 symbols for the current ordinary-plate model";
    else if (result.symbols[0] > 30 || result.symbols[1] < 41 || result.symbols[1] > 66)
        result.reason = "Province or second-letter position needs review";
    else if (std::any_of(result.symbols.begin() + 2, result.symbols.end(), [](int index) { return index < 31 || index > 66; }))
        result.reason = "Unexpected symbol in the alphanumeric suffix";
    else result.complete = true;
    return result;
}
} // namespace plate
