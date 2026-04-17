# 第 18 章：左值和右值再探

由于这是一项正在进行的工作，没有设计文档指导我，
我偶尔需要删除已经编写的代码并重写它，使其更加通用，或修复缺点。这就是
这一部分旅程的情况。

我们在第15部分添加了对指针的初始支持，这样我们就可以
编写如下代码：

```c
  int  x;
  int *y;
  int  z;
  x= 12; y= &x; z= *y;
```

这一切都很好，但我知道我们最终必须
支持在赋值语句左侧使用指针，例如

```c
  *y = 14;
```

要做到这一点，我们必须重新审视
[左值和右值](https://en.wikipedia.org/wiki/Value_(computer_science)#lrvalue)这一主题。
回顾一下，*左值*是一个绑定到特定位置的值，而
*右值*是一个不是绑定到特定位置的值。左值是持久的，因为我们可以在
未来的指令中检索它们的值。另一方面，
右值是短暂的：我们可以在使用完成后丢弃它们。

### 右值和左值的例子

右值的一个例子是整数字面量，例如23。我们可以在
表达式中使用它，然后在之后丢弃它。左值的例子是
我们可以*存储到*的内存位置，例如：

```
   a            标量变量 a
   b[0]         数组 b 的第零个元素
   *c           指针 c 指向的位置
   (*d)[0]      d 指向的数组的第零个元素
```

正如我之前提到的，*左值*和*右值*这个名字来自
赋值语句的两侧：左值在右边，右值在右边。

## 扩展我们的左值概念

现在，编译器将几乎所有内容都视为右值。对于
变量，它从变量的位置检索值。我们唯一对左值概念的表示是
将赋值左侧的标识符标记为 A_LVIDENT。我们在
`genAST()``中手动处理：

```c
    case A_IDENT:
      return (cgloadglob(n->v.id));
    case A_LVIDENT:
      return (cgstorglob(reg, n->v.id));
    case A_ASSIGN:
      // 工作已经完成，返回结果
      return (rightreg);
```

这是我们用于 `a= b;` 等语句的。但现在我们需要标记
不仅仅是赋值左侧的标识符为左值。

在这个过程中生成汇编代码也很重要。当我写这一部分时，我尝试了一个想法，
在树前面加上一个"A_LVALUE" AST节点作为父节点，
告诉代码生成器输出它的左值版本而不是右值版本。但这被证明是太晚了：
子树已经被求值，并且已经生成了它的右值代码。

### 又一次 AST 节点变更

我不想继续向 AST 节点添加更多字段，但这是我最终做的。我们现在有一个字段来指示节点
应该生成左值代码还是右值代码：

```c
// 抽象语法树结构
struct ASTnode {
  int op;                       // 在这棵树上执行的"操作"
  int type;                     // 这棵树生成的任何表达式的类型
  int rvalue;                   // 如果节点是右值则为真
  ...
};
```

`rvalue` 字段只保存一位信息；稍后，如果我需要
存储其他布尔值，我将能够将其用作位字段。

问题：为什么我让这个字段表示节点的"右值"性而不是"左值"性？毕竟，
我们 AST 树中的大多数节点将保存右值而不是左值。当我在 Nils Holm 的 SubC 书中阅读时，我读到了这一行：

> 由于间接寻址以后无法逆转，解析器假设每个
  部分表达式都是一个左值。

考虑解析器处理语句 `b = a + 2`。解析完
`b` 标识符后，我们还无法判断这是左值还是右值。
直到我们遇到 `=` 标记，我们才能得出结论它是一个左值。

此外，C 语言允许赋值作为表达式，所以我们也可以
写 `b = c = a + 2`。同样，当我们解析 `a` 标识符时，
在解析下一个标记之前，我们无法判断它是左值还是右值。

因此，我选择假设每个 AST 节点默认都是左值。一旦我们能确定地判断一个节点是右值，
我们就可以将 `rvalue` 字段设置为表示这一点。

## 赋值表达式

我还提到 C 语言允许赋值作为表达式。既然我们有了清晰的左值/右值区别，
我们可以将赋值的解析从语句转移并把代码移入表达式解析器。我稍后会介绍这一点。

现在是时候看看对编译器代码库做了什么来实现这一切了。一如既往，我们从标记和扫描器开始。

## 标记和扫描变更

这次我们没有新的标记或新的关键字。但有一个变更
影响了标记代码。`=` 现在是一个二元运算符，两边都有表达式，
所以我们需要将其与其他二元运算符集成。

根据[这个 C 运算符列表](https://en.cppreference.com/w/c/language/operator_precedence)，`=` 运算符的优先级比 `+` 或 `-` 低很多。我们需要重新排列我们的运算符列表及其优先级。在 `defs.h` 中：

```c
// 标记类型
enum {
  T_EOF,
  // 运算符
  T_ASSIGN,
  T_PLUS, T_MINUS, ...
```

在 `expr.c` 中，我们需要更新保存我们二元运算符优先级的代码：

```c
// 每个标记的运算符优先级。必须
// 与 defs.h 中的标记顺序相匹配
static int OpPrec[] = {
   0, 10,                       // T_EOF,  T_ASSIGN
  20, 20,                       // T_PLUS, T_MINUS
  30, 30,                       // T_STAR, T_SLASH
  40, 40,                       // T_EQ, T_NE
  50, 50, 50, 50                // T_LT, T_GT, T_LE, T_GE
};
```

## 解析器的变更

现在我们必须将赋值作为语句的解析移除，使它们成为表达式。我还擅自从语言中删除了"print"语句，
因为我们现在可以调用 `printint()`。所以，在 `stmt.c` 中，我删除了 `print_statement()` 和
`assignment_statement()`。

> 我还从语言中删除了 T_PRINT 和 'print' 关键字。由于现在我们的左值和右值概念不同，
  我也删除了 A_LVIDENT AST 节点类型。

目前，`stmt.c` 中 `single_statement()` 的语句解析器假设如果它不
识别第一个标记，那么接下来的是一个表达式：

```c
static struct ASTnode *single_statement(void) {
  int type;

  switch (Token.token) {
    ...
    default:
    // 目前，查看这是否是一个表达式。
    // 这会捕获赋值语句。
    return (binexpr(0));
  }
}
```

这确实意味着 `2+3;` 目前将被视为一个合法语句。我们稍后会修复这个问题。在 `compound_statement()` 中，我们还确保表达式后面跟有分号：

```c
    // 有些语句后面必须跟分号
    if (tree != NULL && (tree->op == A_ASSIGN ||
                         tree->op == A_RETURN || tree->op == A_FUNCCALL))
      semi();
```

## 表达式解析

你可能认为，既然 `=` 被标记为二元表达式运算符并且我们设置了它的优先级，我们就完成了。并非如此！
我们有两件事需要担心：

1. 我们需要在左侧左值的代码之前生成右侧右值的汇编代码。我们过去在语句解析器中这样做，
   现在我们必须在表达式解析器中做这件事。
2. 赋值表达式是*右结合的*：运算符与右侧表达式的绑定比与左侧的更紧密。

我们之前没有涉及右结合性。让我们看一个例子。
考虑表达式 `2 + 3 + 4`。我们可以愉快地从左到右解析并构建 AST 树：

```
      +
     / \
    +   4
   / \
  2   3
```

对于表达式 `a= b= 3`，如果我们做上面的操作，我们最终会得到树：

```
      =
     / \
    =   3
   / \
  a   b
```

我们不想先做 `a= b`，然后再尝试将 3 分配给这个左子树。相反，我们想要生成这棵树：

```
        =
       / \
      =   a
     / \
    3   b
```

我反转了叶节点以符合汇编输出顺序。我们首先将 3 存储在 `b` 中。然后这个赋值的结果 3 被存储在 `a` 中。

### 修改 Pratt 解析器

我们正在使用 Pratt 解析器来正确解析二元运算符的优先级。我搜索了如何向 Pratt 解析器添加右结合性，
在[维基百科](https://en.wikipedia.org/wiki/Operator-precedence_parser)上找到了这些信息：

```
   当前瞻是一个二元运算符，其优先级大于 op 的优先级，
   或者是一个右结合运算符，其优先级等于 op 的优先级
```

因此，对于右结合运算符，我们测试下一个运算符是否与我们当前的运算符具有相同的优先级。这是对解析器逻辑的简单修改。我在 `expr.c` 中引入了一个新函数来确定一个运算符是否是右结合的：

```c
// 如果标记是右结合的则返回真，
// 否则返回假。
static int rightassoc(int tokentype) {
  if (tokentype == T_ASSIGN)
    return(1);
  return(0);
}

```

在 `binexpr()` 中，我们按照之前提到的那样修改了 while 循环，并且我们还加入了 A_ASSIGN 特定代码来交换子树：

```c
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  struct ASTnode *ltemp, *rtemp;
  int ASTop;
  int tokentype;

  // 获取左边的树
  left = prefix();
  ...

  // 当这个标记的优先级高于前一个标记的优先级，
  // 或者是右结合的且等于前一个标记的优先级时
  while ((op_precedence(tokentype) > ptp) ||
         (rightassoc(tokentype) && op_precedence(tokentype) == ptp)) {
    ...
    // 用我们标记的优先级递归调用 binexpr() 来构建子树
    right = binexpr(OpPrec[tokentype]);

    ASTop = binastop(tokentype);
    if (ASTop == A_ASSIGN) {
      // 赋值
      // 将右树变成右值
      right->rvalue= 1;
      ...

      // 交换左右，这样右表达式的代码将在左表达式之前生成
      ltemp= left; left= right; right= ltemp;
    } else {
      // 我们不是在做赋值，所以两棵树都应该是右值
      left->rvalue= 1;
      right->rvalue= 1;
    }
    ...
  }
  ...
}
```

还要注意代码明确地将赋值表达式的右侧标记为右值。并且，对于非赋值，两边的表达式都被标记为右值。

分散在 `binexpr()` 中还有几行代码明确地将一棵树设置为右值。这些在我们遇到叶节点时执行。例如 `b= a;` 中的 `a` 标识符需要被标记为右值，但我们永远不会进入 while 循环体来做到这一点。

## 打印树

解析器的更改已经完成。我们现在有几个节点被标记为右值，有些则完全没有标记。在这一点上，我意识到我很难可视化生成的 AST 树。我在 `tree.c` 中写了一个名为 `dumpAST()` 的函数来将每个 AST 树打印到标准输出。它并不复杂。编译器现在有一个 `-T` 命令行参数来设置一个内部标志 `O_dumpAST`。并且 `decl.c` 中的 `global_declarations()` 代码现在这样做：

```c
       // 解析函数声明并为它生成汇编代码
       tree = function_declaration(type);
       if (O_dumpAST) {
         dumpAST(tree, NOLABEL, 0);
         fprintf(stdout, "\n\n");
       }
       genAST(tree, NOLABEL, 0);

```

树转储器代码按树遍历顺序打印出每个节点，所以输出不是树形的。但是，
每个节点的缩进表示它在树中的深度。

让我们看一些赋值表达式的示例 AST 树。我们从 `a= b= 34;` 开始：

```
      A_INTLIT 34
    A_WIDEN
    A_IDENT b
  A_ASSIGN
  A_IDENT a
A_ASSIGN
```

34 足够小，可以作为一个字符大小的字面量，但它被加宽以匹配 `b` 的类型。`A_IDENT b` 没有说"右值"，所以它是左值。34 的值存储在 `b` 左值中。然后这个值被存储在 `a` 左值中。

现在让我们尝试 `a= b + 34;`：

```
    A_IDENT rval b
      A_INTLIT 34
    A_WIDEN
  A_ADD
  A_IDENT a
A_ASSIGN
```

你现在可以看到"rval `b`"，所以 `b` 的值被加载到寄存器中，而 `b+34` 表达式的结果被存储在 `a` 左值中。

让我们再做一个，`*x= *y`：

```
    A_IDENT y
  A_DEREF rval
    A_IDENT x
  A_DEREF
A_ASSIGN
```

标识符 `y` 被解引用，这个右值被加载。然后它被存储在 `x` 解引用的左值中。

## 将上述内容转换为代码

既然左值和右值节点被清楚地识别出来，我们就可以将注意力转向如何将每个节点翻译成汇编代码。有许多节点如整数字面量、加法等，它们显然是右值。只有 `gen.c` 中 `genAST()` 代码需要担心的可能是左值的 AST 节点类型。以下是我对这些节点类型的处理：

```c
    case A_IDENT:
      // 如果我们是右值或者正在被解引用，加载我们的值
      if (n->rvalue || parentASTop== A_DEREF)
        return (cgloadglob(n->v.id));
      else
        return (NOREG);

    case A_ASSIGN:
      // 我们是赋值给标识符还是通过指针赋值？
      switch (n->right->op) {
        case A_IDENT: return (cgstorglob(leftreg, n->right->v.id));
        case A_DEREF: return (cgstorderef(leftreg, rightreg, n->right->type));
        default: fatald("Can't A_ASSIGN in genAST(), op", n->op);
      }

    case A_DEREF:
      // 如果我们是右值，解引用以获取我们指向的值，
      // 否则将其保留给 A_ASSIGN 通过指针存储
      if (n->rvalue)
        return (cgderef(leftreg, n->left->type));
      else
        return (leftreg);
```

### 对 x86-64 代码生成器的更改

对 `cg.c` 的唯一更改是一个允许我们通过指针存储值的函数：

```c
// 通过解引用的指针存储
int cgstorderef(int r1, int r2, int type) {
  switch (type) {
    case P_CHAR:
      fprintf(Outfile, "\tmovb\t%s, (%s)\n", breglist[r1], reglist[r2]);
      break;
    case P_INT:
      fprintf(Outfile, "\tmovq\t%s, (%s)\n", reglist[r1], reglist[r2]);
      break;
    case P_LONG:
      fprintf(Outfile, "\tmovq\t%s, (%s)\n", reglist[r1], reglist[r2]);
      break;
    default:
      fatald("Can't cgstoderef on type:", type);
  }
  return (r1);
}
```

这几乎正好与出现在这个新函数之前的 `cgderef()` 相反。

## 结论与下一步

对于这一部分旅程，我认为我采取了两种或三种不同的设计方向，尝试了它们，遇到了死胡同，然后在到达这里的解决方案之前退出了。我知道，在 SubC 中，Nils 传递了一个单一的"左值"结构，它保存了正在处理的 AST 树节点的"左值"性。但他的树只保存一个表达式；这个编译器的 AST 树保存一整个函数的节点。而且我确信，如果你看其他三个编译器，你可能会找到另外三种解决方案。

接下来我们有很多事情可以做。有很多 C 运算符可以相对容易地添加到编译器中。我们有 A_SCALE，所以我们可以尝试结构体。迄今为止，还没有局部变量，这在某个时候需要处理。而且，我们应该将函数泛化以具有多个参数和访问它们的能力。

在我们编译器编写旅程的下一部分，我想处理数组。这将是解引用、左值和右值以及按数组元素大小缩放数组索引的组合。我们已经具备了所有语义组件，但我们需要添加标记、解析和实际的索引功能。这应该是一个像这一个一样有趣的话题。[下一步](../19_Arrays_pt1/Readme_zh.md)