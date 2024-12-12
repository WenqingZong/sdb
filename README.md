# Simple Debugger

Just to record my progress while reading the book [Building A Debugger](https://nostarch.com/building-a-debugger).
The original implementation is [here](https://github.com/TartanLlama/sdb).

## Project Setup
Please develop in a native x86-64 computer, otherwise the code won't work. As a reference, here is my enviroment:

[neofetch](./images/neofetch.png)

Editor used:
- VsCode

### VSCode Settings
```json
{
    "cmake.configureArgs": [
        "-DCMAKE_TOOLCHAIN_FILE=<PATH/TO/VCPKG>/scripts/buildsystems/vcpkg.cmake"
    ]
}
```