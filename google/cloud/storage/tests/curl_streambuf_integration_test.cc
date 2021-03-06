// Copyright 2018 Google LLC
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

#include "google/cloud/internal/getenv.h"
#include "google/cloud/internal/make_unique.h"
#include "google/cloud/internal/random.h"
#include "google/cloud/storage/internal/curl_request_builder.h"
#include "google/cloud/storage/internal/curl_streambuf.h"
#include "google/cloud/storage/object_stream.h"
#include "google/cloud/testing_util/assert_ok.h"
#include <gmock/gmock.h>

namespace google {
namespace cloud {
namespace storage {
inline namespace STORAGE_CLIENT_NS {
namespace {

using ::testing::HasSubstr;

std::string HttpBinEndpoint() {
  return google::cloud::internal::GetEnv("HTTPBIN_ENDPOINT")
      .value_or("https://nghttp2.org/httpbin");
}

TEST(CurlStreambufIntegrationTest, WriteManyBytes) {
  internal::CurlRequestBuilder builder(HttpBinEndpoint() + "/post",
                                       internal::GetDefaultCurlHandleFactory());
  builder.AddHeader("Content-Type: application/octet-stream");
  builder.SetMethod("POST");
  std::unique_ptr<internal::CurlWriteStreambuf> buf(
      new internal::CurlWriteStreambuf(
          builder.BuildUpload(), 128 * 1024,
          google::cloud::internal::make_unique<internal::NullHashValidator>()));
  ObjectWriteStream writer(std::move(buf));

  auto generator = google::cloud::internal::MakeDefaultPRNG();
  auto generate_random_line = [&generator] {
    std::string const characters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        ".,/;:'[{]}=+-_}]`~!@#$%^&*()\t\n\r\v";
    return google::cloud::internal::Sample(generator, 200, characters);
  };

  // We will construct the expected response while streaming the data up.
  std::string expected;
  for (int line = 0; line != 1000; ++line) {
    std::string random = generate_random_line();
    writer << random;
    expected += random;
  }
  writer.Close();
  ASSERT_STATUS_OK(writer.metadata())
      << ", status=" << writer.metadata().status()
      << ", payload=" << writer.payload() << ", headers={" << [&writer] {
           std::string result;
           char const* sep = "";
           for (auto&& kv : writer.headers()) {
             result += sep;
             result += kv.first;
             result += "=";
             result += kv.second;
             sep = ", ";
           }
           result += "}";
           return result;
         }();

  internal::nl::json parsed = internal::nl::json::parse(writer.payload());

  // Verify the server received the right data.
  auto actual = parsed.value("data", "");
  // A common failure mode is to get empty data, in that case printing the delta
  // in EXPECT_EQ() is just distracting.
  ASSERT_FALSE(actual.empty());
  EXPECT_EQ(expected, parsed.value("data", ""));
}

}  // namespace
}  // namespace STORAGE_CLIENT_NS
}  // namespace storage
}  // namespace cloud
}  // namespace google
