# C9AY C 編譯器 書面報告

> 草稿。姓名、學號、課程名稱與日期待補。截圖請置於 `docs/report/assets/`。

---

## 摘要

本專案實作一個 C 語言子集編譯器，以 C++26 (GCC 16 MinGW-w64) 撰寫。前端包含前置處理器、詞法分析器、語法分析器、抽象語法樹、語意分析與診斷系統；後端使用 LLVM 產生 LLVM IR 與 x86-64 COFF object，最後直接呼叫 GNU ld.exe 連結，輸出 64 位元 Windows PE+ 執行檔。

產生的程式**不依賴 C Runtime**。專案自行提供 `_start` 進入點，透過 `ExitProcess` 結束程式；基本輸出使用 `GetStdHandle` 與 `WriteFile` 等 Windows API。最終執行檔僅明確連結 `kernel32.dll`。

目前測試涵蓋 22 個預期成功的 C fixture、33 個預期失敗的 C fixture、8 個 panic mode recovery，以及 68 個 doctest 測試案例（共 673 個 assertions），全部通過。

---

## 專案目標

- 從 C 原始碼產生可執行的 64 位元 Windows PE+ 檔案
- 實作前置處理、詞法分析、語法分析、語意分析與 LLVM code generation
- 實作 C 運算子優先級、指標、陣列、函式、結構、typedef 與初始化列表
- 對錯誤程式產生接近 GCC 風格的檔名、行號、欄位、原始碼與標記
- 使用 panic mode recovery，盡可能回報多個獨立錯誤
- 不依賴 CRT，改用專案 runtime 與 Windows API

---

## 系統架構

整體編譯流程：

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
| `docs` | EBNF、報告與使用手冊 |

---

## 實作重點

### 詞法分析

operator 具有共同 prefix（如 `<`、`<=`、`<<`），若持續貪婪讀取容易合併成不存在的 operator。實作以 trie 列出所有 symbolic operator，使用 longest match 取得正確 token。

keyword mapping 使用 C++26 reflection（`std::meta::enumerators_of`）自動由 `token_type::K_IF` 產生 `"if"`，不須手動維護 keyword table。

前置處理器與 lexer 共用 scanner，避免重複實作字串、註解、空白與 token 邊界辨識。

### 前置處理

支援 object-like、function-like、variadic macro（`__VA_ARGS__`）、`#` stringify、`##` token paste、`#if` / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif`、`#include`（ `"file"` 與 `<file>` ）、`#undef`、`#error`。

前置處理器建立 source map，為每段輸出記錄原始檔案、原始位置與 include stack，使 include 後的錯誤訊息仍能正確指向 header 原始行號。

### 語法分析

Statement 依起始 keyword 進行 LL(1) dispatch。declaration 與 expression statement 可能同樣以 identifier 開頭，使用 probe lexer 與 diagnostic checkpoint 嘗試解析，probe 成功才 sync lexer，失敗則 rollback diagnostic，避免誤判。

Expression 使用 Pratt Parser，將 precedence 與 associativity 集中於 binding power table。assignment 使用相同左右 binding power 形成右結合，其餘 operator 使用遞增 binding power 形成左結合。

Statement dispatch 使用 C++26 reflection 自動列舉 derived type，依 `static constexpr start` 自動對應到個別的 `match`，不須手動維護 dispatch table。

Switch 使用專用 AST 節點（Switch_body、Switch_section、Switch_label），parser 直接建立 code generator 所需的分段結構，不須在後端掃描一般 block 再猜測 case 分段。

### 語意分析

建立 scope stack、symbol table、type table。實作 usual arithmetic conversions、pointer conversions、null pointer constant 判斷。array size inference（initializer list 或 string literal）。

提供 compiler builtin `printf`。format 必須是 string literal，在編譯期解析 format string，檢查 argument 數量與型別是否相符（`%s` 需 character pointer、integer format 需 integer argument、`%f` 需 floating argument）。不使用 C varargs ABI，編譯期展開為對應的 runtime 輸出函式呼叫。

### 程式碼產生

語意分析完成後，AST 轉換為 LLVM IR。實作 local alloca、global variable、function declaration/definition/call、integer 與 floating arithmetic、pointer arithmetic、array decay、subscript、struct member access、if/loop/switch control flow、short-circuit logical expression（phi node）、initializer list、cast 與 type conversion。

最佳化支援 `-O0`、`-O1`、`-O2`、`-O3`，使用 LLVM legacy pass manager 加入 mem2reg、SROA、instruction combining、reassociate、CFG simplification、GVN、dead store elimination、LICM、loop unroll 與 dead code elimination 等 pass。

### 連結與執行

不經 gcc driver，直接呼叫 GNU ld.exe 連結 program object、c9ay_runtime.o 與 kernel32 import library，產生無 CRT 的 x86-64 PE+ 執行檔。

runtime 提供 `_start` entry point（`ExitProcess(main())`）與基礎輸出函式（`write_text`、`write_char`、`print_int`、`println` 等），僅依賴 `kernel32.dll`。

### 錯誤診斷

輸出形式接近 GCC：檔名、行號、欄位、原始碼行與 marker（`^` 與 `~`）。source map 支援 include stack 顯示。panic mode recovery：expression 與 statement 各有 sync token，解析失敗後跳至 `;`、`}`、`)`、`]`、`,`、`|` 等同步點，避免一個錯誤造成後續全部語法被誤判。

---

## 支援的語言功能

### 前置處理器

- `#define` object-like macro、function-like macro、variadic macro (`__VA_ARGS__`)
- `#` stringify、`##` token paste
- `#include "file"`、`#include <file>`
- `#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif`
- `#undef`、`#error`
- include stack 與 source map 診斷

### 型別

- `void`
- `bool`（c26 標準關鍵字，棄用 `_Bool` 與 stdbool.h）
- `char`、`signed char`、`unsigned char`
- `short`、`unsigned short`
- `int`、`unsigned int`
- `long`、`unsigned long`
- `long long`、`unsigned long long`
- `float`、`double`、`long double`
- 指標（含函數指標）、陣列
- 命名 struct type、typedef alias

ABI 遵照 Windows x64 約定，`long` 為 32 bit，pointer 為 64 bit。

### 宣告與初始化

- 多 declarator 宣告，如 `int *p, a, **q;`
- 陣列與多維陣列，未指定第一維大小時自動推導長度
- 函式宣告與定義、函式指標
- `typedef`、`const`、global `static`、static function、block scope static 變數
- scalar、array、nested、struct initializer list
- character array 的字串初始化，如 `char s[] = "hello"`

### Statement

- block、expression statement、declaration statement
- `if` / `else`（else if 由 else + 下一個 if statement 組成）
- `while`、`do while`、`for`
- `switch`、`case`、`default` 與 fallthrough
- `break`、`continue`、`return`

### Expression

Expression 使用 Pratt Parser，支援：

- identifier、number、character、string literal
- prefix `++ -- + - ! ~ * &`
- postfix `++ --`、函式呼叫、陣列取值、`.` 與 `->`
- cast，如 `(int)5`
- `sizeof(type)`、`sizeof(expression)`、`alignof(type)`
- 算術、比較、位元、邏輯與 shift operator
- 賦值與 compound assignment
- `?:` 三元運算子、`,` 逗號運算子
- 短路邏輯 `&&`、`||`

---

## 文法規則（EBNF）

以下為精簡 EBNF。Expression 允許左遞迴，實際以 Pratt Parser 根據 precedence table 解析。完整版本見 `docs/c-language.ebnf` 與 `docs/c-language-detailed.ebnf`。

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

Expression precedence 由低至高：

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

Assignment 與 conditional operator 右結合，其餘 binary operator 左結合。

---

## 使用工具

| 工具 | 用途 |
|---|---|
| C++26 | 編譯器主要實作語言 |
| GCC 16 MinGW-w64 | 編譯本專案 |
| CMake 3.25+ | 建置與 preset 管理 |
| MinGW Makefiles | Windows 建置系統 |
| LLVM | IR、最佳化、x86-64 machine code 與 COFF object 產生 |
| GNU ld.exe | 直接連結 64 位元 Windows PE+ |
| doctest | 單元測試與 fixture test |
| Git | 版本控制 |
| Visual Studio Code | 程式撰寫與除錯 |

---

## 測試設計

### 成功編譯測試

`test/compile_success` 目前包含 22 個案例，涵蓋 return、integer arithmetic、if/else、loops、switch、pointers、arrays、multidimensional arrays、typedef、struct、recursive struct pointer、function pointer、sizeof/alignof、global/static、preprocessor、runtime include、cast/conditional、array inference、integer types、floating types、pointer difference、builtin printf。

### 失敗編譯測試

`test/compile_failure` 目前包含 33 個案例，涵蓋 unknown type、undeclared identifier、global/local redefinition、break/continue context、return mismatch、function call error、invalid dereference/address-of、const assignment、invalid pointer arithmetic、invalid array size、too many initializers、string too long、duplicate switch labels、struct member error、invalid alignof/sizeof、incompatible initializer/return、invalid subscript、float modulo、invalid type combination、dynamic printf format。

每個 fixture 以註解標記預期錯誤數與關鍵訊息，例如：

```c
// EXPECT-ERRORS: 1
// EXPECT: use of undeclared identifier
```

### Panic Recovery 測試

`test/panic_recovery` 目前包含 8 個案例。除了錯誤訊息，也驗證 recovery 後保留的 external declaration 或 main statement 數量，避免僅以「沒有 crash」作為 recovery 成功標準。

### 測試數據

```text
[doctest] test cases:  68 |  68 passed | 0 failed | 0 skipped
[doctest] assertions: 673 | 673 passed | 0 failed
[doctest] Status: SUCCESS!
```

---

## 錯誤診斷範例

### undeclared identifier

```c
int main() {
    return missing;
}
```

輸出：

```text
test\compile_failure\02_undeclared_identifier.c:4:12: error: use of undeclared identifier 'missing'
 4 |     return missing;
   |            ^~~~~~~
```

### unknown type

```c
Unknown value;
```

輸出：

```text
test\compile_failure\01_unknown_type.c:3:1: error: unknown type 'Unknown'
 3 | Unknown value;
   | ^~~~~~~
```

### called expression is not a function

```c
int main() {
    int value = 1;
    return value();
}
```

輸出：

```text
test\compile_failure\10_call_non_function.c:5:12: error: called expression is not a function
 5 |     return value();
   |            ^~~~~
```

### struct has no member

```c
struct Pair {
    int value;
};

int main() {
    Pair pair = {1};
    return pair.missing;
}
```

輸出：

```text
test\compile_failure\21_missing_struct_member.c:9:18: error: struct 'Pair' has no member 'missing'
 9 |     return pair.missing;
   |                  ^~~~~~~
```

---

## 正確程式執行範例

### 基本 return

```c
int main() {
    return 0;
}
```

編譯：`compiler.exe 01_basic_return.c`
輸出：`01_basic_return.exe`

### struct 與函式呼叫

```c
struct Pair {
    int first;
    int second;
};

int sum(struct Pair *pair) {
    return pair->first + pair->second;
}

int main() {
    struct Pair value = {20, 22};
    return sum(&value);
}
```

編譯後執行，return value 為 42。

### switch 與 fallthrough

```c
int classify(int value) {
    switch (value) {
        case 1:
            return 10;
        case 2:
        case 3:
            return 20;
        default:
            return 30;
    }
}

int main() {
    return classify(2);
}
```

編譯後執行，return value 為 20。

### 前置處理器巨集

```c
#define VALUE 40
#define ADD(left, right) ((left) + (right))

int main() {
    return ADD(VALUE, 2);
}
```

編譯後執行，return value 為 42。

### 使用 runtime 輸出

```c
#include <c9ay.h>

int main() {
    println("Hello from C9AY");
    print_int(42);
    println("");
    return 0;
}
```

編譯：

```powershell
compiler.exe hello.c -o hello.exe
.\hello.exe
```

預期輸出：

```text
Hello from C9AY
42
```

---

## 截圖待補清單

| 編號 | 內容 | 建議檔名 |
|---|---|---|
| 1 | CMake configure 與 build 成功畫面 | `docs/report/assets/01-build-success.png` |
| 2 | doctest 全部通過畫面 | `docs/report/assets/02-test-summary.png` |
| 3 | 原始碼、編譯成功、執行檔存在與執行輸出 | `docs/report/assets/03-valid-program.png` |
| 4 | 錯誤診斷的 GCC 風格輸出 | `docs/report/assets/04-invalid-diagnostic.png` |
| 5 | 同一檔案回報多個錯誤，且後續程式仍被分析 | `docs/report/assets/05-panic-recovery.png` |
| 6 | include stack 與 header diagnostic | `docs/report/assets/06-include-diagnostic.png` |
| 7 | PE 格式、architecture 與 import table | `docs/report/assets/07-no-crt-pe.png` |

---

## 結論與心得

> （此處留給作者自行撰寫）

---

## 參考文獻

- ISO/IEC 9899，*Programming Languages — C*。
- cppreference，C language reference，<https://en.cppreference.com/w/c/language>。
- LLVM Project，LLVM Language Reference Manual，<https://llvm.org/docs/LangRef.html>。
- LLVM Project，Writing an LLVM Backend / Code Generation 文件，<https://llvm.org/docs/>。
- LLVM Project，LLVM source code，<https://github.com/llvm/llvm-project>。
- GNU Binutils，GNU `ld` documentation，<https://sourceware.org/binutils/docs/ld/>。
- Microsoft Learn，Windows API documentation，<https://learn.microsoft.com/windows/win32/api/>。
- Microsoft Learn，PE Format，<https://learn.microsoft.com/windows/win32/debug/pe-format>。
- Bob Nystrom，*Crafting Interpreters*，<https://craftinginterpreters.com/>。
- doctest，C++ testing framework，<https://github.com/doctest/doctest>。
- CMake Documentation，<https://cmake.org/documentation/>。
