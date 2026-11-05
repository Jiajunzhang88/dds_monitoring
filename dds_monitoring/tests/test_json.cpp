#include <cassert>
#include <iostream>

#include "ddsmon/json.hpp"

using ddsmon::Json;

int main()
{
  std::string err;
  Json root;
  const std::string text = R"({
    "name": "demo",
    "domains": [{"name": "perception", "domain_id": 10}, {"name": "planning", "domain_id": 20}],
    "enabled": true,
    "ratio": 0.75,
    "empty": [],
    "nested": {"a": {"b": 2}}
  })";
  assert(Json::parse(text, &root, &err));
  assert(root.get_string("name", "") == "demo");
  assert(root.get_bool("enabled", false));
  assert(root.get_number("ratio", 0) == 0.75);

  const Json* domains = root.find("domains");
  assert(domains != nullptr);
    assert(domains->is_array());
  assert(domains->items().size() == 2);
  assert(domains->items()[0].get_int("domain_id", 0) == 10);

  const Json* nested = root.find("nested");
  assert(nested != nullptr);
  assert(nested->find("a")->find("b")->as_int() == 2);

  assert(root.find("empty")->is_array());
  assert(root.find("empty")->items().empty());

  // 序列化 → 再解析，数值与字符串保持一致
  std::string dumped = root.dump(2);
  Json again;
  assert(Json::parse(dumped, &again, &err));
  assert(again.get_string("name", "") == "demo");
  assert(again.find("domains")->items().size() == 2);

  // 转义
  Json s = Json::object();
  s.set("q", Json("line\nwith \"quote\""));
  Json parsed;
  assert(Json::parse(s.dump(0), &parsed, &err));
  assert(parsed.get_string("q", "") == "line\nwith \"quote\"");

  // 非法输入
  Json bad;
  assert(!Json::parse("{\"a\": }", &bad, &err));
  assert(!Json::parse("[1,2", &bad, &err));

  std::cout << "test_json ok\n";
  return 0;
}
