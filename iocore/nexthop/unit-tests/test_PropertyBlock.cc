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
  Unless REQUIRE by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "catch.hpp"

#include "PropertyBlock.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

// ======= test for PropBlock ========
struct Derived : PropertyBlock<Derived> {
  string m_str;

  Derived() { propBlockInit(); }
  static bool
  hasReadAccess(const Derived *)
  {
    return true;
  }
  static bool
  hasWriteAccess(const Derived *)
  {
    return true;
  }
};

struct testProperty {
  int arr[5];
  static int alive;
  static void
  init(Derived *, void *ptr)
  {
    testProperty &tp = *static_cast<testProperty *>(ptr);
    int x            = 1;
    for (int &a : tp.arr) {
      a = x;
      x *= 2;
    }
    alive++;
  }
  static void
  destroy(Derived *, void *ptr)
  {
    testProperty &tp = *static_cast<testProperty *>(ptr);
    for (int &a : tp.arr) {
      a = 0;
    }
    alive--;
  }
};
int testProperty::alive = 0;

size_t a, b, c, d;
Derived *ptr;

// test cases:
//[constructor] [operator] [type] [access] [capacity] [modifier] [operation] [compare] [find]
// I don't use SECTIONS because this modifies static variables many times, is not thread safe.
TEST_CASE("PropBlock Tests", "[constructor] [access] [modifier]")
{
  ptr = new Derived();
  REQUIRE(ptr != nullptr);
  delete ptr;

  ptr = new Derived();
  REQUIRE(Derived::resetSchema() == false);
  delete ptr;
  REQUIRE(Derived::resetSchema() == true);

  // Bit Init
  a   = Derived::PropBlockDeclareBit(9);
  ptr = new Derived();
  REQUIRE(ptr != nullptr);
  REQUIRE(ptr->propGetBit(a) == false);
  CHECK(ptr->propGetBit(a + 1) == false);
  CHECK(ptr->propGetBit(a + 2) == false);
  CHECK(ptr->propGetBit(a + 3) == false);
  CHECK(ptr->propGetBit(a + 4) == false);
  CHECK(ptr->propGetBit(a + 5) == false);
  CHECK(ptr->propGetBit(a + 6) == false);
  CHECK(ptr->propGetBit(a + 7) == false);
  CHECK(ptr->propGetBit(a + 8) == false);
}

TEST_CASE("PropBlock Tests 2", "[constructor] [access] [modifier]")
{
  delete ptr;
  REQUIRE(Derived::resetSchema() == true);

  // Store Bit
  a   = Derived::PropBlockDeclareBit(5);
  ptr = new Derived();
  CHECK(ptr != nullptr);
  ptr->propPutBit(a, true);
  ptr->propPutBit(a + 2, true);
  ptr->propPutBit(a + 4, true);
  ptr->m_str = "Hello";
  CHECK(ptr->propGetBit(a) == true);
  CHECK(ptr->propGetBit(a + 1) == false);
  CHECK(ptr->propGetBit(a + 2) == true);
  CHECK(ptr->propGetBit(a + 3) == false);
  CHECK(ptr->propGetBit(a + 4) == true);
}

TEST_CASE("PropBlock Store Int", "[constructor] [access] [modifier]")
{
  delete ptr;
  REQUIRE(Derived::resetSchema() == true);

  a   = Derived::PropBlockDeclare<int>(1);
  b   = Derived::PropBlockDeclare<int>(1);
  ptr = new Derived();
  CHECK(ptr != nullptr);
  CHECK(ptr->propRead<int>(a + 0) == 0);
  ptr->propWrite<int>(a) = 12;
  ptr->propWrite<int>(b) = 34;
  ptr->m_str             = "Hello";
  CHECK(ptr->propRead<int>(a) == 12);
  CHECK(ptr->propRead<int>(b) == 34);
}

TEST_CASE("PropBlock destruct int", "")
{
  delete ptr;
  REQUIRE(Derived::resetSchema() == true);
}

TEST_CASE("PropBlock construct string", "[constructor] [access] [modifier]")
{
  // store string (init using class constructors)
  b                         = Derived::PropBlockDeclare<std::string>(1); // default class constructors
  ptr                       = new Derived();
  ptr->propWrite<string>(b) = "Bye";
  ptr->m_str                = "Hello";
  CHECK(ptr->propRead<string>(b) == "Bye");
}

TEST_CASE("PropBlock destruct string", "")
{
  delete ptr;
  REQUIRE(Derived::resetSchema() == true);
}

TEST_CASE("PropBlock struct declare", "[constructor] [access] [modifier]")
{
  a = Derived::PropBlockDeclare<testProperty>(1);
  REQUIRE(Derived::resetSchema() == true);
}

TEST_CASE("PropBlock struct construct noop", "[constructor] [access] [modifier]")
{
  a = Derived::PropBlockDeclare<testProperty>(3, nullptr, nullptr);
  CHECK(a == sizeof(Derived));
  ptr        = new Derived();
  ptr->m_str = "Hello";
  {
    const testProperty &dv = ptr->propRead<testProperty>(a);
    CHECK(dv.arr[0] == 0);
    CHECK(dv.arr[1] == 0);
    CHECK(dv.arr[2] == 0);
    CHECK(dv.arr[3] == 0);
    CHECK(dv.arr[4] == 0);
  }
}

TEST_CASE("PropBlock struct destruct noop", "")
{
  delete ptr;
  REQUIRE(Derived::resetSchema() == true);
}

TEST_CASE("PropBlock struct construct default", "[constructor] [access] [modifier]")
{
  a = Derived::PropBlockDeclare<testProperty>(3);
  CHECK(a == sizeof(Derived));
  ptr        = new Derived();
  ptr->m_str = "Hello";
  {
    const testProperty &dv = ptr->propRead<testProperty>(a);
    CHECK(dv.arr[0] == 0);
    CHECK(dv.arr[1] == 0);
    CHECK(dv.arr[2] == 0);
    CHECK(dv.arr[3] == 0);
    CHECK(dv.arr[4] == 0);
  }
}

TEST_CASE("PropBlock struct destruct default", "")
{
  delete ptr;
  REQUIRE(Derived::resetSchema() == true);
}

TEST_CASE("PropBlock struct declare custom", "[constructor] [access] [modifier]")
{
  a = Derived::PropBlockDeclare<testProperty>(3, testProperty::init, testProperty::destroy);
  CHECK(a == sizeof(Derived));
}

TEST_CASE("PropBlock struct construct custom", "[constructor] [access] [modifier]")
{
  ptr = new Derived();
  CHECK(testProperty::alive == 3);
  ptr->m_str = "Hello";
  {
    const testProperty &dv = ptr->propRead<testProperty>(a);
    CHECK(dv.arr[0] == 1);
    CHECK(dv.arr[1] == 2);
    CHECK(dv.arr[2] == 4);
    CHECK(dv.arr[3] == 8);
    CHECK(dv.arr[4] == 16);
  }
}

TEST_CASE("PropBlock struct destruct custom", "")
{
  delete ptr;
  CHECK(testProperty::alive == 0);
  REQUIRE(Derived::resetSchema() == true);
}

///////////

TEST_CASE("PropBlock declare all", "[constructor] [access] [modifier]")
{
  a = Derived::PropBlockDeclare<testProperty>(3, testProperty::init, testProperty::destroy);
  b = Derived::PropBlockDeclareBit(5);
  c = Derived::PropBlockDeclare<std::string>(1); // default class constructors
  d = Derived::PropBlockDeclare<int>(1);

  CHECK(a == sizeof(Derived));
  CHECK(b == 2);
  CHECK(c == sizeof(Derived) + 3 * sizeof(testProperty));
  CHECK(d == sizeof(Derived) + 3 * sizeof(testProperty) + sizeof(std::string));
}

TEST_CASE("PropBlock init all", "[constructor] [access] [modifier]")
{
  ptr = new Derived();
  CHECK(testProperty::alive == 3);
  ptr->m_str = "Hello";
  {
    const testProperty *tp = &ptr->propRead<testProperty>(a);
    const testProperty &dv = tp[1];
    CHECK(dv.arr[0] == 1);
    CHECK(dv.arr[1] == 2);
    CHECK(dv.arr[2] == 4);
    CHECK(dv.arr[3] == 8);
    CHECK(dv.arr[4] == 16);
  }
}

TEST_CASE("PropBlock struct modify custom", "")
{
  ptr->propWrite<std::string>(c) = "Foo";
  //
  testProperty *tp = &ptr->propWrite<testProperty>(a);
  tp[0].arr[1]     = 3;
  tp[1].arr[3]     = 3;
  tp[0].arr[3]     = 7;
  //
  ptr->propPutBit(b + 2, true);
  ptr->propWrite<int>(d) = 42;
  tp[0].arr[2]           = 3;
}

TEST_CASE("PropBlock struct access custom", "")
{
  CHECK(ptr->propRead<std::string>(c) == "Foo");
  CHECK(ptr->m_str == "Hello");
  const testProperty *tp = &ptr->propRead<testProperty>(a);
  CHECK(tp[0].arr[0] == 1);
  CHECK(tp[0].arr[1] == 3);
  CHECK(tp[0].arr[2] == 3);
  CHECK(tp[0].arr[3] == 7);
  CHECK(tp[0].arr[4] == 16);
  CHECK(tp[1].arr[0] == 1);
  CHECK(tp[1].arr[1] == 2);
  CHECK(tp[1].arr[2] == 4);
  CHECK(tp[1].arr[3] == 3);
  CHECK(tp[1].arr[4] == 16);

  CHECK(ptr->propGetBit(b + 0) == false);
  CHECK(ptr->propGetBit(b + 1) == false);
  CHECK(ptr->propGetBit(b + 2) == true);
  CHECK(ptr->propGetBit(b + 3) == false);
  CHECK(ptr->propGetBit(b + 4) == false);
  CHECK(ptr->propRead<int>(d) == 42);
}

TEST_CASE("PropBlock struct cleanup", "")
{
  delete ptr;
  CHECK(testProperty::alive == 0);
  REQUIRE(Derived::resetSchema() == true);
}

// TODO: write multithreaded tests.
