#include "ddsmon/util.hpp"

#include <time.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "ddsmon/types.hpp"

namespace ddsmon {

int64_t now_ns()
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int64_t mono_ns()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

std::string fmt_double(double v, int precision)
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
  return buf;
}

std::string fmt_ns_short(int64_t ns)
{
  double v = static_cast<double>(ns);
  if (ns < 1000) return fmt_double(v, 0) + "ns";
  if (ns < 1000LL * 1000) return fmt_double(v / 1000.0, 2) + "us";
  if (ns < 1000LL * 1000 * 1000) return fmt_double(v / 1e6, 2) + "ms";
  return fmt_double(v / 1e9, 2) + "s";
}

std::string fmt_ms(double ms)
{
  if (ms < 0) return "-";
  return fmt_double(ms, 2) + " ms";
}

std::string wallclock_str(int64_t ns)
{
  std::time_t sec = static_cast<std::time_t>(ns / 1000000000LL);
  int ms = static_cast<int>((ns % 1000000000LL) / 1000000LL);
  struct tm tmv;
  localtime_r(&sec, &tmv);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tmv.tm_year + 1900,
                tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
  return buf;
}

std::string wallclock_iso(int64_t ns)
{
  std::time_t sec = static_cast<std::time_t>(ns / 1000000000LL);
  int ms = static_cast<int>((ns % 1000000000LL) / 1000000LL);
  struct tm tmv;
  localtime_r(&sec, &tmv);
  char buf[80];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d+08:00", tmv.tm_year + 1900,
                tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
  return buf;
}

std::string make_report_id()
{
  int64_t ns = now_ns();
  std::string ts = wallclock_str(ns);
  for (char& c : ts) {
    if (c == '-' || c == ':' || c == ' ') c = '_';
    if (c == '.') c = '-';
  }
  return "RPT_" + ts;
}

std::string to_lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string trim(const std::string& s)
{
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::vector<std::string> split(const std::string& s, char sep)
{
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

bool starts_with(const std::string& s, const std::string& prefix)
{
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string severity_name(int severity)
{
  switch (severity) {
    case kSeverityCritical: return "CRITICAL";
    case kSeverityError: return "ERROR";
    case kSeverityWarn: return "WARN";
    case kSeverityInfo: return "INFO";
    default: return "UNKNOWN";
  }
}

std::string severity_color(int severity)
{
  switch (severity) {
    case kSeverityCritical: return "\033[1;35m";
    case kSeverityError: return "\033[1;31m";
    case kSeverityWarn: return "\033[1;33m";
    case kSeverityInfo: return "\033[1;36m";
    default: return "\033[0m";
  }
}

std::string repeat(char c, size_t n)
{
  return std::string(n, c);
}

std::string pad_right(const std::string& s, size_t width)
{
  // 中文字符按 2 列宽计算，保证表格对齐
  size_t display = 0;
  for (unsigned char c : s) {
    if (c < 0x80) {
      display += 1;
    } else if ((c & 0xC0) != 0x80) {
      display += 2;
    }
  }
  if (display >= width) return s;
  return s + repeat(' ', width - display);
}

std::string pad_left(const std::string& s, size_t width)
{
  size_t display = 0;
  for (unsigned char c : s) {
    if (c < 0x80) {
      display += 1;
    } else if ((c & 0xC0) != 0x80) {
      display += 2;
    }
  }
  if (display >= width) return s;
  return repeat(' ', width - display) + s;
}

std::string ellipsize(const std::string& s, size_t width)
{
  size_t display = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      display += 1;
    } else if ((c & 0xC0) != 0x80) {
      display += 2;
    }
    if (display > width) return s.substr(0, i) + "...";
  }
  return s;
}

std::string guid_str(const uint8_t* v16)
{
  // 必须保留完整 16 字节：同进程内多个 participant/endpoint 的 GUID 前缀相同，
  // 只取前几字节做 key 会让拓扑里的实体互相覆盖。
  char buf[64];
  std::snprintf(buf, sizeof(buf),
                "%02x%02x%02x%02x.%02x%02x%02x%02x.%02x%02x%02x%02x.%02x%02x%02x%02x",
                v16[0], v16[1], v16[2], v16[3], v16[4], v16[5], v16[6], v16[7], v16[8],
                v16[9], v16[10], v16[11], v16[12], v16[13], v16[14], v16[15]);
  return buf;
}

std::string severity_label(Severity s) { return severity_name(static_cast<int>(s)); }

Severity severity_from_int(int v)
{
  if (v >= 4) return Severity::Critical;
  if (v == 3) return Severity::Error;
  if (v == 2) return Severity::Warn;
  return Severity::Info;
}

}  // namespace ddsmon
