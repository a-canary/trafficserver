/** @file
  Test file for NextHop namespace
  @section license License
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at
      http://www.apache.org/licenses/LICENSE-2.0
  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "catch.hpp"

#include "NextHopHost.h"
#include "NextHopHost.cc"
#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace NextHop;

typename HostRecord::BitFieldId bit_a, bit_b;
shared_ptr<HostRecord> host_ptr, host_ptr2;

// ======= test for string_view ========
// test cases:
//[constructor] [operator] [type] [access] [capacity] [modifier] [operation] [compare] [find]

TEST_CASE("NextHop Host constructor", "[NextHop] [Host] [constructor]")
{
  SECTION("Declare fields")
  {
    REQUIRE(HostRecord::schema.addField(bit_a, "bit_a"));
    REQUIRE(HostRecord::schema.addField(bit_b, "bit_b"));
  }

  SECTION("find_or_alloc")
  {
    REQUIRE(HostRecord::find_or_alloc("test_host.com", host_ptr) == false);
    REQUIRE(HostRecord::find_or_alloc("test_host.com", host_ptr2) == true);
    REQUIRE(host_ptr == host_ptr2);
    REQUIRE(host_ptr);
    REQUIRE(host_ptr2);
  }

  SECTION("use fields")
  {
    auto &host = *host_ptr;
    host.writeBit(bit_a, 1);
    REQUIRE(host[bit_a] == 1);
    REQUIRE(host[bit_b] == 0);
    host.writeBit(bit_b, 1);
    host.writeBit(bit_a, 0);
    REQUIRE(host[bit_a] == 0);
    REQUIRE(host[bit_b] == 1);
  }
}