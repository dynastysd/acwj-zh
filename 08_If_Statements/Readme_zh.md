# 第 8 章：If 语句

既然我们可以比较值，是时候为我们的语言添加 IF 语句了。所以，首先，让我们看看 IF 语句的一般语法以及它们如何转换为汇编语言。

## IF 语法

IF 语句的语法是：

```
  if (条件为真) 
    执行第一个代码块
  else
    执行另一个代码块
```

现在，这通常如何转换为汇编语言？事实证明，我们做相反的比较，如果相反比较为真则跳转/分支：

```
       执行相反的比较
       如果为真则跳转到 L1
       执行第一个代码块
       跳转到 L2
L1:
       执行另一个代码块
L2:
   
```

其中 `L1` 和 `L2` 是汇编语言标签。

## 在我们的编译器中生成汇编代码

目前，我们输出代码来根据比较结果设置寄存器，例如

```
   int x; x= 7 < 9;         来自 input04
```

变成

```
        movq    $7, %r8
        movq    $9, %r9
        cmpq    %r9, %r8
        setl    %r9b        如果小于则设置 
        andq    $255,%r9
```

但对于 IF 语句，我们需要基于相反的比较进行跳转：

```
   if (7 < 9) 
```

应该变成：

```
        movq    $7, %r8
        movq    $9, %r9
        cmpq    %r9, %r8
        jge     L1         如果大于或等于则跳转
        ....
L1:
```

所以，我在这部分旅程中实现了 IF 语句。由于这是一个工作项目，我确实需要撤销一些东西并重构它们作为旅程的一部分。我会尽力涵盖过程中的变化以及新增的内容。

## 新的词法单元和悬空 Else

我们的语言中需要一堆新的词法单元。我（目前）也想避免[悬空 else 问题](https://en.wikipedia.org/wiki/Dangling_else)。为此，我改变了语法，使得所有语句组都用 '{' ... '}' 花括号包裹；我称这样的分组为"复合语句"。我们还需要 '(' ... ')' 括号来包含 IF 表达式，加上关键字 'if' 和 'else'。因此，新的词法单元是（在 `defs.h` 中）：

```c
  T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN,
  // 关键字
  ..., T_IF, T_ELSE
```

## 扫描词法单元

单字符词法单元应该是显而易见的，我不会给出扫描它们的代码。关键字也应该相当明显，但我会给出 `scan.c` 中 `keyword()` 的扫描代码：

```c
  switch (*s) {
    case 'e':
      if (!strcmp(s, "else"))
        return (T_ELSE);
      break;
    case 'i':
      if (!strcmp(s, "if"))
        return (T_IF);
      if (!strcmp(s, "int"))
        return (T_INT);
      break;
    case 'p':
      if (!strcmp(s, "print"))
        return (T_PRINT);
      break;
  }
```

## 新的 BNF 语法

我们的语法开始变得庞大了，所以我重写了一下：

```
 compound_statement: '{' '}'          // 空，即没有语句
      |      '{' statement '}'
      |      '{' statement statements '}'
      ;

 statement: print_statement
      |     declaration
      |     assignment_statement
      |     if_statement
      ;

 print_statement: 'print' expression ';'  ;

 declaration: 'int' identifier ';'  ;

 assignment_statement: identifier '=' expression ';'   ;

 if_statement: if_head
      |        if_head 'else' compound_statement
      ;

 if_head: 'if' '(' true_false_expression ')' compound_statement  ;

 identifier: T_IDENT ;
```

我省略了 `true_false_expression` 的定义，但当我们添加更多运算符时，我会把它加进去。

注意 IF 语句的语法：它要么是一个 `if_head`（没有 'else' 子句），要么是一个 `if_head` 后跟一个 'else' 和一个 `compound_statement`。

我分离出了所有不同的语句类型，让它们都有自己的非终结符名称。另外，之前的 `statements` 非终结符现在是 `compound_statement` 非终结符，这需要用 '{' ... '}' 包围语句。

这意味着 head 中的 `compound_statement` 被 '{' ... '}' 包围，'else' 关键字后的任何 `compound_statement` 也是如此。所以如果我们有嵌套的 IF 语句，它们必须看起来像：

```
  if (condition1 为真) {
    if (condition2 为真) {
      statements;
    } else {
      statements;
    }
  } else {
    statements;
  }
```

这样就没有歧义关于每个 'else' 属于哪个 'if'。这解决了悬空 else 问题。以后，我会让 '{' ... '}' 变成可选的。

## 解析复合语句

旧的 `void statements()` 函数现在是 `compound_statement()`，看起来像这样：

```c
// 解析一个复合语句
// 并返回其 AST
struct ASTnode *compound_statement(void) {
  struct ASTnode *left = NULL;
  struct ASTnode *tree;

  // 要求一个左花括号
  lbrace();

  while (1) {
    switch (Token.token) {
      case T_PRINT:
        tree = print_statement();
        break;
      case T_INT:
        var_declaration();
        tree = NULL;            // 这里不生成 AST
        break;
      case T_IDENT:
        tree = assignment_statement();
        break;
      case T_IF:
        tree = if_statement();
        break;
    case T_RBRACE:
        // 当遇到右花括号时，
        // 跳过它并返回 AST
        rbrace();
        return (left);
      default:
        fatald("语法错误，词法单元", Token.token);
    }

    // 对于每个新树，如果 left 为空则保存它，
    // 否则将 left 和新树粘合在一起
    if (tree) {
      if (left == NULL)
        left = tree;
      else
        left = mkastnode(A_GLUE, left, NULL, tree, 0);
    }
  }
}
```

首先，注意代码强制解析器用 `lbrace()` 匹配复合语句开头的 '{'，我们只能在用 `rbrace()` 匹配结尾的 '}' 时才能退出。

其次，注意 `print_statement()`、`assignment_statement()` 和 `if_statement()` 都返回一个 AST 树，`compound_statement()` 也是如此。在我们旧的代码中，`print_statement()` 本身调用 `genAST()` 来求值表达式，然后调用 `genprintint()`。同样，`assignment_statement()` 也调用 `genAST()` 来执行赋值。

好吧，这意味着我们在这里有 AST 树，在那里也有其他 AST 树。生成一个单一的 AST 树，然后调用一次 `genAST()` 来为其生成汇编代码是有意义的。

这不是强制的。例如，SubC 仅为表达式生成 AST。对于语言的结构部分（如语句），SubC 像我在以前版本的编译器中那样对代码生成器进行特定调用。

我决定目前让解析器为整个输入生成一个 AST 树。一旦输入被解析，汇编输出就可以从这一个 AST 树生成。

以后，我可能会为每个函数生成一个 AST 树。以后再说。

## 解析 IF 语法

因为我们是一个递归下降解析器，解析 IF 语句并不太难：

```c
// 解析一个 IF 语句包括
// 任何可选的 ELSE 子句
// 并返回其 AST
struct ASTnode *if_statement(void) {
  struct ASTnode *condAST, *trueAST, *falseAST = NULL;

  // 确保我们有 'if' '('
  match(T_IF, "if");
  lparen();

  // 解析后续表达式
  // 和后面的 ')'。确保
  // 树的运算是比较运算。
  condAST = binexpr(0);

  if (condAST->op < A_EQ || condAST->op > A_GE)
    fatal("错误的比较运算符");
  rparen();

  // 获取复合语句的 AST
  trueAST = compound_statement();

  // 如果我们有 'else'，跳过它
  // 并获取复合语句的 AST
  if (Token.token == T_ELSE) {
    scan(&Token);
    falseAST = compound_statement();
  }
  // 为这个语句构建并返回 AST
  return (mkastnode(A_IF, condAST, trueAST, falseAST, 0));
}
```

目前，我不想处理像 `if (x-2)` 这样的输入，所以我限制了 `binexpr()` 的二元表达式，其根必须是六个比较运算符 A_EQ、A_NE、A_LT、A_GT、A_LE 或 A_GE 之一。

## 第三个子节点

我几乎瞒着你没有解释清楚就偷偷带过了什么东西。在 `if_statement()` 的最后一行，我用以下内容构建了一个 AST 节点：

```c
   mkastnode(A_IF, condAST, trueAST, falseAST, 0);
```

那是*三个* AST 子树！这是怎么回事？你可以看到，IF 语句将有三个子节点：

  + 求值条件的子树
  + 紧随其后的复合语句
  + 可选的 else 关键字后的复合语句

所以我们现在需要一个具有三个子节点的 AST 节点结构（在 `defs.h` 中）：

```c
// AST 节点类型。
enum {
  ...
  A_GLUE, A_IF
};

// 抽象语法树结构
struct ASTnode {
  int op;                       // 在此树上执行的"操作"
  struct ASTnode *left;         // 左、中、右子树
  struct ASTnode *mid;
  struct ASTnode *right;
  union {
    int intvalue;               // 对于 A_INTLIT，整数值
    int id;                     // 对于 A_IDENT，符号槽号
  } v;
};
```

因此，A_IF 树看起来像这样：

```
                      IF
                    / |  \
                   /  |   \
                  /   |    \
                 /    |     \
                /     |      \
               /      |       \
      condition   statements   statements
```

## 粘合 AST 节点

还有一个新的 A_GLUE AST 节点类型。这有什么用？我们现在构建一个包含多个语句的单一 AST 树，所以我们需要一种将它们粘合在一起的方法。

查看 `compound_statement()` 循环代码的结尾：

```c
      if (left != NULL)
        left = mkastnode(A_GLUE, left, NULL, tree, 0);
```

每次我们得到一个新的子树，我们就把它粘合到现有的树上。所以，对于这个语句序列：

```
    stmt1;
    stmt2;
    stmt3;
    stmt4;
```

我们最终得到：

```
             A_GLUE
              /  \
          A_GLUE stmt4
            /  \
        A_GLUE stmt3
          /  \
      stmt1  stmt2
```

而且，当我们深度优先从左到右遍历树时，这仍然按正确顺序生成汇编代码。

## 通用代码生成器

现在我们的 AST 节点有多个子节点，我们的通用代码生成器会变得更复杂。而且，对于比较运算符，我们需要知道我们是在作为 IF 语句的一部分进行比较（基于相反的比较跳转）还是作为正常表达式（基于正常比较将寄存器设置为 1 或 0）。

为此，我修改了 `getAST()` 以便我们可以传入父 AST 节点的 operation：

```c
// 给定一个 AST，一个持有
// 之前右值的寄存器（如果有的话），以及父节点的 AST op，
// 递归生成汇编代码。
// 返回包含树的最终值的寄存器 id
int genAST(struct ASTnode *n, int reg, int parentASTop) {
   ...
}
```

### 处理特定的 AST 节点

`genAST()` 中的代码现在需要处理特定的 AST 节点：

```c
  // 我们现在在顶部有特定的 AST 节点处理
  switch (n->op) {
    case A_IF:
      return (genIFAST(n));
    case A_GLUE:
      // 处理每个子语句，并在每个子语句之后释放
      // 寄存器
      genAST(n->left, NOREG, n->op);
      genfreeregs();
      genAST(n->right, NOREG, n->op);
      genfreeregs();
      return (NOREG);
  }
```

如果我们不返回，我们继续处理正常的二元运算符 AST 节点，但有一个例外：比较节点：

```c
    case A_EQ:
    case A_NE:
    case A_LT:
    case A_GT:
    case A_LE:
    case A_GE:
      // 如果父 AST 节点是 A_IF，生成一个比较
      // 后跟一个跳转。否则，比较寄存器并根据比较结果
      // 将其中一个设置为 1 或 0。
      if (parentASTop == A_IF)
        return (cgcompare_and_jump(n->op, leftreg, rightreg, reg));
      else
        return (cgcompare_and_set(n->op, leftreg, rightreg));
```

我将在下面介绍新函数 `cgcompare_and_jump()` 和 `cgcompare_and_set()`。

### 生成 IF 汇编代码

我们用一个特定函数以及一个生成新标签号的函数来处理 A_IF AST 节点：

```c
// 生成并返回一个新的标签号
static int label(void) {
  static int id = 1;
  return (id++);
}

// 为 IF 语句和可选的 ELSE 子句
// 生成代码
static int genIFAST(struct ASTnode *n) {
  int Lfalse, Lend;

  // 生成两个标签：一个用于
  // 假的复合语句，一个用于
  // 整体 IF 语句的结尾。
  // 当没有 ELSE 子句时，Lfalse _就是_
  // 结尾标签！
  Lfalse = label();
  if (n->right)
    Lend = label();

  // 生成条件代码，后跟
  // 一个到假标签的零跳转。
  // 我们通过将 Lfalse 标签作为寄存器来骗过它。
  genAST(n->left, Lfalse, n->op);
  genfreeregs();

  // 生成真的复合语句
  genAST(n->mid, NOREG, n->op);
  genfreeregs();

  // 如果有可选的 ELSE 子句，
  // 生成跳转到结尾的跳转
  if (n->right)
    cgjump(Lend);

  // 现在是假标签
  cglabel(Lfalse);

  // 可选的 ELSE 子句：生成
  // 假的复合语句和
  // 结尾标签
  if (n->right) {
    genAST(n->right, NOREG, n->op);
    genfreeregs();
    cglabel(Lend);
  }

  return (NOREG);
}
```

实际上，代码在做：

```c
  genAST(n->left, Lfalse, n->op);       // 条件和跳转到 Lfalse
  genAST(n->mid, NOREG, n->op);         // 'if' 后的语句
  cgjump(Lend);                         // 跳转到 Lend
  cglabel(Lfalse);                      // Lfalse: 标签
  genAST(n->right, NOREG, n->op);       // 'else' 后的语句
  cglabel(Lend);                        // Lend: 标签
```

## x86-64 代码生成函数

所以我们现在有几个新的 x86-64 代码生成函数。其中一些取代了旅程上一部分创建的六个 `cgXXX()` 比较函数。

对于正常的比较函数，我们现在传入 AST 操作来选择相关的 x86-64 `set` 指令：

```c
// 比较指令列表，
// 按 AST 顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *cmplist[] =
  { "sete", "setne", "setl", "setg", "setle", "setge" };

// 比较两个寄存器并在为真时设置。
int cgcompare_and_set(int ASTop, int r1, int r2) {

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("cgcompare_and_set() 中错误的 ASTop");

  fprintf(Outfile, "\tcmpq\t%s, %s\n", reglist[r2], reglist[r1]);
  fprintf(Outfile, "\t%s\t%s\n", cmplist[ASTop - A_EQ], breglist[r2]);
  fprintf(Outfile, "\tmovzbq\t%s, %s\n", breglist[r2], reglist[r2]);
  free_register(r1);
  return (r2);
}
```

我还发现了一个 x86-64 指令 `movzbq`，它可以从一个寄存器移动最低字节并将其扩展以适应 64 位寄存器。我现在使用它而不是旧代码中的 `and $255`。

我们需要函数来生成一个标签和跳转到它：

```c
// 生成一个标签
void cglabel(int l) {
  fprintf(Outfile, "L%d:\n", l);
}

// 生成一个跳转到标签的跳转
void cgjump(int l) {
  fprintf(Outfile, "\tjmp\tL%d\n", l);
}
```

最后，我们需要一个函数来进行比较并基于相反的比较结果进行跳转。所以，使用 AST 比较节点类型，我们做相反的比较：

```c
// 反转跳转指令列表，
// 按 AST 顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *invcmplist[] = { "jne", "je", "jge", "jle", "jg", "jl" };

// 比较两个寄存器并在为假时跳转。
int cgcompare_and_jump(int ASTop, int r1, int r2, int label) {

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("cgcompare_and_set() 中错误的 ASTop");

  fprintf(Outfile, "\tcmpq\t%s, %s\n", reglist[r2], reglist[r1]);
  fprintf(Outfile, "\t%s\tL%d\n", invcmplist[ASTop - A_EQ], label);
  freeall_registers();
  return (NOREG);
}
```

## 测试 IF 语句

做 `make test` 来编译 `input05` 文件：

```c
{
  int i; int j;
  i=6; j=12;
  if (i < j) {
    print i;
  } else {
    print j;
  }
}
```

这是生成的汇编输出：

```
        movq    $6, %r8
        movq    %r8, i(%rip)    # i=6;
        movq    $12, %r8
        movq    %r8, j(%rip)    # j=12;
        movq    i(%rip), %r8
        movq    j(%rip), %r9
        cmpq    %r9, %r8        # 比较 %r8-%r9，即 i-j
        jge     L1              # 如果 i >= j 则跳转到 L1
        movq    i(%rip), %r8
        movq    %r8, %rdi       # 打印 i;
        call    printint
        jmp     L2              # 跳过 else 代码
L1:
        movq    j(%rip), %r8
        movq    %r8, %rdi       # 打印 j;
        call    printint
L2:
```

而且，当然，`make test` 显示：

```
cc -o comp1 -g cg.c decl.c expr.c gen.c main.c misc.c
      scan.c stmt.c sym.c tree.c
./comp1 input05
cc -o out out.s
./out
6                   # 因为 6 小于 12
```

## 结论与下一步

我们用 IF 语句为语言添加了第一个控制结构。一路上我必须重写一些现有的东西，而且鉴于我脑子里没有一个完整的架构计划，我将来可能需要重写更多的东西。

这部分旅程的难题是，我们必须为 IF 决策执行与正常比较运算符相反的比较。我的解决方案是通知每个 AST 节点其父节点的节点类型；比较节点现在可以看到父节点是否是 A_IF 节点。

我知道 Nils Holm 在实现 SubC 时选择了不同的方法，所以你应该看看他的代码，只是为了看同一问题的不同解决方案。

在我们编译器编写的下一步中，我们将添加另一个控制结构：WHILE 循环。[下一步](../09_While_Loops/Readme_zh.md)
