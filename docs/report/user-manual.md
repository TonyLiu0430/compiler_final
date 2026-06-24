# C9AY C Compiler 使用手冊（初稿）

## 1. 系統需求

- Windows x86-64
- CMake 3.25 以上
- GCC 16 MinGW-w64，需支援 `-freflection`
- MinGW Makefiles
- 專案所附或已完成 bootstrap 的 LLVM X86 libraries

目前 `CMakePresets.json` 內含開發電腦上的 GCC 絕對路徑。若在其他電腦建置，需修改：

```json
CMAKE_C_COMPILER
CMAKE_CXX_COMPILER
CMAKE_MAKE_PROGRAM
PATH
```

## 2. 第一次建置 LLVM

LLVM 只需要 bootstrap 一次：

```powershell
cmake --preset gcc16-cxx26-llvm
cmake --build --preset gcc16-cxx26-llvm
```

完成後，LLVM X86 libraries 位於：

```text
build/gcc16-cxx26/llvm
```

一般建置不會重新編譯 LLVM。

## 3. 建置編譯器

Configure：

```powershell
cmake --preset gcc16-cxx26
```

Build：

```powershell
cmake --build --preset gcc16-cxx26 -- -j2
```

主要輸出：

```text
build/gcc16-cxx26/compiler.exe
build/gcc16-cxx26/compiler_tests.exe
build/gcc16-cxx26/runtime/c9ay_runtime.o
```

## 4. 編譯 C 程式

### 4.1 產生執行檔

```powershell
compiler.exe input.c -o output.exe
```

若省略 `-o`，輸出檔名使用輸入檔名並改為 `.exe`。

```powershell
compiler.exe hello.c
```

輸出：

```text
hello.exe
```

### 4.2 只產生 object

```powershell
compiler.exe input.c -c -o output.o
```

### 4.3 輸出 LLVM IR

輸出到終端：

```powershell
compiler.exe input.c --emit-llvm
```

輸出到檔案：

```powershell
compiler.exe input.c --emit-llvm -o output.ll
```

### 4.4 只執行前置處理

輸出到終端：

```powershell
compiler.exe input.c -E
```

輸出到檔案：

```powershell
compiler.exe input.c -E -o output.i
```

### 4.5 最佳化

```powershell
compiler.exe input.c -O0 -o output.exe
compiler.exe input.c -O1 -o output.exe
compiler.exe input.c -O2 -o output.exe
compiler.exe input.c -O3 -o output.exe
```

預設為 `-O0`。

## 5. 使用內建 runtime

Header：

```c
#include <c9ay.h>
```

提供函式：

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

編譯器也提供 builtin `printf`：

```c
printf("%s: %d %u %f %c %%\n", text, value, unsigned_value, ratio, ch);
```

支援：

```text
%% %s %c %d %i %u %ld %lu %lld %llu %f
```

Format 必須是 string literal。它會在編譯期展開，不使用真正的 varargs。
`%f` 目前使用固定六位小數輸出。目前不支援 width、precision 或動態 format，且回傳值固定為 `0`。

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
compiler.exe hello.c -o hello.exe
.\hello.exe
```

## 6. 錯誤訊息

範例：

```c
int main() {
    return missing;
}
```

輸出形式：

```text
file.c:2:12: error: use of undeclared identifier 'missing'
 2 |     return missing;
   |            ^~~~~~~
```

如果錯誤位於 include file，編譯器也會顯示 include stack。

## 7. 執行測試

建置測試：

```powershell
cmake --build --preset gcc16-cxx26 --target compiler_tests -- -j2
```

直接執行：

```powershell
.\build\gcc16-cxx26\compiler_tests.exe
```

或使用 CTest：

```powershell
ctest --test-dir build/gcc16-cxx26 --output-on-failure
```

測試資料夾：

```text
test/compile_success
test/compile_failure
test/panic_recovery
tests/parser_test.cpp
tests/fixture_test.cpp
```

## 8. 目前限制

- 這是 C 語言子集，不是完整 ISO C。
- struct 變數可使用 `Name value;` 或 `struct Name value;`。
- `sizeof` 只支援括號形式。
- `alignof` 只接受 type name。
- local static 具有 static storage duration，但 initializer 必須是常量初始化。
- runtime 尚未提供 `argc`/`argv`。
- executable 目前只明確連結 `kernel32`。
- compiler 本身以 MinGW GCC 建置，執行 compiler 時可能需要將同一套 MinGW `bin` 放入 PATH。
- compiler 產生的目標程式本身不使用 MinGW CRT，只依賴 Windows `KERNEL32.dll`。

## 9. 繳交包建議結構

```text
submission/
├─ compiler.exe
├─ required-mingw-dlls/
├─ src/
├─ runtime/
├─ test/
├─ tests/
├─ docs/
│  ├─ c-language.ebnf
│  └─ report/
│     ├─ report-draft.md
│     ├─ user-manual.md
│     └─ assets/
├─ CMakeLists.txt
├─ CMakePresets.json
└─ README.md
```

不建議把完整 LLVM build cache 放入作業壓縮檔，除非作業明確要求可在沒有預建 library 的環境重新建置。
