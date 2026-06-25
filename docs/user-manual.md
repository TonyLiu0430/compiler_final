# C9AY C Compiler 使用手冊

## 系統需求

本專案目前以 Windows x86-64 為主要開發與執行環境，使用 MinGW-w64 GCC 建置編譯器本體，並透過 LLVM 產生 x86-64 Windows 目標碼。

需要準備的工具：

- Windows 10 或更新版本，x86-64
- CMake 3.25 以上
- Git
- WinLibs MinGW-w64 GCC 16 或更新版本
- 可用的 PowerShell 或 Windows Terminal
- 足夠磁碟空間與記憶體，用於第一次建置 LLVM

本專案使用 C++26 反射功能，因此 GCC 必須支援 `-freflection`。建議使用 WinLibs 提供的 GCC 16 release 或更新版本。

## 下載 MinGW

WinLibs 下載頁面：

```text
https://winlibs.com/#download-release
```

建議下載項目：

- `Release versions`
- `UCRT runtime`
- `GCC 16.x.x`
- `POSIX threads`
- `Win64 x86_64`
- `without LLVM/Clang/LLD/LLDB` 版本即可
- `.zip` 或 `.7z` 都可以，`.7z` 較小但需要 7-Zip 解壓縮

下載後解壓縮到固定位置，例如：

```text
C:\dev\winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r3
```

解壓縮後應該可以看到：

```text
C:\dev\winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r3\mingw64\bin\gcc.exe
C:\dev\winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r3\mingw64\bin\g++.exe
C:\dev\winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r3\mingw64\bin\mingw32-make.exe
```

確認版本：

```powershell
C:\dev\winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r3\mingw64\bin\g++.exe --version
```

如果解壓縮路徑不同，後續 `CMakePresets.json` 內的 MinGW 路徑也要一起改成你的實際路徑。

## 取得專案原始碼

若是從 GitHub clone 專案，請連同 submodule 一起下載：

```powershell
git clone --recursive https://github.com/TonyLiu0430/compiler_final.git
```

若已經 clone，但尚未下載 submodule，請在專案根目錄執行：

```powershell
git submodule update --init --recursive
```

LLVM 原始碼位於：

```text
third_party/llvm-project
```

## 設定 CMakePresets.json

本專案提供兩個主要 preset：

```text
gcc16-cxx26
gcc16-cxx26-llvm
```

`gcc16-cxx26` 用於一般建置。`gcc16-cxx26-llvm` 用於第一次 bootstrap LLVM X86 backend。

`CMakePresets.json` 內目前使用固定的 WinLibs 路徑，例如：

```text
C:/dev/winlibs-x86_64-posix-seh-gcc-16.0.1-snapshot20260222-mingw-w64ucrt-13.0.0-r1/mingw64/bin
```

如果你的 MinGW 解壓縮在不同位置，請修改以下欄位：

```json
"PATH": "C:/your/winlibs/path/mingw64/bin;$penv{PATH}",
"CMAKE_C_COMPILER": "C:/your/winlibs/path/mingw64/bin/gcc.exe",
"CMAKE_CXX_COMPILER": "C:/your/winlibs/path/mingw64/bin/g++.exe",
"CMAKE_MAKE_PROGRAM": "C:/your/winlibs/path/mingw64/bin/mingw32-make.exe"
```

同一份檔案底部的 CTest `PATH` 也要改成同一個 MinGW `bin` 目錄：

```json
"PATH=path_list_prepend:C:/your/winlibs/path/mingw64/bin"
```

路徑建議使用 `/`，例如 `C:/dev/.../mingw64/bin`，避免 JSON 裡反斜線需要跳脫。

## 第一次建置 LLVM

第一次建置前，先進入專案根目錄：

```powershell
cd E:\CODE_programming\compiler
```

Configure LLVM bootstrap：

```powershell
cmake --preset gcc16-cxx26-llvm
```

建置 LLVM X86 backend：

```powershell
cmake --build --preset gcc16-cxx26-llvm
```

這一步只需要做一次，時間會比較久。完成後，LLVM CMake package 會位於：

```text
build/gcc16-cxx26/llvm/lib/cmake/llvm
```

一般重新編譯本專案時，不需要再次使用 `gcc16-cxx26-llvm`。

## 編譯本專案編譯器

一般建置使用 `gcc16-cxx26` preset。

Configure：

```powershell
cmake --preset gcc16-cxx26
```

Build：

```powershell
cmake --build --preset gcc16-cxx26 -- -j2
```

如果電腦記憶體足夠，可以把 `-j2` 改成更高的平行數，例如 `-j4` 或 `-j8`。

主要輸出檔案：

```text
build/gcc16-cxx26/compiler.exe
build/gcc16-cxx26/compiler_tests.exe
build/gcc16-cxx26/runtime/c9ay_runtime.o
```

`compiler.exe` 是本專案產生的 C 編譯器。`c9ay_runtime.o` 是由本專案編譯器自行編譯出的 runtime object，之後產生 `.exe` 時會自動連結。

## 編譯器基本用法

基本格式：

```powershell
.\build\gcc16-cxx26\compiler.exe [options] <source.c>
```

常用選項：

```text
-o <path>      指定輸出檔案
-c             只產生 object file，不連結
-E             只執行前處理器
--emit-llvm    輸出 LLVM IR
-O0            不做最佳化，預設值
-O1            基本最佳化
-O2            標準最佳化
-O3            較積極最佳化
```

## 產生執行檔

範例 C 程式：

```c
#include <c9ay.h>

int main() {
    println("hello compiler");
    return 0;
}
```

假設檔案存成 `hello.c`，編譯成 `hello.exe`：

```powershell
.\build\gcc16-cxx26\compiler.exe hello.c -o hello.exe
```

執行：

```powershell
.\hello.exe
```

如果省略 `-o`，輸出檔名會使用輸入檔名並改成 `.exe`：

```powershell
.\build\gcc16-cxx26\compiler.exe hello.c
```

輸出檔案：

```text
hello.exe
```

## 只產生 object file

```powershell
.\build\gcc16-cxx26\compiler.exe hello.c -c -o hello.o
```

這會只產生 `hello.o`，不進行連結，也不產生 `.exe`。

## 輸出 LLVM IR

輸出到終端機：

```powershell
.\build\gcc16-cxx26\compiler.exe hello.c --emit-llvm
```

輸出到檔案：

```powershell
.\build\gcc16-cxx26\compiler.exe hello.c --emit-llvm -o hello.ll
```

## 只執行前處理器

輸出到終端機：

```powershell
.\build\gcc16-cxx26\compiler.exe hello.c -E
```

輸出到檔案：

```powershell
.\build\gcc16-cxx26\compiler.exe hello.c -E -o hello.i
```

## 最佳化選項

```powershell
.\build\gcc16-cxx26\compiler.exe hello.c -O0 -o hello.exe
.\build\gcc16-cxx26\compiler.exe hello.c -O1 -o hello.exe
.\build\gcc16-cxx26\compiler.exe hello.c -O2 -o hello.exe
.\build\gcc16-cxx26\compiler.exe hello.c -O3 -o hello.exe
```

預設為 `-O0`。需要觀察產生的 LLVM IR 或除錯時，建議使用 `-O0`。

## 使用內建 runtime

需要輸出文字或數字時，可以 include runtime header：

```c
#include <c9ay.h>
```

目前提供的常用函式：

```c
void write_text(char *text);
void write_char(char value);
void print(char *text);
void println(char *text);
void print_int(int value);
void print_long_long(long long value);
void print_unsigned_long_long(unsigned long long value);
int printf(char *format, ...);
```

範例：

```c
#include <c9ay.h>

int main() {
    print("answer = ");
    print_int(42);
    println("");
    return 0;
}
```

編譯與執行：

```powershell
.\build\gcc16-cxx26\compiler.exe example.c -o example.exe
.\example.exe
```

## printf 支援範圍

本編譯器提供 builtin `printf`。Format string 必須是 string literal，編譯器會在編譯期解析 format 並展開為 runtime 呼叫，不使用真正的 C varargs。

支援格式：

```text
%% %s %c %d %i %u %ld %lu %lld %llu %f
```

範例：

```c
#include <c9ay.h>

int main() {
    char *name = "value";
    int value = -42;
    unsigned long long count = 18446744073709551615ULL;
    double ratio = 0.5;
    printf("%s: %d %llu %f %%\n", name, value, count, ratio);
    return 0;
}
```

目前限制：

- `%f` 使用固定六位小數輸出
- 不支援 width 與 precision，例如 `%8d`、`%.2f`
- 不支援動態 format string
- `printf` 回傳值目前固定為 `0`

## 錯誤訊息

範例：

```c
int main() {
    return missing;
}
```

編譯：

```powershell
.\build\gcc16-cxx26\compiler.exe error.c -o error.exe
```

錯誤輸出形式：

```text
error.c:2:12: error: use of undeclared identifier 'missing'
 2 |     return missing;
   |            ^~~~~~~
```

如果錯誤來自 include file，編譯器會顯示 include stack，方便回到原始檔案定位問題。

## 執行測試

建置測試：

```powershell
cmake --build --preset gcc16-cxx26 --target compiler_tests -- -j2
```

直接執行測試程式：

```powershell
.\build\gcc16-cxx26\compiler_tests.exe
```

或使用 CTest：

```powershell
ctest --test-dir build/gcc16-cxx26 --output-on-failure
```

主要測試資料夾：

```text
test/compile_success
test/compile_failure
test/panic_recovery
test/new
tests/parser_test.cpp
tests/fixture_test.cpp
```

## 常見問題

### CMake 找不到 gcc.exe 或 g++.exe

請檢查 `CMakePresets.json` 內的以下欄位是否指向實際存在的檔案：

```text
CMAKE_C_COMPILER
CMAKE_CXX_COMPILER
CMAKE_MAKE_PROGRAM
PATH
```

也可以直接用 PowerShell 測試：

```powershell
C:\your\winlibs\mingw64\bin\g++.exe --version
C:\your\winlibs\mingw64\bin\mingw32-make.exe --version
```

### 找不到 LLVMConfig.cmake

代表還沒有 bootstrap LLVM，或 `C9AY_LLVM_DIR` 指到錯誤位置。請先執行：

```powershell
cmake --preset gcc16-cxx26-llvm
cmake --build --preset gcc16-cxx26-llvm
```

完成後再執行一般建置：

```powershell
cmake --preset gcc16-cxx26
cmake --build --preset gcc16-cxx26 -- -j2
```

### GCC 不認得 -freflection

表示使用的 GCC 版本不支援 C++26 reflection。請改用 WinLibs GCC 16 或更新版本，並確認 `CMakePresets.json` 指到新版 WinLibs 的 `g++.exe`。

### 執行 compiler.exe 時找不到 DLL

建置流程會嘗試把 MinGW runtime DLL 複製到 `compiler.exe` 所在目錄。如果仍然找不到 DLL，請把同一套 MinGW 的 `mingw64\bin` 加到 PATH：

```powershell
$env:PATH = "C:\your\winlibs\mingw64\bin;$env:PATH"
```

### 產生的 exe 需要 MinGW runtime 嗎

本編譯器產生的目標程式使用自訂 runtime，主要依賴 Windows `KERNEL32.dll`。它不依賴 MinGW CRT。MinGW runtime 主要是給 `compiler.exe` 本身使用。

## 目前限制

- 這是 C 語言子集，不是完整 ISO C 編譯器。
- `struct` 目前以命名 struct 為主。
- `sizeof` 只支援括號形式。
- `alignof` 只接受 type name。
- local static 具有 static storage duration，但 initializer 必須是常量初始化。
- runtime 尚未提供 `argc` 與 `argv`。
- executable 目前只明確連結 `kernel32`。
- `printf` 是編譯器 builtin，不是真正的 C varargs printf。
