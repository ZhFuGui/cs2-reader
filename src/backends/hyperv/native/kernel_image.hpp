#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cs2::backends::hyperv::native {
// 仅解析已在运行的内核映像；所有 RVA 均经边界和节属性检查。
class KernelImage {
  public:
    KernelImage(std::uint64_t caller_cr3, std::uint64_t base);
    [[nodiscard]] std::uint64_t base() const noexcept { return base_; }
    [[nodiscard]] bool writable_data(std::uint64_t address) const noexcept;
    [[nodiscard]] std::uint64_t export_address(std::string_view name, bool executable) const;
    [[nodiscard]] std::uint32_t accessor_offset(std::string_view name) const;
    [[nodiscard]] std::uint32_t address_accessor_offset(std::string_view name) const;
    struct BitField { std::uint32_t offset, mask; };
    [[nodiscard]] BitField bit_accessor(std::string_view name) const;

  private:
    [[nodiscard]] bool range(std::uint64_t rva, std::uint64_t bytes) const noexcept;
    [[nodiscard]] std::string ascii(std::uint32_t rva) const;
    [[nodiscard]] const IMAGE_SECTION_HEADER* section(std::uint32_t rva) const noexcept;
    void exact(std::uint64_t address, void* destination, std::size_t size) const;
    template <class T> [[nodiscard]] T value(std::uint64_t address) const {
        T result{};
        exact(address, &result, sizeof(result));
        return result;
    }

    std::uint64_t cr3_, base_;
    IMAGE_NT_HEADERS64 nt_{};
    IMAGE_EXPORT_DIRECTORY exports_{};
    IMAGE_DATA_DIRECTORY export_range_{};
    std::vector<IMAGE_SECTION_HEADER> sections_;
};
} // namespace cs2::backends::hyperv::native
