#include "ddsmon/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ddsmon {

namespace {

class Parser {
 public:
  Parser(const std::string& text) : s_(text) {}

  bool parse(Json* out, std::string* err)
  {
    skip();
    if (!parse_value(out)) {
      if (err) *err = err_.empty() ? ("unexpected token at offset " + std::to_string(pos_)) : err_;
      return false;
    }
    skip();
    if (pos_ != s_.size()) {
      if (err) *err = "trailing characters at offset " + std::to_string(pos_);
      return false;
    }
    return true;
  }

 private:
  void skip()
  {
    while (pos_ < s_.size()) {
      char c = s_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/') {
        while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
      } else {
        break;
      }
    }
  }

  bool fail(const std::string& msg)
  {
    if (err_.empty()) err_ = msg + " at offset " + std::to_string(pos_);
    return false;
  }

  bool parse_value(Json* out)
  {
    skip();
    if (pos_ >= s_.size()) return fail("unexpected end of input");
    char c = s_[pos_];
    switch (c) {
      case '{': return parse_object(out);
      case '[': return parse_array(out);
      case '"': {
        std::string str;
        if (!parse_string(&str)) return false;
        *out = Json(str);
        return true;
      }
      case 't':
        if (s_.compare(pos_, 4, "true") == 0) { pos_ += 4; *out = Json(true); return true; }
        return fail("invalid literal");
      case 'f':
        if (s_.compare(pos_, 5, "false") == 0) { pos_ += 5; *out = Json(false); return true; }
        return fail("invalid literal");
      case 'n':
        if (s_.compare(pos_, 4, "null") == 0) { pos_ += 4; *out = Json(); return true; }
        return fail("invalid literal");
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);
        return fail("unexpected character");
    }
  }

  bool parse_object(Json* out)
  {
    ++pos_;  // {
    *out = Json::object();
    skip();
    if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
    while (true) {
      skip();
      if (pos_ >= s_.size() || s_[pos_] != '"') return fail("expected object key");
      std::string key;
      if (!parse_string(&key)) return false;
      skip();
      if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected ':'");
      ++pos_;
      Json value;
      if (!parse_value(&value)) return false;
      out->set(key, std::move(value));
      skip();
      if (pos_ >= s_.size()) return fail("unterminated object");
      if (s_[pos_] == ',') { ++pos_; continue; }
      if (s_[pos_] == '}') { ++pos_; return true; }
      return fail("expected ',' or '}'");
    }
  }

  bool parse_array(Json* out)
  {
    ++pos_;  // [
    *out = Json::array();
    skip();
    if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
    while (true) {
      Json value;
      if (!parse_value(&value)) return false;
      out->push(std::move(value));
      skip();
      if (pos_ >= s_.size()) return fail("unterminated array");
      if (s_[pos_] == ',') { ++pos_; continue; }
      if (s_[pos_] == ']') { ++pos_; return true; }
      return fail("expected ',' or ']'");
    }
  }

  bool parse_string(std::string* out)
  {
    ++pos_;  // "
    out->clear();
    while (pos_ < s_.size()) {
      char c = s_[pos_++];
      if (c == '"') return true;
      if (c == '\\') {
        if (pos_ >= s_.size()) return fail("bad escape");
        char e = s_[pos_++];
        switch (e) {
          case 'n': out->push_back('\n'); break;
          case 't': out->push_back('\t'); break;
          case 'r': out->push_back('\r'); break;
          case 'b': out->push_back('\b'); break;
          case 'f': out->push_back('\f'); break;
          case '"': out->push_back('"'); break;
          case '\\': out->push_back('\\'); break;
          case '/': out->push_back('/'); break;
          case 'u': {
            if (pos_ + 4 > s_.size()) return fail("bad unicode escape");
            unsigned code = static_cast<unsigned>(std::strtoul(s_.substr(pos_, 4).c_str(), nullptr, 16));
            pos_ += 4;
            if (code < 0x80) {
              out->push_back(static_cast<char>(code));
            } else {
              out->push_back(static_cast<char>(0xE0 | (code >> 12)));
              out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
          }
          default: return fail("unsupported escape");
        }
      } else {
        out->push_back(c);
      }
    }
    return fail("unterminated string");
  }

  bool parse_number(Json* out)
  {
    size_t start = pos_;
    if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
    while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    if (pos_ < s_.size() && s_[pos_] == '.') {
      ++pos_;
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    if (start == pos_) return fail("invalid number");
    *out = Json(std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr));
    return true;
  }

  const std::string& s_;
  size_t pos_{0};
  std::string err_;
};

}  // namespace

void append_escaped(std::string* out, const std::string& s)
{
  for (char c : s) {
    switch (c) {
      case '"': out->append("\\\""); break;
      case '\\': out->append("\\\\"); break;
      case '\n': out->append("\\n"); break;
      case '\t': out->append("\\t"); break;
      case '\r': out->append("\\r"); break;
      default: out->push_back(c);
    }
  }
}

std::string json_escape(const std::string& s)
{
  std::string out;
  append_escaped(&out, s);
  return out;
}

Json Json::object()
{
  Json j;
  j.type_ = Type::Object;
  return j;
}

Json Json::array()
{
  Json j;
  j.type_ = Type::Array;
  return j;
}

bool Json::as_bool(bool def) const { return type_ == Type::Bool ? bool_ : def; }

double Json::as_number(double def) const
{
  if (type_ == Type::Number) return num_;
  if (type_ == Type::Bool) return bool_ ? 1.0 : 0.0;
  return def;
}

long long Json::as_int(long long def) const
{
  if (type_ == Type::Number) return static_cast<long long>(num_);
  return def;
}

std::string Json::as_string(const std::string& def) const
{
  return type_ == Type::String ? str_ : def;
}

Json& Json::set(const std::string& key, Json value)
{
  if (type_ != Type::Object) {
    type_ = Type::Object;
  }
  for (auto& kv : obj_) {
    if (kv.first == key) {
      kv.second = std::move(value);
      return *this;
    }
  }
  obj_.emplace_back(key, std::move(value));
  return *this;
}

Json& Json::push(Json value)
{
  if (type_ != Type::Array) {
    type_ = Type::Array;
  }
  arr_.push_back(std::move(value));
  return *this;
}

const Json* Json::find(const std::string& key) const
{
  if (type_ != Type::Object) return nullptr;
  for (const auto& kv : obj_) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

double Json::get_number(const std::string& key, double def) const
{
  const Json* v = find(key);
  return v ? v->as_number(def) : def;
}

long long Json::get_int(const std::string& key, long long def) const
{
  const Json* v = find(key);
  return v ? v->as_int(def) : def;
}

std::string Json::get_string(const std::string& key, const std::string& def) const
{
  const Json* v = find(key);
  return v ? v->as_string(def) : def;
}

bool Json::get_bool(const std::string& key, bool def) const
{
  const Json* v = find(key);
  return v ? v->as_bool(def) : def;
}

std::string Json::dump(int indent) const { return dump_impl(indent, 0); }

std::string Json::dump_impl(int indent, int level) const
{
  std::string pad;
  std::string pad_inner;
  if (indent > 0) {
    pad = std::string(static_cast<size_t>(indent) * level, ' ');
    pad_inner = std::string(static_cast<size_t>(indent) * (level + 1), ' ');
  }
  const bool pretty = indent > 0;
  switch (type_) {
    case Type::Null: return "null";
    case Type::Bool: return bool_ ? "true" : "false";
    case Type::Number: {
      char buf[64];
      if (num_ == static_cast<long long>(num_) && std::fabs(num_) < 1e15) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(num_));
      } else {
        std::snprintf(buf, sizeof(buf), "%.6g", num_);
      }
      return buf;
    }
    case Type::String: {
      std::string out = "\"";
      append_escaped(&out, str_);
      out += "\"";
      return out;
    }
    case Type::Array: {
      if (arr_.empty()) return "[]";
      std::string out = "[";
      for (size_t i = 0; i < arr_.size(); ++i) {
        if (i) out += ",";
        if (pretty) out += "\n" + pad_inner;
        out += arr_[i].dump_impl(indent, level + 1);
      }
      if (pretty) out += "\n" + pad;
      out += "]";
      return out;
    }
    case Type::Object: {
      if (obj_.empty()) return "{}";
      std::string out = "{";
      for (size_t i = 0; i < obj_.size(); ++i) {
        if (i) out += ",";
        if (pretty) out += "\n" + pad_inner;
        out += "\"" + json_escape(obj_[i].first) + "\":";
        if (pretty) out += " ";
        out += obj_[i].second.dump_impl(indent, level + 1);
      }
      if (pretty) out += "\n" + pad;
      out += "}";
      return out;
    }
  }
  return "null";
}

bool Json::parse(const std::string& text, Json* out, std::string* err)
{
  Parser p(text);
  return p.parse(out, err);
}

}  // namespace ddsmon
