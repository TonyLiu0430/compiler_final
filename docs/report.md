---
title: "C9ay C Compiler Report"
author: "Tony"
date: "2026"
documentclass: article
papersize: a4
geometry:
  - top=2.5cm
  - bottom=2.5cm
  - left=2.5cm
  - right=2.5cm
fontsize: 12pt
mainfont: "Noto Serif CJK TC"
monofont: "Consolas"
CJKmainfont: "Noto Serif CJK TC"
colorlinks: true
linkcolor: blue
urlcolor: blue
toc: true
toc-depth: 3
numbersections: false
---

# 編譯器 期末專案 c - compiler

## 摘要
本專案實作了，一個 C 語言編譯器，由 c++ 撰寫 (gcc16 c++26 標準)，選取一個 C 語言子集，對其實作一個編譯器，以手寫的recursive descent配上 LLVM 後端，產生 機器無關最佳化 及 機器相關最佳化，與目的程式碼產生 (llvm ir 與 x86-64 binary obj 檔案)。
llvm 產生的 obj code 由 GNU 工具鏈 (由 mingw 提供) 的 ld.exe 連結至 windows api 函式庫(kernel32.dll) ，此為windows 官方提供的最低階 api，因為windows不保證 syscall 的穩定性，也不鼓勵開發者使用 系統調用。
產生的程式碼僅依賴於 kernel32.dll ，而完全不依賴於 msvcrt or ucrt 或 mingw 提供的 c runtime 所以 windows 電腦都可以順利執行。

---

## 技術選擇與取捨 (前言)
專案採用手寫的遞迴下降parser，而不採用 bison + yacc 的傳統方案，雖然我原本以為遞迴下降只是一個比較簡單的實作，但是在觀察過現有編譯器實作後，發現大多數現今主流的編譯器都採用遞迴下降如 gcc、 clang 、golang 的 compiler 等等，讓我意識到其實遞迴下降才是主流。

編譯器的實作語言採用 c++ 撰寫，因為我只會 c++， 而且我愛 c++ ，且 c++ 在 c++26 (2026 預計 2027 定版) 版本推出了我期待已久的靜態反射功能，所以我迫不及待地想嘗試靜態反射在中大型軟體上的應用，所以編譯器只能選用最新版的 mingw toolchain，因為目前只有 gcc16 實作出對反射的支持，雖然 bloomberg 也有自己分叉出一版 clang 做出對反射的支持，但是那個要自己編譯要快 1 小時，而 MSVC 沒有對反射做出任何支持，看起來微軟需要多幾個懂編譯器前端的，所以最後是採用 mingw-gcc 作為本專案的編譯工具鏈，如果想要順利編譯本專案的話就要下載最新版的 mingw。

編譯器後端採用 llvm 作為產生機器碼的後端，原本是想直接產生 nasm 的，但是因為時間不夠，且只能實作簡單的優化，而 llvm 能進行相當複雜及工程化且經過驗證的優化，所以我打算先用 llvm 後面再嘗試手寫 nasm。

編譯產生的執行檔，並沒有選擇 link 傳統 c runtime ，而是採用自己寫一個簡易的 c library，因為反正我的編譯器也不能完整解析 gcc 的 header ，所以索性用自己的 runtime 還可以自己控制 entry point，所以產生的產物只需要link所編譯出來的 runtime 以及 windows 必須要 link (動態連結) 的 kernel32.dll (64位元)。

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

其中對於陣列長度等標準強制要求為常數的表達式 也有同時實作 編譯期求值

### 語意分析

目前語意分析包含：

- lexical scope 與 symbol table
- typedef name scope
- unknown type 與 undeclared identifier
- redefinition
- 函式參數數量與型別
- arithmetic conversion
- pointer conversion 與 pointer arithmetic
- lvalue 與 const 檢查
- array size constant expression
- `sizeof` / `alignof`
- struct member 檢查
- initializer 相容性與元素數量
- switch case 常量、duplicate case/default
- return type
- loop/switch context 中的 break/continue。

---

## 支援文法（EBNF）

以下為本專案目前實作的精簡 EBNF。Expression 使用允許左遞迴的表示方式，實際程式則以 Pratt Parser 根據 precedence table 解析。

完整獨立檔案位於：

- `docs/c-language.ebnf`
- `docs/c-language-detailed.ebnf`

其中 detailed 版本將 expression precedence 逐層展開，可作為傳統無歧義文法的參考。

```ebnf
program =
    { external-declaration } ;

external-declaration =
    function-definition
  | declaration
  | struct-definition ;

function-definition =
    declaration-specifiers,
    declarator,
    block ;

struct-definition =
    "struct", IDENTIFIER,
    "{", { declaration }, "}", ";" ;

declaration =
    declaration-specifiers,
    init-declarator-list,
    ";" ;

declaration-specifiers =
    { storage-class-specifier | type-qualifier },
    type-specifier ;

storage-class-specifier =
    "static" | "typedef" ;

type-qualifier =
    "const" ;

type-specifier =
    typedef-name
  | "void"
  | "bool"
  | "_Bool"
  | "struct", IDENTIFIER
  | "char"
  | "signed", [ "char" | integer-type ]
  | "unsigned", [ "char" | integer-type ]
  | integer-type
  | "float"
  | "double"
  | "long", "double" ;

integer-type =
    "int"
  | "short", [ "int" ]
  | "long", [ "int" ]
  | "long", "long", [ "int" ] ;

typedef-name =
    IDENTIFIER ;

init-declarator-list =
    init-declarator,
    { ",", init-declarator } ;

init-declarator =
    declarator,
    [ "=", initializer ] ;

declarator =
    { "*" },
    direct-declarator ;

direct-declarator =
    ( IDENTIFIER | "(", declarator, ")" ),
    { declarator-suffix } ;

declarator-suffix =
    "[", [ expression-without-top-level-comma ], "]"
  | "(", [ parameter-list ], ")" ;

parameter-list =
    parameter-declaration,
    { ",", parameter-declaration } ;

parameter-declaration =
    declaration-specifiers,
    [ declarator ] ;

initializer =
    expression-without-top-level-comma
  | initializer-list ;

initializer-list =
    "{",
    [ initializer, { ",", initializer }, [ "," ] ],
    "}" ;

statement =
    block
  | struct-definition
  | if-statement
  | switch-statement
  | for-statement
  | while-statement
  | do-while-statement
  | expression-statement
  | jump-statement ;

block =
    "{",
    { declaration | statement },
    "}" ;

if-statement =
    "if", "(", expression, ")",
    statement,
    [ "else", statement ] ;

switch-statement =
    "switch", "(", expression, ")",
    switch-body ;

switch-body =
    "{", { switch-section }, "}" ;

switch-section =
    switch-label,
    { switch-label },
    { statement } ;

switch-label =
    "case", expression, ":"
  | "default", ":" ;

while-statement =
    "while", "(", expression, ")", statement ;

do-while-statement =
    "do", statement,
    "while", "(", expression, ")", ";" ;

for-statement =
    "for", "(",
    for-initializer,
    [ expression ], ";",
    [ expression ], ")",
    statement ;

for-initializer =
    ";"
  | expression, ";"
  | declaration ;

expression-statement =
    [ expression ], ";" ;

jump-statement =
    "return", [ expression ], ";"
  | "break", ";"
  | "continue", ";" ;

type-name =
    { type-qualifier },
    type-specifier,
    { type-qualifier },
    [ abstract-declarator ] ;

abstract-declarator =
    { "*" },
    { "[", [ expression-without-top-level-comma ], "]" } ;

expression =
    primary-expression
  | "(", expression, ")"
  | "(", type-name, ")", expression
  | prefix-operator, expression
  | "sizeof", "(", ( type-name | expression ), ")"
  | "alignof", "(", type-name, ")"
  | expression, postfix-operation
  | expression, binary-operator, expression
  | expression, "?", expression, ":", expression ;

expression-without-top-level-comma =
    expression ;

primary-expression =
    IDENTIFIER
  | NUMBER
  | STRING_CONSTANT
  | CHAR_CONSTANT ;

postfix-operation =
    "[", expression, "]"
  | "(", [ argument-list ], ")"
  | ".", IDENTIFIER
  | "->", IDENTIFIER
  | "++"
  | "--" ;

argument-list =
    expression-without-top-level-comma,
    { ",", expression-without-top-level-comma } ;

prefix-operator =
    "++" | "--"
  | "+" | "-"
  | "!" | "~"
  | "*" | "&" ;

binary-operator =
    ","
  | "=" | "*=" | "/=" | "%=" | "+=" | "-="
  | "<<=" | ">>=" | "&=" | "^=" | "|="
  | "||" | "&&"
  | "|" | "^" | "&"
  | "==" | "!="
  | "<" | ">" | "<=" | ">="
  | "<<" | ">>"
  | "+" | "-"
  | "*" | "/" | "%" ;
```

Expression precedence 由低至高為：

```text
,
=  *=  /=  %=  +=  -=  <<=  >>=  &=  ^=  |=
?:
||
&&
|
^
&
==  !=
<  >  <=  >=
<<  >>
+  -
*  /  %
prefix 與 cast
()  []  .  ->  postfix ++  postfix --
```

Assignment 與 conditional operator 為右結合，其餘 binary operator 為左結合。

---

## 製作工具

### 開發工具

| 工具 | 用途 |
|---|---|
| C++26 | 編譯器主要實作語言 |
| GCC 16 MinGW-w64 | 編譯本專案之編譯器 |
| CMake 3.25+ | 建置與 preset 管理 |
| MinGW `ld.exe` | 編譯產物的連結器 linker |
| LLVM | IR、最佳化、x86-64 machine code 與 COFF object 產生 |
| doctest | 單元測試與 fixture test |
| Git | 版本控制 |
| Visual Studio Code | 程式撰寫與除錯 |
| ChatGPT | AI 協作完成編譯器程式碼 |

### 第三方元件

- LLVM Project：後端 IR 與 target code generation。
- doctest：C++ 測試框架。

---

## 各模組的製作方式

### Reader（`src/reader/`）

`Reader` 類別封裝原始碼的讀取與位置追蹤。它持有原始碼的 `string_view` 與一個共用的 `Diagnostic` 物件。提供 `next_char()`、`peek_next()` 等介面供上層使用，並可透過 `report_error()` 直接報告帶位置的錯誤。

### Preprocessor（`src/preprocessor/`）

前處理器是一個獨立的文字處理階段，支援：

- **巨集系統**：物件型巨集與函式型巨集（含可變參數 `__VA_ARGS__`）、`#` 字串化運算子、`##` 符號粘接
- **條件編譯**：`#if` / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif`，支援 `defined()` 運算子
- **檔案引入**：`#include "..."` 與 `#include <...>`，支援多層搜尋路徑
- **行拼接**：反斜線加換行的行續接處理

前處理器內建一個常數運算式求值器（`constant_expression.hpp`），用於 `#if` 條件的算術與邏輯運算。

特別值得注意的是 Source Map 機制：前處理器在展開巨集與引入檔案時，會記錄每一段輸出文字對應到哪個原始檔案的哪個範圍，使得後續階段的錯誤訊息能正確指向原始碼位置，而非展開後的中間結果。

### Scanner / Lexer（`src/lexer/`）

詞法分析分為兩層：

**Scanner**（低階）以手寫的字元掃描實作，辨識：
- 空白與換行
- 識別字（`[a-zA-Z_][a-zA-Z0-9_]*`）
- 數值常數（整數、浮點、科學記號、後綴）
- 字串常數與字元常數（含跳脫序列）
- 行註解 `//` 與區塊註解 `/* */`
- 標點符號與運算子 — 使用 Trie 資料結構最長匹配，一個靜態 `Punctuator_trie` 包含所有 C 運算子

**Lexer**（高階）在 Scanner 之上：
- 過濾空白、換行與註解
- **關鍵字辨識**使用 C++26 靜態反射 — `keyword_mapping()` 函式以 `template for` 遍歷 `token_type` 列舉中所有 `K_` 前綴的成員，自動將列舉名稱轉為小寫與識別字比對，無需手寫 map
- 區分 `PUNCTUATOR`（`{}#;`）與 `OPERATOR`（其餘所有運算符號）
- 驗證跳脫序列的合法性

`LexerMgr` 管理延遲 token 化（lazy tokenization），支援任意前瞻（lookahead）與回溯。

### Parser（`src/parser/`）

語法分析採用遞迴下降搭配 Pratt parsing 的混合策略：

- **運算式**：Pratt parser 以 binding power 表處理所有中綴、前綴、後綴運算子。`infix_binding_power()` 回傳 `(left_bp, right_bp)` 以正確處理左結合、右結合（賦值、三元）。特別處理 cast（`(type) expr`）與 `sizeof` / `alignof`，透過探測性解析（probe parsing）區分型別名稱與括弧運算式。
- **陳述式**：以反射驅動的 LL(1) 分派 — `dispatch_ll1_statement()` 使用 `template for` 在編譯期遍歷所有 `Statement` 衍生類別，若該類別含有 `static constexpr token_type start` 成員，即以此 token type 做為 LL(1) 的前瞻分派依據，自動呼叫對應的 `match()` 函式
- **宣告 vs 運算式**：`can_start_declaration()` 以探測性解析嘗試匹配 declaration-specifiers + declarator 前綴來決定
- **型別說明符**：`match_type_specifier()` 支援所有合法的型別詞組合與重排（如 `long unsigned int`、`unsigned long long` 等）

**錯誤復原（Panic Recovery）**：
- 每個 AST 節點型別定義自己的 `Panic_sync` 同步 token 集合
- 遇到語法錯誤時，parser 消耗 token 直到遇到同步 token（如 `;` 或 `}`），然後繼續解析
- AST 節點的 `error_occur` 旗標向上傳播，標記含有錯誤的子樹

### Semantic Analyzer（`src/semantic/`）

語意分析器遍歷 AST 執行以下檢查：

- **符號表**：巢狀作用域鏈，支援變數、函式、參數的宣告與查找
- **型別系統**：完整的型別階層（`Primitive_type`、`Pointer_type`、`Array_type`、`Function_type`、`Record_type`），以 `shared_ptr` 管理。支援 `const` 限定、型別衰減（array→pointer、function→pointer）
- **型別檢查**：賦值相容性、算術型別提升、指標算術合法性、函式呼叫引數匹配
- **常數求值**：在編譯期計算 `sizeof`、`alignof` 與 `case` 標籤的常數運算式
- **錯誤偵測**：未宣告識別字、重複定義、型別不相容、`break`/`continue` 出現在迴圈外、`switch` 重複 `case`/`default`、陣列大小非正整數等
- **內建 printf 驗證**：靜態解析 format string，檢查引數數量與型別

型別分派同樣使用反射 — `parser::reflect::dispatch<R>()` 以 `template for` 遍歷 `Expression` 的所有衍生類別，生成 `dynamic_cast` 鏈，將多型分派轉換為型別安全的 visitor 呼叫。

### LLVM Code Generation（`src/codegen/`）

程式碼生成模組使用 LLVM C++ API：

- **型別映射**：將 semantic 層的型別映射到 LLVM IR 型別（`i8`/`i16`/`i32`/`i64`/`float`/`double`/`x86_fp80`/pointer/array/struct/function）
- **運算式生成**：覆蓋所有 AST 運算式節點，產生對應的 LLVM 指令。特別處理短路求值（`&&`/`||`）、三元運算子的 phi node、指標算術的 GEP 指令
- **陳述式生成**：if/while/do-while/for 翻譯為 basic block 跳轉；switch 使用 LLVM SwitchInst；break/continue 使用迴圈目標堆疊
- **初始器**：支援巢狀初始器列表（陣列、結構體）、字串常數初始化
- **全域/靜態變數**：常數初始器在編譯期求值，產生 `GlobalVariable`
- **內建 printf**：以 c++ 模板(template)作為發想，在編譯時同時解析 format string 後直接生成 runtime 呼叫序列，避免依賴 C runtime 的 printf，以避免需要實作可變參數函數，並且可以在編譯期檢查是否寫錯 format 一舉兩得
- **最佳化**：支援 O0～O3，使用 LLVM legacy PassManager 執行 mem2reg、SROA、InstCombine、Reassociate、CFG Simplify、GVN、DSE、LICM、Loop Unroll 等 pass
- **目標碼產出**：呼叫 LLVM TargetMachine 產生 `.o` 目標檔；再呼叫 MinGW `ld` 連結 runtime 與 kernel32.dll 產生 `.exe`

### Runtime Library（`runtime/`）

一個極簡的自包含 runtime，以編譯器自身編譯：

- 入口點 `_start()` 呼叫 `main()` 後以 `ExitProcess()` 結束
- I/O 直接呼叫 Windows API `WriteFile()` / `GetStdHandle()`，不依賴 C runtime
- 提供 `print_int`、`print_long_long`、`print_unsigned_long_long`、`print_float`、`write_text`、`write_char` 等函式

---

# 製作成果

## 測試程式與執行結果

為了方便截圖，本報告只展示 10 個短檔名測試，全部放在 `test/new`。成功案例會直接輸出結果，不再用 `return` 值當作答案；錯誤案例則用來展示 diagnostic 與 panic recovery。

編譯成功案例的指令格式：

```powershell
.\build\gcc16-cxx26\compiler.exe test/new/1.c -o .temp/1.exe
.\.temp\1.exe
```

錯誤案例只需要執行 compiler 並截錯誤訊息：

```powershell
.\build\gcc16-cxx26\compiler.exe test/new/9.c -o .temp/9.exe
```

### 1. 基本程式：`test/new/1.c`

```c
int main() {
    return 0;
}
```

預期行為：編譯成功並產生 `.exe`，執行後沒有輸出，exit code 為 0。

![](report-image/1.png)

### 2. 整數算術：`test/new/2.c`

```c
#include <c9ay.h>

int main() {
    int a = 10;
    int b = 3;
    int sum = a + b;
    int diff = a - b;
    int prod = a * b;
    int quot = a / b;
    int rem = a % b;
    printf("sum = %d\n", sum);
}
```

![](report-image/2.png)

### 3. 控制流程：`test/new/3.c`

```c
#include <c9ay.h>
int main() {
    int result = 0;
    for (int i = 0; i < 5; i++) {
        if (i == 2) continue;
        result += i;
    }
    while (result < 20) result++;
    do {
        result--;
    } while (result > 19);
    printf("%d", result);
}
```

![](report-image/3.png)

### 4. 指標：`test/new/4.c`

```c
#include <c9ay.h>
int main() {
    int value = 40;
    int* pointer = &value;
    *pointer += 2;
    print_int(*pointer);
}
```

![](report-image/4.png)

### 5. struct 與成員存取：`test/new/5.c`

```c
#include <c9ay.h>

struct Point {
    int x;
    int y;
};

int main() {
    struct Point p;
    p.x = 3;
    p.y = 4;

    struct Point *ptr = &p;
    printf(
        "test 5: point = (%d, %d), sum = %d\n",
        ptr->x,
        ptr->y,
        ptr->x + ptr->y);
    return 0;
}
```

![](report-image/5.png)

### 6. 陣列：`test/new/6.c`

```c
#include <c9ay.h>

int main() {
    int values[] = {1, 2, 3, 4, 5};
    int sum = 0;

    for (int i = 0; i < 5; i++) {
        sum = sum + values[i];
    }

    printf("test 6: array sum = %d\n", sum);
    return 0;
}
```

![](report-image/6.png)

### 7. 型別與浮點輸出：`test/new/7.c`

```c
#include <c9ay.h>

int main() {
    bool ok = 1;
    long long big = 1234567890123LL;
    double half = 0.5;

    printf("test 7: bool = %d\n", ok);
    printf("test 7: long long = %lld\n", big);
    printf("test 7: double = %f\n", half);
    return 0;
}
```

![](report-image/7.png)

### 8. 前處理器：`test/new/8.c`

```c
#include <c9ay.h>

#define BASE 40
#define ADD(left, right) ((left) + (right))

int main() {
#if BASE == 40
    int value = ADD(BASE, 2);
#else
    int value = 0;
#endif

    printf("test 8: preprocessor value = %d\n", value);
    return 0;
}
```

![](report-image/8.png)

### 9. 錯誤報告：未宣告識別字 `test/new/9.c`

```c
int main() {
    return missing;
}
```

預期會報告：

```text
error: use of undeclared identifier 'missing'
```

![](report-image/9.png)

### 10. Panic recovery：`test/new/10.c`

```c
int main() {
    int broken = ;
    int still_seen = 42;
    return still_seen;
}
```

預期會先報告 initializer expression 缺失，並繼續解析後面的宣告與 `return`。

![](report-image/10.png)

### 11. Panic recovery 連環錯誤：`test/new/11.c`

```c
#include <c9ay.h>

int add(int left, int right) {
    return left + right;
}

int main() {
    int first = ;
    int second = 10 + ;

    if () {
        println("unreachable");
    }

    int third = add(1, );
    int values[3] = {1, 2, 3};
    int fourth = values[];

    println("panic recovery should still reach here");
    return first + second + third + fourth;
}
```

編譯器實作 panic mode recovery 所以可以辨識出多個錯誤。

![](report-image/11.png)

### 12. 綜合測試：`test/new/12.c`

```c
#include <c9ay.h>

struct Item {
    int id;
    int price;
    int count;
};

int item_total(struct Item *item) {
    return item->price * item->count;
}

int clamp_discount(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 30) {
        return 30;
    }
    return value;
}

int main() {
    struct Item items[4] = {
        {1, 120, 2},
        {2, 80, 5},
        {3, 40, 10},
        {4, 300, 1},
    };

    int subtotal = 0;
    int expensive_count = 0;

    for (int i = 0; i < 4; i++) {
        int total = item_total(&items[i]);
        subtotal = subtotal + total;

        if (total >= 300) {
            expensive_count++;
        }
    }

    int discount_percent = clamp_discount(expensive_count * 8);
    int discount = subtotal * discount_percent / 100;
    int final_total = subtotal - discount;

    printf("subtotal = %d\n", subtotal);
    printf("discount = %d%%\n", discount_percent);
    printf("final = %d\n", final_total);

    return 0;
}
```

一個比較複雜的程式

![](report-image/12.png)

## 命令列介面

```
compiler [options] <source.c>

選項：
  -o <path>      指定輸出路徑
  -c             僅編譯為目標檔 (.o)，不連結
  -E             僅執行前處理，輸出展開後的原始碼
  --emit-llvm    輸出 LLVM IR 文字格式
  -O0            無最佳化（預設）
  -O1            基本最佳化
  -O2            標準最佳化
  -O3            積極最佳化
```

## 自動化測試

測試框架使用 doctest，測試分為三類：

| 測試套件 | 測試案例數 | 說明 |
|----------|-----------|------|
| compile success | 22 個 | 確認正確程式可通過編譯與 LLVM codegen |
| compile failure | 33 個 | 確認錯誤程式產生正確數量與內容的錯誤訊息 |
| panic recovery | 8 個 | 確認語法錯誤後能正確復原並繼續解析 |

執行 `ctest` 即可自動跑完所有測試。

---

# 製作的結論

本專案成功使用手寫的遞迴下降解析器與llvm製作出可執行的 c 語言 子集編譯器，能夠與自訂 c runtime link 以達成真正可用的 可執行檔。

# 製作過程遇到的困難與心得


這個專案花了很多時間，從一開始選擇技術方向，例如到底要不要使用 bison ，到後來考慮是否要使用手寫 nasm 等，結果後來決定還是轉而用 llvm來完成。

雖然一開始就知道 c++26 有反射相關提案，但也是因為這個編譯器專案才去找後發現 gcc16 已經實作了反射相關功能，且 mingw 也發布了 與之對應的 windows 版，但使用上還是遇到不少問題，例如 使用了 c++26 反射後 vscode 的語法提示基本上就廢了，因為語法提示不認識 c++26 反射的語法，所以基本上都會報語法錯誤，連帶的舊的語法也無法被正確辨識，其實 vscode 的語法提示也是編譯器前端的一種，或許可以在之後運用這堂課所學的知識，對vscode c++ extension做出修補與貢獻。

還有就是 cmake llvm 的最佳實踐問題，與取捨 其實可以採用 llvm 預編譯專案來大幅降低編譯時間，但是觀察過各個編譯器的作法，後來還是採用 submodule + 自己編譯來實現比較好的實踐，也避免不同編譯器不相容的問題，因為 mingw 通常換個版本就炸了，而我又是用最新版 mingw。

mingw runtime 問題，因為另外下載了新版的 mingw ，跟 system path 的不一樣，所以執行時被這個搞到，因為 runtime 會抓到舊版的，而且根本不會跳錯誤訊息，解決方法是通靈出問題後，在cmake加入將 mingw runtime 複製至 編譯器執行檔所在的資料夾以解決這個問題。

總結來說，能完成一個自己的編譯器還是非常開心，且也因為不是使用bison所以可以達到不錯的錯誤訊息效果。

# 參考文獻

- LLVM Project Documentation (llvm 相關參考), https://llvm.org/docs/ 
- WG21 P2996R：Reflection for C++26 (c++26 反射相關提案), https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2996r8.html
- C++26 静态反射提案解析 (中文 c++26 反射參考) , https://zhuanlan.zhihu.com/p/661692275
- Aleksey Kladov, “Simple but Powerful Pratt Parsing” (Pratt parser 參考), https://matklad.github.io/2020/04/13/simple-but-powerful-pratt-parsing.html

- fatbrother/toyc (學長的編譯器專案1), https://github.com/fatbrother/toyc
- fatbrother/DDLBX (學長的編譯器專案2), https://github.com/fatbrother/DDLBX
- lkvlkvlkv/compiler_hw (學長的編譯器專案3), https://github.com/lkvlkvlkv/compiler_hw

