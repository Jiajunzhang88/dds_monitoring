#include "ddsmon/types.hpp"

#include <algorithm>
#include <cstring>
#include <map>

#include "ddsmon/util.hpp"
#include "ddsmon.h"  // idlc 生成的 C 类型

namespace ddsmon {

namespace {

template <typename T>
uint32_t seq_of(const void* p)
{
  return static_cast<const T*>(p)->seq;
}

template <typename T>
int64_t stamp_of(const void* p)
{
  return static_cast<const T*>(p)->stamp_ns;
}

int64_t null_stamp(const void*) { return 0; }
uint32_t null_seq(const void*) { return 0; }

int64_t obstacle_origin(const void* p)
{
  return static_cast<const ddsmon_planning_ObstacleList*>(p)->source_stamp_ns;
}

struct AdapterTable {
  std::vector<PayloadAdapter> items;

  AdapterTable()
  {
    using P = PayloadAdapter;
    items = {
        P{"CameraFrame", "ddsmon::perception::CameraFrame", &ddsmon_perception_CameraFrame_desc,
          seq_of<ddsmon_perception_CameraFrame>, stamp_of<ddsmon_perception_CameraFrame>, nullptr},
        P{"ObjectList", "ddsmon::perception::ObjectList", &ddsmon_perception_ObjectList_desc,
          seq_of<ddsmon_perception_ObjectList>, stamp_of<ddsmon_perception_ObjectList>, nullptr},
        P{"LidarCloud", "ddsmon::perception::LidarCloud", &ddsmon_perception_LidarCloud_desc,
          seq_of<ddsmon_perception_LidarCloud>, stamp_of<ddsmon_perception_LidarCloud>, nullptr},
        P{"ObstacleList", "ddsmon::planning::ObstacleList", &ddsmon_planning_ObstacleList_desc,
          seq_of<ddsmon_planning_ObstacleList>, stamp_of<ddsmon_planning_ObstacleList>,
          obstacle_origin},
        P{"Trajectory", "ddsmon::planning::Trajectory", &ddsmon_planning_Trajectory_desc,
          seq_of<ddsmon_planning_Trajectory>, stamp_of<ddsmon_planning_Trajectory>, nullptr},
        P{"ControlCommand", "ddsmon::planning::ControlCommand", &ddsmon_planning_ControlCommand_desc,
          seq_of<ddsmon_planning_ControlCommand>, stamp_of<ddsmon_planning_ControlCommand>, nullptr},
        P{"VehicleState", "ddsmon::planning::VehicleState", &ddsmon_planning_VehicleState_desc,
          seq_of<ddsmon_planning_VehicleState>, stamp_of<ddsmon_planning_VehicleState>, nullptr},
        P{"Heartbeat", "ddsmon::diag::Heartbeat", &ddsmon_diag_Heartbeat_desc,
          seq_of<ddsmon_diag_Heartbeat>, stamp_of<ddsmon_diag_Heartbeat>, nullptr},
        P{"TopicMetric", "ddsmon::diag::TopicMetric", &ddsmon_diag_TopicMetric_desc, null_seq,
          null_stamp, nullptr},
        P{"DiagReport", "ddsmon::diag::DiagReport", &ddsmon_diag_DiagReport_desc, null_seq,
          stamp_of<ddsmon_diag_DiagReport>, nullptr},
        P{"DiagEvent", "ddsmon::diag::DiagEvent", &ddsmon_diag_DiagEvent_desc, null_seq,
          stamp_of<ddsmon_diag_DiagEvent>, nullptr},
    };
  }
};

const AdapterTable& table()
{
  static AdapterTable t;
  return t;
}

}  // namespace

const PayloadAdapter* find_adapter(const std::string& key)
{
  std::string k = to_lower(trim(key));
  // 兼容 "perception::ObjectList" 这种带模块的写法
  size_t pos = k.find_last_of(':');
  if (pos != std::string::npos) k = k.substr(pos + 1);
  for (const auto& a : table().items) {
    if (to_lower(a.key) == k) return &a;
  }
  return nullptr;
}

const PayloadAdapter* find_adapter_by_dds_name(const std::string& dds_name)
{
  for (const auto& a : table().items) {
    if (a.dds_type_name == dds_name) return &a;
  }
  return nullptr;
}

std::vector<std::string> adapter_keys()
{
  std::vector<std::string> out;
  for (const auto& a : table().items) out.push_back(a.key);
  return out;
}

}  // namespace ddsmon
