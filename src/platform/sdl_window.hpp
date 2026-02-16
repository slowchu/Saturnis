#pragma once

#include <cstdint>
#include <vector>

namespace saturnis::platform {
void present_framebuffer_if_available(int width, int height, const std::vector<std::uint32_t> &pixels, bool headless);
}
