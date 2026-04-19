# 第 25 章：函数调用和实参

在编译器编写的旅程的这一部分，我打算添加调用带有任意数量实参的函数的能力。实参的值会被复制到函数的形参中，并作为局部变量出现。

我之前没有做到这一点，因为编码之前需要进行一些设计思考。让我们再次查看 Eli Bendersky 关于 [x86-64 上的栈帧布局](https://eli.thegreenplace.net/2011/09/06/stack-frame-layout-on-x86-64/) 的文章中的图片。

![](../22_Design_Locals/Figs/x64_frame_nonleaf.png)

最多六个"按值传递"的实参通过寄存器 `%rdi` 到 `%r9` 传递。超过六个实参时，剩下的实参会被压入栈中。

仔细观察栈上的实参值。尽管 `h` 是最后一个实参，它却最先被压入栈中（栈向下增长），而 `g` 实参在 `h` 之后被压入。

C 语言的坏处之一是表达式的求值顺序是未定义的。正如[这里](https://en.cppreference.com/w/c/language/eval_order)所述：

> [任何 C 运算符的操作数的求值顺序，包括函数调用表达式中实参的求值顺序]是未指定的……编译器可以按任意顺序对它们进行求值……

这使得语言可能不可移植：在某个平台上用某个编译器编译的代码，在不同的平台或不同的编译器下可能有不同的行为。

然而，对我们来说，这种未定义的求值顺序是一件好事，只是因为我们可以按更容易编写编译器的方式来生成实参值。我在这里说得轻巧：这其实并不是什么好事。

因为 x86-64 平台期望最后一个实参的值最先被压入栈中，我需要从后往前处理实参来编写代码。我应该确保代码能够很容易地被修改为允许另一个方向的处理：也许可以写一个 `genXXX()` 查询函数来告诉我们的代码以哪个方向处理实参。这个留到以后再写。

### 生成表达式的 AST

我们已经有了 A_GLUE AST 节点类型，所以写一个函数来解析实参表达式并构建 AST 树应该很容易。对于函数调用 `function(expr1, expr2, expr3, expr4)`，我决定按以下方式构建树：

```
                 A_FUNCCALL
                  /
              A_GLUE
               /   \
           A_GLUE  expr4
            /   \
        A_GLUE  expr3
         /   \
     A_GLUE  expr2
     /    \
   NULL  expr1
```

每个表达式都在右侧，之前的表达式在左侧。我必须从右到左遍历表达式子树，以确保在 expr4 可能需要在 x86-64 栈上先于 expr3 压栈的情况下，先处理 expr4。

我们已经有了一个 `funccall()` 函数来解析只有一个实参的简单函数调用。我将修改它来调用 `expression_list()` 函数解析表达式列表并构建 A_GLUE 子树。它返回一个表达式计数，方法是将这个计数存储在顶部 A_GLUE AST 节点中。然后，在 `funccall()` 中，我们可以根据存储在全局符号表中的函数原型检查所有表达式的类型。

我觉得设计方面已经说得够多了。现在开始实现吧。

## 表达式解析的变更

嗯，我花了一个小时左右完成了代码，而且相当惊喜。借用 Twitter 上流传的一句话：

> 数周的编程可以节省数小时的计划。

反过来，花点时间在设计上总是有助于提高编码效率。让我们看看这些变更。我们从解析开始。

现在我们必须解析一个逗号分隔的表达式列表，并构建 A_GLUE AST 树，其中子表达式在右侧，之前的表达式树在左侧。以下是 `expr.c` 中的代码：

```c
// expression_list: <null>
//        | expression
//        | expression ',' expression_list
//        ;

// Parse a list of zero or more comma-separated expressions and
// return an AST composed of A_GLUE nodes with the left-hand child
// being the sub-tree of previous expressions (or NULL) and the right-hand
// child being the next expression. Each A_GLUE node will have size field
// set to the number of expressions in the tree at this point. If no
// expressions are parsed, NULL is returned
static struct ASTnode *expression_list(void) {
  struct ASTnode *tree = NULL;
  struct ASTnode *child = NULL;
  int exprcount = 0;

  // Loop until the final right parentheses
  while (Token.token != T_RPAREN) {

    // Parse the next expression and increment the expression count
    child = binexpr(0);
    exprcount++;

    // Build an A_GLUE AST node with the previous tree as the left child
    // and the new expression as the right child. Store the expression count.
    tree = mkastnode(A_GLUE, P_NONE, tree, NULL, child, exprcount);

    // Must have a ',' or ')' at this point
    switch (Token.token) {
      case T_COMMA:
        scan(&Token);
        break;
      case T_RPAREN:
        break;
      default:
        fatald("Unexpected token in expression list", Token.token);
    }
  }

  // Return the tree of expressions
  return (tree);
}
```

这比我预期的要容易编写得多。现在，我们需要将其与现有的函数调用解析器对接：

```c
// Parse a function call and return its AST
static struct ASTnode *funccall(void) {
  struct ASTnode *tree;
  int id;

  // Check that the identifier has been defined as a function,
  // then make a leaf node for it.
  if ((id = findsymbol(Text)) == -1 || Symtable[id].stype != S_FUNCTION) {
    fatals("Undeclared function", Text);
  }
  // Get the '('
  lparen();

  // Parse the argument expression list
  tree = expression_list();

  // XXX Check type of each argument against the function's prototype

  // Build the function call AST node. Store the
  // function's return type as this node's type.
  // Also record the function's symbol-id
  tree = mkastunary(A_FUNCCALL, Symtable[id].type, tree, id);

  // Get the ')'
  rparen();
  return (tree);
}
```

注意这个 `XXX`，这是我的提醒，表示我还有工作要做。解析器确实会检查函数是否已被声明，但目前它还没有将实参类型与函数原型进行比较。我会很快做这件事。

返回的 AST 树现在具有我在文章开头绘制的形状。现在是时候遍历它并生成汇编代码了。

## 通用代码生成器的变更

按照编译器的编写方式，遍历 AST 的代码是架构无关的，位于 `gen.c` 中，而实际的平台相关后端位于 `cg.c` 中。因此我们从 `gen.c` 的变更开始。

遍历这个新 AST 结构需要相当多的代码，所以我现在有一个专门处理函数调用的函数。在 `genAST()` 中我们现在有：

```c
  // n is the AST node being processed
  switch (n->op) {
    ...
    case A_FUNCCALL:
      return (gen_funccall(n));
  }
```

遍历新 AST 结构的代码如下：

```c
// Generate the code to copy the arguments of a
// function call to its parameters, then call the
// function itself. Return the register that holds
// the function's return value.
static int gen_funccall(struct ASTnode *n) {
  struct ASTnode *gluetree = n->left;
  int reg;
  int numargs=0;

  // If there is a list of arguments, walk this list
  // from the last argument (right-hand child) to the
  // first
  while (gluetree) {
    // Calculate the expression's value
    reg = genAST(gluetree->right, NOLABEL, gluetree->op);
    // Copy this into the n'th function parameter: size is 1, 2, 3, ...
    cgcopyarg(reg, gluetree->v.size);
    // Keep the first (highest) number of arguments
    if (numargs==0) numargs= gluetree->v.size;
    genfreeregs();
    gluetree = gluetree->left;
  }

  // Call the function, clean up the stack (based on numargs),
  // and return its result
  return (cgcall(n->v.id, numargs));
}
```

有几件事需要注意。我们通过在右子节点上调用 `genAST()` 来生成表达式代码。同时，我们将 `numargs` 设置为第一个 `size` 值，这是实参的数量（从一开始，而不是从零开始）。然后我们调用 `cgcopyarg()` 将这个值复制到函数的第 n 个形参中。复制完成后，我们可以释放所有寄存器为下一个表达式做准备，然后向左子节点移动处理上一个表达式。

最后，我们运行 `cgcall()` 生成对函数的实际调用。因为我们可能已经在栈上压入了实参值，所以我们向它提供实参的总数，这样它就能计算出需要弹出多少。

这里没有硬件特定的代码，但正如我在开头提到的，我们是从最后一个表达式到第一个表达式遍历表达式树的。并非所有架构都希望这样，所以我们还有空间使代码在求值顺序方面更加灵活。

## `cg.c` 的变更

现在我们来到了生成实际 x86-64 汇编代码输出的函数。我们创建了一个新函数 `cgcopyarg()`，并修改了一个现有函数 `cgcall()`。

但首先提醒一下，我们有这些寄存器列表：

```c
#define FIRSTPARAMREG 9         // Position of first parameter register
static char *reglist[] =
  { "%r10", "%r11", "%r12", "%r13", "%r9", "%r8", "%rcx", "%rdx", "%rsi", "%rdi" };

static char *breglist[] =
  { "%r10b", "%r11b", "%r12b", "%r13b", "%r9b", "%r8b", "%cl", "%dl", "%sil", "%dil" };

static char *dreglist[] =
  { "%r10d", "%r11d", "%r12d", "%r13d", "%r9d", "%r8d", "%ecx", "%edx", "%esi", "%edi" };
```

FIRSTPARAMREG 设置为最后一个索引位置：我们将向后遍历这个列表。

另外，记住我们得到的实参位置编号是从一开始的（即 1、2、3、4、……），而不是从零开始（0、1、2、3、……），但上面的数组是从零开始的。你会在下面的代码中看到一些 `+1` 或 `-1` 的调整。

以下是 `cgcopyarg()`：

```c
// Given a register with an argument value,
// copy this argument into the argposn'th
// parameter in preparation for a future function
// call. Note that argposn is 1, 2, 3, 4, ..., never zero.
void cgcopyarg(int r, int argposn) {

  // If this is above the sixth argument, simply push the
  // register on the stack. We rely on being called with
  // successive arguments in the correct order for x86-64
  if (argposn > 6) {
    fprintf(Outfile, "\tpushq\t%s\n", reglist[r]);
  } else {
    // Otherwise, copy the value into one of the six registers
    // used to hold parameter values
    fprintf(Outfile, "\tmovq\t%s, %s\n", reglist[r],
            reglist[FIRSTPARAMREG - argposn + 1]);
  }
}
```

除了那个 `+1` 之外，这非常简单。现在是 `cgcall()` 的代码：

```c
// Call a function with the given symbol id
// Pop off any arguments pushed on the stack
// Return the register with the result
int cgcall(int id, int numargs) {
  // Get a new register
  int outr = alloc_register();
  // Call the function
  fprintf(Outfile, "\tcall\t%s\n", Symtable[id].name);
  // Remove any arguments pushed on the stack
  if (numargs>6)
    fprintf(Outfile, "\taddq\t$%d, %%rsp\n", 8*(numargs-6));
  // and copy the return value into our register
  fprintf(Outfile, "\tmovq\t%%rax, %s\n", reglist[outr]);
  return (outr);
}
```

同样，非常简单。

## 测试这些变更

在编译器编写旅程的最后一部分，我们有两个独立的测试程序 `input27a.c` 和 `input27b.c`：我们必须用 `gcc` 编译其中一个。现在，我们可以将它们合并在一起，用我们的编译器编译所有内容。还有第二个测试程序 `input28.c`，包含更多函数调用的示例。和往常一样：

```
$ make test
cc -o comp1 -g -Wall cg.c decl.c expr.c gen.c main.c
    misc.c scan.c stmt.c sym.c tree.c types.c
(cd tests; chmod +x runtests; ./runtests)
  ...
input25.c: OK
input26.c: OK
input27.c: OK
input28.c: OK
```

## 结论与下一步

此刻，我感觉我们的编译器刚从"玩具编译器"变成了一个几乎可用的编译器：我们现在可以编写多函数程序并在函数之间调用。走到这一步花了好几个步骤，但我觉得每一步都不是巨大的一步。

显然还有很长的路要走。我们需要添加结构体、共用体、外部标识符和预处理器。然后我们必须使编译器更健壮，提供更好的错误检测，可能还要添加警告等等。所以，在这一点上，我们可能已经走了一半的路程。

在编译器编写旅程的下一部分，我想我会添加编写函数原型的能力。这将允许我们链接外部函数。我想到的是那些基于 `int` 和 `char *` 的原始 Unix 函数和系统调用，如 `open()`、`read()`、`write()`、`strcpy()` 等。用我们的编译器编译一些有用的程序会很不错。[下一步](../26_Prototypes/Readme_zh.md)