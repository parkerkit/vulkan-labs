#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

#include "framework/application.h"
#include "sample.h"

int main() {
  try {
    fw::ApplicationDesc desc{};
    desc.window.title = "GPU Driven Rendering | Instance Culling";
    desc.device.enable_multi_draw_indirect = true;
    desc.device.enable_draw_indirect_count = true;
    desc.device.enable_draw_indirect_first_instance = true;
    desc.frames.enable_vsync = false;
    fw::Application::Run(desc, [] { return std::make_unique<demo::Sample>(); });
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  } catch (...) {
    std::cerr << "Unknown application failure\n";
  }
  return EXIT_FAILURE;
}
