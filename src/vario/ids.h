#pragma once

#include <stdint.h>

template <typename TDerived, typename TValue>
class GenericID {
 public:
  explicit GenericID(TValue id) : id_(id) {}
  TValue index() const { return id_; }
  bool operator==(const TDerived& other) const { return id_ == other.id_; }
  bool operator!=(const TDerived& other) const { return id_ != other.id_; }

 protected:
  TValue id_;
};
