// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/bigtable/table_admin.h"
#include "google/cloud/bigtable/grpc_error.h"
#include "google/cloud/bigtable/internal/async_future_from_callback.h"
#include "google/cloud/bigtable/internal/grpc_error_delegate.h"
#include "google/cloud/bigtable/internal/poll_longrunning_operation.h"
#include "google/cloud/bigtable/internal/unary_client_utils.h"
#include <google/protobuf/duration.pb.h>
#include <sstream>

namespace btadmin = ::google::bigtable::admin::v2;

namespace google {
namespace cloud {
namespace bigtable {
inline namespace BIGTABLE_CLIENT_NS {
static_assert(std::is_copy_constructible<bigtable::TableAdmin>::value,
              "bigtable::TableAdmin must be constructible");
static_assert(std::is_copy_assignable<bigtable::TableAdmin>::value,
              "bigtable::TableAdmin must be assignable");

StatusOr<btadmin::Table> TableAdmin::CreateTable(std::string table_id,
                                                 TableConfig config) {
  grpc::Status status;
  auto result =
      impl_.CreateTable(std::move(table_id), std::move(config), status);
  if (!status.ok()) {
    return internal::MakeStatusFromRpcError(status);
  }
  return result;
}

future<google::bigtable::admin::v2::Table> TableAdmin::AsyncCreateTable(
    CompletionQueue& cq, std::string table_id, TableConfig config) {
  promise<google::bigtable::admin::v2::Table> p;
  auto result = p.get_future();

  impl_.AsyncCreateTable(
      cq,
      internal::MakeAsyncFutureFromCallback(std::move(p), "AsyncCreateTable"),
      std::move(table_id), std::move(config));

  return result;
}

future<google::bigtable::admin::v2::Table> TableAdmin::AsyncGetTable(
    CompletionQueue& cq, std::string const& table_id,
    btadmin::Table::View view) {
  promise<google::bigtable::admin::v2::Table> p;
  auto result = p.get_future();

  impl_.AsyncGetTable(
      cq, internal::MakeAsyncFutureFromCallback(std::move(p), "AsyncGetTable"),
      table_id, view);

  return result;
}

StatusOr<std::vector<btadmin::Table>> TableAdmin::ListTables(
    btadmin::Table::View view) {
  grpc::Status status;
  auto result = impl_.ListTables(view, status);
  if (!status.ok()) {
    return internal::MakeStatusFromRpcError(status);
  }
  return result;
}

StatusOr<btadmin::Table> TableAdmin::GetTable(std::string const& table_id,
                                              btadmin::Table::View view) {
  grpc::Status status;
  auto result = impl_.GetTable(table_id, status, view);
  if (!status.ok()) {
    return internal::MakeStatusFromRpcError(status);
  }
  return result;
}

Status TableAdmin::DeleteTable(std::string const& table_id) {
  grpc::Status status;
  impl_.DeleteTable(table_id, status);
  return internal::MakeStatusFromRpcError(status);
}

StatusOr<btadmin::Table> TableAdmin::ModifyColumnFamilies(
    std::string const& table_id,
    std::vector<ColumnFamilyModification> modifications) {
  grpc::Status status;
  auto result =
      impl_.ModifyColumnFamilies(table_id, std::move(modifications), status);
  if (!status.ok()) {
    return internal::MakeStatusFromRpcError(status);
  }
  return result;
}

Status TableAdmin::DropRowsByPrefix(std::string const& table_id,
                                    std::string row_key_prefix) {
  grpc::Status status;
  impl_.DropRowsByPrefix(table_id, std::move(row_key_prefix), status);
  return internal::MakeStatusFromRpcError(status);
}

Status TableAdmin::DropAllRows(std::string const& table_id) {
  grpc::Status status;
  impl_.DropAllRows(table_id, status);
  return internal::MakeStatusFromRpcError(status);
}

std::future<StatusOr<btadmin::Snapshot>> TableAdmin::SnapshotTable(
    bigtable::ClusterId const& cluster_id,
    bigtable::SnapshotId const& snapshot_id, bigtable::TableId const& table_id,
    std::chrono::seconds duration_ttl) {
  return std::async(std::launch::async, &TableAdmin::SnapshotTableImpl, this,
                    cluster_id, snapshot_id, table_id, duration_ttl);
}

StatusOr<btadmin::Snapshot> TableAdmin::SnapshotTableImpl(
    bigtable::ClusterId const& cluster_id,
    bigtable::SnapshotId const& snapshot_id, bigtable::TableId const& table_id,
    std::chrono::seconds duration_ttl) {
  using ClientUtils = bigtable::internal::noex::UnaryClientUtils<AdminClient>;

  btadmin::SnapshotTableRequest request;
  request.set_name(impl_.TableName(table_id.get()));
  request.set_cluster(impl_.ClusterName(cluster_id));
  request.set_snapshot_id(snapshot_id.get());
  request.mutable_ttl()->set_seconds(duration_ttl.count());

  MetadataUpdatePolicy metadata_update_policy(
      instance_name(), MetadataParamTypes::NAME, cluster_id, snapshot_id);

  grpc::Status status;
  auto operation = ClientUtils::MakeCall(
      *(impl_.client_), impl_.rpc_retry_policy_->clone(),
      impl_.rpc_backoff_policy_->clone(), metadata_update_policy,
      &AdminClient::SnapshotTable, request, "SnapshotTable", status, true);

  if (!status.ok()) {
    return bigtable::internal::MakeStatusFromRpcError(status);
  }

  auto result =
      internal::PollLongRunningOperation<btadmin::Snapshot, AdminClient>(
          impl_.client_, impl_.polling_policy_->clone(),
          impl_.metadata_update_policy_, operation, "TableAdmin::SnapshotTable",
          status);
  if (!status.ok()) {
    return bigtable::internal::MakeStatusFromRpcError(status);
  }

  return result;
}

StatusOr<btadmin::Snapshot> TableAdmin::GetSnapshot(
    bigtable::ClusterId const& cluster_id,
    bigtable::SnapshotId const& snapshot_id) {
  grpc::Status status;
  auto result = impl_.GetSnapshot(cluster_id, snapshot_id, status);
  if (!status.ok()) {
    return internal::MakeStatusFromRpcError(status);
  }
  return result;
}

StatusOr<ConsistencyToken> TableAdmin::GenerateConsistencyToken(
    std::string const& table_id) {
  grpc::Status status;
  std::string token = impl_.GenerateConsistencyToken(table_id, status);
  if (!status.ok()) {
    return internal::MakeStatusFromRpcError(status);
  }
  return ConsistencyToken(token);
}

StatusOr<Consistency> TableAdmin::CheckConsistency(
    bigtable::TableId const& table_id,
    bigtable::ConsistencyToken const& consistency_token) {
  grpc::Status status;
  bool consistent = impl_.CheckConsistency(table_id, consistency_token, status);
  if (!status.ok()) {
    return internal::MakeStatusFromRpcError(status);
  }
  return consistent ? Consistency::kConsistent : Consistency::kInconsistent;
}

StatusOr<bool> TableAdmin::WaitForConsistencyCheckImpl(
    bigtable::TableId const& table_id,
    bigtable::ConsistencyToken const& consistency_token) {
  grpc::Status status;
  bool consistent =
      impl_.WaitForConsistencyCheckHelper(table_id, consistency_token, status);
  if (!status.ok()) {
    return bigtable::internal::MakeStatusFromRpcError(status);
  }
  return consistent;
}

Status TableAdmin::DeleteSnapshot(bigtable::ClusterId const& cluster_id,
                                  bigtable::SnapshotId const& snapshot_id) {
  grpc::Status status;
  impl_.DeleteSnapshot(cluster_id, snapshot_id, status);
  return internal::MakeStatusFromRpcError(status);
}

std::future<StatusOr<btadmin::Table>> TableAdmin::CreateTableFromSnapshot(
    bigtable::ClusterId const& cluster_id,
    bigtable::SnapshotId const& snapshot_id, std::string table_id) {
  return std::async(std::launch::async,
                    &TableAdmin::CreateTableFromSnapshotImpl, this, cluster_id,
                    snapshot_id, table_id);
}

StatusOr<btadmin::Table> TableAdmin::CreateTableFromSnapshotImpl(
    bigtable::ClusterId const& cluster_id,
    bigtable::SnapshotId const& snapshot_id, std::string table_id) {
  // Copy the policies in effect for the operation.
  auto rpc_policy = impl_.rpc_retry_policy_->clone();
  auto backoff_policy = impl_.rpc_backoff_policy_->clone();
  // Build the RPC request, try to minimize copying.
  btadmin::Table result;
  btadmin::CreateTableFromSnapshotRequest request;
  request.set_parent(instance_name());
  request.set_source_snapshot(impl_.SnapshotName(cluster_id, snapshot_id));
  request.set_table_id(std::move(table_id));

  grpc::Status status;
  using ClientUtils = bigtable::internal::noex::UnaryClientUtils<AdminClient>;

  auto operation = ClientUtils::MakeCall(
      *impl_.client_, *rpc_policy, *backoff_policy,
      impl_.metadata_update_policy_, &AdminClient::CreateTableFromSnapshot,
      request, "TableAdmin", status, true);
  if (!status.ok()) {
    return bigtable::internal::MakeStatusFromRpcError(status);
  }

  result = internal::PollLongRunningOperation<btadmin::Table, AdminClient>(
      impl_.client_, impl_.polling_policy_->clone(),
      impl_.metadata_update_policy_, operation,
      "TableAdmin::CreateTableFromSnapshot", status);
  if (!status.ok()) {
    return bigtable::internal::MakeStatusFromRpcError(status);
  }
  return result;
}

StatusOr<std::vector<::google::bigtable::admin::v2::Snapshot>>
TableAdmin::ListSnapshots(bigtable::ClusterId const& cluster_id) {
  grpc::Status status;
  auto res = impl_.ListSnapshots(status, cluster_id);

  if (!status.ok()) {
    return internal::MakeStatusFromRpcError(status);
  }
  return res;
}

}  // namespace BIGTABLE_CLIENT_NS
}  // namespace bigtable
}  // namespace cloud
}  // namespace google
