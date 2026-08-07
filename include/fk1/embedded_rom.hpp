#pragma once

#include <cstdint>
#include <span>

namespace fk1 {

[[nodiscard]] std::span<const std::uint8_t> embedded_boot_rom() noexcept;

} // namespace fk1
