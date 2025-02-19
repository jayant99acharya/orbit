// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>
#include <memory>
#include <utility>

#include "OrbitBase/Result.h"
#include "OrbitBase/TestUtils.h"
#include "OrbitGgp/SshInfo.h"

namespace orbit_ggp {

TEST(SshInfoTest, CreateFromJson) {
  // Empty json
  {
    QByteArray json = QString("").toUtf8();
    EXPECT_THAT(SshInfo::CreateFromJson(json), orbit_base::HasError("Unable to parse JSON"));
  }

  // invalid json
  {
    QByteArray json = QString("{..dfP}").toUtf8();
    EXPECT_THAT(SshInfo::CreateFromJson(json), orbit_base::HasError("Unable to parse JSON"));
  }

  // empty object
  {
    QByteArray json = QString("{}").toUtf8();
    EXPECT_THAT(SshInfo::CreateFromJson(json), orbit_base::HasError("Unable to parse JSON"));
  }

  // object without all necessary fields
  {
    QByteArray json = QString("{\"host\":\"0.0.0.1\"}").toUtf8();
    EXPECT_THAT(SshInfo::CreateFromJson(json), orbit_base::HasError("Unable to parse JSON"));
  }

  // valid object

  // Pretty print test data
  // {
  //  "host": "1.1.0.1",
  //  "keyPath": "/usr/local/some/path/.ssh/id_rsa",
  //  "knownHostsPath": "/usr/local/another/path/known_hosts",
  //  "port": "11123",
  //  "user": "a username"
  // }
  {
    QByteArray json = QString(
                          "{\"host\":\"1.1.0.1\",\"keyPath\":\"/usr/local/some/path/.ssh/"
                          "id_rsa\",\"knownHostsPath\":\"/usr/local/another/path/"
                          "known_hosts\",\"port\":\"11123\",\"user\":\"a username\"}")
                          .toUtf8();
    const auto ssh_info_result = SshInfo::CreateFromJson(json);
    ASSERT_THAT(ssh_info_result, orbit_base::HasValue());
    const SshInfo ssh_info = std::move(ssh_info_result.value());
    EXPECT_EQ(ssh_info.host, "1.1.0.1");
    EXPECT_EQ(ssh_info.key_path,
              "/usr/local/some/path/.ssh/"
              "id_rsa");
    EXPECT_EQ(ssh_info.known_hosts_path,
              "/usr/local/another/path/"
              "known_hosts");
    EXPECT_EQ(ssh_info.port, 11123);
    EXPECT_EQ(ssh_info.user, "a username");
  }

  // valid object - but port is formatted as an int.
  {
    QByteArray json = QString(
                          "{\"host\":\"1.1.0.1\",\"keyPath\":\"/usr/local/some/path/.ssh/"
                          "id_rsa\",\"knownHostsPath\":\"/usr/local/another/path/"
                          "known_hosts\",\"port\":11123,\"user\":\"a username\"}")
                          .toUtf8();
    // This is supposed to fail, since its expected that the port is a string
    EXPECT_THAT(SshInfo::CreateFromJson(json), orbit_base::HasError("Unable to parse JSON"));
  }
}

}  // namespace orbit_ggp
