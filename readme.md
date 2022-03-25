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