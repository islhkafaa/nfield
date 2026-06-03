#include "core/App.hpp"
#include <exception>
#include <iostream>

int main() {
  try {
    App app(1280, 720, "Nfield Gravitational Simulator");
    app.run();
  } catch (const std::exception &e) {
    std::cerr << "Unhandled exception: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
