# 第 17 章：更好的类型检查和指针偏移

在前面的几章中，我介绍了指针并实现了一些代码来检查类型兼容性。当时，我意识到对于这样的代码：

```c
  int   c;
  int  *e;

  e= &c + 1;
```

对由 `&c` 计算出的指针加 1 需要转换为 `c` 的大小，以确保跳过 `c` 之后的下一个 `int`。换句话说，我们必须对整数进行缩放。

我们需要对指针这样做，以后对数组也需要这样做。考虑这段代码：

```c
  int list[10];
  int x= list[3];
```

要做到这一点，我们需要找到 `list[]` 的基地址，然后加上 `int` 大小的三倍来找到索引位置 3 处的元素。

当时，我在 `types.c` 中写了一个名为 `type_compatible()` 的函数来确定两个类型是否兼容，并指出是否需要"加宽"一个小整数类型使其与更大的整数类型大小相同。然而，这种加宽是在其他地方执行的。实际上，它最终在编译器中的三个地方被完成了。

## 替代 `type_compatible()`

如果 `type_compatible()` 表明需要加宽，我们会 A_WIDEN 一个树以匹配更大的整数类型。现在我们需要 A_SCALE 一个树，使其值按类型的大小进行缩放。而且我想重构重复的加宽代码。

为此，我抛弃了 `type_compatible()` 并替换了它。这花了我不少思考，我可能还需要调整或扩展它。让我们看看设计。

现有的 `type_compatible()`：
 + 需要两个类型值作为参数，外加一个可选的方向，
 + 如果类型兼容则返回 true，
 + 如果任一方需要加宽则在左侧或右侧返回 A_WIDEN，
 + 实际上并没有将 A_WIDEN 节点添加到树中，
 + 如果类型不兼容则返回 false，并且
 + 不处理指针类型

现在让我们看看类型比较的用例：

 + 当对两个表达式执行二元运算时，它们的类型是否兼容，我们是否需要加宽或缩放其中一个？
 + 当执行 `print` 语句时，表达式是否为整数，是否需要加宽？
 + 当执行赋值语句时，表达式是否需要加宽，是否与左值类型匹配？
 + 当执行 `return` 语句时，表达式是否需要加宽，是否与函数的返回类型匹配？

在这些用例中，只有一个有两个表达式。因此，我选择编写一个新函数，它接受一个 AST 树和我们希望它成为的类型。对于二元运算的用例，我们将调用它两次，看看每次调用会发生什么。

## 引入 `modify_type()`

`types.c` 中的 `modify_type()` 是 `type_compatible()` 的替代代码。函数的 API 是：

```c
// 给定一个 AST 树和我们希望它成为的类型，
// 可能通过加宽或缩放来修改树，使其与此类型兼容。
// 如果没有发生更改则返回原始树，修改后的树，
// 或者如果树与给定类型不兼容则返回 NULL。
// 如果这将是二元运算的一部分，则 AST op 不为零。
struct ASTnode *modify_type(struct ASTnode *tree, int rtype, int op);
```

问：为什么我们需要对树和另一个树执行任何二元运算？答案是，我们只能对指针进行加法或减法。我们不能做其他任何事情，例如

```c
  int x;
  int *ptr;

  x= *ptr;	   // OK
  x= *(ptr + 2);   // 从 ptr 指向的位置向上两个 int
  x= *(ptr * 4);   // 没有任何意义
  x= *(ptr / 13);  // 也没有任何意义
```

这是目前的代码。有很多特定的测试，而且目前我无法合理化所有可能的测试。此外，它以后还需要扩展。

```c
struct ASTnode *modify_type(struct ASTnode *tree, int rtype, int op) {
  int ltype;
  int lsize, rsize;

  ltype = tree->type;

  // 比较标量整数类型
  if (inttype(ltype) && inttype(rtype)) {

    // 两种类型相同，无需处理
    if (ltype == rtype) return (tree);

    // 获取每种类型的大小
    lsize = genprimsize(ltype);
    rsize = genprimsize(rtype);

    // 树的大小太大
    if (lsize > rsize) return (NULL);

    // 向右加宽
    if (rsize > lsize) return (mkastunary(A_WIDEN, rtype, tree, 0));
  }

  // 左侧为指针的情况
  if (ptrtype(ltype)) {
    // 如果右侧类型相同且不是二元运算，则 OK
    if (op == 0 && ltype == rtype) return (tree);
  }

  // 我们只能在 A_ADD 或 A_SUBTRACT 运算上进行缩放
  if (op == A_ADD || op == A_SUBTRACT) {

    // 左侧为整数类型，右侧为指针类型，且原始类型的大小 >1：缩放左侧
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = genprimsize(value_at(rtype));
      if (rsize > 1)
        return (mkastunary(A_SCALE, rtype, tree, rsize));
    }
  }

  // 如果到达这里，则类型不兼容
  return (NULL);
}
```

添加 AST A_WIDEN 和 A_SCALE 操作的代码现在只在一个地方完成。A_WIDEN 操作将子节点的类型转换为父节点的类型。A_SCALE 操作将子节点的值乘以存储在新的 `struct ASTnode` 联合字段中的大小（在 `defs.h` 中）：

```c
// 抽象语法树结构
struct ASTnode {
  ...
  union {
    int size;                   // 对于 A_SCALE，要缩放的大小
  } v;
};
```

## 使用新的 `modify_type()` API

有了这个新的 API，我们可以移除 `stmt.c` 和 `expr.c` 中重复的 A_WIDEN 代码。然而，这个新函数只接受一棵树。当我们确实只有一棵树时，这没问题。现在 `stmt.c` 中有三处对 `modify_type()` 的调用。它们都很相似，所以这里有一个来自 `assignment_statement()` 的例子：

```c
  // 为赋值左值生成 AST 节点
  right = mkastleaf(A_LVIDENT, Gsym[id].type, id);

  ...
  // 解析后续表达式
  left = binexpr(0);

  // 确保两种类型兼容。
  left = modify_type(left, right->type, 0);
  if (left == NULL) fatal("Incompatible expression in assignment");
```

这比我们之前的代码要清晰得多。

### 在 `binexpr()` 中也是如此...

但是在 `expr.c` 的 `binexpr()` 中，我们现在需要用加法、乘法等二元运算来组合两个 AST 树。这里，我们尝试让每个树通过 `modify_type()` 来匹配另一个树的类型。现在，一个可能会加宽：这也意味着另一个会失败并返回 NULL。因此，我们不能简单地看 `modify_type()` 的一个结果是否为 NULL：我们需要看到两个都为 NULL 才能假定类型不兼容。以下是 `binexpr()` 中的新比较代码：

```c
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, *rtemp;
  int ASTop;
  int tokentype;

  ...
  // 获取左边的树。
  // 同时获取下一个标记。
  left = prefix();
  tokentype = Token.token;

  ...
  // 使用我们标记的优先级递归调用 binexpr() 来构建子树
  right = binexpr(OpPrec[tokentype]);

  // 通过尝试修改每个树来匹配对方的类型来确保两种类型兼容。
  ASTop = arithop(tokentype);
  ltemp = modify_type(left, right->type, ASTop);
  rtemp = modify_type(right, left->type, ASTop);
  if (ltemp == NULL && rtemp == NULL)
    fatal("Incompatible types in binary expression");

  // 更新任何被加宽或缩放的树
  if (ltemp != NULL) left = ltemp;
  if (rtemp != NULL) right = rtemp;
```

代码有点乱，但不比之前的代码更乱，而且它现在同时处理 A_SCALE 和 A_WIDEN。

## 执行缩放

我们已经在 `defs.h` 的 AST 节点操作列表中添加了 A_SCALE。现在我们需要实现它。

正如我之前提到的，A_SCALE 操作将子节点的值乘以存储在新的 `struct ASTnode` 联合字段中的大小。对于我们所有的整数类型，这将是 2 的倍数。由于这个事实，我们可以用左移一定数量的位来乘以子节点的值。

以后，我们会遇到大小不是 2 的幂的结构。因此，当缩放因子合适时，我们可以进行移位优化，但我们也需要实现一个用于更一般缩放因子的乘法。

这是在 `gen.c` 的 `genAST()` 中执行此操作的新代码：

```c
    case A_SCALE:
      // 小优化：如果缩放值是已知的 2 的幂，则使用移位
      switch (n->v.size) {
        case 2: return(cgshlconst(leftreg, 1));
        case 4: return(cgshlconst(leftreg, 2));
        case 8: return(cgshlconst(leftreg, 3));
        default:
          // 用大小加载一个寄存器，
          // 然后用这个大小乘以 leftreg
          rightreg= cgloadint(n->v.size, P_INT);
          return (cgmul(leftreg, rightreg));
```

## 在 x86-64 代码中左移

我们现在需要一个 `cgshlconst()` 函数来将寄存器值左移一个常量。当我们稍后添加 C 的 '<<' 运算符时，我将编写一个更通用的左移函数。现在，我们可以使用带有整数字面值的 `salq` 指令：

```c
// 将寄存器左移一个常量
int cgshlconst(int r, int val) {
  fprintf(Outfile, "\tsalq\t$%d, %s\n", val, reglist[r]);
  return(r);
}
```

## 我们的测试程序不能工作

我的缩放功能测试程序是 `tests/input16.c`：

```c
int   c;
int   d;
int  *e;
int   f;

int main() {
  c= 12; d=18; printint(c);
  e= &c + 1; f= *e; printint(f);
  return(0);
}
```

我曾希望当生成这些汇编指令时，汇编器会将 `d` 立即放在 `c` 之后：

```
        .comm   c,1,1
        .comm   d,4,4
```

但当我编译汇编输出并检查时，它们并不相邻：

```
$ cc -o out out.s lib/printint.c
$ nm -n out | grep 'B '
0000000000201018 B d
0000000000201020 B b
0000000000201028 B f
0000000000201030 B e
0000000000201038 B c
```

`d` 实际上在 `c` 之前！我必须想出一种方法来确保相邻性，所以我查看了 *SubC* 在这里生成的代码，并更改了我们的编译器，现在生成：

```
        .data
        .globl  c
c:      .long   0	# 四字节整数
        .globl  d
d:      .long   0
        .globl  e
e:      .quad   0	# 八字节指针
        .globl  f
f:      .long   0
```

现在当我们运行 `input16.c` 测试时，`e= &c + 1; f= *e;` 获取 `c` 向上一个整数的地址，并将该整数的值存储在 `f` 中。正如我们声明的：

```c
  int   c;
  int   d;
  ...
  c= 12; d=18; printint(c);
  e= &c + 1; f= *e; printint(f);

```

我们将打印两个数字：

```
cc -o comp1 -g -Wall cg.c decl.c expr.c gen.c main.c misc.c
      scan.c stmt.c sym.c tree.c types.c
./comp1 tests/input16.c
cc -o out out.s lib/printint.c
./out
12
18
```

## 结论和下一步

我对类型之间转换的代码感到更满意了。在幕后，我编写了一些测试代码，向 `modify_type()` 提供所有可能的类型值，以及二元运算和零作为运算。我检查了输出，看起来符合我的预期。只有时间才能证明。

在我们编译器编写之旅的下一部分，我不知道我会做什么！[下一步](../18_Lvalues_Revisited/Readme_zh.md)