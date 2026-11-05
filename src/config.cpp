#include "ddsmon/config.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "ddsmon/json.hpp"
#include "ddsmon/types.hpp"
#include "ddsmon/util.hpp"

namespace ddsmon {

namespace {

bool read_file(const std::string& path, std::string* out, std::string* err)
{
  std::ifstream in(path);
  if (!in) {
    if (err) *err = "cannot open config file: " + path;
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

void parse_rules(const Json& j, RuleCfg* r)
{
  r->rate_low_ratio = j.get_number("rate_low_ratio", r->rate_low_ratio);
  r->rate_critical_ratio = j.get_number("rate_critical_ratio", r->rate_critical_ratio);
  r->latency_warn_ms = j.get_number("latency_warn_ms", r->latency_warn_ms);
  r->latency_error_ms = j.get_number("latency_error_ms", r->latency_error_ms);
  r->jitter_warn_ratio = j.get_number("jitter_warn_ratio", r->jitter_warn_ratio);
  r->loss_warn = j.get_number("loss_warn", r->loss_warn);
  r->loss_error = j.get_number("loss_error", r->loss_error);
  r->stale_warn_periods = j.get_number("stale_warn_periods", r->stale_warn_periods);
  r->no_publisher_sec = j.get_number("no_publisher_sec", r->no_publisher_sec);
  r->no_consumer_sec = j.get_number("no_consumer_sec", r->no_consumer_sec);
  r->chain_warn_ms = j.get_number("chain_warn_ms", r->chain_warn_ms);
  r->chain_error_ms = j.get_number("chain_error_ms", r->chain_error_ms);
  r->heartbeat_lease_sec = j.get_number("heartbeat_lease_sec", r->heartbeat_lease_sec);
  r->debounce_windows = static_cast<int>(j.get_int("debounce_windows", r->debounce_windows));
  r->eval_interval_ms = static_cast<int>(j.get_int("eval_interval_ms", r->eval_interval_ms));
}

}  // namespace

Config Config::defaults()
{
  Config c;
  c.description = "ADAS DDS 通信诊断默认配置：感知域 / 规控域 / 监控诊断域";

  DomainCfg perception;
  perception.name = "perception";
  perception.domain_id = 10;
  perception.topics = {
      TopicCfg{"rt/perception/camera_front", "CameraFrame", 30.0, true, true},
      TopicCfg{"rt/perception/object_list", "ObjectList", 30.0, true, true},
      TopicCfg{"rt/perception/lidar_top", "LidarCloud", 10.0, true, false},
  };

  DomainCfg planning;
  planning.name = "planning";
  planning.domain_id = 20;
  planning.topics = {
      TopicCfg{"rt/perception/obstacle_list", "ObstacleList", 30.0, true, true},
      TopicCfg{"rt/planning/trajectory", "Trajectory", 10.0, true, true},
      TopicCfg{"rt/planning/control_cmd", "ControlCommand", 50.0, true, true},
      TopicCfg{"rt/planning/vehicle_state", "VehicleState", 100.0, true, false},
  };

  DomainCfg diag;
  diag.name = "diag";
  diag.domain_id = 30;
  diag.topics = {
      TopicCfg{"rt/diag/heartbeat", "Heartbeat", 1.0, false, true},
  };

  c.domains = {perception, planning, diag};
  c.nodes = {
      NodeCfg{"perception_node", "perception", 3.0},
      NodeCfg{"planning_node", "planning", 3.0},
      NodeCfg{"diag_node", "diag", 3.0},
  };
  return c;
}

bool Config::load(const std::string& path, Config* out, std::string* err)
{
  std::string text;
  if (!read_file(path, &text, err)) return false;

  Json root;
  std::string perr;
  if (!Json::parse(text, &root, &perr)) {
    if (err) *err = "config parse error: " + perr;
    return false;
  }
  if (!root.is_object()) {
    if (err) *err = "config root must be an object";
    return false;
  }

  Config cfg = defaults();
  cfg.name = root.get_string("name", cfg.name);
  cfg.description = root.get_string("description", cfg.description);

  if (const Json* diag = root.find("diag"); diag && diag->is_object()) {
    cfg.diag_domain = diag->get_string("domain", cfg.diag_domain);
    cfg.report_topic = diag->get_string("report_topic", cfg.report_topic);
    cfg.event_topic = diag->get_string("event_topic", cfg.event_topic);
    cfg.heartbeat_topic = diag->get_string("heartbeat_topic", cfg.heartbeat_topic);
  }

  if (const Json* rules = root.find("rules"); rules && rules->is_object()) {
    parse_rules(*rules, &cfg.rules);
  }

  const Json* domains = root.find("domains");
  if (domains && domains->is_array()) {
    cfg.domains.clear();
    for (const Json& d : domains->items()) {
      DomainCfg dc;
      dc.name = d.get_string("name", "");
      dc.domain_id = static_cast<int>(d.get_int("domain_id", -1));
      if (dc.name.empty()) {
        if (err) *err = "domain entry missing name";
        return false;
      }
      if (dc.domain_id < 0 || dc.domain_id > 232) {
        if (err) *err = "domain " + dc.name + " has invalid domain_id";
        return false;
      }
      const Json* topics = d.find("topics");
      if (topics && topics->is_array()) {
        for (const Json& t : topics->items()) {
          TopicCfg tc;
          tc.name = t.get_string("name", "");
          tc.type = t.get_string("type", "");
          tc.expect_hz = t.get_number("expect_hz", 0.0);
          tc.consumer_required = t.get_bool("consumer_required", true);
          tc.critical = t.get_bool("critical", true);
          if (tc.name.empty() || tc.type.empty()) {
            if (err) *err = "topic entry in domain " + dc.name + " missing name/type";
            return false;
          }
          if (tc.expect_hz <= 0.0) {
            if (err) *err = "topic " + tc.name + " has non-positive expect_hz";
            return false;
          }
          if (find_adapter(tc.type) == nullptr) {
            if (err) *err = "topic " + tc.name + " uses unsupported type '" + tc.type + "'";
            return false;
          }
          dc.topics.push_back(tc);
        }
      }
      cfg.domains.push_back(dc);
    }
  }

  if (cfg.domains.empty()) {
    if (err) *err = "config has no domains";
    return false;
  }

  const Json* nodes = root.find("nodes");
  if (nodes && nodes->is_array()) {
    cfg.nodes.clear();
    for (const Json& n : nodes->items()) {
      NodeCfg nc;
      nc.node_id = n.get_string("node_id", "");
      nc.domain = n.get_string("domain", "");
      nc.lease_sec = n.get_number("lease_sec", cfg.rules.heartbeat_lease_sec);
      if (!nc.node_id.empty()) cfg.nodes.push_back(nc);
    }
  }

  *out = cfg;
  return true;
}

const DomainCfg* Config::domain_by_name(const std::string& name) const
{
  for (const auto& d : domains) {
    if (d.name == name) return &d;
  }
  return nullptr;
}

const DomainCfg* Config::diag() const { return domain_by_name(diag_domain); }

}  // namespace ddsmon
