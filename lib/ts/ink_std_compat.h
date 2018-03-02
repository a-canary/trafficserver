/** @file

  Compatibility with future versions of the C++ standard library

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

#pragma once
//
#if __cplusplus < 201402L
//
// C++ 14
//
#include <memory>
namespace std
{
template <typename T, typename... Args>
std::unique_ptr<T>
make_unique(Args &&... args)
{
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
} // namespace std
#endif

//
#if __cplusplus < 201700L
//
// C++ 17
//
#include "ink_assert.h"
namespace std
{
template <typename T>
constexpr const T &
clamp(const T &v, const T &lo, const T &hi)
{
  ink_release_assert(lo <= hi);
  return (v < lo) ? lo : (hi < v) ? hi : v;
}

} // namespace std
#endif