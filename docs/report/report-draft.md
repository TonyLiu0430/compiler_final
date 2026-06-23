# C9AY C 語言子集編譯器書面報告（初稿）

> 本文件為初稿。姓名、學號、課程名稱、日期與成果截圖仍需在繳交前補齊。

## 封面資料

- 課程名稱：`[待填]`
- 作業名稱：編譯器實作
- 專案名稱：C9AY C Compiler
- 姓名：`[待填]`
- 學號：`[待填]`
- 繳交日期：`[待填]`

---

## 一、摘要

本專案實作一個可於 Windows x86-64 環境執行的 C 語言子集編譯器。編譯器以 C++26 撰寫，前端包含前置處理器、詞法分析器、語法分析器、抽象語法樹、語意分析、型別系統、常量求值與錯誤診斷；後端使用 LLVM 產生 LLVM IR 與 64 位元 COFF object，最後直接呼叫 MinGW GNU `ld.exe` 產生 Windows PE+ 執行檔。

產生的程式不依賴 C Runtime（CRT）。專案自行實作 `_start` 進入點，並透過 `ExitProcess` 結束程式；基本輸出函式則使用 `GetStdHandle` 與 `WriteFile` 等 Windows API。最終執行檔目前只明確連結 `kernel32`。

專案同時包含成功編譯、預期編譯失敗與 panic mode recovery 三類測試。目前共有：

- 22 個預期成功的 C 程式測試；
- 33 個預期失敗的 C 程式測試；
- 8 個 panic mode recovery 測試；
- 68 個 doctest 測試案例，共 673 個 assertions。

在目前版本中，全部 doctest 測試皆通過。

---

## 二、專案目標與範圍

本專案的目標不是完整重製 ISO C，而是實作一個具有完整編譯流程、可產生真實 Windows 執行檔，且錯誤處理足以支援實際除錯的 C 語言子集。

主要目標如下：

1. 從 C 原始碼產生可執行的 64 位元 Windows PE+ 檔案。
2. 實作前置處理、詞法分析、語法分析、語意分析與 LLVM code generation。
3. 建立具有明確欄位的 AST，而非使用通用 children 陣列表示所有節點。
4. 實作 C 運算子優先級、指標、陣列、函式、結構、typedef 與初始化列表。
5. 對錯誤程式產生接近 GCC 風格的檔名、行號、欄位、原始碼與標記。
6. 使用 panic mode recovery，在單一原始碼內盡可能回報多個獨立錯誤。
7. 不依賴 CRT，改用專案 runtime 與 Windows API。

---

## 三、系統架構

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

### 3.1 主要目錄

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

## 四、支援的語言功能

### 4.1 前置處理器

目前支援：

- `#define` object-like macro；
- function-like macro；
- variadic macro 與 `__VA_ARGS__`；
- `#` stringify；
- `##` token paste；
- `#undef`；
- `#include "file"` 與 `#include <file>`；
- `#if`、`#ifdef`、`#ifndef`；
- `#elif`、`#else`、`#endif`；
- `#error`；
- include 後的 source map 與 include stack diagnostic。

前置處理器與一般 lexer 共用 scanner，避免重複實作字串、註解、空白與 token 邊界辨識。

### 4.2 型別

目前支援的主要型別：

- `void`
- `bool`（並保留 `_Bool` 相容拼法）
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
- pointer
- array
- function type
- named struct type
- typedef alias

Windows x64 採 LLP64 資料模型，因此 `long` 為 32 位元，pointer 與 `ptrdiff` 使用 64 位元。

### 4.3 宣告與初始化

目前支援：

- 多 declarator 宣告，例如 `int a, b, c;`
- 每個 declarator 各自擁有指標層級，例如 `int *p, a, **q;`
- 陣列與多維陣列；
- 未指定第一維大小的陣列推導；
- 函式宣告與函式定義；
- 函式指標 declarator；
- `typedef`；
- `const`；
- global `static` 與 static function；
- scalar initializer；
- array initializer list；
- nested initializer list；
- struct initializer list；
- character array 的字串初始化。

### 4.4 Statement

目前支援：

- block；
- expression statement；
- declaration statement；
- `if` / `else`；
- `while`；
- `do while`；
- `for`；
- `switch`、`case`、`default` 與 fallthrough；
- `break`；
- `continue`；
- `return`；
- block scope struct definition。

`switch` 使用專用 AST：

```text
Switch_body
└─ Switch_section[]
   ├─ Switch_label[]
   └─ Statement[]
```

因此 code generator 不需要把一般 block 當成 switch body 後，再以 RTTI 掃描並猜測 case 分段。

### 4.5 Expression

Expression 使用 Pratt Parser，支援：

- identifier、number、character、string literal；
- prefix `++ -- + - ! ~ * &`；
- postfix `++ --`；
- 函式呼叫；
- array subscript；
- `.` 與 `->`；
- cast；
- `sizeof(type)`；
- `sizeof(expression)`；
- `alignof(type)`；
- 算術、比較、位元、邏輯與 shift operator；
- assignment 與 compound assignment；
- `?:` conditional expression；
- comma operator；
- short-circuit `&&`、`||`。

另外提供 compiler builtin `printf`。它不使用 C varargs ABI，而要求
format 為 string literal，並在編譯期展開為 runtime 輸出函式呼叫。目前
支援 `%%`、`%s`、`%c`、`%d`、`%i`、`%u`、`%ld`、`%lu`、
`%lld` 與 `%llu`。

### 4.6 語意分析

目前語意分析包含：

- lexical scope 與 symbol table；
- typedef name scope；
- unknown type 與 undeclared identifier；
- redefinition；
- 函式參數數量與型別；
- arithmetic conversion；
- pointer conversion 與 pointer arithmetic；
- lvalue 與 const 檢查；
- array size constant expression；
- `sizeof` / `alignof`；
- struct member 檢查；
- initializer 相容性與元素數量；
- switch case 常量、duplicate case/default；
- return type；
- loop/switch context 中的 break/continue。

---

## 五、支援文法（EBNF）

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

## 六、製作工具

### 6.1 開發工具

| 工具 | 用途 |
|---|---|
| C++26 | 編譯器主要實作語言 |
| GCC 16 MinGW-w64 | 編譯本專案與實驗性 C++26 reflection |
| CMake 3.25+ | 建置與 preset 管理 |
| MinGW Makefiles | Windows 建置系統 |
| LLVM | IR、最佳化、x86-64 machine code 與 COFF object 產生 |
| GNU `ld.exe` | 直接連結 64 位元 Windows PE+ |
| doctest | 單元測試與 fixture test |
| Git | 版本控制 |
| Visual Studio Code | 程式撰寫與除錯 |

### 6.2 第三方元件

- LLVM Project：後端 IR 與 target code generation。
- doctest：C++ 測試框架。
- MinGW-w64 Windows import libraries：提供 `kernel32` import library。

專案早期曾使用 `magic_enum`，目前關鍵 keyword 與 dispatch 邏輯已逐步改用 C++26 reflection 或明確資料結構。

---

## 七、製作過程

### 7.1 Scanner 與 Lexer

最初先完成 Reader 與基本 token 辨識，再將 lexer 與 preprocessor 共通的字元掃描邏輯抽成 scanner。

C 運算子具有共同 prefix，例如：

```text
<  <=  <<
>  >=  >>
-  -=  --  ->
```

若只以「看到 operator 字元就持續讀取」處理，容易把不存在的 operator 組合錯誤合併。因此後來改以明確列出所有 symbolic operator 的 trie，使用 longest match 取得 token。

### 7.2 Parser 與 AST

Statement 主要依照起始 keyword 進行 LL(1) dispatch；declaration 因為 typedef name 與 expression 可能同樣以 identifier 開始，所以使用 probe lexer 與 diagnostic checkpoint 進行嘗試解析。

AST 一開始曾以通用 `Node::children` 表示子節點，但不同語法節點需要不同資料布局，例如：

- Binary expression 需要 `lhs`、`rhs` 與 operator；
- If statement 需要 condition、then 與 else；
- Function call 需要 callee 與 argument list；
- Block 才適合持有一般 statement list。

因此後來移除通用 children，改由每個 AST node 明確持有自己的語意欄位。

Expression 最終使用 Pratt Parser。相較將每一層 precedence 寫成獨立 recursive-descent function，Pratt Parser 可以用 binding power table 集中描述 C operator precedence，並自然處理 prefix、postfix、infix、assignment、conditional 與 comma operator。

### 7.3 Declarator 與型別系統

C declarator 是本專案最複雜的部分之一。以下兩個宣告雖然共用同一個 base type，但每個 declarator 的指標資訊不同：

```c
int *pointer, value, **table;
```

因此 pointer depth、array suffix、function suffix 與 nested declarator 必須屬於各自的 declarator，而不能放在 declaration specifier 上。

語意階段再將 base type 與 declarator 結合為：

- primitive type；
- pointer type；
- array type；
- function type；
- record type。

typedef 與 struct name 皆放入 type scope，因此後續宣告可以沿用同一套 type lookup。

### 7.4 Semantic Analyzer

Parser 只確認語法形狀；型別是否存在、operator 是否可用、initializer 是否相容等問題由 Semantic Analyzer 處理。

Semantic Analyzer 建立：

- scope stack；
- symbol table；
- type table；
- expression type cache；
- declarator 與 symbol 對應；
- constant expression cache；
- loop 與 switch context。

如此 code generator 可以直接使用分析結果，不必在 LLVM lowering 時重新猜測 expression type。

### 7.5 Diagnostic 與 Panic Mode

Diagnostic 由 Reader 持有，使 lexer、parser、preprocessor 與 semantic analyzer 能共用同一套錯誤引擎。

錯誤輸出包含：

```text
file:line:column: error: message
 line | source text
      | marker
```

前置處理器會建立 source map。即使錯誤發生於 `#include` 展開後的文字，diagnostic 仍可回到原始 header 的檔名、行號與 include stack。

Expression 與 Statement 各自具有 panic sync token。解析失敗後跳至 `;`、`}`、`)`、`]`、`,` 或 `:` 等同步點，避免一個錯誤造成後續全部語法被誤判。

### 7.6 LLVM Code Generation

語意分析完成後，AST 被轉換成 LLVM IR。實作內容包括：

- local alloca 與 scope；
- global variable；
- function declaration/definition/call；
- integer 與 floating arithmetic；
- pointer arithmetic 與 pointer difference；
- array decay、subscript；
- struct type、member access；
- if、loop、switch control flow；
- short-circuit logical expression；
- initializer list；
- cast 與 type conversion。

最佳化支援 `-O0`、`-O1`、`-O2`、`-O3`。目前使用 LLVM legacy pass manager 加入 mem2reg、SROA、instruction combining、reassociate、CFG simplification、GVN、dead store elimination、LICM、loop unroll 與 dead code elimination 等 pass。

### 7.7 產生無 CRT 的 Windows 執行檔

一開始使用 `gcc` driver 連結 object。雖然 runtime 本身只呼叫 Windows API，但 GCC driver 仍會自動加入 MinGW CRT startup 與預設 runtime library。

後來改為直接呼叫 GNU `ld.exe`：

```text
ld -m i386pep
   --entry _start
   --subsystem console
   program.o
   c9ay_runtime.o
   -lkernel32
```

其中 `i386pep` 是 GNU ld 對 x86-64 PE+ 的 emulation 名稱，並非 32 位元。32 位元版本為 `i386pe`。

runtime 自行提供：

```c
void _start() {
    ExitProcess(main());
}
```

經 `objdump` 驗證，產物格式為 `pei-x86-64`，architecture 為 `i386:x86-64`，目前唯一 DLL import 為 `KERNEL32.dll`。

---

## 八、遭遇的困難與解決方式

### 8.1 LLVM 在 MinGW 上的建置時間與 archive 問題

LLVM 原始碼規模大，若每次建置專案都重新經過 LLVM target，時間成本很高。曾發生 `libLLVMScalarOpts.a: malformed archive` 與 build rule 遺失等問題。

解決方式：

1. 只建置 LLVM X86 backend 與本專案需要的 component；
2. 使用 `gcc16-cxx26-llvm` preset 完成一次 bootstrap；
3. 一般建置改用已存在的 `LLVMConfig.cmake` 與 static archives；
4. CMake configure 時檢查必要 X86 archive 是否存在；
5. 避免刪除已完成的 LLVM build cache。

### 8.2 C 宣告語法的歧義

`Identifier Identifier;` 可能是 typedef type 的變數宣告，也可能與 expression 開頭相似。若將所有 declaration 當成單純 LL(1)，會錯誤判斷。

解決方式是：

- 使用 type-name scope 記錄 typedef；
- 複製 lexer 狀態進行 probe；
- 使用 diagnostic checkpoint；
- probe 成功才同步 lexer，失敗則 rollback diagnostic。

### 8.3 Operator precedence 與 associativity

以一般 recursive descent 展開完整 C expression grammar 會產生大量相似函式，維護成本高。

解決方式是使用 Pratt Parser，將 precedence 與 associativity集中於 binding power table。Assignment 使用相同左右 binding power形成右結合，其餘 operator 使用遞增 binding power 形成左結合。

### 8.4 Include 後行號錯誤

若 preprocessor 只輸出合併後的文字，parser 所看到的位置會與原始檔案不同，header 中的錯誤也會被顯示成主檔案行號。

解決方式是建立 source map，為每段 preprocessed output 記錄：

- 原始檔案；
- 原始位置；
- output range；
- include stack。

Diagnostic 最後再將 processed range 映射回原始來源。

### 8.5 AST dispatch 設計

專案希望避免每個 node 都手動加入 `Node_kind` 或重複 virtual visitor 函式，因此使用 C++26 reflection 列舉 derived node type，再進行 dispatch。

目前實作使用 reflected `dynamic_cast` chain。此方法 runtime 為 `O(node kinds)`，但 node 數量小且固定，程式簡單。未來候選方案是 reflection 產生連續 trampoline array，使用 `typeid` 後進行線性或二分搜尋。是否替換仍應先 benchmark，避免為了理論複雜度增加大量 template compile time。

### 8.6 Switch AST

若直接把 switch body 表示成一般 block，code generator 必須掃描 `case/default` node 才能重新建立 section 與 fallthrough。

後來改為專用 `Switch_body`、`Switch_section` 與 `Switch_label`。Parser 直接建立 code generator 所需的分段結構，semantic analyzer 也可直接檢查 duplicate label。

---

## 九、製作成果

### 9.1 建置結果

建置指令：

```powershell
cmake --preset gcc16-cxx26
cmake --build --preset gcc16-cxx26 -- -j2
```

預期結果：

```text
Built target compiler
Built target c9ay_runtime
Built target compiler_tests
```

> **[待補截圖 1]** CMake configure 與 build 成功畫面。  
> 建議檔名：`docs/report/assets/01-build-success.png`

### 9.2 自動測試結果

目前測試結果：

```text
[doctest] test cases:  68 |  68 passed | 0 failed | 0 skipped
[doctest] assertions: 673 | 673 passed | 0 failed
[doctest] Status: SUCCESS!
```

> **[待補截圖 2]** doctest 全部通過畫面。  
> 建議檔名：`docs/report/assets/02-test-summary.png`

### 9.3 正確程式產生執行檔

範例：

```c
#include <c9ay.h>

int main() {
    printLine("Hello from C9AY");
    printIntLine(42);
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

> **[待補截圖 3]** 原始碼、編譯成功、執行檔存在與執行輸出。  
> 建議檔名：`docs/report/assets/03-valid-program.png`

### 9.4 錯誤程式正確報告錯誤

測試程式：

```c
int main() {
    return missing;
}
```

實際 diagnostic：

```text
test\compile_failure\02_undeclared_identifier.c:4:12: error: use of undeclared identifier 'missing'
 4 |     return missing;
   |            ^~~~~~~
```

> **[待補截圖 4]** undeclared identifier 的 GCC 風格錯誤訊息。  
> 建議檔名：`docs/report/assets/04-invalid-diagnostic.png`

### 9.5 Panic Mode Recovery

`test/panic_recovery` 中的測試刻意在不同 statement 或 expression 位置加入錯誤，並檢查：

- 錯誤數量是否精確；
- 後續 declaration/statement 是否仍被保留；
- global error 後的 function 是否仍可解析；
- expression recovery 是否停在正確的 `) ] , : ; }`。

> **[待補截圖 5]** 同一檔案回報多個錯誤，且後續程式仍被分析。  
> 建議檔名：`docs/report/assets/05-panic-recovery.png`

### 9.6 Include Source Mapping

專案測試 header 中發生錯誤時：

- 顯示 `In file included from ...`；
- 錯誤位置指向 header 原始行；
- 顯示 header 中的原始碼與 marker。

> **[待補截圖 6]** include stack 與 header diagnostic。  
> 建議檔名：`docs/report/assets/06-include-diagnostic.png`

### 9.7 64 位元無 CRT 執行檔

使用 `objdump` 檢查：

```text
file format pei-x86-64
architecture: i386:x86-64
DLL Name: KERNEL32.dll
```

> **[待補截圖 7]** PE 格式、architecture 與 import table。  
> 建議檔名：`docs/report/assets/07-no-crt-pe.png`

---

## 十、測試設計

### 10.1 Compile Success

`test/compile_success` 目前包含 22 個案例，涵蓋：

- return；
- integer arithmetic；
- if/else；
- loops；
- switch；
- pointers；
- arrays；
- multidimensional arrays；
- typedef；
- struct；
- recursive struct pointer；
- function pointer；
- sizeof/alignof；
- global/static；
- preprocessor；
- runtime include；
- cast/conditional；
- array size inference；
- integer types；
- floating types；
- pointer difference。
- builtin printf。

### 10.2 Compile Failure

`test/compile_failure` 目前包含 33 個案例，涵蓋：

- unknown type；
- undeclared identifier；
- global/local redefinition；
- break/continue context；
- return mismatch；
- function call error；
- invalid dereference/address-of；
- const assignment；
- invalid pointer arithmetic；
- invalid array size；
- too many initializers；
- string too long；
- duplicate switch labels；
- struct member error；
- invalid alignof/sizeof；
- incompatible initializer/return；
- invalid subscript；
- float modulo；
- invalid type combination。
- dynamic printf format。

每個 fixture 以註解標記預期錯誤數與關鍵訊息，例如：

```c
// EXPECT-ERRORS: 1
// EXPECT: use of undeclared identifier
```

測試程式會驗證實際 error count 與訊息內容。

### 10.3 Panic Recovery

`test/panic_recovery` 目前包含 8 個案例。除了錯誤訊息，也驗證 recovery 後保留的 external declaration 或 main statement 數量，避免僅以「沒有 crash」作為 recovery 成功標準。

---

## 十一、目前限制與後續工作

本專案目前並非完整 ISO C，主要限制如下：

1. `sizeof` 僅支援括號形式；`alignof` 僅接受 type name。
2. 尚未完整支援十六進位、八進位與所有 C literal suffix 規則。
3. 不支援 union、enum、goto、一般 label、bit-field 與 variadic C function。
4. 不支援完整 storage class 與 qualifier，例如 `extern`、`register`、`volatile`、`restrict`。
5. block scope `static` 尚未實作真正的 static storage duration。
6. global floating constant expression 與一般 address constant initializer 尚未完整。
7. no-CRT runtime 尚未建立 `argc`/`argv`。
8. linker 目前固定連結 `kernel32`，尚未提供命令列指定其他 Windows system library。
9. preprocessor 為本專案子集，並非完整符合所有 ISO C preprocessing 規則。
10. C++26 reflection dispatch 仍需 benchmark，再決定是否由 dynamic-cast chain 改為 array search。
11. builtin `printf` 只接受 literal format，不支援 width、precision、`%f`
    或動態 format；目前回傳值固定為 `0`。

---

## 十二、結論與心得

本專案完成了從原始碼到 Windows 執行檔的完整編譯流程。實作過程中，最重要的收穫並不只是將語法轉換為 LLVM IR，而是理解各階段之間的責任邊界。

Lexer 應只決定 token；parser 應建立正確的語法結構；semantic analyzer 應處理型別、scope 與非法操作；code generator 則應依賴已驗證的 AST 與 semantic result。若這些責任混在一起，後續加入 pointer、struct、typedef 或 initializer 時會快速變得難以維護。

C declarator 與 expression precedence 是實作中最需要反覆修正的部分。Declarator 讓我理解 C 型別並非單純由左至右閱讀，而必須將 base type、pointer、array、function 與括號巢狀共同組合。Pratt Parser 則提供一個比逐層 expression function 更集中、可維護的 operator 模型。

Diagnostic 與 panic mode 也讓我理解「能拒絕錯誤程式」只是最低要求。真正有用的編譯器還必須指出正確位置、保留 include 來源，並在錯誤後繼續分析。為此建立 source map、diagnostic checkpoint 與 expression-specific recovery，工作量不小，但對最終使用體驗有明顯改善。

LLVM 大幅降低 machine code generation 的難度，但仍需要自行處理型別轉換、control-flow graph、lvalue/address、data layout、object emission 與 linker。尤其將產物改成無 CRT 的 Windows PE 後，更清楚理解 compiler、runtime、linker、import library 與 Windows loader 之間的關係。

整體而言，本專案已達到可編譯實際 C 子集程式、產生 64 位元執行檔並精確回報多類錯誤的目標。後續若繼續開發，優先項目會是完整常量值系統、block static、更多 Windows library linking、argc/argv，以及以 benchmark 驗證 AST dispatch 的最佳資料結構。

> **[待修改]** 本節建議再加入個人實際感受，例如最花時間的部分、原先誤判的設計，以及完成 executable 時的心得。

---

## 十三、參考文獻

1. ISO/IEC 9899，*Programming Languages — C*。
2. cppreference，C language reference，<https://en.cppreference.com/w/c/language>。
3. LLVM Project，LLVM Language Reference Manual，<https://llvm.org/docs/LangRef.html>。
4. LLVM Project，Writing an LLVM Backend / Code Generation 文件，<https://llvm.org/docs/>。
5. LLVM Project，LLVM source code，<https://github.com/llvm/llvm-project>。
6. GNU Binutils，GNU `ld` documentation，<https://sourceware.org/binutils/docs/ld/>。
7. Microsoft Learn，Windows API documentation，<https://learn.microsoft.com/windows/win32/api/>。
8. Microsoft Learn，PE Format，<https://learn.microsoft.com/windows/win32/debug/pe-format>。
9. Bob Nystrom，*Crafting Interpreters*，Pratt Parser 與錯誤復原相關章節，<https://craftinginterpreters.com/>。
10. doctest，C++ testing framework，<https://github.com/doctest/doctest>。
11. CMake Documentation，<https://cmake.org/documentation/>。

---

## 十四、繳交前檢查表

- [ ] 填寫姓名、學號、課程與日期。
- [ ] 修改結論與心得，使其符合個人實際製作經驗。
- [ ] 補上七張成果截圖。
- [ ] 確認報告中的測試數字與最終版本一致。
- [ ] 放入編譯器原始碼。
- [ ] 放入可執行的 `compiler.exe`。
- [ ] 放入 compiler 執行所需的 MinGW DLL，或提供可設定 PATH 的啟動 script。
- [ ] 放入 `test/` 與 `tests/`。
- [ ] 放入 `docs/report/user-manual.md`。
- [ ] 放入 `docs/c-language.ebnf`。
- [ ] 確認 LLVM 原始碼與 build cache 是否需要排除，避免壓縮檔過大。
- [ ] 在乾淨資料夾實際解壓縮並依使用手冊測試一次。
