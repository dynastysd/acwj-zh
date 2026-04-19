# 第 47 章：`sizeof` 子集

在真正的 C 编译器中，`sizeof()` 运算符给出以下内容的字节大小：

 + 类型定义
 + 表达式类型

我查看了编译器代码，发现我们只将 `sizeof()` 用于上述两个选项中的第一个，所以我也只实现第一个。这让事情变得稍微简单一些，因为我们可以假设 `sizeof()` 内部的词法单元是类型定义。

## 新的词法单元和关键字

我们需要一个新的 "sizeof" 关键字和新的词法单元 T_SIZEOF。和往常一样，我让你查看 `scan.c` 中的更改。

现在，在添加新的词法单元时，我们还必须更新：

```c
// List of token strings, for debugging purposes
char *Tstring[] = {
  "EOF", "=", "+=", "-=", "*=", "/=",
  "||", "&&", "|", "^", "&",
  "==", "!=", ",", ">", "<=", ">=", "<<", ">>",
  "+", "-", "*", "/", "++", "--", "~", "!",
  "void", "char", "int", "long",
  "if", "else", "while", "for", "return",
  "struct", "union", "enum", "typedef",
  "extern", "break", "continue", "switch",
  "case", "default", "sizeof",
  "intlit", "strlit", ";", "identifier",
  "{", "}", "(", ")", "[", "]", ",", ".",
  "->", ":"
};
```

我最初忘记这样做，调试时发现 "default" 之后的词法单元显示的是"错误的"词法单元描述。真见鬼！

## 解析器的更改

`sizeof()` 运算符是表达式解析的一部分，因为它接受一个表达式并返回一个新值。我们可以这样做：

```c
  int x= 43 + sizeof(char);
```

因此，我们要修改 `expr.c` 来添加 `sizeof()`。它不是二元运算符，也不是前缀或后缀运算符，最佳位置是作为解析原始表达式的一部分。

事实上，一旦我发现了那些愚蠢的 bug，实现 `sizeof()` 的新代码量很少。如下所示：

```c
// Parse a primary factor and return an
// AST node representing it.
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;
  int type=0;
  int size, class;
  struct symtable *ctype;

  switch (Token.token) {
  case T_SIZEOF:
    // Skip the T_SIZEOF and ensure we have a left parenthesis
    scan(&Token);
    if (Token.token != T_LPAREN)
      fatal("Left parenthesis expected after sizeof");
    scan(&Token);

    // Get the type inside the parentheses
    type= parse_stars(parse_type(&ctype, &class));
    // Get the type's size
    size= typesize(type, ctype);
    rparen();
    // Return a leaf node int literal with the size
    return (mkastleaf(A_INTLIT, P_INT, NULL, size));
    ...
  }
  ...
}
```

我们已经有 `parse_type()` 函数来解析类型定义，也有 `parse_stars()` 函数来解析任何后续的星号。最后，我们已经有 `typesize()` 函数来返回类型的字节数。我们所要做的就是扫描词法单元，调用这三个函数，创建一个包含整数字面量的叶子 AST 节点，然后返回它。

是的，我知道 `sizeof()` 有很多细节，但我遵循"KISS 原则"，只做足够让我们的编译器能够自编译。

## 测试新代码

`tests/input115.c` 文件包含了对基本类型、指针和我们编译器中的结构体的测试：

```c
struct foo { int x; char y; long z; };
typedef struct foo blah;

int main() {
  printf("%ld\n", sizeof(char));
  printf("%ld\n", sizeof(int));
  printf("%ld\n", sizeof(long));
  printf("%ld\n", sizeof(char *));
  printf("%ld\n", sizeof(blah));
  printf("%ld\n", sizeof(struct symtable));
  printf("%ld\n", sizeof(struct ASTnode));
  return(0);
}
```

目前，我们编译器的输出是：

```
1
4
8
8
13
64
48
```

我在想是否需要将 `struct foo` 结构体填充到 16 字节而不是 13 字节。等我们到那时候再说。

## 结论与下一步

好吧，`sizeof()` 实现起来很简单，至少对于我们编译器需要的功能是这样。实际上，`sizeof()` 对于一个成熟的 C 编译器来说相当复杂。

在我们编译器编写的下一部分旅程中，我将处理 `static`。[下一步](../48_Static/Readme_zh.md)