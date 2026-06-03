# Nfield Gravitational Simulator

Nfield is a GPU-accelerated N-body gravitational simulator written in C++20 using Vulkan for compute and rendering, and ImGui for the control interface. It simulates gravitational interactions between particles in real-time, supporting both Direct O(N²) force computation and a tree-based Barnes-Hut O(N log N) algorithm implemented directly in Vulkan compute shaders.

## Installation

### Prerequisites

To build and run the simulator, install the following:

- A C++20 compatible compiler (e.g., GCC 10+, Clang 10+, or MSVC 2019+).
- CMake (version 3.25 or higher).
- Vulkan SDK (including the `glslc` compiler).
- GLFW 3 library.
- GLM library.

For MSYS2 environments on Windows, install the dependencies using:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-vulkan-sdk mingw-w64-x86_64-glfw mingw-w64-x86_64-glm
```

### Build Instructions

Build the project from the root directory:

```bash
cmake -S . -B build
cmake --build build
```

## Usage

### Execution

Run the compiled executable from the build directory:

```bash
./build/nfield
```

### Interactive Controls

- **Left Mouse Button (LMB) + Drag**: Rotate the camera around the focus point.
- **Right Mouse Button (RMB) + Drag**: Pan the camera.
- **Scroll Wheel**: Zoom the camera.

## Configuration

The simulator parameters are adjusted dynamically through the ImGui control panel window. The available settings include:

- **Simulation Controls**:
  - `Pause Simulation`: Suspends particle state updates.
  - `Time Step (dt)`: Time increment per frame (default: `0.001`).
  - `Gravitational Constant (G)`: Overall strength of gravity (default: `1.0`).
  - `Softening Factor`: Softening value to prevent division by zero at small distances (default: `0.01`).
  - `Algorithm`: Selects between Direct O(N²) and Barnes-Hut O(N log N).
  - `Theta`: Opening angle parameter for the Barnes-Hut algorithm (default: `0.5`).

- **Particles & Initial Conditions**:
  - `Particle Count`: Number of particles (from `512` up to `65,536`).
  - `Select Preset`: Initial positions and velocities layout:
    - _Random Sphere Cloud_: A uniform sphere of particles.
    - _Rotating Disk Galaxy_: A flat rotating galaxy around a massive central body.
    - _Binary Orbiting Stars_: Two central stars in mutual orbit surrounded by disks.
    - _Concentric Orbiting Rings_: A star with concentric planetary rings.
    - _Colliding Galaxies_: Two galaxies on a collision course.
  - `Reset & Load Initial State`: Applies the selected particle count and preset.

- **Rendering**:
  - `Bloom`: Toggle bloom post-processing, with sliders for threshold and intensity.
  - `Particle Trails`: Toggle motion blur trails, with a decay slider.
  - `Color Mode`: Color particles based on velocity, mass, kinetic energy, or rainbow spectrum.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

For third-party software licenses, see the [LICENSE-3RD-PARTY](LICENSE-3RD-PARTY) file.
