// Copyright (c) 2009-2017 The OTS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OTS_GRAPHITE_H_
#define OTS_GRAPHITE_H_

#include <vector>
#include <type_traits>

namespace ots {

template<typename ParentType>
class TablePart {
 public:
  TablePart(ParentType* parent) : parent(parent) { }
  virtual bool ParsePart(Buffer& table) = 0;
  virtual bool SerializePart(OTSStream* out) const = 0;
 protected:
  ParentType* parent;
};

template<typename T>
bool SerializeParts(const std::vector<T>& vec, OTSStream* out) {
  for (size_t i = 0; i < vec.size(); i++) {
    const T& part = vec[i];
    if (!part.SerializePart(out)) {
      return false;
    }
  }
  return true;
}

template<typename T>
bool SerializeParts(const std::vector<std::vector<T>>& vec, OTSStream* out) {
  for (size_t i = 0; i < vec.size(); i++) {
    const std::vector<T>& part = vec[i];
    if (!SerializeParts(part, out)) {
      return false;
    }
  }
  return true;
}

inline bool SerializeParts(const std::vector<uint8_t>& vec, OTSStream* out) {
  for (size_t i = 0; i < vec.size(); i++) {
    uint8_t part = vec[i];
    if (!out->WriteU8(part)) {
      return false;
    }
  }
  return true;
}

inline bool SerializeParts(const std::vector<uint16_t>& vec, OTSStream* out) {
  for (size_t i = 0; i < vec.size(); i++) {
    uint16_t part = vec[i];
    if (!out->WriteU16(part)) {
      return false;
    }
  }
  return true;
}

inline bool SerializeParts(const std::vector<int16_t>& vec, OTSStream* out) {
  for (size_t i = 0; i < vec.size(); i++) {
    int16_t part = vec[i];
    if (!out->WriteS16(part)) {
      return false;
    }
  }
  return true;
}

inline bool SerializeParts(const std::vector<uint32_t>& vec, OTSStream* out) {
  for (size_t i = 0; i < vec.size(); i++) {
    uint32_t part = vec[i];
    if (!out->WriteU32(part)) {
      return false;
    }
  }
  return true;
}

inline bool SerializeParts(const std::vector<int32_t>& vec, OTSStream* out) {
  for (size_t i = 0; i < vec.size(); i++) {
    int32_t part = vec[i];
    if (!out->WriteS32(part)) {
      return false;
    }
  }
  return true;
}

template<typename T>
size_t datasize(std::vector<T> vec) {
  return sizeof(T) * vec.size();
}

}  // namespace ots

#endif  // OTS_GRAPHITE_H_
