#include "platform/file_io.hpp"

#include <fstream>
#include <stdexcept>

namespace saturnis::platform {

std::vector<std::uint8_t> read_binary_file(const std::string &path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

} // namespace saturnis::platform
