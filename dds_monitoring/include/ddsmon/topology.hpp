#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include "dds/dds.h"
}

#include "ddsmon/qos_model.hpp"

namespace ddsmon {

struct EndpointInfo {
  std::string guid;
  std::string participant_guid;
  std::string topic;
  std::string type_name;
  QosInfo qos;
  int64_t first_seen_ns{0};
  int64_t last_seen_ns{0};
  bool alive{false};
};

struct ParticipantInfo {
  std::string guid;
  int64_t first_seen_ns{0};
  int64_t last_seen_ns{0};
  bool alive{false};
};

/// 域内拓扑：从 Cyclone DDS 内置主题（DcpsParticipant / DcpsPublication /
/// DcpsSubscription）采集，可看到任意第三方 DDS 应用的端点与 QoS。
class Topology {
 public:
  void on_participant(const dds_builtintopic_participant_t& p, const dds_sample_info_t& si, int64_t now);
  void on_endpoint(const dds_builtintopic_endpoint_t& e, const dds_sample_info_t& si, int64_t now,
                   bool is_writer);

  int participants_alive() const;
  int writers_alive() const;
  int readers_alive() const;

  std::vector<EndpointInfo> writers() const;
  std::vector<EndpointInfo> readers() const;

  /// 话题名 → 发布者/订阅者
  int publishers_on(const std::string& topic) const;
  int subscribers_on(const std::string& topic) const;
  /// 该话题上出现的类型名集合
  std::vector<std::string> types_on(const std::string& topic) const;
  std::vector<std::string> topic_names() const;

 private:
  std::map<std::string, ParticipantInfo> participants_;
  std::map<std::string, EndpointInfo> writers_;
  std::map<std::string, EndpointInfo> readers_;
};

}  // namespace ddsmon
