#include "ddsmon/topology.hpp"

#include <algorithm>
#include <set>

#include "ddsmon/util.hpp"

namespace ddsmon {

namespace {

bool sample_alive(const dds_sample_info_t& si)
{
  return si.valid_data && si.instance_state == DDS_ALIVE_INSTANCE_STATE;
}

}  // namespace

void Topology::on_participant(const dds_builtintopic_participant_t& p, const dds_sample_info_t& si,
                              int64_t now)
{
  std::string key = guid_str(p.key.v);
  if (!sample_alive(si)) {
    participants_.erase(key);
    return;
  }
  auto it = participants_.find(key);
  if (it == participants_.end()) {
    ParticipantInfo info;
    info.guid = key;
    info.first_seen_ns = now;
    info.last_seen_ns = now;
    info.alive = true;
    participants_[key] = info;
  } else {
    it->second.last_seen_ns = now;
    it->second.alive = true;
  }
}

void Topology::on_endpoint(const dds_builtintopic_endpoint_t& e, const dds_sample_info_t& si,
                           int64_t now, bool is_writer)
{
  std::string key = guid_str(e.key.v);
  auto& map = is_writer ? writers_ : readers_;
  if (!sample_alive(si)) {
    map.erase(key);
    return;
  }

  EndpointInfo info;
  info.guid = key;
  info.participant_guid = guid_str(e.participant_key.v);
  info.topic = e.topic_name ? e.topic_name : "";
  info.type_name = e.type_name ? e.type_name : "";
  info.qos = read_qos(e.qos);
  info.last_seen_ns = now;
  info.alive = true;

  auto it = map.find(key);
  if (it == map.end()) {
    info.first_seen_ns = now;
    map[key] = info;
  } else {
    info.first_seen_ns = it->second.first_seen_ns;
    it->second = info;
  }
}

int Topology::participants_alive() const
{
  int n = 0;
  for (const auto& kv : participants_) {
    if (kv.second.alive) ++n;
  }
  return n;
}

int Topology::writers_alive() const
{
  int n = 0;
  for (const auto& kv : writers_) {
    if (kv.second.alive) ++n;
  }
  return n;
}

int Topology::readers_alive() const
{
  int n = 0;
  for (const auto& kv : readers_) {
    if (kv.second.alive) ++n;
  }
  return n;
}

std::vector<EndpointInfo> Topology::writers() const
{
  std::vector<EndpointInfo> out;
  for (const auto& kv : writers_) out.push_back(kv.second);
  return out;
}

std::vector<EndpointInfo> Topology::readers() const
{
  std::vector<EndpointInfo> out;
  for (const auto& kv : readers_) out.push_back(kv.second);
  return out;
}

int Topology::publishers_on(const std::string& topic) const
{
  int n = 0;
  for (const auto& kv : writers_) {
    if (kv.second.alive && kv.second.topic == topic) ++n;
  }
  return n;
}

int Topology::subscribers_on(const std::string& topic) const
{
  int n = 0;
  for (const auto& kv : readers_) {
    if (kv.second.alive && kv.second.topic == topic) ++n;
  }
  return n;
}

std::vector<std::string> Topology::types_on(const std::string& topic) const
{
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const auto& kv : writers_) {
    if (kv.second.alive && kv.second.topic == topic && !kv.second.type_name.empty()) {
      if (seen.insert(kv.second.type_name).second) out.push_back(kv.second.type_name);
    }
  }
  for (const auto& kv : readers_) {
    if (kv.second.alive && kv.second.topic == topic && !kv.second.type_name.empty()) {
      if (seen.insert(kv.second.type_name).second) out.push_back(kv.second.type_name);
    }
  }
  return out;
}

std::vector<std::string> Topology::topic_names() const
{
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const auto& kv : writers_) {
    if (kv.second.alive && seen.insert(kv.second.topic).second) out.push_back(kv.second.topic);
  }
  for (const auto& kv : readers_) {
    if (kv.second.alive && seen.insert(kv.second.topic).second) out.push_back(kv.second.topic);
  }
  return out;
}

}  // namespace ddsmon
