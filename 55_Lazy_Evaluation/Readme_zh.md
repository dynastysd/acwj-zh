# 第五十五部分：惰性求值

我决定把 `&&` 和 `||` 的修复内容移到这里，而不是放在我们编译器编写之旅的前一部分，因为前一部分已经足够庞大了。

那么为什么我们最初对 `&&` 和 `||` 的实现是有缺陷的呢？
C 程序员期望这些运算符会执行[惰性求值](https://en.wikipedia.org/wiki/Lazy_evaluation)。
换句话说，只有当左操作数的值不足以确定结果时，才会对 `&&` 和 `||` 的右操作数进行求值。

惰性求值的一个常见用途是：仅在指针确实指向某个对象时，才检查该指针是否指向特定值。
`test/input138.c` 中有一个这样的例子：

```c
  int *aptr;
  ...
  if (aptr && *aptr == 1)
    printf("aptr points at 1\n");
  else
    printf("aptr is NULL or doesn't point at 1\n");
```

我们不想对 `&&` 运算符的两个操作数都进行求值：如果
`aptr` 是 NULL，那么 `*aptr == 1` 表达式将导致 NULL
解引用并使程序崩溃。

## 问题所在

问题在于我们当前对 `&&` 和 `||` 的实现*确实*会对两个操作数都进行求值。在 `gen.c` 的 `genAST()` 中：

```c
  // Get the left and right sub-tree values
  leftreg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
  rightreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);

  switch (n->op) {
    ...
    case A_LOGOR:
      return (cglogor(leftreg, rightreg));
    case A_LOGAND:
      return (cglogand(leftreg, rightreg));
    ...
  }
```

我们必须重写这段代码，*不再*对两个操作数都进行求值。相反，
我们必须首先对左操作数进行求值。如果它足以给出结果，
我们就可以跳转到设置结果的代码。如果不能，
现在我们才对右操作数进行求值。同样，我们跳转到
设置结果的代码。如果我们没有跳转，那我们必定得到了
相反的结果。

这非常类似于 IF 语句的代码生成器，但它有所不同，
所以我在 `gen.c` 中编写了一个新的代码生成器。
它在运行 `genAST()` 处理左右操作数*之前*被调用。代码是分阶段的：

```c
// Generate the code for an
// A_LOGAND or A_LOGOR operation
static int gen_logandor(struct ASTnode *n) {
  // Generate two labels
  int Lfalse = genlabel();
  int Lend = genlabel();
  int reg;

  // Generate the code for the left expression
  // followed by the jump to the false label
  reg= genAST(n->left, NOLABEL, NOLABEL, NOLABEL, 0);
  cgboolean(reg, n->op, Lfalse);
  genfreeregs(NOREG);
```

左操作数被求值了。假设我们正在执行 `&&`
操作。如果这个结果为零，我们就可以跳转到 `Lfalse` 并
将结果设置为零（假）。此外，一旦表达式被
求值完毕，我们就可以释放所有寄存器。这也有助于减轻
寄存器分配的压力。

```c
  // Generate the code for the right expression
  // followed by the jump to the false label
  reg= genAST(n->right, NOLABEL, NOLABEL, NOLABEL, 0);
  cgboolean(reg, n->op, Lfalse);
  genfreeregs(reg);
```

我们对右操作数做完全相同的处理。如果它是假的，我们
就跳转到 `Lfalse` 标签。如果我们没有跳转，`&&` 的结果必定是
真的。对于 `&&`，我们现在执行：

```c
  cgloadboolean(reg, 1);
  cgjump(Lend);
  cglabel(Lfalse);
  cgloadboolean(reg, 0);
  cglabel(Lend);
  return(reg);
}
```

`cgloadboolean()` 将寄存器设置为真（如果参数是 1）或
假（如果参数是 0）。对于 x86-64，这是 1 和 0，但我这样编码
是为了其他架构可能有不同的真和假的寄存器值。以上代码对于
表达式 `(aptr && *aptr == 1)` 产生如下输出：

```
        movq    aptr(%rip), %r10
        test    %r10, %r10              # Test if aptr is not NULL
        je      L38                     # No, jump to L38
        movq    aptr(%rip), %r10
        movslq  (%r10), %r10            # Get *aptr in %r10
        movq    $1, %r11
        cmpq    %r11, %r10              # Is *aptr == 1?
        sete    %r11b
        movzbq  %r11b, %r11
        test    %r11, %r11
        je      L38                     # No, jump to L38
        movq    $1, %r11                # Both true, true is the result
        jmp     L39                     # Skip the false code
L38:
        movq    $0, %r11                # One or both false, false is the result
L39:                                    # Continue on with the rest
```

我没有给出求值 `||` 操作的 C 代码。从本质上讲，
如果左操作数或右操作数中的任何一个为真，我们就跳转并将真设置为结果。
如果我们没有跳转，我们就执行将假设置为结果的代码，
并跳过真值设置的代码。

## 测试这些更改

`test/input138.c` 也有代码来打印 AND 和 OR 真值表：

```c
  // See if generic AND works
  for (x=0; x <= 1; x++)
    for (y=0; y <= 1; y++) {
      z= x && y;
      printf("%d %d | %d\n", x, y, z);
    }

  // See if generic AND works
  for (x=0; x <= 1; x++)
    for (y=0; y <= 1; y++) {
      z= x || y;
      printf("%d %d | %d\n", x, y, z);
    }
```

这产生了如下输出（添加了一个空格）：

```
0 0 | 0
0 1 | 0
1 0 | 0
1 1 | 1

0 0 | 0
0 1 | 1
1 0 | 1
1 1 | 1
```

## 结论与下一步

现在我们的编译器已经为 `&&` 和 `||` 实现了惰性求值，这对于
编译器能够自行编译是绝对必要的。事实上，在这一点上，
编译器唯一不能解析（在其自身源代码中）的就是
局部数组的声明和使用。所以，猜猜看...

在我们编译器编写之旅的下一部分中，我将尝试弄清楚
如何声明和使用局部数组。[下一步](../56_Local_Arrays/Readme_zh.md)