#ifdef __linux__

#include <experimental/filesystem>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "src/common/base/base.h"
#include "src/stirling/cgroup_stats_connector.h"
#include "src/stirling/cgroups/cgroup_manager.h"
#include "src/stirling/cgroups/proc_parser.h"

namespace pl {
namespace stirling {

Status CGroupStatsConnector::InitImpl() {
  InitClockRealTimeOffset();
  return cgroup_mgr_->UpdateCGroupInfo();
}

Status CGroupStatsConnector::StopImpl() { return Status::OK(); }

void CGroupStatsConnector::TransferCGroupStatsTable(types::ColumnWrapperRecordBatch* record_batch) {
  auto status = cgroup_mgr_->UpdateCGroupInfo();
  if (!status.ok()) {
    LOG(ERROR) << "Failed to get the latest cgroup stat files: " << status.msg();
    return;
  }

  auto now = std::chrono::steady_clock::now();
  int64_t timestamp =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() +
      ClockRealTimeOffset();

  auto& columns = *record_batch;
  for (const auto& [pod, pod_info] : cgroup_mgr_->cgroup_info()) {
    for (const auto& [container, process_info] : pod_info.container_info_by_name) {
      for (const auto& pid : process_info.pids) {
        ProcParser::ProcessStats stats;
        auto s = cgroup_mgr_->GetProcessStats(pid, &stats);
        if (!s.ok()) {
          LOG(ERROR) << absl::StrFormat(
              "Failed to fetch info for PID (%ld). Error=\"%s\" skipping.", pid, s.msg());
          continue;
        }

        int64_t col_idx = 0;
        columns[col_idx++]->Append<types::Time64NSValue>(timestamp);
        columns[col_idx++]->Append<types::StringValue>(
            CGroupManager::CGroupQoSToString(pod_info.qos));
        columns[col_idx++]->Append<types::StringValue>(std::string(pod));
        columns[col_idx++]->Append<types::StringValue>(std::string(container));
        columns[col_idx++]->Append<types::StringValue>(std::move(stats.process_name));
        columns[col_idx++]->Append<types::Int64Value>(stats.pid);
        columns[col_idx++]->Append<types::Int64Value>(stats.major_faults);
        columns[col_idx++]->Append<types::Int64Value>(stats.minor_faults);
        columns[col_idx++]->Append<types::Int64Value>(stats.utime_ns);
        columns[col_idx++]->Append<types::Int64Value>(stats.ktime_ns);
        columns[col_idx++]->Append<types::Int64Value>(stats.num_threads);
        columns[col_idx++]->Append<types::Int64Value>(stats.vsize_bytes);
        columns[col_idx++]->Append<types::Int64Value>(stats.rss_bytes);
        columns[col_idx++]->Append<types::Int64Value>(stats.rchar_bytes);
        columns[col_idx++]->Append<types::Int64Value>(stats.wchar_bytes);
        columns[col_idx++]->Append<types::Int64Value>(stats.read_bytes);
        columns[col_idx++]->Append<types::Int64Value>(stats.write_bytes);
      }
    }
  }
}

void CGroupStatsConnector::TransferNetStatsTable(types::ColumnWrapperRecordBatch* record_batch) {
  auto status = cgroup_mgr_->UpdateCGroupInfo();
  if (!status.ok()) {
    LOG(ERROR) << "Failed to get the latest cgroup stat files: " << status.msg();
    return;
  }

  auto now = std::chrono::steady_clock::now();
  int64_t timestamp =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() +
      ClockRealTimeOffset();

  auto& columns = *record_batch;
  for (const auto& [pod, pod_info] : cgroup_mgr_->cgroup_info()) {
    PL_UNUSED(pod_info);
    ProcParser::NetworkStats stats;
    auto s = cgroup_mgr_->GetNetworkStatsForPod(pod, &stats);
    if (!s.ok()) {
      LOG(ERROR) << absl::StrFormat(
          "Failed to fetch network stats for pod \"%s\". Error=\"%s\" skipping.", pod, s.msg());
      continue;
    }

    int64_t col_idx = 0;
    columns[col_idx++]->Append<types::Time64NSValue>(timestamp);
    columns[col_idx++]->Append<types::StringValue>(std::string(pod));

    columns[col_idx++]->Append<types::Int64Value>(stats.rx_bytes);
    columns[col_idx++]->Append<types::Int64Value>(stats.rx_packets);
    columns[col_idx++]->Append<types::Int64Value>(stats.rx_errs);
    columns[col_idx++]->Append<types::Int64Value>(stats.rx_drops);

    columns[col_idx++]->Append<types::Int64Value>(stats.tx_bytes);
    columns[col_idx++]->Append<types::Int64Value>(stats.tx_packets);
    columns[col_idx++]->Append<types::Int64Value>(stats.tx_errs);
    columns[col_idx++]->Append<types::Int64Value>(stats.tx_drops);
  }
}

void CGroupStatsConnector::TransferDataImpl(uint32_t table_num,
                                            types::ColumnWrapperRecordBatch* record_batch) {
  CHECK_LT(table_num, num_tables())
      << absl::StrFormat("Trying to access unexpected table: table_num=%d", table_num);

  switch (table_num) {
    case 0:
      TransferCGroupStatsTable(record_batch);
      break;
    case 1:
      TransferNetStatsTable(record_batch);
      break;
    default:
      LOG(ERROR) << "Unknown table: " << table_num;
  }
}

}  // namespace stirling
}  // namespace pl

#endif
