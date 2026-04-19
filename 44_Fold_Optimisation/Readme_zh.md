# 第 44 章：常量折叠

在编译器编写旅程的上一部分，我意识到必须添加[常量折叠](https://en.wikipedia.org/wiki/Constant_folding)作为优化，以便能够在全局变量声明时解析表达式。

因此，在这一部分，我为通用表达式添加了常量折叠优化，在下一部分中我将重写全局变量声明的代码。

## 什么是常量折叠？

常量折叠是一种优化形式，表达式可以在编译时由编译器求值，而不需要生成代码在运行时求值。

例如，我们可以看到 `x= 5 + 4 * 5;` 实际上等同于 `x= 25;`，因此我们可以让编译器求值表达式，只输出 `x= 25;` 的汇编代码。

## 我们如何实现它？

答案是：在 AST 树中寻找叶子节点为整数字面量的子树。如果一个二元运算有两个整数字面量叶子，编译器就可以求值表达式，并用单个整数字面量节点替换子树。

同样，如果一个一元运算有一个整数字面量子节点，编译器也可以求值表达式，并用单个整数字面量节点替换子树。

一旦我们能够对子树执行此操作，就可以编写一个函数来遍历整棵树，寻找需要折叠的子树。在任何节点上，我们都可以使用这个算法：

  1. 尝试折叠并替换左子节点，即递归进行。
  1. 尝试折叠并替换右子节点，即递归进行。
  1. 如果是带有两个字面量子叶的二元运算，则折叠它。
  1. 如果是带有一个字面量子叶的一元运算，则折叠它。

由于我们替换了子树，这意味着我们先递归优化树的边缘，然后从边缘向上回到树的根节点。下面是一个例子：

```
     *         *        *     50
    / \       / \      / \
   +   -     10  -    10  5
  / \ / \       / \
  6 4 8 3      8   3
```

## 一个新文件，`opt.c`

我为编译器创建了一个新的源文件 `opt.c`，其中重写了 [SubC](http://www.t3x.org/subc/) 编译器作者 Nils M Holm 编写的三个函数：`fold2()`、`fold1()` 和 `fold()`。

Nils 在他的代码中花费大量时间来确保计算的正确性。这在编译器是交叉编译器时非常重要。例如，如果我们在 64 位机器上进行常量折叠，那么整数字面量的范围比 32 位机器大得多。我们在 64 位机器上进行的任何常量折叠可能与在 32 位机器上计算相同表达式得到的结果不同（由于缺乏截断）。

我知道这是一个重要的问题，但我会坚持我们的"KISS 原则"，现在先写简单的代码。如有需要，我会回头修复。

## 折叠二元运算

下面是折叠作为二元运算且有两个子节点的 AST 子树的代码。我只折叠了少数运算，`expr.c` 中还有更多可以折叠的运算。

```c
// Fold an AST tree with a binary operator
// and two A_INTLIT children. Return either
// the original tree or a new leaf node.
static struct ASTnode *fold2(struct ASTnode *n) {
  int val, leftval, rightval;

  // Get the values from each child
  leftval = n->left->a_intvalue;
  rightval = n->right->a_intvalue;
```

另一个函数会调用 `fold2()`，这确保 `n->left` 和 `n->right` 都是指向 A_INTLIT 叶子节点的非 NULL 指针。现在我们有了两个子节点的值，可以开始工作了。

```c
  // Perform some of the binary operations.
  // For any AST op we can't do, return
  // the original tree.
  switch (n->op) {
    case A_ADD:
      val = leftval + rightval;
      break;
    case A_SUBTRACT:
      val = leftval - rightval;
      break;
    case A_MULTIPLY:
      val = leftval * rightval;
      break;
    case A_DIVIDE:
      // Don't try to divide by zero.
      if (rightval == 0)
        return (n);
      val = leftval / rightval;
      break;
    default:
      return (n);
  }
```

我们折叠了通常的四个数学运算。注意除法的特殊代码：如果我们尝试除以零，编译器会崩溃。相反，我们保持子树完整，让代码在成为可执行文件时崩溃！显然，这里可以使用 `fatal()` 调用。我们带着一个表示子树计算值的单个值 `val` 离开 switch 语句。是时候替换子树了。

```c
  // Return a leaf node with the new value
  return (mkastleaf(A_INTLIT, n->type, NULL, val));
}
```

所以一个二元 AST 树进入，如果顺利的话，一个叶子 AST 节点会出来。

## 折叠一元运算

既然你已经看到了二元运算上的折叠，一元运算的代码应该很简单。我只折叠了两个一元运算，但还有空间添加更多。

```c
// Fold an AST tree with a unary operator
// and one INTLIT children. Return either
// the original tree or a new leaf node.
static struct ASTnode *fold1(struct ASTnode *n) {
  int val;

  // Get the child value. Do the
  // operation if recognised.
  // Return the new leaf node.
  val = n->left->a_intvalue;
  switch (n->op) {
    case A_WIDEN:
      break;
    case A_INVERT:
      val = ~val;
      break;
    case A_LOGNOT:
      val = !val;
      break;
    default:
      return (n);
  }

  // Return a leaf node with the new value
  return (mkastleaf(A_INTLIT, n->type, NULL, val));
}
```

在我们的编译器中实现 `fold1()` 有一个小问题，那就是我们有将值从一种类型扩展到另一种类型的 AST 节点。例如，在这个表达式 `x= 3000 + 1;` 中，'1' 被解析为 `char` 字面量。它需要被扩展为 `int` 类型，以便可以加到 '3000' 上。没有优化的编译器会生成这个 AST 树：

```
       A_ADD
      /     \
  A_INTLIT A_WIDEN
    3000      \
           A_INTLIT
               1
```

我们在这里做的是将 A_WIDEN 视为一元 AST 运算，从子节点复制字面量值，并返回一个具有扩展类型和字面量值的叶子节点。

## 递归折叠整个 AST 树

我们有两个函数来处理树的边缘。现在我们可以编写递归函数来优化边缘，并从边缘向上回到树的根节点。

```c
// Attempt to do constant folding on
// the AST tree with the root node n
static struct ASTnode *fold(struct ASTnode *n) {

  if (n == NULL)
    return (NULL);

  // Fold on the left child, then
  // do the same on the right child
  n->left = fold(n->left);
  n->right = fold(n->right);

  // If both children are A_INTLITs, do a fold2()
  if (n->left && n->left->op == A_INTLIT) {
    if (n->right && n->right->op == A_INTLIT)
      n = fold2(n);
    else
      // If only the left is A_INTLIT, do a fold1()
      n = fold1(n);
  }

  // Return the possibly modified tree
  return (n);
}
```

首先要做的是对空树返回 NULL。这允许我们在接下来的两行代码上递归调用 `fold()` 来处理这个节点的子节点。我们刚刚优化了我们下面的子树。

现在，对于具有两个整数字面量叶子子节点的 AST 节点，调用 `fold2()` 来尽可能地优化掉它们。如果我们只有一个整数字面量叶子子节点，则调用 `fold1()` 来做同样的处理。

我们要么已经修剪了树，要么树没有改变。无论哪种方式，我们现在可以将其返回给上层的递归。

## 一个通用优化函数

常量折叠只是我们可以对 AST 树进行的优化之一；之后还会有其他优化。因此，编写一个应用所有优化到树上的前端函数是有意义的。下面是只有常量折叠的版本：

```c
// Optimise an AST tree by
// constant folding in all sub-trees
struct ASTnode *optimise(struct ASTnode *n) {
  n = fold(n);
  return (n);
}
```

我们以后可以扩展它。这在 `decl.c` 的 `function_declaration()` 中被调用。一旦我们解析了一个函数及其函数体，我们将 A_FUNCTION 节点放在树的顶部，然后：

```c
  // Build the A_FUNCTION node which has the function's symbol pointer
  // and the compound statement sub-tree
  tree = mkastunary(A_FUNCTION, type, tree, oldfuncsym, endlabel);

  // Do optimisations on the AST tree
  tree= optimise(tree);
```

## 一个示例函数

下面的程序 `tests/input111.c` 应该能充分测试折叠代码。

```c
#include <stdio.h>
int main() {
  int x= 2000 + 3 + 4 * 5 + 6;
  printf("%d\n", x);
  return(0);
}
```

编译器应该将初始化替换为 `x=2029;`。让我们执行 `cwj -T -S tests/input111.c` 看看：

```
$ ./cwj -T -S z.c
    A_INTLIT 2029
  A_WIDEN
  A_IDENT x
A_ASSIGN
...
$ ./cwj -o tests/input111 tests/input111.c
$ ./tests/input111
2029
```

它似乎可以工作，编译器仍然通过了所有 110 个之前的测试，所以目前它完成了它的工作。

## 结论与下一步

我本来打算把优化留到旅程的最后，但我认为现在看到一种优化也是好的。

在我们编译器编写旅程的下一部分，我们将用使用 `binexpr()` 和这个新的常量折叠代码的代码替换当前的全局声明解析器。[下一步](../45_Globals_Again/Readme_zh.md)
