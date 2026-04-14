# 第 7 章：比较运算符

我本来打算下一步添加 IF 语句，但后来意识到我最好先添加一些比较运算符。这事实证明相当容易，因为它们像现有的运算符一样是二元运算符。

让我们快速看看添加六个比较运算符 `==`、`!=`、`<`、`>`、`<=` 和 `>=` 需要做哪些修改。

## 添加新的词法单元

我们有六个新的词法单元，让我们将它们添加到 `defs.h` 中：

```c
// 词法单元类型
enum {
  T_EOF,
  T_PLUS, T_MINUS,
  T_STAR, T_SLASH,
  T_EQ, T_NE,
  T_LT, T_GT, T_LE, T_GE,
  T_INTLIT, T_SEMI, T_ASSIGN, T_IDENT,
  // 关键字
  T_PRINT, T_INT
};
```

我重新排列了这些词法单元，以便具有优先级的词法单元按照从低到高的优先级顺序排列在没有优先级的词法单元之前。

## 扫描词法单元

现在我们需要扫描它们。注意，我们必须区分 `=` 和 `==`、`<` 和 `<=`、`>` 和 `>=`。所以我们需要从输入中读取一个额外的字符，如果不需要就放回去。以下是 `scan.c` 中 `scan()` 的新代码：

```c
  case '=':
    if ((c = next()) == '=') {
      t->token = T_EQ;
    } else {
      putback(c);
      t->token = T_ASSIGN;
    }
    break;
  case '!':
    if ((c = next()) == '=') {
      t->token = T_NE;
    } else {
      fatalc("无法识别的字符", c);
    }
    break;
  case '<':
    if ((c = next()) == '=') {
      t->token = T_LE;
    } else {
      putback(c);
      t->token = T_LT;
    }
    break;
  case '>':
    if ((c = next()) == '=') {
      t->token = T_GE;
    } else {
      putback(c);
      t->token = T_GT;
    }
    break;
```

我还把 `=` 词法单元的名称改成了 T_ASSIGN，以确保不会与新的 T_EQ 词法单元混淆。

## 新的表达式代码

现在我们可以扫描六个新的词法单元了。所以现在我们必须在表达式中出现时解析它们，并且强制执行它们的运算符优先级。

到现在你应该已经明白了：

  + 我正在构建一个将成为自编译编译器的东西
  + 使用 C 语言
  + 以 SubC 编译器作为参考

这意味着我正在为足够多的 C 子集编写编译器（就像 SubC 一样），以便它能够编译自身。因此，我应该使用正常的 [C 运算符优先级顺序](https://en.cppreference.com/w/c/language/operator_precedence)。这意味着比较运算符的优先级低于乘法和除法。

我还意识到，我用来将词法单元映射到 AST 节点类型的 switch 语句只会越来越大。所以我决定重新排列 AST 节点类型，使得所有二元运算符（`defs.h` 中）都有 1:1 的映射关系：

```c
// AST 节点类型。前几个与
// 相关的词法单元对齐
enum {
  A_ADD=1, A_SUBTRACT, A_MULTIPLY, A_DIVIDE,
  A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE,
  A_INTLIT,
  A_IDENT, A_LVIDENT, A_ASSIGN
};
```

现在在 `expr.c` 中，我可以简化词法单元到 AST 节点的转换，并添加新词法单元的优先级：

```c
// 将二元运算符词法单元转换为 AST 操作。
// 我们依赖于从词法单元到 AST 操作的 1:1 映射
static int arithop(int tokentype) {
  if (tokentype > T_EOF && tokentype < T_INTLIT)
    return(tokentype);
  fatald("语法错误，词法单元", tokentype);
}

// 每个词法单元的运算符优先级。必须
// 与 defs.h 中词法单元的顺序相匹配
static int OpPrec[] = {
  0, 10, 10,                    // T_EOF, T_PLUS, T_MINUS
  20, 20,                       // T_STAR, T_SLASH
  30, 30,                       // T_EQ, T_NE
  40, 40, 40, 40                // T_LT, T_GT, T_LE, T_GE
};
```

这就是解析和运算符优先级的全部内容！

## 代码生成

由于六个新运算符都是二元运算符，修改 `gen.c` 中的通用代码生成器来处理它们很容易：

```c
  case A_EQ:
    return (cgequal(leftreg, rightreg));
  case A_NE:
    return (cgnotequal(leftreg, rightreg));
  case A_LT:
    return (cglessthan(leftreg, rightreg));
  case A_GT:
    return (cggreaterthan(leftreg, rightreg));
  case A_LE:
    return (cglessequal(leftreg, rightreg));
  case A_GE:
    return (cggreaterequal(leftreg, rightreg));
```

## x86-64 代码生成

现在有点棘手了。在 C 语言中，比较运算符返回一个值。如果它们求值为真，结果是 1。如果求值为假，结果是 0。我们需要编写 x86-64 汇编代码来反映这一点。

幸运的是有一些 x86-64 指令可以做到这一点。不幸的是，一路上有一些问题需要处理。考虑这个 x86-64 指令：

```
    cmpq %r8,%r9
```

上述 `cmpq` 指令执行 %r9 - %r8，并设置包括负标志和零标志在内的几个状态标志。因此，我们可以查看标志组合来看比较的结果：

| 比较 | 操作 | 为真时的标志 |
|------------|-----------|---------------|
| %r8 == %r9 | %r9 - %r8 |  零         |
| %r8 != %r9 | %r9 - %r8 |  非零     |
| %r8 > %r9  | %r9 - %r8 |  非零、负 |
| %r8 < %r9  | %r9 - %r8 |  非零、非负 |
| %r8 >= %r9 | %r9 - %r8 |  零或负 |
| %r8 <= %r9 | %r9 - %r8 |  零或非负 |

有六个 x86-64 指令，它们根据两个标志值将寄存器设置为 1 或 0：`sete`、`setne`、`setg`、`setl`、`setge` 和 `setle`，顺序见上表。

问题是，这些指令只设置寄存器的最低字节。如果寄存器已经在最低字节之外设置了位，它们将保持设置状态。所以我们可能将变量设置为 1，但如果它已经有了值 1000（十进制），那么它现在将是 1001，这不是我们想要的。

解决方案是在 `setX` 指令之后对寄存器执行 `andq` 以去除不需要的位。在 `cg.c` 中有一个通用的比较函数来做这件事：

```c
// 比较两个寄存器。
static int cgcompare(int r1, int r2, char *how) {
  fprintf(Outfile, "\tcmpq\t%s, %s\n", reglist[r2], reglist[r1]);
  fprintf(Outfile, "\t%s\t%s\n", how, breglist[r2]);
  fprintf(Outfile, "\tandq\t$255,%s\n", reglist[r2]);
  free_register(r1);
  return (r2);
}
```

其中 `how` 是 `setX` 指令之一。注意我们执行

```
   cmpq reglist[r2], reglist[r1]
```

因为这实际上是 `reglist[r1] - reglist[r2]`，这才是我们真正想要的。

## x86-64 寄存器

我们需要一个简短的转场来讨论 x86-64 架构中的寄存器。x86-64 有多个 64 位通用寄存器，但我们也可以使用不同的寄存器名称来访问和处理这些寄存器的子部分。

![](https://i.stack.imgur.com/N0KnG.png)

上面来自 *stack.imgur.com* 的图片显示，对于 64 位 *r8* 寄存器，我们可以通过使用 "*r8d*" 寄存器名访问该寄存器的低 32 位。同样，"*r8w*" 寄存器是低 16 位，"*r8b*" 寄存器是 *r8* 寄存器的低 8 位。

在 `cgcompare()` 函数中，代码使用 `reglist[]` 数组来比较两个 64 位寄存器，但随后通过使用 `breglist[]` 数组中的名称在第二个寄存器的 8 位版本中设置标志。x86-64 架构只允许 `setX` 指令在 8 位寄存器名称上操作，因此需要 `breglist[]` 数组。

## 创建多个比较指令

现在我们有了这个通用函数，我们可以编写六个实际比较函数：

```c
int cgequal(int r1, int r2) { return(cgcompare(r1, r2, "sete")); }
int cgnotequal(int r1, int r2) { return(cgcompare(r1, r2, "setne")); }
int cglessthan(int r1, int r2) { return(cgcompare(r1, r2, "setl")); }
int cggreaterthan(int r1, int r2) { return(cgcompare(r1, r2, "setg")); }
int cglessequal(int r1, int r2) { return(cgcompare(r1, r2, "setle")); }
int cggreaterequal(int r1, int r2) { return(cgcompare(r1, r2, "setge")); }
```

与其他二元运算符函数一样，一个寄存器被释放，另一个寄存器返回结果。

# 付诸实践

看一下 `input04` 输入文件：

```c
int x;
x= 7 < 9;  print x;
x= 7 <= 9; print x;
x= 7 != 9; print x;
x= 7 == 7; print x;
x= 7 >= 7; print x;
x= 7 <= 7; print x;
x= 9 > 7;  print x;
x= 9 >= 7; print x;
x= 9 != 7; print x;
```

所有这些比较都是真的，所以我们应该输出九个 1。做 `make test` 来确认这一点。

让我们看看第一个比较输出的汇编代码：

```
        movq    $7, %r8
        movq    $9, %r9
        cmpq    %r9, %r8        # 执行 %r8 - %r9，即 7 - 9
        setl    %r9b            # 如果 7 小于 9 则将 %r9b 设置为 1
        andq    $255,%r9        # 去除 %r9 中的所有其他位
        movq    %r9, x(%rip)    # 将结果保存到 x
        movq    x(%rip), %r8
        movq    %r8, %rdi
        call    printint        # 打印 x
```

是的，上面的汇编代码有些低效。我们甚至还没有开始担心优化代码的问题。引用 Donald Knuth 的话：

> **过早优化是万恶之源（或至少是大部分）。**

## 结论与下一步

这是对编译器的一个简单而轻松的补充。旅程的下一部分将更加复杂。

在我们编译器编写的下一步中，我们将向编译器添加 IF 语句，并利用我们刚刚添加的比较运算符。[下一步](../08_If_Statements/Readme_zh.md)
