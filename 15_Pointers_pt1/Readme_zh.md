# 第 15 章：指针，第一部分

在本章的编译器编写旅程中，我想开始为我们的语言添加指针支持。
具体来说，我想添加以下功能：

 + 指针变量的声明
 + 将地址赋值给指针
 + 解引用指针以获取它所指向的值

由于这是一个正在开发中的工作，我确信我会实现一个目前可用的简化版本，
但稍后我必须对其进行更改或扩展以使其更加通用。

## 新的关键字和词法单元

本次没有新的关键字，只有两个新的词法单元：

 + '&'，T_AMPER
 + '&&'，T_LOGAND

我们还不需要 T_LOGAND，但我现在可以把这部分代码添加到 `scan()` 中：

```c
    case '&':
      if ((c = next()) == '&') {
        t->token = T_LOGAND;
      } else {
        putback(c);
        t->token = T_AMPER;
      }
      break;
```

## 类型的相关代码

我在语言中添加了一些新的原生类型（在 `defs.h` 中）：

```c
// 原始类型
enum {
  P_NONE, P_VOID, P_CHAR, P_INT, P_LONG,
  P_VOIDPTR, P_CHARPTR, P_INTPTR, P_LONGPTR
};
```

我们将有两个新的前缀运算符：

 + '&' 用于获取标识符的地址
 + '*' 用于解引用指针并获取它指向的值

每个运算符产生的表达式类型与它操作的类型不同。我们需要在
`types.c` 中添加几个函数来进行类型转换：

```c
// 给定一个原始类型，返回
// 指向它的指针类型
int pointer_to(int type) {
  int newtype;
  switch (type) {
    case P_VOID: newtype = P_VOIDPTR; break;
    case P_CHAR: newtype = P_CHARPTR; break;
    case P_INT:  newtype = P_INTPTR;  break;
    case P_LONG: newtype = P_LONGPTR; break;
    default:
      fatald("Unrecognised in pointer_to: type", type);
  }
  return (newtype);
}

// 给定一个原始指针类型，返回
// 它所指向的类型
int value_at(int type) {
  int newtype;
  switch (type) {
    case P_VOIDPTR: newtype = P_VOID; break;
    case P_CHARPTR: newtype = P_CHAR; break;
    case P_INTPTR:  newtype = P_INT;  break;
    case P_LONGPTR: newtype = P_LONG; break;
    default:
      fatald("Unrecognised in value_at: type", type);
  }
  return (newtype);
}
```

那么，我们将在哪里使用这些函数呢？

## 声明指针变量

我们希望能够声明标量变量和指针变量，例如：

```c
  char  a; char *b;
  int   d; int  *e;
```

我们已经有了一个 `parse_type()` 函数在 `decl.c` 中，它将类型关键字转换为类型。
让我们扩展它来扫描后续的词法单元，如果下一个词法单元是 '*'，则更改类型：

```c
// 解析当前词法单元并返回
// 一个原始类型的枚举值。同时
// 扫描下一个词法单元
int parse_type(void) {
  int type;
  switch (Token.token) {
    case T_VOID: type = P_VOID; break;
    case T_CHAR: type = P_CHAR; break;
    case T_INT:  type = P_INT;  break;
    case T_LONG: type = P_LONG; break;
    default:
      fatald("Illegal type, token", Token.token);
  }

  // 扫描一个或多个后续的 '*' 词法单元
  // 并确定正确的指针类型
  while (1) {
    scan(&Token);
    if (Token.token != T_STAR) break;
    type = pointer_to(type);
  }

  // 我们离开时下一个词法单元已经被扫描
  return (type);
}
```

这将允许程序员尝试这样做：

```c
   char *****fred;
```

这会失败，因为 `pointer_to()` 还不能将 P_CHARPTR 转换为 P_CHARPTRPTR（暂时）。
但 `parse_type()` 中的代码已经准备好实现它了！

`var_declaration()` 中的代码现在可以愉快地解析指针变量声明：

```c
// 解析变量的声明
void var_declaration(void) {
  int id, type;

  // 获取变量的类型
  // 同时扫描标识符
  type = parse_type();
  ident();
  ...
}
```

### 前缀运算符 '*' 和 '&'

声明的问题解决了，现在让我们看看解析包含 '*' 和 '&' 作为运算符的表达式，
这些运算符位于表达式之前。BNF 语法如下：

```
 prefix_expression: primary
     | '*' prefix_expression
     | '&' prefix_expression
     ;
```

从技术上讲，这允许：

```
   x= ***y;
   a= &&&b;
```

为了防止这两个运算符的不可用，我们添加了一些语义检查。代码如下：

```c
// 解析前缀表达式并返回
// 表示它的子树
struct ASTnode *prefix(void) {
  struct ASTnode *tree;
  switch (Token.token) {
    case T_AMPER:
      // 获取下一个词法单元并递归地
      // 将其解析为前缀表达式
      scan(&Token);
      tree = prefix();

      // 确保它是一个标识符
      if (tree->op != A_IDENT)
        fatal("& operator must be followed by an identifier");

      // 现在将运算符改为 A_ADDR，类型改为
      // 原始类型的指针类型
      tree->op = A_ADDR; tree->type = pointer_to(tree->type);
      break;
    case T_STAR:
      // 获取下一个词法单元并递归地
      // 将其解析为前缀表达式
      scan(&Token); tree = prefix();

      // 目前，确保它是另一个解引用或
      // 标识符
      if (tree->op != A_IDENT && tree->op != A_DEREF)
        fatal("* operator must be followed by an identifier or *");

      // 在树前添加一个 A_DEREF 操作
      tree = mkastunary(A_DEREF, value_at(tree->type), tree, 0);
      break;
    default:
      tree = primary();
  }
  return (tree);
}
```

我们仍在做递归下降，但我们也添加了错误检查以防止输入错误。
目前，`value_at()` 的限制会阻止多个 '*' 运算符连续出现，
但稍后当我们修改 `value_at()` 时，我们不需要回头修改 `prefix()`。

注意，`prefix()` 在没有看到 '*' 或 '&' 运算符时也会调用 `primary()`。
这允许我们更改 `binexpr()` 中的现有代码：

```c
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  int lefttype, righttype;
  int tokentype;

  // 获取左边的树。
  // 同时获取下一个词法单元。
  // 以前是调用 primary()。
  left = prefix();
  ...
}
```

## 新的 AST 节点类型

在上面的 `prefix()` 中，我引入了两个新的 AST 节点类型
（在 `defs.h` 中声明）：

 + A_DEREF：解引用子节点中的指针
 + A_ADDR：获取此节点中标识符的地址

注意，A_ADDR 节点不是一个父节点。对于表达式 `&fred`，
`prefix()` 中的代码将 "fred" 节点中的 A_IDENT 替换为 A_ADDR 节点类型。

## 生成新的汇编代码

在我们的通用代码生成器 `gen.c` 中，`genAST()` 只有几行新代码：

```c
    case A_ADDR:
      return (cgaddress(n->v.id));
    case A_DEREF:
      return (cgderef(leftreg, n->left->type));
```

A_ADDR 节点生成将 `n->v.id` 标识符的地址加载到寄存器中的代码。
A_DEREF 节点获取 `leftreg` 中的指针地址及其关联类型，
并返回一个包含此地址值的寄存器。

### x86-64 实现

我通过查看其他编译器生成的汇编代码得出了以下汇编输出。
它可能不正确！

```c
// 生成将全局标识符的地址加载到
// 变量中的代码。返回一个新的寄存器
int cgaddress(int id) {
  int r = alloc_register();

  fprintf(Outfile, "\tleaq\t%s(%%rip), %s\n", Gsym[id].name, reglist[r]);
  return (r);
}

// 解引用指针以获取它
// 指向的值到同一个寄存器中
int cgderef(int r, int type) {
  switch (type) {
    case P_CHARPTR:
      fprintf(Outfile, "\tmovzbq\t(%s), %s\n", reglist[r], reglist[r]);
      break;
    case P_INTPTR:
    case P_LONGPTR:
      fprintf(Outfile, "\tmovq\t(%s), %s\n", reglist[r], reglist[r]);
      break;
  }
  return (r);
}
```

`leaq` 指令加载命名标识符的地址。在函数中，
`(%r8)` 语法加载寄存器 `%r8` 所指向的值。

## 测试新功能

这是我们的新测试文件 `tests/input15.c` 以及我们编译它时的结果：

```c
int main() {
  char  a; char *b; char  c;
  int   d; int  *e; int   f;

  a= 18; printint(a);
  b= &a; c= *b; printint(c);

  d= 12; printint(d);
  e= &d; f= *e; printint(f);
  return(0);
}

```

```
$ make test15
cc -o comp1 -g -Wall cg.c decl.c expr.c gen.c main.c misc.c
   scan.c stmt.c sym.c tree.c types.c
./comp1 tests/input15.c
cc -o out out.s lib/printint.c
./out
18
18
12
12
```

我决定将测试文件改为以 `.c` 后缀结尾，
因为它们实际上是 C 程序。我还更改了 `tests/mktests` 脚本，
通过使用"真正的"编译器来编译我们的测试文件，从而生成*正确的*结果。

## 结论和下一步

好吧，我们已经实现了指针的初步版本。它们还不完全正确。
例如，如果我写这段代码：

```c
int main() {
  int x; int y;
  int *iptr;
  x= 10; y= 20;
  iptr= &x + 1;
  printint( *iptr);
}
```

它应该打印 20，因为 `&x + 1` 应该指向 `x` 之后的那个 `int`，
也就是 `y`。这距离 `x` 是 8 个字节。然而，
我们的编译器只是简单地在 `x` 的地址上加 1，这是不正确的。
我必须想办法解决这个问题。

在编译器编写旅程的下一部分中，我们将尝试解决这个问题。
[下一步](../16_Global_Vars/Readme_zh.md)