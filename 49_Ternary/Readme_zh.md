# 第四十九部分：三元运算符

在编译器编写的旅程的这一部分，我实现了
[三元运算符](https://en.wikipedia.org/wiki/%3F:)。这是
C语言中非常精巧的运算符之一，可以减少
源代码文件中的代码行数。其基本语法是：

```
ternary_expression:
        logical_expression '?' true_expression ':' false_expression
        ;
```

我们首先计算逻辑表达式。如果为真，则只计算
真表达式。否则，只计算假表达式。
真表达式或假表达式的结果成为
整个表达式的结果。

这里有一个微妙之处，例如：

```c
   x= y != 5 ? y++ : ++y;
```

如果 `y != 5`，则 `x = y++`，否则 `x = ++y`。无论如何，`y` 只
递增一次。

我们可以将上述代码重写为IF语句：

```c
if (y != 5)
  x= y++;
else
  x= ++y;
```

然而，三元运算符是一个表达式，所以我们还可以：

```c
  x= 23 * (y != 5 ? y++ : ++y) - 18;
```

这不容易转换成IF语句。但是，我们可以
从IF代码生成器中借用一些机制来用于
三元运算符。

## 词素、运算符和运算符优先级

我们的语法中已经有 ':' 作为词素；现在我们需要添加 '?'
词素。这将被视为一个运算符，所以我们最好设置
其优先级。

根据[C运算符列表](https://en.cppreference.com/w/c/language/operator_precedence)，'?' 运算符的优先级刚好在
赋值运算符之上。

根据我们设计的优先级方式，我们的运算符词素必须按
优先级顺序排列，并且AST运算符必须与词素对应。

因此，在 `defs.h` 中，我们现在有：

```c
// Token types
enum {
  T_EOF,

  // Binary operators
  T_ASSIGN, T_ASPLUS, T_ASMINUS,
  T_ASSTAR, T_ASSLASH,
  T_QUESTION,                   // The '?' token
  ...
enum {
  A_ASSIGN = 1, A_ASPLUS, A_ASMINUS, A_ASSTAR, A_ASSLASH,
  A_TERNARY,                    // The ternary AST operator
  ...
```

在 `expr.c` 中，我们现在有：

```c
static int OpPrec[] = {
  0, 10, 10,                    // T_EOF, T_ASSIGN, T_ASPLUS,
  10, 10, 10,                   // T_ASMINUS, T_ASSTAR, T_ASSLASH,
  15,                           // T_QUESTION
  ...
```

一如既往，我让你浏览 `scan.c` 中的更改以了解
新的 T_QUESTION 词素。

## 解析三元运算符

即使三元运算符不是二元运算符，因为它
具有优先级，我们需要在与二元
运算符相同的 `binexpr()` 中实现它。以下是代码：

```c
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, ...

    switch (ASTop) {
    case A_TERNARY:
      // Ensure we have a ':' token, scan in the expression after it
      match(T_COLON, ":");
      ltemp= binexpr(0);

      // Build and return the AST for this statement. Use the middle
      // expression's type as the return type. XXX We should also
      // consider the third expression's type.
      return (mkastnode(A_TERNARY, right->type, left, right, ltemp, NULL, 0));
      ...
    }
    ...
}
```

当我们遇到 A_TERNARY case时，逻辑表达式的AST树
存储在 `left` 中，真表达式在 `right` 中，并且我们已经解析了 '?'
词素。现在我们需要解析 ':' 词素和假表达式。

解析完所有三个词素后，我们现在可以构建一个AST节点来保存这三个。
一个问题是如何确定此节点的类型。正如你所看到的，选择中间词素的类型很容易。
正确地做这件事，我应该看看真和假表达式，哪个更宽并选择那个。
我现在先留下，以后再回头看。

## 生成汇编代码：问题

为三元运算符生成汇编代码与
IF语句非常相似：我们计算一个逻辑表达式。如果为真，我们
计算一个表达式；如果为假，我们计算另一个。我们需要一些标签，并且我们必须根据需要插入跳转到这些标签。

我确实尝试修改 `gen.c` 中的 `genIF()` 代码来同时处理IF
和三元运算符，但写另一个函数更容易。

生成汇编代码有一个问题。考虑：

```c
   x= (y > 4) ? 2 * y - 18 : y * z - 3 * a;
```

我们有三个表达式，我们需要分配寄存器来计算
每个。逻辑表达式计算完毕后，我们跳转到
正确的下一段代码，我们可以释放
评估中使用的所有寄存器。对于真和假表达式，我们可以释放
所有寄存器*除一个*：保存表达式右值的寄存器。

我们也无法预测这将是哪个寄存器，因为每个表达式
具有不同的操作数和运算符；因此，使用的寄存器数量
会有所不同，并且分配来保存结果的（最后一个）寄存器可能
不同。

但我们需要知道哪个寄存器确实保存了真
和假表达式的结果，这样当我们跳转到将使用此
结果的代码时，它知道要访问哪个寄存器。

因此，我们需要做三件事：

  + 在运行真或假表达式*之前*分配一个寄存器来保存结果，
  + 将真和假表达式的结果复制到此寄存器中，以及
  + 释放所有寄存器*除*保存结果的寄存器。

## 释放寄存器

我们已经有了一个释放所有寄存器的函数 `freeall_registers()`，
它不需要参数。我们的寄存器从零开始编号。我
修改了这个函数，让它接受一个参数，即我们想要
*保留*的寄存器。而且，为了释放*所有*寄存器，我们传递 NOREG，它被定义为数字 `-1`：

```c
// Set all registers as available.
// But if reg is positive, don't free that one.
void freeall_registers(int keepreg) {
  int i;
  for (i = 0; i < NUMFREEREGS; i++)
    if (i != keepreg)
      freereg[i] = 1;
}
```

在整个编译器中，你现在将看到 `freeall_registers(-1)` 来
替换以前的 `freeall_registers()`。

## 生成汇编代码

我们现在在 `gen.c` 中有一个函数来处理三元运算符。
它从 `genAST()` 的顶部被调用：

```c
  // We have some specific AST node handling at the top
  // so that we don't evaluate the child sub-trees immediately
  switch (n->op) {
  ...
  case A_TERNARY:
    return (gen_ternary(n));
```

让我们分阶段看一下这个函数。

```c
// Generate code for a ternary expression
static int gen_ternary(struct ASTnode *n) {
  int Lfalse, Lend;
  int reg, expreg;

  // Generate two labels: one for the
  // false expression, and one for the
  // end of the overall expression
  Lfalse = genlabel();
  Lend = genlabel();

  // Generate the condition code followed
  // by a jump to the false label.
  genAST(n->left, Lfalse, NOLABEL, NOLABEL, n->op);
  genfreeregs(-1);
```

这与IF生成代码几乎完全相同。我们将逻辑表达式子树、
假标签和 A_TERNARY 运算符传递给 `genAST()`。当 `genAST()` 看到
这些时，它知道如果为假则生成跳转到此标签。

```c
  // Get a register to hold the result of the two expressions
  reg = alloc_register();

  // Generate the true expression and the false label.
  // Move the expression result into the known register.
  expreg = genAST(n->mid, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  // Don't free the register holding the result, though!
  genfreeregs(reg);
  cgjump(Lend);
  cglabel(Lfalse);
```

逻辑表达式完成后，我们现在可以分配寄存器来
保存真和假表达式的结果。我们调用 `genAST()` 来
生成真表达式代码，并获得带有
结果的寄存器。我们现在必须将此寄存器的值移动到已知寄存器中。
完成后，我们可以释放除已知寄存器外的所有寄存器。如果
我们做了真表达式，我们现在跳转到三元
汇编代码的末尾。

```c
  // Generate the false expression and the end label.
  // Move the expression result into the known register.
  expreg = genAST(n->right, NOLABEL, NOLABEL, NOLABEL, n->op);
  cgmove(expreg, reg);
  // Don't free the register holding the result, though!
  genfreeregs(reg);
  cglabel(Lend);
  return (reg);
}
```

计算假表达式的代码非常相似。无论
哪种方式，执行都会到达结束标签，一旦我们到达这里，我们知道
三元结果在已知寄存器中。

## 测试新代码

我担心嵌套的三元运算符，我在其他代码中用过很多次。
三元运算符是*右结合的*，这意味着
我们比左边更紧密地绑定 '?' 到右边。

幸运的是，当我们贪婪地寻找 ':' 词素和假表达式
一旦我们解析了 '?' 词素，我们的解析器已经将三元
运算符视为右结合。

`tests/input121.c` 是嵌套三元运算符的一个例子：

```c
#include <stdio.h>

int x;
int y= 3;

int main() {
  for (y= 0; y < 10; y++) {
    x= (y < 4) ? y + 2 :
       (y > 7) ? 1000 : y + 9;
    printf("%d\n", x);
  }
  return(0);
}
```

如果 `y<4`，则 `x` 变为 `y+2`。如果没有，我们计算第二个三元
运算符。如果 `y>7`，`x` 变为 1000，否则变为 `y+9`。

效果是对 y 值 0 到 3 执行 `y+2`，对 y 值
4 到 7 执行 `y+9`，对更高的 y 值执行 1000：

```
2
3
4
5
13
14
15
16
1000
1000
```

## 结论和下一步

像迄今为止的一些步骤一样，我担心处理三元
运算符，因为它非常困难。我确实在将其放入IF生成代码时遇到了问题，所以我退后了一步。实际上，
我和妻子出去看了电影，这给了我一个思考的机会。
我意识到我必须释放除一个之外的所有寄存器，而且我
应该写一个单独的函数。之后，编写代码就很直接了。偶尔离开键盘总是好的。

在我们编译器编写旅程的下一部分，我将把编译器
喂给自身，查看我得到的解析错误并选择其中之一
或多个来修复。

> 附注。我们已经达到了 README 文件中的 5,000 行代码和 90,000 字。
我们快完成了！[下一步](../50_Mop_up_pt1/Readme_zh.md)