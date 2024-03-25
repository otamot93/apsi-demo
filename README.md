# Description


# Usage
## Timing Diagram
![Timing Diagram](./assets/apsi_use.png)




# Build

## Install Dependency

```
vcpkg install
```
## Build
```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE={{VCPKG_ROOT}}/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

