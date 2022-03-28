# PONS 2
```from latin pōns (“bridge”)```

Second step towards my rendering engine.

Timeline:
```
V1 OpenGl 101 -> V2 Vulkan 101 -> V3 Mini Game Engine -> ???
                       ^
                   This repo
```

## Short Description
* Based on vulkan-tutorial.com
* uses vulkan-hpp and sdl
* ...

## Dependencies
* sdl2
* glm
* gli
* tl-expected
* assimp
* freetype

## Troubleshooting
To use vscode-cmake-tools on archlinux-based systems, create user-local kit for proper compiler selection. (vscode cmake extension doesn't support format of compiler paths on arch)
[link](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/kits.md#user-local-kits)

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