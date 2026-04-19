# 第 43 章：Bug 修复和更多运算符

我开始将编译器的部分源代码作为输入传递给编译器本身，因为这是我们最终让它能够自我编译的方式。第一个重大难关是让编译器能够解析和识别自己的源代码。第二个重大难关是让编译器从源代码生成正确、可工作的代码。

这也是编译器第一次获得一些实质性的输入来消化，这将会暴露出大量的 bug、特性和缺失的功能。

## Bug 修复

我从 `cwj -S defs.h` 开始，发现缺少了几个头文件。目前它们存在但是空的。有了这些文件后，编译器崩溃并出现段错误。我有几个指针应该初始化为 NULL，还有一些地方没有检查 NULL 指针。

## 缺失的功能

接下来，我在 `defs.h` 中遇到了 `enum { NOREG = -1 ...`，意识到扫描器没有处理以负号开头的整数字面量。所以我在 `scan.c` 的 `scan()` 中添加了这段代码：

```c
    case '-':
      if ((c = next()) == '-') {
        t->token = T_DEC;
      } else if (c == '>') {
        t->token = T_ARROW;
      } else if (isdigit(c)) {          // Negative int literal
        t->intvalue = -scanint(c);
        t->token = T_INTLIT;
      } else {
        putback(c);
        t->token = T_MINUS;
      }
```

如果 '-' 后面跟着一个数字，就扫描整数字面量并取其值的负数。起初我担心表达式 `1 - 1` 会被当作两个词法单元 '1'、'int literal -1'，但我忘了 `next()` 不会跳过空格。因此，通过在 '-' 和 '1' 之间加一个空格，表达式 `1 - 1` 被正确解析为 '1'、'-'、'1'。

然而，正如 [Luke Gruber](https://github.com/luke-gru) 指出的，这也意味着输入 `1-1` **被当作** `1 -1` 而不是 `1 - 1`。换句话说，扫描器过于贪婪，强制将 `-1` 始终视为 T_INTLIT，但有时候它不应该是这样。我现在先保持这样，因为我们在编写源代码时可以绕过这个问题。显然，在生产编译器中这必须被修复。

## 有问题的特性

在 AST 节点和符号表节点结构中，我一直使用联合体来尝试减少每个节点的大小。我想我有点老派了，担心浪费内存。下面是一个 AST 节点结构的例子：

```c
struct ASTnode {
  int op;                       // "Operation" to be performed on this tree
  ...
  union {                       // the symbol in the symbol table
    int intvalue;               // For A_INTLIT, the integer value
    int size;                   // For A_SCALE, the size to scale by
  };
};
```

但编译器无法解析和处理结构体内部的联合体，尤其是无名联合体。我可以添加这个功能，但重做使用联合体的两个结构体会更容易。所以我做了以下修改：

```c
// Symbol table structure
struct symtable {
  char *name;                   // Name of a symbol
  ...
#define st_endlabel st_posn     // For functions, the end label
  int st_posn;                  // For locals, the negative offset
                                // from the stack base pointer
  ...
};

// Abstract Syntax Tree structure
struct ASTnode {
  int op;                       // "Operation" to be performed on this tree
  ...
#define a_intvalue a_size       // For A_INTLIT, the integer value
  int a_size;                   // For A_SCALE, the size to scale by
};
```

这样，我仍然有两个命名字段共享每个结构体中的同一位置，但编译器在每个结构体中只会看到一个字段名。我给每个 `#define` 赋予了不同的前缀，以防止污染全局命名空间。

这样做的一个后果是，我不得不在半打源文件中重命名 `endlabel`、`posn`、`intvalue` 和 `size` 字段。就是这样。

现在编译器在执行 `cwj -S misc.c` 时会运行到：

```
Expected:] on line 16 of data.h, where the line is
extern char Text[TEXTLEN + 1];
```

这会失败，因为目前的编译器不能解析全局变量声明中的表达式。我得重新考虑这个问题。

目前我的想法是使用 `binexpr()` 来解析表达式，并添加一些优化代码来对生成的 AST 树执行[常量折叠](https://en.wikipedia.org/wiki/Constant_folding)。这应该会生成一个单一的 A_INTLIT 节点，从中我可以提取字面量值。我甚至可以让 `binexpr()` 解析任何类型转换，例如：

```c
 char x= (char)('a' + 1024);
```

总之，这是未来的事情。我本来打算在某个时候做常量折叠，但我以为会在更后面的阶段。

在这段旅程的这部分，我要做的是添加一些更多的运算符：具体来说，是 '`+=`'、'`-=`'、'`*=`' 和 '`/=`'。我们目前在编译器的源代码中使用了前两个运算符。

## 新的词法单元、扫描和解析

向编译器添加新的关键字很容易：一个新的词法单元和对扫描器的修改。添加新的运算符要困难得多，因为我们必须：

  + 使词法单元与 AST 操作对齐
  + 处理优先级和结合性

我们正在添加四个运算符：'`+=`'、'`-=`'、'`*=`' 和 '`/=`'。它们有对应的词法单元：T_ASPLUS、T_ASMINUS、T_ASSTAR 和 T_ASSLASH。这些有对应的 AST 操作：A_ASPLUS、A_ASMINUS、A_ASSTAR、A_ASSLASH。AST 操作**必须**与词法单元具有相同的枚举值，因为 `expr.c` 中的这个函数：

```c
// Convert a binary operator token into a binary AST operation.
// We rely on a 1:1 mapping from token to AST operation
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_SLASH)
    return (tokentype);
  fatald("Syntax error, token", tokentype);
  return (0);                   // Keep -Wall happy
}
```

我们还需要配置新运算符的优先级。根据[这个 C 运算符列表](https://en.cppreference.com/w/c/language/operator_precedence)，这些新运算符与现有的赋值运算符具有相同的优先级，所以我们可以按如下方式修改 `expr.c` 中的 `OpPrec[]` 表：

```c
// Operator precedence for each token. Must
// match up with the order of tokens in defs.h
static int OpPrec[] = {
  0, 10, 10,                    // T_EOF, T_ASSIGN, T_ASPLUS,
  10, 10, 10,                   // T_ASMINUS, T_ASSTAR, T_ASSLASH,
  20, 30,                       // T_LOGOR, T_LOGAND
  ...
};
```

但是那个 C 运算符列表也指出赋值运算符是**右结合的**。这意味着，例如：

```c
   a += b + c;          // needs to be parsed as
   a += (b + c);        // not
   (a += b) + c;
```

所以我们还需要更新 `expr.c` 中的这个函数来实现这一点：

```c
// Return true if a token is right-associative,
// false otherwise.
static int rightassoc(int tokentype) {
  if (tokentype >= T_ASSIGN && tokentype <= T_ASSLASH)
    return (1);
  return (0);
}
```

幸运的是，这些是我们需要对扫描器和表达式解析器进行的全部修改：用于二元表达式的 Pratt 解析器现在已经准备好处理新的运算符了。

## 处理 AST 树

既然我们可以解析带有四个新运算符的表达式，我们需要处理为每个表达式创建的 AST。我们需要做的一件事是转储 AST 树。所以，在 `tree.c` 的 `dumpAST()` 中，我添加了这段代码：

```c
    case A_ASPLUS:
      fprintf(stdout, "A_ASPLUS\n"); return;
    case A_ASMINUS:
      fprintf(stdout, "A_ASMINUS\n"); return;
    case A_ASSTAR:
      fprintf(stdout, "A_ASSTAR\n"); return;
    case A_ASSLASH:
      fprintf(stdout, "A_ASSLASH\n"); return;
```

现在当我用表达式 `a += b + c` 运行 `cwj -T input.c` 时，我看到：

```
  A_IDENT rval a
    A_IDENT rval b
    A_IDENT rval c
  A_ADD
A_ASPLUS
```

我们可以将其重绘为：

```
          A_ASPLUS
         /        \
     A_IDENT     A_ADD
     rval a     /     \
            A_IDENT  A_IDENT
             rval b  rval c
```

## 为运算符生成汇编代码

在 `gen.c` 中，我们已经遍历 AST 树并处理 A_ADD 和 A_ASSIGN。有没有办法利用现有代码让实现新的 A_ASPLUS 运算符更容易一些？有！

我们可以将上面的 AST 树重写为：

```
                A_ASSIGN
               /       \
            A_ADD      lval a
         /        \
     A_IDENT     A_ADD
     rval a     /     \
            A_IDENT   A_IDENT
             rval b   rval c
```

现在，只要我们执行树遍历时**好像**树已经被这样重写了，我们就不必*实际*重写树。

所以在 `genAST()` 中，我们有：

```c
int genAST(...) {
  ...
  // Get the left and right sub-tree values. This code already here.
  if (n->left)
    leftreg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
  if (n->right)
    rightreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
}
```

从处理 A_ASPLUS 节点的角度来看，我们计算了左子节点（例如 `a` 的值）和右子节点（例如 `b+c`），并将值存储在两个寄存器中。如果这是一个 A_ADD 操作，我们此时会执行 `cgadd(leftreg, rightreg)`。嗯，这些子节点确实是一个 A_ADD 操作，然后接着是将结果赋值回 `a`。

所以，`genAST()` 代码现在有这样的内容：

```c
  switch (n->op) {
    ...
    case A_ASPLUS:
    case A_ASMINUS:
    case A_ASSTAR:
    case A_ASSLASH:
    case A_ASSIGN:

      // For the '+=' and friends operators, generate suitable code
      // and get the register with the result. Then take the left child,
      // make it the right child so that we can fall into the assignment code.
      switch (n->op) {
        case A_ASPLUS:
          leftreg= cgadd(leftreg, rightreg);
          n->right= n->left;
          break;
        case A_ASMINUS:
          leftreg= cgsub(leftreg, rightreg);
          n->right= n->left;
          break;
        case A_ASSTAR:
          leftreg= cgmul(leftreg, rightreg);
          n->right= n->left;
          break;
        case A_ASSLASH:
          leftreg= cgdiv(leftreg, rightreg);
          n->right= n->left;
          break;
      }

      // And the existing code to do A_ASSIGN is here
     ...
  }
```

换句话说，对于每个新运算符，我们对子节点执行正确的数学操作。但在我们进入 A_ASSIGN 代码之前，我们必须将左子节点指针移到右子节点。为什么？因为 A_ASSIGN 代码期望目标在右子节点：

```c
      return (cgstorlocal(leftreg, n->right->sym));
```

就这样。我们很幸运有一些可以适配的代码来添加这四个新运算符。还有更多的赋值运算符我没有实现：`'%=` `、` `<=` `、` `>>=` `、` `&=` `、` `^=` `和 `|=`。它们应该和我们刚刚添加的四个一样容易添加。

## 示例代码

`tests/input110.c` 程序是我们的测试程序：

```c
#include <stdio.h>

int x;
int y;

int main() {
  x= 3; y= 15; y += x; printf("%d\n", y);
  x= 3; y= 15; y -= x; printf("%d\n", y);
  x= 3; y= 15; y *= x; printf("%d\n", y);
  x= 3; y= 15; y /= x; printf("%d\n", y);
  return(0);
}
```

产生的结果：

```
18
12
45
5
```

## 结论与下一步

我们添加了一些更多的运算符，最困难的部分确实是使所有词法单元、AST 运算符保持一致，并设置优先级和右结合性。之后，我们可以重用 `genAST()` 中的一些代码生成代码来让我们的生活更轻松。

在我们编译器编写旅程的下一部分，我似乎要向编译器添加常量折叠。[下一步](../44_Fold_Optimisation/Readme_zh.md)