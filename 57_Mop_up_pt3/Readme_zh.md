# 第57部分：清理，第3部分

在编译器编写的旅程的这一部分，我修复了编译器的几个小问题。

## 没有 -D 标志

我们的编译器没有运行时的 `-D` 标志来定义预处理器符号，添加它会有些复杂。但我们在 `Makefile` 中使用它来设置头文件所在目录的位置。

我重写了 `Makefile`，将此位置写入一个新的头文件：

```
# 定义include目录的位置
INCDIR=/tmp/include
...

incdir.h:
        echo "#define INCDIR \"$(INCDIR)\"" > incdir.h
```

现在在 `defs.h` 中我们有：

```c
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "incdir.h"
```

这确保了源代码知道该目录的位置。

## 加载外部变量

我在 `include/stdio.h` 中添加了这三个外部变量：

```c
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
```

但当我尝试使用它们时，它们被当作局部变量处理了！
原来我选择全局变量的逻辑是错误的。在 `gen.c` 的 `genAST()` 中，我们现在有：

```c
    case A_IDENT:
      // 如果我们是rvalue或正在被解引用，则加载我们的值
      if (n->rvalue || parentASTop == A_DEREF) {
        if (n->sym->class == C_GLOBAL || n->sym->class == C_STATIC
            || n->sym->class == C_EXTERN) {
          return (cgloadglob(n->sym, n->op));
        } else {
          return (cgloadlocal(n->sym, n->op));
        }
```

其中添加了 `C_EXTERN` 选项。

## Pratt 解析器的问题

早在旅程的第3部分，我介绍了
[Pratt 解析器](https://en.wikipedia.org/wiki/Pratt_parser)，
它有一个与每个标记关联的优先级表。
从那以后我们一直在使用它，因为它有效。

但是，我引入了一些不被 Pratt 解析器解析的标记：前缀运算符、后缀运算符、类型转换、
数组元素访问等。在过程中，我打破了确保 Pratt 解析器知道前一个运算符
标记优先级的链。

下面是 `expr.c` 中 `binexpr()` 代码所示的基本 Pratt 算法：

```c
  // 获取左边的树。
  // 同时获取下一个标记。
  left = prefix();
  tokentype = Token.token;

  // 当这个标记的优先级高于
  // 前一个标记的优先级，或者它是右结合的
  // 且等于前一个标记的优先级
  while ((op_precedence(tokentype) > ptp) ||
         (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    // 获取下一个整数常量
    scan(&Token);

    // 使用我们标记的优先级递归调用 binexpr()
    // 来构建一个子树
    right = binexpr(OpPrec[tokentype]);

    // 将那个子树与我们的连接起来（代码未给出）

    // 更新当前标记的详细信息。
    // 如果遇到终止标记则离开循环（代码未给出）
    tokentype = Token.token;
  }

  // 当优先级相同或更低时返回我们拥有的树
  return (left);
```

我们必须确保 `binexpr()` 以先前标记的优先级被调用。现在让我们看看这是如何被破坏的。

考虑这个检查三个指针是否有效的表达式：

```c
  if (a == NULL || b == NULL || c == NULL)
```

`==` 运算符的优先级高于 `||` 运算符，因此
Pratt 解析器应该将其视为与以下相同：

```c
  if ((a == NULL) || (b == NULL) || (c == NULL))
```

现在，NULL 被定义为这个表达式，它包含一个类型转换：

```c
#define NULL (void *)0
```

让我们看看上面 IF 行的调用链：

 + `binexpr(0)` 从 `if_statement()` 调用
 + `binexpr(0)` 解析 `==`（优先级为40）并
    调用 `binexpr(40)`
 + `binexpr(40)` 调用 `prefix()`
 + `prefix()` 调用 `postfix()`
 + `postfix()` 调用 `primary()`
 + `primary()` 在 `(void *)0` 开始时看到左括号
    并调用 `paren_expression()`
 + `paren_expression()` 看到 `void` 标记并调用
   `parse_cast()`。一旦类型转换被解析，它调用 `binexpr(0)` 来
    解析 `0`。

这就是问题所在。NULL 的值，即 `0` 应该仍然处于优先级40，但 `paren_expression()` 只是将其重置回零。

这意味着我们现在将解析 `NULL || b`，从中生成一个 AST 树，而不是解析 `a == NULL` 并构建该 AST 树。

解决方案是确保先前的标记优先级从 `binexpr()` 一直传递到
`paren_expression()` 的调用链中。这意味着：

 + `prefix()`、`postfix()`、`primary()` 和 `paren_expression()`

现在都接受一个 `int ptp` 参数并传递它。

程序 `tests/input143.c` 检查这个更改现在对
`if (a==NULL || b==NULL || c==NULL)` 有效。

## 指针、`+=` 和 `-=`

前一段时间，我意识到如果我们将一个整数值加到指针上，需要将整数按指针指向的类型大小进行缩放。例如：

```c
int list[]= {3, 5, 7, 9, 11, 13, 15};
int *lptr;

int main() {
  lptr= list;
  printf("%d\n", *lptr);
  lptr= lptr + 1; printf("%d\n", *lptr);
}
```

应该打印 `list` 基址处的值，即3。`lptr` 应该按 `int` 的大小递增，即4，这样它现在指向
`list` 中的下一个元素。

现在，我们对 `+` 和 `-` 运算符执行此操作，但我忘记为 `+=` 和 `-=` 运算符实现它。幸运的是，这很容易修复。在 `types.c` 中 `modify_type()` 的底部，我们现在有：

```c
  // 我们只能对加法和减法运算进行缩放
  if (op == A_ADD || op == A_SUBTRACT ||
      op == A_ASPLUS || op == A_ASMINUS) {

    // 左边是int类型，右边是指针类型，且
    // 原始类型的大小>1：缩放左边
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = genprimsize(value_at(rtype));
      if (rsize > 1)
        return (mkastunary(A_SCALE, rtype, rctype, tree, NULL, rsize));
      else
        return (tree);          // 大小为1，无需缩放
    }
  }
```

您可以看到，我将 A_ASPLUS 和 A_ASMINUS 添加到了可以缩放整数值的运算列表中。

## 结论和下一步

现在清理工作已经足够了。当我修复了 `+=` 和 `-=` 问题时，
它突出显示了 `++` 和 `--` 运算符（前缀和后缀）应用于指针时的一个大问题。

在编译器编写旅程的下一部分，我将解决此问题。[下一步](../58_Ptr_Increments/Readme_zh.md)