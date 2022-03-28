# PONS 2
```from latin pōns (“bridge”)```

Second step towards my rendering engine.

Timeline:
```
V1 OpenGl 101 -> V2 Vulkan 101 -> V3 Small demo game -> ???
                       ^
                   This repo
```

## Short Description
* Based on vulkan-tutorial.com
* uses vulkan-hpp and sdl2
* ...

## Dependencies
* sdl2
* glm
* gli
* tl-expected
* assimp
* freetype

### Example of packages needed for Arch linux
```sh
llvm
clang
lldb
lldb-mi-git
ninja
cmake
amdvlk
vulkan-validation-layers
vulkan-extra-layers
vulkan-extra-tools
vulkan-html-docs
vulkan-tools
tl-expected
```

## Troubleshooting
To use vscode-cmake-tools on archlinux-based systems, create user-local kit for proper compiler selection. Problem is likely not present on other distros. (vscode cmake extension doesn't support format of compiler paths on arch)

[docs link](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/kits.md#user-local-kits)

example:
```
{
    "name": "Clang Default",
    "compilers": {
        "C": "clang",
        "CXX": "clang++"
    }
}
```