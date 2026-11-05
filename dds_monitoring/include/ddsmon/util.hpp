#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace ddsmon {

/// CLOCK_REALTIME，纳秒
int64_t now_ns();

/// 单调时钟，纳秒（用于间隔/超时判断，不受系统时间跳变影响）
int64_t mono_ns();

std::string fmt_double(double v, int precision = 2);
std::string fmt_ns_short(int64_t ns);   ///< 1.23ms / 4.56s
std::string fmt_ms(double ms);          ///< 12.3 ms
std::string wallclock_str(int64_t ns);  ///< 2026-11-05 20:41:03.123
std::string wallclock_iso(int64_t ns);  ///< 2026-11-05T20:41:03.123+08:00
std::string make_report_id();

std::string to_lower(std::string s);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);
bool starts_with(const std::string& s, const std::string& prefix);

std::string severity_name(int severity);    ///< INFO / WARN / ERROR / CRITICAL
std::string severity_color(int severity);   ///< ANSI 颜色
std::string pad_right(const std::string& s, size_t width);
std::string pad_left(const std::string& s, size_t width);
std::string ellipsize(const std::string& s, size_t width);
std::string repeat(char c, size_t n);

/// 16 字节 GUID 转为可读短串，例如 "a1b2c3d4.5e6f"（取前 8 字节，够用于区分端点）
std::string guid_str(const uint8_t* v16);

constexpr int kSeverityInfo = 1;
constexpr int kSeverityWarn = 2;
constexpr int kSeverityError = 3;
constexpr int kSeverityCritical = 4;

}  // namespace ddsmon
