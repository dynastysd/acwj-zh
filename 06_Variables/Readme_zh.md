# 第 6 章：变量

我刚刚完成了向编译器添加全局变量，正如我所怀疑的那样，这是大量的工作。而且，几乎编译器中的每个文件都在这个过程中被修改了。所以这部分的旅程将会很长。

## 我们对变量的需求

我们希望能够：

 + 声明变量
 + 使用变量获取存储的值
 + 向变量赋值

这是我们的测试程序 `input02`：

```
int fred;
int jim;
fred= 5;
jim= 12;
print fred + jim;
```

最明显的变化是语法现在有了变量声明、赋值语句和表达式中的变量名。然而，在讨论这些之前，让我们看看如何实现变量。

## 符号表

每个编译器都需要一个[符号表](https://en.wikipedia.org/wiki/Symbol_table)。以后，我们除了全局变量外还会保存更多东西。但现在，这里是表中一个条目的结构（来自 `defs.h`）：

```c
// 符号表结构
struct symtable {
  char *name;                   // 符号的名称
};
```

我们在 `data.h` 中有一个符号数组：

```c
#define NSYMBOLS        1024            // 符号表条目的数量
extern_ struct symtable Gsym[NSYMBOLS]; // 全局符号表
static int Globs = 0;                   // 下一个空闲全局符号槽的位置
```

`Globs` 实际上在 `sym.c` 中，这是管理符号表的文件。在其中我们有这些管理函数：

  + `int findglob(char *s)`：判断符号 s 是否在全局符号表中。返回其槽位置，若未找到则返回 -1。
  + `static int newglob(void)`：获取新全局符号槽的位置，若已用完所有位置则终止程序。
  + `int addglob(char *name)`：将全局符号添加到符号表中。返回符号表中的槽号。

代码相当直接，所以我不会在这里给出代码。有了这些函数，我们就可以在符号表中查找符号和添加新符号。

## 扫描和新的词法单元

如果你看示例输入文件，我们需要几个新的词法单元：

  + 'int'，称为 T_INT
  + '='，称为 T_EQUALS
  + 标识符名称，称为 T_IDENT

在 `scan()` 中添加 '=' 的扫描很容易：

```c
  case '=':
    t->token = T_EQUALS; break;
```

我们可以将 'int' 关键字添加到 `keyword()` 中：

```c
  case 'i':
    if (!strcmp(s, "int"))
      return (T_INT);
    break;
```

对于标识符，我们已经在使用 `scanident()` 将单词存储到 `Text` 变量中。我们可以修改它，使得当一个单词不是关键字时不报错，而是返回一个 T_IDENT 词法单元：

```c
   if (isalpha(c) || '_' == c) {
      // 读入关键字或标识符
      scanident(c, Text, TEXTLEN);

      // 如果是已识别关键字，返回该词法单元
      if (tokentype = keyword(Text)) {
        t->token = tokentype;
        break;
      }
      // 不是已识别关键字，所以是标识符
      t->token = T_IDENT;
      break;
    }
```

## 新的语法

我们即将查看输入语言语法的变化。和之前一样，我将用 BNF 表示法来定义它：

```
 statements: statement
      |      statement statements
      ;

 statement: 'print' expression ';'
      |     'int'   identifier ';'
      |     identifier '=' expression ';'
      ;

 identifier: T_IDENT
      ;
```

标识符作为 T_IDENT 词法单元返回，我们已经有了解析 print 语句的代码。但是，由于我们现在有三种类型的语句，写一个函数来处理每种语句是合理的。我们 `stmt.c` 中的顶级 `statements()` 函数现在看起来是：

```c
// 解析一个或多个语句
void statements(void) {

  while (1) {
    switch (Token.token) {
    case T_PRINT:
      print_statement();
      break;
    case T_INT:
      var_declaration();
      break;
    case T_IDENT:
      assignment_statement();
      break;
    case T_EOF:
      return;
    default:
      fatald("语法错误，词法单元", Token.token);
    }
  }
}
```

我已经将旧的 print 语句代码移到了 `print_statement()` 中，你可以自己查看。

## 变量声明

让我们看看变量声明。这在一个新文件 `decl.c` 中，因为我们以后会有很多其他类型的声明。

```c
// 解析变量的声明
void var_declaration(void) {

  // 确保有一个 'int' 词法单元，后跟一个标识符
  // 和一个分号。Text 现在有标识符的名称。
  // 将其添加为已知标识符
  match(T_INT, "int");
  ident();
  addglob(Text);
  genglobsym(Text);
  semi();
}
```

`ident()` 和 `semi()` 函数是 `match()` 的包装：

```c
void semi(void)  { match(T_SEMI, ";"); }
void ident(void) { match(T_IDENT, "identifier"); }
```

回到 `var_declaration()`，一旦我们将标识符扫描到 `Text` 缓冲区中，就可以用 `addglob(Text)` 将其添加到全局符号表中。其中的代码允许变量被重复声明（目前是这样）。

## 赋值语句

这是 `stmt.c` 中 `assignment_statement()` 的代码：

```c
void assignment_statement(void) {
  struct ASTnode *left, *right, *tree;
  int id;

  // 确保有一个标识符
  ident();

  // 检查它是否已被定义，然后为其创建一个叶子节点
  if ((id = findglob(Text)) == -1) {
    fatals("未声明的变量", Text);
  }
  right = mkastleaf(A_LVIDENT, id);

  // 确保有一个等号
  match(T_EQUALS, "=");

  // 解析后续表达式
  left = binexpr(0);

  // 创建一个赋值 AST 树
  tree = mkastnode(A_ASSIGN, left, right, 0);

  // 为赋值生成汇编代码
  genAST(tree, -1);
  genfreeregs();

  // 匹配后续分号
  semi();
}
```

我们有几个新的 AST 节点类型。A_ASSIGN 将左子节点中的表达式赋值给右子节点。而右子节点将是一个 A_LVIDENT 节点。

为什么我把这个节点叫做 *A_LVIDENT*？因为它代表一个*左值*标识符。那么什么是[左值](https://en.wikipedia.org/wiki/Value_(computer_science)#lrvalue)？

左值是一个与特定位置绑定的值。这里，它是保存变量值的内存地址。当我们做：

```
   area = width * height;
```

我们将右侧的结果（即*右值*）赋值给左侧的变量（即*左值*）。*右值*不与特定位置绑定。这里，表达式的结果可能在某个任意寄存器中。

还要注意，虽然赋值语句的语法是

```
  identifier '=' expression ';'
```

我们将把表达式作为 A_ASSIGN 节点的左子树，并把 A_LVIDENT 的详情保存在右子树中。为什么？因为我们需要在将表达式保存到变量之前先求值。

## AST 结构的变化

我们现在需要在 A_INTLIT AST 节点中存储整数字面量值，或者在 A_IDENT AST 节点中存储符号的详情。我在 AST 结构中添加了一个*联合体*来做到这一点（在 `defs.h` 中）：

```c
// 抽象语法树结构
struct ASTnode {
  int op;                       // 在此树上执行的"操作"
  struct ASTnode *left;         // 左右子树
  struct ASTnode *right;
  union {
    int intvalue;               // 对于 A_INTLIT，整数值
    int id;                     // 对于 A_IDENT，符号槽号
  } v;
};
```

## 生成赋值代码

现在让我们看看 `gen.c` 中 `genAST()` 的变化：

```c
int genAST(struct ASTnode *n, int reg) {
  int leftreg, rightreg;

  // 获取左右子树的值
  if (n->left)
    leftreg = genAST(n->left, -1);
  if (n->right)
    rightreg = genAST(n->right, leftreg);

  switch (n->op) {
  ...
    case A_INTLIT:
    return (cgloadint(n->v.intvalue));
  case A_IDENT:
    return (cgloadglob(Gsym[n->v.id].name));
  case A_LVIDENT:
    return (cgstorglob(reg, Gsym[n->v.id].name));
  case A_ASSIGN:
    // 工作已经完成，返回结果
    return (rightreg);
  default:
    fatald("未知的 AST 运算符", n->op);
  }

```

注意，我们首先求值左 AST 子树，并获得一个包含左子树值的寄存器号。我们现在将此寄存器号传递给右子树。我们需要对 A_LVIDENT 节点这样做，以便 `cg.c` 中的 `cgstorglob()` 函数知道哪个寄存器保存了赋值表达式的右值结果。

所以，考虑这个 AST 树：

```
            A_ASSIGN
           /        \
      A_INTLIT   A_LVIDENT
         (3)        (5)
```

我们调用 `leftreg = genAST(n->left, -1);` 来求值 A_INTLIT 操作。这将 `return (cgloadint(n->v.intvalue));`，即加载一个寄存器，值为 3，并返回寄存器 id。

然后，我们调用 `rightreg = genAST(n->right, leftreg);` 来求值 A_LVIDENT 操作。这将 `return (cgstorglob(reg, Gsym[n->v.id].name));`，即把寄存器存储到 `Gsym[5]` 中名称对应的变量中。

然后我们切换到 A_ASSIGN case。我们的工作已经完成了，右值仍在寄存器中，所以我们让它留在那里并返回它。以后，我们将能够做这样的表达式：

```
  a= b= c = 0;
```

赋值不仅仅是语句，还是表达式。

## 生成 x86-64 代码

你可能已经注意到，我将旧的 `cgload()` 函数改名为 `cgloadint()`。这更具体了。我们现在有一个从全局变量加载值的函数（在 `cg.c` 中）：

```c
int cgloadglob(char *identifier) {
  // 获取一个新寄存器
  int r = alloc_register();

  // 打印出初始化代码
  fprintf(Outfile, "\tmovq\t%s(\%%rip), %s\n", identifier, reglist[r]);
  return (r);
}
```

同样，我们需要一个将寄存器保存到变量的函数：

```c
// 将寄存器的值存储到变量中
int cgstorglob(int r, char *identifier) {
  fprintf(Outfile, "\tmovq\t%s, %s(\%%rip)\n", reglist[r], identifier);
  return (r);
}
```

我们还需要一个创建新的全局整数变量的函数：

```c
// 生成一个全局符号
void cgglobsym(char *sym) {
  fprintf(Outfile, "\t.comm\t%s,8,8\n", sym);
}
```

当然，我们不能让解析器直接访问这些代码。相反，在 `gen.c` 中的通用代码生成器有一个函数作为接口：

```c
void genglobsym(char *s) { cgglobsym(s); }
```

## 表达式中的变量

所以现在我们可以给变量赋值了。但是我们如何将一个变量的值获取到表达式中呢？我们已经有一个 `primary()` 函数来获取整数字面量。让我们修改它来也加载变量的值：

```c
// 解析一个基本因子并返回
// 一个代表它的 AST 节点。
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;

  switch (Token.token) {
  case T_INTLIT:
    // 对于 INTLIT 词法单元，为其创建一个叶子 AST 节点。
    n = mkastleaf(A_INTLIT, Token.intvalue);
    break;

  case T_IDENT:
    // 检查此标识符是否存在
    id = findglob(Text);
    if (id == -1)
      fatals("未知变量", Text);

    // 为其创建一个叶子 AST 节点
    n = mkastleaf(A_IDENT, id);
    break;

  default:
    fatald("语法错误，词法单元", Token.token);
  }

  // 扫描下一个词法单元并返回叶子节点
  scan(&Token);
  return (n);
}
```

注意 T_IDENT case 中的语法检查，以确保变量在我们尝试使用它之前已被声明。

还要注意，*检索*变量值的 AST 叶子节点是 A_IDENT 节点。*保存*到变量的叶子节点是 A_LVIDENT 节点。这就是*右值*和*左值*之间的区别。

## 试用一下

我认为变量声明的内容差不多了，让我们用 `input02` 文件试一下：

```
int fred;
int jim;
fred= 5;
jim= 12;
print fred + jim;
```

我们可以 `make test` 来做这个：

```
$ make test
cc -o comp1 -g cg.c decl.c expr.c gen.c main.c misc.c scan.c
               stmt.c sym.c tree.c
...
./comp1 input02
cc -o out out.s
./out
17
```

如你所见，我们计算了 `fred + jim`，即 5 + 12 或 17。以下是 `out.s` 中的新汇编行：

```
        .comm   fred,8,8                # 声明 fred
        .comm   jim,8,8                 # 声明 jim
        ...
        movq    $5, %r8
        movq    %r8, fred(%rip)         # fred = 5
        movq    $12, %r8
        movq    %r8, jim(%rip)          # jim = 12
        movq    fred(%rip), %r8
        movq    jim(%rip), %r9
        addq    %r8, %r9                # fred + jim
```

## 其他变化

我可能还做了一些其他的变化。我能记住的唯一主要变化是在 `misc.c` 中创建了一些辅助函数，使报告致命错误更容易：

```c
// 打印致命消息
void fatal(char *s) {
  fprintf(stderr, "%s on line %d\n", s, Line); exit(1);
}

void fatals(char *s1, char *s2) {
  fprintf(stderr, "%s:%s on line %d\n", s1, s2, Line); exit(1);
}

void fatald(char *s, int d) {
  fprintf(stderr, "%s:%d on line %d\n", s, d, Line); exit(1);
}

void fatalc(char *s, int c) {
  fprintf(stderr, "%s:%c on line %d\n", s, c, Line); exit(1);
}
```

## 结论与下一步

这是大量的工作。我们必须编写符号表管理的基础代码。我们必须处理两种新语句类型。我们必须添加一些新的词法单元和一些新的 AST 节点类型。最后，我们必须添加一些代码来生成正确的 x86-64 汇编输出。

尝试编写一些示例输入文件，看看编译器是否按预期工作，特别是它是否能检测语法错误和语义错误（使用未声明的变量）。

在我们编译器编写的下一步中，我们将向语言添加六个比较运算符。这将允许我们在后面的部分开始处理控制结构。[下一步](../07_Comparisons/Readme_zh.md)
