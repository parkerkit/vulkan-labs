#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

#include "cube_sample.h"
#include "framework/application.h"

int main() {
  // 运行失败统一在可执行入口报告，框架在异常传播到这里之前已完成资源清理。
  try {
    fw::ApplicationDesc desc{};
    desc.window.title = "Framework | Interactive Cube";
    fw::Application::Run(desc, [] { return std::make_unique<demo::CubeSample>(); });
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  } catch (...) {
    std::cerr << "Unknown application failure\n";
  }
  return EXIT_FAILURE;
}
