#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace saturnis::platform {
[[nodiscard]] std::vector<std::uint8_t> read_binary_file(const std::string &path);
}
