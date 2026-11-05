#pragma once

#include <string>
#include <vector>

#include "ddsmon/engine.hpp"

namespace ddsmon {

/// 文本表格（终端快照，ANSI 着色由调用方决定）
std::string render_text(const Snapshot& snap, const Config& cfg, bool color);

/// Markdown 报告（人读，最终归档产物）
std::string render_markdown(const Snapshot& snap, const Config& cfg, double duration_sec);

/// JSON 报告（机读，供 CI / 平台消费）
std::string render_json(const Snapshot& snap, const Config& cfg, double duration_sec);

/// 一行摘要，用于 watch 顶栏
std::string render_health_line(const Snapshot& snap);

bool write_text_file(const std::string& path, const std::string& content, std::string* err);

}  // namespace ddsmon
