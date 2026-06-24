# 編譯器 期末專案 c - compiler

## 摘要
本專案實作了，一個 C 語言編譯器，由 c++ 撰寫 (gcc16 c++26 標準)，選取一個 C 語言子集，對其實作一個編譯器，以手寫的recursive descent配上 LLVM 後端，產生 機器無關最佳化 及 機器相關最佳化，與目的程式碼產生 (llvm ir 與 x86-64 binary obj 檔案)。
llvm 產生的 obj code 由 GNU 工具鏈 (由 mingw 提供) 的 ld.exe 連結至 windows api 函式庫(kernel32.dll) ，此為windows 官方提供的最低階 api，因為windows不保證 syscall 的穩定性，也不鼓勵開發者使用 系統調用。
產生的程式碼僅依賴於 kernel32.dll ，而完全不依賴於 msvcrt or ucrt 或 mingw 提供的 c runtime 所以 windows 電腦都可以順利執行。

---

## 專案目標
1. 從 C 原始碼產生可執行的 64 位元 Windows PE+ 檔案。
2. 實作前置處理、詞法分析、語法分析、語意分析與 LLVM code generation。
3. 實作 C 運算子優先級、指標、陣列、函式、結構、typedef 與初始化列表。
4. 對錯誤程式產生接近 GCC 風格的檔名、行號、欄位、原始碼與標記。
5. 使用 panic mode recovery，盡可能回報多個獨立錯誤。
6. 不依賴 CRT，改用專案 runtime 與 Windows API。

---

## 系統架構

整體編譯流程如下：

```text
C 原始碼
   │
   ▼
Preprocessor
巨集展開、條件編譯、include、source map
   │
   ▼
Scanner / Lexer
最長匹配 operator trie、token 分類
   │
   ▼
Parser
statement parser、declarator parser、Pratt expression parser
   │
   ▼
AST
Expression、Statement、Declaration、Switch body 等節點
   │
   ▼
Semantic Analyzer
scope、symbol、type、constant expression、diagnostic
   │
   ▼
LLVM Code Generator
LLVM IR、控制流程、資料配置、最佳化
   │
   ▼
LLVM Target
64-bit COFF object
   │
   ▼
MinGW ld.exe + c9ay runtime + kernel32 import library
   │
   ▼
Windows x86-64 PE+ 執行檔
```

---

### 主要目錄

| 路徑 | 用途 |
|---|---|
| `src/reader` | 原始碼讀取狀態與 diagnostic 的持有者 |
| `src/lexer` | scanner、operator trie、token 與 lexer |
| `src/preprocessor` | 巨集、條件編譯、include 與前置處理常量表達式 |
| `src/parser` | AST、statement、declarator、Pratt parser |
| `src/semantic` | 型別、symbol、scope、常量求值與語意分析 |
| `src/codegen` | LLVM IR、最佳化、object emission 與 linker |
| `src/diagnostic` | GCC 風格錯誤訊息與 source map |
| `runtime` | 不依賴 CRT 的 Windows runtime |
| `test` | C 語言 fixture 測試 |
| `tests` | doctest 測試程式 |
| `docs` | EBNF、TODO、報告與使用手冊 |

---

## 支援的語言功能

### 前置處理器

目前支援：

- `#define` object-like macro；
- function-like macro；
- `__VA_ARGS__` 可變參數巨集；
- `#` 字串化 如 #define str(x) #x -> "x"；
- `##` token 組合；
- `#include "file"` 與 `#include <file>`；
- `#if`、`#ifdef`、`#ifndef`；
- `#elif`、`#else`、`#endif`；
- `#undef`；
- `#error`；

### 型別

目前支援的主要型別：

- `void`
- `bool` (c26 標準已經將 bool 作為標準 bool 而已棄用 _Bool 與 stdbool.h)
- `char`
- `signed char`
- `unsigned char`
- `short`
- `unsigned short`
- `int`
- `unsigned int`
- `long`
- `unsigned long`
- `long long`
- `unsigned long long`
- `float`
- `double`
- `long double`
- 指標 (及函數指標)
- 陣列
- 命名 struct type (unnamed 沒寫)
- typedef 

abi 遵照 windows x64 約定 所以 long 是 32 bit 寬

### 宣告與初始化

目前支援：

- 多 declarator 宣告，如 `int a, b, c;`
- 每個 declarator 各自擁有指標層級，如 `int *p, a, **q;`
- 陣列與多維陣列
- 未指定第一維大小的陣列長度自動推導 如 `int a[] = {1, 2, 3}`
- 函式宣告與函式定義
- 函式指標
- `typedef`
- `const`
- global `static`、static function 與 block scope static 變數
- scalar initializer
- array initializer list
- nested initializer list
- struct initializer list
- character array 的字串初始化，如 `char s[] = "hello"`

### Statement

目前支援：

- block
- expression statement (expression 本身是合法的 statement)
- 變數宣告
- `if` / `else` (else if 由else + 下一個 if statement 組合成，符合 c 語言標準語法)
- `while`
- `do while`
- `for`
- `switch`、`case`、`default` 與 fallthrough
- `break`
- `continue`
- `return`

### Expression

### 4.5 Expression

Expression 使用 Pratt Parser，支援：

- identifier、number、character、string literal
- prefix `++ -- + - ! ~ * &`
- postfix `++ --`
- 函式呼叫
- 陣列取值 如 `arr[5]`
- `.` 與 `->` (struct 取 field 與 struct 指標取 field)
- cast 如 `(int)5`
- `sizeof(type)`
- `sizeof(expression)`
- `alignof(type)`
- 算術、比較、位元、邏輯與 shift operator
- 賦值 與 compound assignment
- `?:` 三元運算子
- `,` 逗號運算子
- 短路邏輯比較 `&&`、`||`

