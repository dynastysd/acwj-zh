# 第 45 章：全局变量声明（再谈）

在两章之前，我尝试编译这一行代码：

```c
enum { TEXTLEN = 512 };         // Length of identifiers in input
extern char Text[TEXTLEN + 1];
```

然后意识到我们的声明解析代码只能处理单个整数字面量作为数组大小。但我的编译器代码如上所示，使用了包含两个整数字面量的表达式。

在上一章中，我为编译器添加了常量折叠功能，这样整数字面量的表达式就会被折叠成单个整数字面量。

现在我们需要丢弃所有那些手动编写的字面量值解析和相关类型转换的代码，转而调用表达式解析器来获取包含字面量值的 AST 树。

## 保留还是丢弃 `parse_literal()`？

在我们当前 `decl.c` 中的编译器里，有一个名为 `parse_literal()` 的函数，它负责手动解析字符串和整数字面量。我们应该保留这个函数，还是直接丢弃它并在其他地方手动调用 `binexpr()`？

我决定保留这个函数，抛弃所有现有代码，并稍微改变这个函数的用途。它现在还可以解析任何位于带有多个字面量值的表达式之前的类型转换。

`decl.c` 中的函数原型现在是：

```c
// Given a type, parse an expression of literals and ensure
// that the type of this expression matches the given type.
// Parse any type cast that precedes the expression.
// If an integer literal, return this value.
// If a string literal, return the label number of the string.
int parse_literal(int type);
```

所以，它是旧的 `parse_literal()` 的直接替代品，只是之前所有的类型转换解析代码都可以丢弃了。让我们现在来看看 `parse_literal()` 中的新代码。

```c
int parse_literal(int type) {
  struct ASTnode *tree;

  // Parse the expression and optimise the resulting AST tree
  tree= optimise(binexpr(0));
```

啊哈。我们调用 `binexpr()` 来解析输入文件中此时的任何表达式，然后调用 `optimise()` 来折叠所有字面量表达式。

现在，对于我们能使用的树，根节点应该是 A_INTLIT、A_STRLIT 或 A_CAST（如果前面有类型转换）。

```c
  // If there's a cast, get the child and
  // mark it as having the type from the cast
  if (tree->op == A_CAST) {
    tree->left->type= tree->type;
    tree= tree->left;
  }
```

它是一个类型转换，所以我们去掉 A_CAST 节点，但保留子节点被转换到的类型。


```c
  // The tree must now have an integer or string literal
  if (tree->op != A_INTLIT && tree->op != A_STRLIT)
    fatal("Cannot initialise globals with a general expression");
```

哎呀，他们给了我们一些无法使用的东西，所以告诉他们并停止。

```c
  // If the type is char * and
  if (type == pointer_to(P_CHAR)) {
    // We have a string literal, return the label number
    if (tree->op == A_STRLIT)
      return(tree->a_intvalue);
    // We have a zero int literal, so that's a NULL
    if (tree->op == A_INTLIT && tree->a_intvalue==0)
      return(0);
  }
```

我们需要能够接受这两种输入：

```c
              char *c= "Hello";
              char *c= (char *)0;
```

上面两个内部 IF 语句分别匹配这两行输入。如果不是字面量字符串...

```c
  // We only get here with an integer literal. The input type
  // is an integer type and is wide enough to hold the literal value
  if (inttype(type) && typesize(type, NULL) >= typesize(tree->type, NULL))
    return(tree->a_intvalue);

  fatal("Type mismatch: literal vs. variable");
  return(0);    // Keep -Wall happy
}
```

这花了我一段时间才想明白。我们必须解析这些：

```c
  long  x= 3;    // allow this, where 3 is type char
  char  y= 4000; // prevent this, where 4000 is too wide
  char *z= 4000; // prevent this, as z is not integer type
```

所以 IF 语句检查输入类型并确保它足够宽以容纳整数字面量。

## `decl.c` 中的其他解析更改

现在我们有了一个可以解析可能带有类型转换的字面量表达式的函数，我们可以使用它了。这就是我们抛弃旧的类型转换解析代码并替换它的地方。更改如下：

```c
// Parse a scalar declaration
static struct symtable *scalar_declaration(...) {
    ...
    // Globals must be assigned a literal value
    if (class == C_GLOBAL) {
      // Create one initial value for the variable and
      // parse this value
      sym->initlist= (int *)malloc(sizeof(int));
      sym->initlist[0]= parse_literal(type);
    }
    ...
}

// Parse an array declaration
static struct symtable *array_declaration(...) {

  ...
  // See we have an array size
  if (Token.token != T_RBRACKET) {
    nelems= parse_literal(P_INT);
    if (nelems <= 0)
      fatald("Array size is illegal", nelems);
  }

  ...
  // Get the list of initial values
  while (1) {
    ...
    initlist[i++]= parse_literal(type);
    ...
  }
  ...
}
```

通过这样做，我们丢失了大约 20 到 30 行代码来解析旧的 `parse_literal()` 之前可能出现的任何类型转换。请注意，为了获得那 30 行的减少，我们不得不添加 100 行常量折叠代码！幸运的是，常量折叠在一般表达式以及这里都有使用，所以这仍然是一个胜利。

## 一个 `expr.c` 更改

为了支持新的 `parse_literal()`，我们的编译器代码还有一处更改。在解析表达式的通用函数 `binexpr()` 中，我们现在必须告知它表达式可以由 '}' 词法单元结束，就像这里出现的情况：

```c
  int fred[]= { 1, 2, 6 };
```

对 `binexpr()` 的小更改是：

```c
    // If we hit a terminating token, return just the left node
    tokentype = Token.token;
    if (tokentype == T_SEMI || tokentype == T_RPAREN ||
        tokentype == T_RBRACKET || tokentype == T_COMMA ||
        tokentype == T_COLON || tokentype == T_RBRACE) {    // T_RBRACE is new
      left->rvalue = 1;
      return (left);
    }
```

## 测试更改的代码

我们现有的测试将测试为全局变量初始化单个字面量值的情况。`tests/input112.c` 中的这段代码测试了用字面量表达式初始化标量变量，以及用字面量表达式作为数组大小：

```c
#include <stdio.h>
char* y = NULL;
int x= 10 + 6;
int fred [ 2 + 3 ];

int main() {
  fred[2]= x;
  printf("%d\n", fred[2]);
  return(0);
}
```

## 结论与下一步

在我们编译器编写的下一部分旅程中，我可能会将更多的编译器源代码喂给编译器本身，看看我们还有什么需要实现的。[下一步](../46_Void_Functions/Readme_zh.md)