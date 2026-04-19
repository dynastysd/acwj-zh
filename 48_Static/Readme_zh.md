# 第 48 章：`static` 子集

在一个真正的 C 编译器中，`static` 有三种类型：

 + static 函数，其声明只在该函数出现的源文件中可见；
 + static 全局变量，其声明只在该变量出现的源文件中可见；
 + static 局部变量，这些变量像全局变量一样工作，但每个 static 局部变量只在该变量出现的函数内部可见。

前两种实现起来应该很简单：

  + 在声明时将它们作为全局变量添加，
  + 在该源文件关闭时将它们从全局符号表中移除

第三种则困难得多。这里有一个例子。假设我们用函数来递增两个私有计数器：

```c
int inc_counter1(void) {
  static int counter= 0;
  return(counter);
}

int inc_counter2(void) {
  static int counter= 0;
  return(counter);
}

```

两个函数各自看到自己的 `counter` 变量，并且两个计数器的值在函数调用之间保持不变。变量的持久性使它们成为"全局"的（即活在函数作用域之外），但它们只对一个函数可见，这又使它们有些"局部"的味道。

我在这里提一下[闭包](https://en.wikipedia.org/wiki/Closure_(computer_programming))作为参考，但理论部分有点超出范围，主要是因为我*不*打算实现这第三种 static 东西。

为什么不实现？主要是因为同时实现全局和局部特性会很困难。而且，现在我已经重写了一些代码，我们的编译器中没有任何 static 局部变量，所以也不需要这个功能。

因此，我们可以专注于 static 全局函数和 static 全局变量。

## 新的关键字与词法单元

我们有一个新的关键字 `static` 和一个新的词法单元 T_STATIC。和往常一样，请阅读 `scan.c` 查看变化。

## 解析 `static`

`static` 关键字的解析与 `extern` 在同一位置进行。我们还想拒绝任何在局部上下文中使用 `static` 关键字的尝试。所以在 `decl.c` 中，我们修改 `parse_type()`：

```c
// Parse the current token and return a primitive type enum value,
// a pointer to any composite type and possibly modify
// the class of the type.
int parse_type(struct symtable **ctype, int *class) {
  int type, exstatic = 1;

  // See if the class has been changed to extern or static
  while (exstatic) {
    switch (Token.token) {
      case T_EXTERN:
        if (*class == C_STATIC)
          fatal("Illegal to have extern and static at the same time");
        *class = C_EXTERN;
        scan(&Token);
        break;
      case T_STATIC:
        if (*class == C_LOCAL)
          fatal("Compiler doesn't support static local declarations");
        if (*class == C_EXTERN)
          fatal("Illegal to have extern and static at the same time");
        *class = C_STATIC;
        scan(&Token);
        break;
      default:
        exstatic = 0;
    }
  }
  ...
}
```

如果我们看到 `static` 或 `extern`，首先检查在当前声明类别下这是否合法。然后更新 `class` 变量。如果我们看到的不是这两个词法单元，就退出循环。

现在我们有了标记为 static 声明的类型，该如何将其添加到全局符号表中？我们需要在编译器的几乎每个地方，将所有使用 C_GLOBAL 类的地方也改为包含 C_STATIC。这在多个文件中出现了很多次，但你应该留意类似这样的代码：

```c
    if (class == C_GLOBAL || class == C_STATIC) ...
```

出现在 `cg.c`、`decl.c`、`expr.c` 和 `gen.c` 中。

## 清除 `static` 声明

一旦我们完成对 static 声明的解析，就需要将它们从全局符号表中移除。在 `main.c` 的 `do_compile()` 中，就在我们关闭输入文件之后，现在要做的是：

```c
  genpreamble();                // Output the preamble
  global_declarations();        // Parse the global declarations
  genpostamble();               // Output the postamble
  fclose(Outfile);              // Close the output file
  freestaticsyms();             // Free any static symbols in the file
```

现在让我们看看 `sym.c` 中的 `freestaticsyms()`。我们遍历全局符号表。对于任何 static 节点，我们重新链接列表将其移除。我不太擅长链表代码，所以在一张纸上写出了所有可能的替代方案，才得出以下代码：

```c
// Remove all static symbols from the global symbol table
void freestaticsyms(void) {
  // g points at current node, prev at the previous one
  struct symtable *g, *prev= NULL;

  // Walk the global table looking for static entries
  for (g= Globhead; g != NULL; g= g->next) {
    if (g->class == C_STATIC) {

      // If there's a previous node, rearrange the prev pointer
      // to skip over the current node. If not, g is the head,
      // so do the same to Globhead
      if (prev != NULL) prev->next= g->next;
      else Globhead->next= g->next;

      // If g is the tail, point Globtail at the previous node
      // (if there is one), or Globhead
      if (g == Globtail) {
        if (prev != NULL) Globtail= prev;
        else Globtail= Globhead;
      }
    }
  }

  // Point prev at g before we move up to the next node
  prev= g;
}
```

整体效果是将 static 声明当作全局声明来处理，但在处理完输入文件后将其从符号表中移除。

## 测试更改

有三个程序可以测试这些更改：`tests/input116.c` 到 `tests/input118.c`。让我们看看第一个：

```c
#include <stdio.h>

static int counter=0;
static int fred(void) { return(counter++); }

int main(void) {
  int i;
  for (i=0; i < 5; i++)
    printf("%d\n", fred());
  return(0);
}
```

让我们看看其中一些的汇编输出：

```
        ...
        .data
counter:
        .long   0
        .text
fred:
        pushq   %rbp
        movq    %rsp, %rbp
        addq    $0,%rsp
        ...
```

通常，`counter` 和 `fred` 会有 `.globl` 标记。现在它们是 static 的，它们有标签但我们告诉汇编器不要让这些标签全局可见。

## 结论与下一步

我曾经担心 `static`，但当我决定不实现那个真正困难的第三种替代方案之后，就没那么糟糕了。让我费了些心思的是遍历代码、找到所有 C_GLOBAL 的使用，并确保我也添加了适当的 C_STATIC 代码。

在我们编译器编写旅程的下一部分，我觉得是时候解决[三元运算符](https://en.wikipedia.org/wiki/%3F:)了。[下一步](../49_Ternary/Readme_zh.md)