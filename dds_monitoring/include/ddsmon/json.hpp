#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ddsmon {

/// 极简 JSON DOM：用于读取 config、输出诊断报告。
/// 只支持 JSON 标准子集：对象/数组/字符串/数字/真假/null，
/// 不做转义序列的语义处理（配置里不需要），但写入时会正确转义。
class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json() = default;
  Json(bool v) : type_(Type::Bool), bool_(v) {}
  Json(double v) : type_(Type::Number), num_(v) {}
  Json(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(const char* v) : type_(Type::String), str_(v) {}
  Json(std::string v) : type_(Type::String), str_(std::move(v)) {}

  static Json object();
  static Json array();

  Type type() const { return type_; }
  bool is_object() const { return type_ == Type::Object; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }

  bool as_bool(bool def = false) const;
  double as_number(double def = 0.0) const;
  long long as_int(long long def = 0) const;
  std::string as_string(const std::string& def = "") const;

  /// 对象写入（会覆盖同名键）
  Json& set(const std::string& key, Json value);
  /// 数组追加
  Json& push(Json value);

  /// 只读访问：不存在返回 nullptr
  const Json* find(const std::string& key) const;
  /// 便捷读取（缺失或类型不符返回默认值）
  double get_number(const std::string& key, double def) const;
  long long get_int(const std::string& key, long long def) const;
  std::string get_string(const std::string& key, const std::string& def) const;
  bool get_bool(const std::string& key, bool def) const;

  const std::vector<Json>& items() const { return arr_; }
  const std::vector<std::pair<std::string, Json>>& members() const { return obj_; }

  /// 序列化，indent<=0 输出紧凑单行
  std::string dump(int indent = 0) const;
  /// 解析；失败返回 false 并填充 err
  static bool parse(const std::string& text, Json* out, std::string* err);

 private:
  Type type_{Type::Null};
  bool bool_{false};
  double num_{0};
  std::string str_;
  std::vector<Json> arr_;
  std::vector<std::pair<std::string, Json>> obj_;

  std::string dump_impl(int indent, int level) const;
};

std::string json_escape(const std::string& s);

}  // namespace ddsmon
