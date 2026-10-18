#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

#include "framework/application.h"
#include "sample.h"

int main() {
  try {
    fw::ApplicationDesc desc{};
    desc.window.title = "GPU Driven Rendering | Bindless Material";
    desc.device.enable_sampled_image_non_uniform_indexing = true;
    fw::Application::Run(desc, [] { return std::make_unique<demo::Sample>(); });
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  } catch (...) {
    std::cerr << "Unknown application failure\n";
  }
  return EXIT_FAILURE;
}
