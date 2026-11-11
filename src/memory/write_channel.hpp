#pragma once
#include "memory/transfer_result.hpp"
namespace cs2::memory {
class IWriteChannel {
  public:
    virtual ~IWriteChannel() = default;
    virtual TransferResult write(Address address, const void* source, std::size_t bytes) noexcept = 0;
};
} // namespace cs2::memory
