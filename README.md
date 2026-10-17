# NotepadSharp

NotepadSharp 基于 [Scintilla](https://www.scintilla.org/) 打造。编辑组件是 Scintilla 5.5.8，语法着色来自 [Lexilla](https://www.scintilla.org/Lexilla.html) 5.5.4。本项目采用与 Scintilla 相同的开源许可，见 `LICENSE.txt`。Scintilla 与 Lexilla 的版权归 Neil Hodgson 及其贡献者，源码位于 `third_party/`。

Windows、Ubuntu 和 macOS 使用同一套编辑命令：

- 新建、打开、保存、另存为、关闭、退出
- 撤销、重做、剪切、复制、粘贴、删除、全选
- 查找、查找下一个、查找上一个、替换、全部替换、转到行
- 自动换行、行号、空白字符、换行符、缩放
- 按扩展名选择词法并做语法着色
- 命令行传入一个文件路径时直接打开

Windows 目标子系统是 6.01，可在 Windows 7 SP1 上运行。Scintilla 的高 DPI 接口按运行时检测加载，Windows 7 上会退回 GDI。

## 构建

需要 C++17 编译器和 CMake 3.16 或更高版本。

Windows（Visual Studio 2019/2022，或 MinGW-w64）：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

用 MinGW 构建时，C++ 标准库会静态链进程序，避免系统里另一份旧的 `libstdc++-6.dll` 导致启动失败（提示找不到 `filesystem` 一类的程序输入点）。Windows 7 上如果使用 MSVC 构建，还需要已安装 Universal C Runtime（KB2999226）。MSVC 构建使用静态 VC 运行库。

Ubuntu：

```bash
sudo apt install build-essential cmake pkg-config libgtk-3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

macOS：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

生成的程序名是 `notepadSharp`。

第三方源码和许可证在 `third_party/scintilla` 与 `third_party/lexilla`。
