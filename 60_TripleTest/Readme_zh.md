# 第60部分：通过三重测试

在本系列的编译器编写旅程中，我们将让编译器通过三重测试！我怎么知道？
我刚刚通过更改编译器中的几行源代码使其通过了三重测试。但我还不知道
原始行为什么不工作。

因此，这部分将是一次调查，我们收集线索，推断问题，修复它，
最后让编译器正确地通过三重测试。

或者，希望如此！

## 第一个证据

我们现在有三个编译器二进制文件：

  1. `cwj`，使用 Gnu C 编译器构建，
  2. `cwj0`，使用 `cwj` 编译器构建，
  2. `cwj1`，使用 `cwj0` 编译器构建

最后两个应该相同，但它们不同。因此，`cwj0`
没有生成正确的汇编输出，这是因为
编译器源代码中存在缺陷。

我们如何缩小问题范围？嗯，我们在 `tests/` 目录中有一堆
测试程序。让我们对所有这些测试运行 `cwj` 和
`cwj0`，看看是否有差异。

确实存在差异，在 `tests/input002.c` 中：

```
$ ./cwj -o z tests/input002.c ; ./z
17
$ ./cwj0 -o z tests/input002.c ; ./z
24
```

## 问题是什么？

所以，`cwj0` 正在生成不正确的汇编输出。让我们从
测试源代码开始：

```c
void main()
{
  int fred;
  int jim;
  fred= 5;
  jim= 12;
  printf("%d\n", fred + jim);
}
```

我们有两个局部变量 `fred` 和 `jim`。两个编译器
生成的汇编代码有以下差异：

```
42c42
<       movl    %r10d, -4(%rbp)
---
>       movl    %r10d, -8(%rbp)
51c51
<       movslq  -4(%rbp), %r10
---
>       movslq  -8(%rbp), %r10
```

嗯，第二个编译器错误地计算了 `fred` 的偏移量。
第一个编译器正确地将偏移量计算为帧指针下方 `-4`。
第二个编译器将偏移量计算为帧指针下方 `-8`。

## 是什么导致了这个问题？

这些偏移量是由 `cg.c` 中的函数
`newlocaloffset()` 计算的：

```c
// 创建新局部变量的位置。
static int localOffset;
static int newlocaloffset(int size) {
  // 递减至少 4 字节的偏移量
  // 并在栈上分配
  localOffset += (size > 4) ? size : 4;
  return (-localOffset);
}
```

在每个函数开始时，`localOffset` 被设置为零。
当我们创建局部变量时，我们得到每个变量的大小，
将其传递给 `newlocaloffset()` 并获得偏移量。

`fred` 和 `jim` 局部变量都是 `int`，大小为 4。因此，
它们的偏移量应该是 `-4` 和 `-8`。

## 请提供更多证据

让我们将 `newlocaloffset()` 抽象为一个单独源文件 `z.c`（我的"首选"临时文件名）
并编译它。源文件是：

```c
static int localOffset=0;
static int newlocaloffset(int size) {
  localOffset += (size > 4) ? size : 4;
  return (-localOffset);
}
```

以下是带注释的输出汇编：

```
        .data
localOffset:
        .long   0

        .text
newlocaloffset:
        pushq   %rbp
        movq    %rsp, %rbp               # 设置栈和
        movl    %edi, -4(%rbp)           # 帧指针
        addq    $-16,%rsp
        movslq  localOffset(%rip), %r10  # 将 localOffset 获取到 %r10
                                         # 准备进行 += 操作
        movslq  -4(%rbp), %r11           # 将 size 获取到 %r11
        movq    $4, %r12                 # 将 4 获取到 %r12
        cmpl    %r12d, %r11d             # 比较它们
        jle     L2                       # 如果 size < 4 则跳转
        movslq  -4(%rbp), %r11
        movq    %r11, %r10               # 将 size 获取到 %r10
        jmp     L3                       # 跳转到 L3
L2:
        movq    $4, %r11                 # 否则获取 4
        movq    %r11, %r10               # 到 %r10
L3:
        addq    %r10, %r10               # 将 += 表达式添加到
                                         # localOffset 的缓存副本
        movl    %r10d, localOffset(%rip) # 将 %r10 保存到 localOffset
        movslq  localOffset(%rip), %r10
        negq    %r10                     # 取反 localOffset
        movl    %r10d, %eax              # 设置返回值
        jmp     L1
L1:
        addq    $16,%rsp                 # 恢复栈和
        popq    %rbp                     # 帧指针
        ret                              # 并返回
```

嗯，代码试图做 `localOffset += expression`，而我们有一个
localOffset 的副本缓存在 `%r10` 中。然而，表达式本身也使用 `%r10`，
因此破坏了 localOffset 的缓存副本。

特别是 `addq %r10, %r10` 是错误的：
它应该添加两个不同的寄存器。

## 通过作弊通过三重测试

我们可以通过重写 `newlocaloffset()` 的源代码来通过三重测试：

```c
static int newlocaloffset(int size) {
  if (size > 4)
    localOffset= localOffset + size;
  else
    localOffset= localOffset + 4;
  return (-localOffset);
}
```

现在当我们执行：

```
$ make triple
cc -Wall -o cwj  cg.c decl.c expr.c gen.c main.c misc.c opt.c scan.c stmt.c sym.c tree.c types.c
./cwj    -o cwj0 cg.c decl.c expr.c gen.c main.c misc.c opt.c scan.c stmt.c sym.c tree.c types.c
./cwj0   -o cwj1 cg.c decl.c expr.c gen.c main.c misc.c opt.c scan.c stmt.c sym.c tree.c types.c
size cwj[01]
   text    data     bss     dec     hex filename
  109652    3028      48  112728   1b858 cwj0
  109652    3028      48  112728   1b858 cwj1
```

最后两个编译器二进制文件是 100% 相同的。但这掩盖了事实：
原始的 `newlocaloffset()` 源代码应该可以工作，但它不能。

为什么我们要在知道 `%r10` 已被分配的情况下重新分配它？

## 一个可能的罪魁祸首

我在 `cg.c` 中重新添加了 `printf()` 行来查看何时分配和释放寄存器。
我注意到在这些汇编行之后：

```
        movslq  -4(%rbp), %r11           # 将 size 获取到 %r11
        movq    $4, %r12                 # 将  4   获取到 %r12
        cmpl    %r12d, %r11d             # 比较它们
        jle     L2                       # 如果 size < 4 则跳转
```

所有寄存器都被释放了，即使 `%r10` 持有 localOffset 的缓存副本。
哪个函数正在生成这些行并释放寄存器？答案是：

```c
// 比较两个寄存器并在假时跳转。
int cgcompare_and_jump(int ASTop, int r1, int r2, int label, int type) {
  int size = cgprimsize(type);

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  switch (size) {
  case 1:
    fprintf(Outfile, "\tcmpb\t%s, %s\n", breglist[r2], breglist[r1]);
    break;
  case 4:
    fprintf(Outfile, "\tcmpl\t%s, %s\n", dreglist[r2], dreglist[r1]);
    break;
  default:
    fprintf(Outfile, "\tcmpq\t%s, %s\n", reglist[r2], reglist[r1]);
  }
  fprintf(Outfile, "\t%s\tL%d\n", invcmplist[ASTop - A_EQ], label);
  freeall_registers(NOREG);
  return (NOREG);
}
```

看着代码，我们肯定可以释放 `r1` 和 `r2`，所以让我们
尝试而不是释放所有寄存器。

是的，这有帮助，我们所有的回归测试仍然通过。
然而，另一个函数也在释放所有寄存器。
是时候使用 `gdb` 并跟踪执行了。

## 真正的罪魁祸首

看起来真正的罪魁祸首是我忘记了许多操作可能是表达式的一部分，
在表达式的结果被使用或丢弃之前，我不能释放所有寄存器。

当我用 `gdb` 查看执行时，我看到处理三元运算符的代码正在释放寄存器，
即使这可能只是一个较大表达式的一部分，该表达式已经分配了寄存器（在 `gen.c` 中）：

```c
static int gen_ternary(struct ASTnode *n) {
  ...
  // 生成条件代码
  genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  genfreeregs(NOREG);           // 这里

  // 获取一个寄存器来保存两个表达式的结果
  reg = alloc_register();

  // 生成真表达式和假标签。
  // 将表达式结果移动到已知寄存器中。
  // 但不要释放持有结果的寄存器！
  expreg = genAST(n->mid, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  genfreeregs(reg);             // 这里

  // 生成假表达式和结束标签。
  // 将表达式结果移动到已知寄存器中。
  // 但不要释放持有结果的寄存器！
  expreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  genfreeregs(reg);             // 这里
  ...
}
```

查看 `cg.c`，所有函数都释放不再使用的寄存器，
所以我认为我们可以放弃条件代码生成之后的 `genfreeregs()`。

接下来，一旦我们将真表达式的值移动到为三元结果保留的寄存器中，
我们就可以释放 `expreg`。假表达式值也是如此。

为了实现这一点，我将 `cg.c` 中的一个以前是 static 的函数设为全局并重命名：

```c
// 将寄存器返回到可用寄存器列表。
// 检查它是否已经在那里。
void cgfreereg(int reg) { ... }
```

现在我们可以重写 `gen.c` 中的三元处理代码：

```c
static int gen_ternary(struct ASTnode *n) {
  ...
    // 生成条件代码，后跟
  // 跳转到假标签的跳转。
  genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);

  // 获取一个寄存器来保存两个表达式的结果
  reg = alloc_register();

  // 生成真表达式和假标签。
  // 将表达式结果移动到已知寄存器中。
  expreg = genAST(n->mid, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  cgfreereg(expreg);
  ...
  // 生成假表达式和结束标签。
  // 将表达式结果移动到已知寄存器中。
  expreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  cgfreereg(expreg);
  ...
}
```

通过此更改，编译器现在通过多个测试：

  + 三重测试： `$ make triple`
  + 四重测试，我们再进行一次编译器编译：

```
$ make quad
...
./cwj  -o cwj0 cg.c decl.c expr.c gen.c main.c misc.c opt.c scan.c stmt.c sym.c tree.c types.c
./cwj0 -o cwj1 cg.c decl.c expr.c gen.c main.c misc.c opt.c scan.c stmt.c sym.c tree.c types.c
./cwj1 -o cwj2 cg.c decl.c expr.c gen.c main.c misc.c opt.c scan.c stmt.c sym.c tree.c types.c
size cwj[012]
   text    data     bss     dec     hex filename
  109636    3028      48  112712   1b848 cwj0
  109636    3028      48  112712   1b848 cwj1
  109636    3028      48  112712   1b848 cwj2
```

  + 使用 Gnu C 编译的编译器进行回归测试： `$ make test`
  + 使用自身编译的编译器进行回归测试： `$ make test0`

这感觉非常令人满意。

## 结论和下一步

我达到了这次旅程的最初目标：编写一个
自编译编译器。这需要 60 部分、5,700 行代码、149 个回归测试
和 108,000 个 Readme 文件中的单词。

也就是说，这不必是旅程的终点。编译器仍有很多工作可以做
使其更接近生产就绪。然而，我断断续续地做了大约两个月，
所以我觉得我可以（至少）休息一下。

在我们编译器编写旅程的下一部分中，我将概述
我们的编译器还可以做更多的事情。也许我会做其中一些；
也许你会做。 [下一步](../61_What_Next/Readme_zh.md)