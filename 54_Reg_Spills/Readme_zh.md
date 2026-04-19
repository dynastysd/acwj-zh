# 第54部分：寄存器溢出（Spilling Registers）

我一直拖延着处理
[寄存器溢出](https://en.wikipedia.org/wiki/Register_allocation#Spilling)
问题，因为我知道这个问题会非常棘手。我认为这是我在这方面的第一次尝试。虽然很 naive，但这是一个开始。

## 问题所在

在大多数 CPU 中，寄存器是一种稀缺资源。它们是最快的
存储单元，我们用它们来保存计算表达式时的临时结果。一旦我们将结果存储到更持久的位置
（例如一个代表变量的内存位置），我们就可以释放正在使用的寄存器并重用它们。

一旦我们遇到非常复杂的表达式，就会耗尽足够的寄存器来保存中间结果，这将阻止我们计算表达式。

目前编译器最多可以分配四个寄存器。是的，我知道这有点人为设置；然而，总会有一个表达式是如此复杂，以至于它无法用固定数量的寄存器来计算。

考虑一下这个表达式，并记住 C 运算符的优先级顺序：

```c
  int x= 5 || 6 && 7 | 8 & 9 << 2 + 3 * 4;
```

右边每个运算符的优先级都比左边的运算符高。
因此，我们需要将 5 存入一个寄存器，然后计算表达式的其余部分。现在将 6 存入一个寄存器，以此类推。现在 7 在一个寄存器中，以此类推。现在 8 在一个寄存器中，以此类推。

糟糕！我们现在需要将 9 加载到一个寄存器中，但所有四个寄存器都已分配。事实上，我们需要再分配*四个*寄存器来计算这个表达式。解决方案是什么？

解决方案是
[将寄存器溢出](https://en.wikipedia.org/wiki/Register_allocation#Spilling)
到主内存的某个地方，这样我们就可以释放一个寄存器。但是，我们还需要在需要时重新加载溢出的寄存器；这意味着此时它必须是空闲的，以便能够重新加载其旧值。

所以，我们不仅需要能够将寄存器溢出到某个地方，还需要跟踪哪些寄存器被溢出了，什么时候溢出的，并在需要时重新加载它们。这很棘手。从上面的外部链接可以看到，最优寄存器分配和溢出背后有大量理论。这不是讲解那个理论的地方。我将实现一个简单的解决方案，并留给你机会基于理论改进代码！

那么，寄存器被溢出到哪里呢？我们可以分配一个任意大小的
[内存堆](https://en.wikipedia.org/wiki/Memory_management#Dynamic_memory_allocation)
并将所有溢出的寄存器存储在这里。然而，通常情况下，大多数寄存器溢出实现都使用现有的堆栈。为什么？

答案是，我们已经有硬件定义的栈上的*push*和*pop*操作，这些操作很快。我们可以（通常）依赖操作系统无限扩展栈大小。此外，我们将栈划分为栈帧，每个函数一个。在函数结束时，我们可以简单地移动栈指针，而不必担心弹出我们溢出并可能忘记的任何寄存器。

我将使用栈来溢出编译器中的寄存器。
让我们看看溢出和使用栈的影响。

## 影响

要进行寄存器溢出，我们需要：

+ 当需要分配寄存器但没有空闲寄存器时，选择并溢出某个寄存器的值。它将被 push 到栈上。
+ 在需要时从栈上重新加载溢出寄存器的值。
+ 确保在需要重新加载时该寄存器是空闲的。
+ 在函数调用之前，我们需要溢出所有正在使用的寄存器。这是因为函数调用是一个表达式。我们需要能够做
  `2 + 3 * fred(4,5) - 7`，并且在函数返回其值后仍然让 2 和 3 保持在寄存器中。
+ 因此，我们需要在函数调用之前重新加载所有我们溢出的寄存器。

以上是我们需要的，与机制无关。现在让我们引入栈，看看它会如何约束我们。

如果我们只能将寄存器的值 push 到栈上来溢出它，并从栈上 pop 寄存器的值，这意味着我们必须按照将它们溢出到栈上的相反顺序重新加载寄存器。我们能保证这一点吗？换句话说，我们是否需要 out-of-order 重新加载寄存器？如果是这样，栈就不是我们需要的机制。或者，我们可以编写编译器来确保寄存器按反向溢出顺序重新加载？

## 一些优化

如果你已经阅读了上面的外部链接，或者你已经了解一些寄存器分配的知识，那么你知道有很多方法可以优化寄存器分配和溢出。你可能比我懂得多，所以下一节不要笑得太多。

当我们调用函数时，并不是所有寄存器都已经被分配了。此外，某些寄存器将用于保存函数的某些参数值。此外，函数可能会返回一个值，从而破坏一个寄存器。因此，在函数调用之前，我们不必将所有寄存器都溢出到栈上。如果我们够聪明，我们可以找出哪些寄存器必须被溢出，只溢出这些寄存器。

我们甚至可以退一步重写 AST 树来减轻表达式计算的压力。例如，我们可以使用一种形式的
[强度削减](https://en.wikipedia.org/wiki/Strength_reduction)
来减少分配的寄存器数量。

考虑一下这个表达式：

```c
  2 + (3 + (4 + (5 + (6 + (7 + 8)))))
```

按照它的写法，我们必须将 2 加载到一个寄存器中，开始计算其余部分，将 3 加载到一个寄存器中，以此类推。我们最终需要七个寄存器分配。

然而，加法是*可交换的*，因此我们可以将上述表达式重新想象为：

```c
  ((((2 + 3) + 4) + 5) + 6) + 7
```

现在我们可以计算 `2+3` 并将其放入一个寄存器，加上 `4` 仍然只需要一个寄存器，等等。这是
[SubC](http://www.t3x.org/subc/) 编译器对其 AST 树所做的处理，这也是我稍后将实现的东西。

但现在，没有优化。事实上，溢出代码会产生一些非常糟糕的汇编代码。但至少它产生的汇编代码是有效的。记住，"*过早优化是万恶之源*"——Donald Knuth。

## 细节

让我们从 `cg.c` 中最基本的新函数开始：

```c
// Push and pop a register on/off the stack
static void pushreg(int r) {
  fprintf(Outfile, "\tpushq\t%s\n", reglist[r]);
}

static void popreg(int r) {
  fprintf(Outfile, "\tpopq\t%s\n", reglist[r]);
}
```

我们可以使用这些函数在栈上溢出和重新加载寄存器。注意，我没有将它们称为 `spillreg()` 和 `reloadreg()`。它们是通用目的的，我们以后可能将它们用于其他事情。

## `spillreg`

接下来是 `cg.c` 中的一个新 static 变量：

```c
static int spillreg=0;
```

这是我们将要选择溢出到栈上的下一个寄存器。每次我们溢出 一个寄存器时，我们会递增 `spillreg`。所以它最终会变成 4，然后 5，... 然后 8，... 然后 3002 等等。

问题：为什么我们在超过最大寄存器数量时不将其重置为零？答案是，当从栈上弹出寄存器时，我们需要知道何时*停止*弹出寄存器。如果我们使用了模运算，我们会按固定周期弹出，不知道何时停止。

也就是说，我们必须只从 0 到 `NUMFREEREGS-1` 溢出寄存器，所以我们在以下代码中会进行一些模运算。

## 溢出单个寄存器

当没有空闲寄存器时，我们会溢出一个寄存器。我们将选择
`spillreg`（模 NUMFREEREGS）寄存器来溢出。在
`cg.c` 的 `alloc_register()` 函数中：

```c
int alloc_register(void) {
  int reg;

  // Try to allocate a register but fail
  ...
  // We have no registers, so we must spill one
  reg= (spillreg % NUMFREEREGS);
  spillreg++;
  fprintf(Outfile, "# spilling reg %d\n", reg);
  pushreg(reg);
  return (reg);
}
```

我们选择 `spillreg % NUMFREEREGS` 作为要溢出的寄存器，并调用 `pushreg(reg)` 来完成溢出。我们递增 `spillreg` 作为下一个要溢出的寄存器，并返回新溢出的寄存器号，因为它现在是空闲的。我还有一个调试语句在那里，我稍后会删除。

## 重新加载单个寄存器

我们只能在一 a) 它变成空闲的，并且 b) 它是最近被溢出到栈上的寄存器时重新加载一个寄存器。这就是我们在代码中插入隐式假设的地方：我们必须始终重新加载最近溢出的寄存器。我们最好确保编译器能保持这个承诺。

`cg.c` 中 `free_register()` 的新代码是：

```c
static void free_register(int reg) {
  ...
  // If this was a spilled register, get it back
  if (spillreg > 0) {
    spillreg--;
    reg= (spillreg % NUMFREEREGS);
    fprintf(Outfile, "# unspilling reg %d\n", reg);
    popreg(reg);
  } else        // Simply free the in-use register
  ...
}
```

我们简单地撤销最近的溢出，并递减 `spillreg`。请注意，这就是为什么我们没有使用模值存储 `spillreg`。一旦它达到零，我们就知道栈上没有溢出的寄存器了，并且没有必要尝试从栈上弹出寄存器值。

## 函数调用前的寄存器溢出

正如我之前提到的，一个聪明的编译器会确定在函数调用之前*必须*溢出哪些寄存器。这不是一个聪明的编译器，所以我们有这些新函数：

```c
// Spill all registers on the stack
void spill_all_regs(void) {
  int i;

  for (i = 0; i < NUMFREEREGS; i++)
    pushreg(i);
}

// Unspill all registers from the stack
static void unspill_all_regs(void) {
  int i;

  for (i = NUMFREEREGS-1; i >= 0; i--)
    popreg(i);
}
```

在这个时候，当你正在笑或哭（或两者兼有）时，我会提醒你 Ken Thompson 的一句话： "*When in doubt, use brute force.*"

## 保持我们的假设完整

我们的代码中有一个隐式假设：任何重新加载的寄存器都是最后溢出的寄存器。我们最好检查一下这是否成立。

对于二元表达式，`gen.c` 中的 `genAST()` 执行以下操作：

```c
  // Get the left and right sub-tree values
  leftreg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
  rightreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);

  switch (n->op) {
    // Do the specific binary operation
  }
```

我们首先为左手表达式分配寄存器，然后为右手表达式分配寄存器。如果我们必须溢出寄存器，那么右手表达式的寄存器将是最近溢出的寄存器。

因此，我们最好*先释放*右手表达式的寄存器，以确保任何溢出的值都将重新加载回该寄存器。

我遍历了 `cg.c` 并对二元表达式生成器进行了一些修改来做到这一点。`cg.c` 中的 `cgadd()` 就是一个例子：

```c
// Add two registers together and return
// the number of the register with the result
int cgadd(int r1, int r2) {
  fprintf(Outfile, "\taddq\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r2);
  return (r1);
}
```

代码曾经是加到 `r2` 中，释放 `r1` 并返回 `r2`。不好，但幸运的是加法是可交换的。我们可以将结果保存在任一寄存器中，所以现在 `r1` 返回结果，`r2` 被释放。如果它被溢出了，它将恢复其旧值。

我*希望*我已经在需要的地方都这样做了，并且我*希望*我们的假设不满足，但我不太确定。我们将需要进行大量测试才能合理地满意。

## 函数调用的变化

现在我们已经有了 spill/reload 的细节。对于普通的寄存器分配和释放，上述代码将根据需要溢出和重新加载。我们还尝试确保释放最近溢出的寄存器。

最后一件事是在函数调用之前溢出寄存器，并在之后重新加载它们。有一个问题：函数可能是表达式的一部分。我们需要：

1. 首先溢出寄存器。
1. 将参数复制到函数（使用寄存器）。
1. 调用函数。
1. 在我们之前重新加载寄存器
1. 复制寄存器的返回值。

如果我们最后两个步骤做反了，我们会在重新加载所有旧寄存器时丢失返回的值。

为了实现上述目标，我不得不在 `gen.c` 和 `cg.c` 之间分担 spill/reload 职责，如下所示。

在 `gen.c` 的 `gen_funccall()` 中：

```c
static int gen_funccall(struct ASTnode *n) {
  ...

  // Save the registers before we copy the arguments
  spill_all_regs();

  // Walk the list of arguments and copy them
  ...
  // Call the function, clean up the stack (based on numargs),
  // and return its result
  return (cgcall(n->sym, numargs));
}
```

它执行步骤 1、2 和 3：溢出、复制、调用。而在 `cg.c` 的 `cgcall()` 中：

```c
int cgcall(struct symtable *sym, int numargs) {
  int outr;

  // Call the function
  ...
  // Remove any arguments pushed on the stack
  ...

  // Unspill all the registers
  unspill_all_regs();

  // Get a new register and copy the return value into it
  outr = alloc_register();
  fprintf(Outfile, "\tmovq\t%%rax, %s\n", reglist[outr]);
  return (outr);
}
```

它执行最后两个步骤：重新加载和复制返回值。

## 示例时间

以下是一些导致寄存器溢出的示例：函数调用和复杂表达式。我们从 `tests/input136.c` 开始：

```c
int add(int x, int y) {
  return(x+y);
}

int main() {
  int result;
  result= 3 * add(2,3) - 5 * add(4,6);
  printf("%d\n", result);
  return(0);
}
```

`add()` 需要被视为一个表达式。我们将 3 放入一个寄存器，并在调用 `add(2,3)` 之前溢出所有寄存器。我们在获取返回值之前重新加载寄存器。汇编代码是：

```
        movq    $3, %r10        # Get 3 into %r10
        pushq   %r10
        pushq   %r11            # Spill all four registers, thus
        pushq   %r12            # preserving the %r10 value
        pushq   %r13
        movq    $3, %r11        # Copy the 3 and 2 arguments
        movq    %r11, %rsi
        movq    $2, %r11
        movq    %r11, %rdi
        call    add@PLT         # Call add()
        popq    %r13
        popq    %r12            # Reload all four registers, thus
        popq    %r11            # restoring the %r10 value
        popq    %r10
        movq    %rax, %r11      # Get the return value into %r11
        imulq   %r11, %r10      # Multiply 3 * add(2,3)
```

是的，这里有很大的优化空间。不过 KISS。

在 `tests/input137.c` 中有这个表达式：

```c
  x= a + (b + (c + (d + (e + (f + (g + h))))));
```

它需要八个寄存器，所以我们需要溢出其中四个。生成的汇编代码是：

```
        movslq  a(%rip), %r10
        movslq  b(%rip), %r11
        movslq  c(%rip), %r12
        movslq  d(%rip), %r13
        pushq   %r10             # spilling %r10
        movslq  e(%rip), %r10
        pushq   %r11             # spilling %r11
        movslq  f(%rip), %r11
        pushq   %r12             # spilling %r12
        movslq  g(%rip), %r12
        pushq   %r13             # spilling %r13
        movslq  h(%rip), %r13
        addq    %r13, %r12
        popq    %r13            # unspilling %r13
        addq    %r12, %r11
        popq    %r12            # unspilling %r12
        addq    %r11, %r10
        popq    %r11            # unspilling %r11
        addq    %r10, %r13
        popq    %r10            # unspilling %r10
        addq    %r13, %r12
        addq    %r12, %r11
        addq    %r11, %r10
        movl    %r10d, -4(%rbp)
```

总体而言，我们最终得到了正确的表达式计算。

## 结论和下一步

寄存器分配和溢出很难做对，有很多优化理论可以应用。我实现了一个相当 naive 的寄存器分配和溢出方法。它可以工作，但有很大的改进空间。

在执行上述操作时，我还修复了 `&&` 和 `||` 的问题。我决定在下一部分写下这些更改，即使这里的代码已经有了这些更改。[下一步](../55_Lazy_Evaluation/Readme_zh.md)