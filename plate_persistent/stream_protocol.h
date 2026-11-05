#pragma once
#include <istream>
#include <ostream>
#include <iomanip>
#include <stdexcept>
#include <string>

namespace plate_stream {
constexpr std::size_t max_line = 65536;
struct Request { std::string id, image, directory; };
inline std::string hex(const std::string& value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : value) {
        result += digits[c >> 4]; result += digits[c & 15];
    }
    return result;
}
inline int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::runtime_error("Invalid hex path");
}
inline std::string unhex(const std::string& value)
{
    if (value.empty() || value.size() % 2) throw std::runtime_error("Invalid hex path length");
    std::string result;
    for (std::size_t i = 0; i < value.size(); i += 2) {
        char c = static_cast<char>((nibble(value[i]) << 4) | nibble(value[i + 1]));
        if (!c) throw std::runtime_error("NUL in path");
        result += c;
    }
    if (result[0] != '/') throw std::runtime_error("Stream paths must be absolute");
    return result;
}
inline Request parse(const std::string& line)
{
    if (line.size() > max_line) throw std::runtime_error("Request too long");
    const auto first = line.find('\t'), last = line.rfind('\t');
    if (first == std::string::npos || first == last || first == 0 || first > 16)
        throw std::runtime_error("Expected numeric ID, hex image path, hex output directory");
    Request result;
    result.id = line.substr(0, first);
    for (char c : result.id) if (c < '0' || c > '9') throw std::runtime_error("Invalid request ID");
    result.image = unhex(line.substr(first + 1, last - first - 1));
    result.directory = unhex(line.substr(last + 1));
    return result;
}
// Bound input allocation even when a peer sends no newline. Partial EOF is invalid.
inline bool read_line(std::istream& input, std::string& line)
{
    line.clear();
    char c;
    while (input.get(c)) {
        if (c == '\n') return true;
        if (line.size() == max_line) throw std::runtime_error("Request too long");
        line += c;
    }
    if (!line.empty()) throw std::runtime_error("Truncated request");
    return false;
}
template<class Process>
int run(std::istream& input, std::ostream& output, Process process)
{
    output << "@PLATE_READY\t1" << std::endl;
    std::string line;
    while (read_line(input, line)) {
        if (line == "QUIT") return 0;
        const auto request = parse(line);
        try {
            const double ms = process(request);
            output << "@PLATE_DONE\t" << request.id << '\t' << std::fixed
                   << std::setprecision(3) << ms << std::endl;
        } catch (const std::exception& error) {
            output << "@PLATE_ERROR\t" << request.id << '\t' << hex(error.what()) << std::endl;
            return 1;
        }
    }
    return 0;
}
} // namespace plate_stream
