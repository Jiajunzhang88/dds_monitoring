#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "dds/dds.h"
}

namespace ddsmon {

enum class Severity : int {
  Info = 1,
  Warn = 2,
  Error = 3,
  Critical = 4,
};

std::string severity_label(Severity s);
Severity severity_from_int(int v);

/// 一条诊断结论
struct Issue {
  std::string code;
  Severity severity{Severity::Warn};
  std::string domain;
  std::string topic;
  std::string detail;
  double value{0.0};
  double threshold{0.0};
};

/// 消息类型适配器：把 IDL 生成结构中的关键字段抽出来，供通用监控使用
struct PayloadAdapter {
  std::string key;             ///< 配置里的短类型名，如 "ObjectList"
  std::string dds_type_name;   ///< DDS 类型名，如 "ddsmon::perception::ObjectList"
  const dds_topic_descriptor_t* desc{nullptr};
  uint32_t (*seq_of)(const void*){nullptr};
  int64_t (*stamp_of)(const void*){nullptr};   ///< 发布端时间戳（ns）
  int64_t (*origin_of)(const void*){nullptr};  ///< 跨域链路起点时间戳（ns），无则为 0
};

/// 按短类型名查找适配器；找不到返回 nullptr
const PayloadAdapter* find_adapter(const std::string& key);
const PayloadAdapter* find_adapter_by_dds_name(const std::string& dds_name);
std::vector<std::string> adapter_keys();

}  // namespace ddsmon
