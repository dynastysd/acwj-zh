# 第 41 章：局部变量初始化

在上一部分的大量修改之后，实现局部变量的初始化就变得相当简单了。

我们希望能做到这样的操作（在函数内部）：

```c
  int x= 2, y= x+3, z= 5 * x - y;
  char *foo= "Hello world";
```

由于处于函数内部，我们可以为表达式构建 AST 树，为变量构建 A_IDENT 节点，并将它们与 A_ASSIGN 父节点连接在一起。而且，由于可能有多个带有赋值的声明，我们需要构建一个 A_GLUE 树来保存所有的赋值树。

唯一的问题是解析局部声明的代码与处理语句解析的代码之间相隔了相当一段距离。事实上：

  + `stmt.c` 中的 `single_statement()` 遇到类型标识符时调用
  + `decl.c` 中的 `declaration_list()` 来解析多个声明，该函数调用
  + `symbol_declaration()` 来解析一个声明，该函数调用
  + `scalar_declaration()` 来解析标量变量声明和赋值

主要问题是所有这些函数都已经有返回值，所以我们无法在 `scalar_declaration()` 中构建 AST 树并将其返回给 `single_statement()`。

此外，`declaration_list()` 会解析多个声明，所以它将负责构建 A_GLUE 树来保存它们。

解决方案是从 `single_statement()` 向 `declaration_list()` 传递一个"指针的指针"，这样我们就可以传回指向 A_GLUE 树的指针。同样地，我们也会从 `declaration_list()` 向 `scalar_declaration()` 传递一个"指针的指针"，后者将传回它所构建的任何赋值树的指针。

## 对 `scalar_declaration()` 的修改

如果我们在局部上下文中遇到标量变量声明中的 '='，我们这样做：

```c
  struct ASTnode *varnode, *exprnode;
  struct ASTnode **tree;                 // 是传入的指针参数

  // 变量正在被初始化
  if (Token.token == T_ASSIGN) {
    ...
    if (class == C_LOCAL) {
      // 用变量创建一个 A_IDENT AST 节点
      varnode = mkastleaf(A_IDENT, sym->type, sym, 0);

      // 获取赋值的表达式，转换为右值
      exprnode = binexpr(0);
      exprnode->rvalue = 1;

      // 确保表达式的类型与变量匹配
      exprnode = modify_type(exprnode, varnode->type, 0);
      if (exprnode == NULL)
        fatal("Incompatible expression in assignment");

      // 创建一个赋值 AST 树
      *tree = mkastnode(A_ASSIGN, exprnode->type, exprnode,
                                        NULL, varnode, NULL, 0);
    }
  }
```

就这样。我们模拟了通常在 `expr.c` 中为赋值表达式构建 AST 树的过程。完成后，我们将赋值树传回。它会向上冒泡到 `declaration_list()`。后者现在这样做：

```c
  struct ASTnode **gluetree;            // 是传入的指针参数
  struct ASTnode *tree;
  *gluetree= NULL;
  ...
  // 现在解析符号列表
  while (1) {
    ...
    // 解析这个符号
    sym = symbol_declaration(type, *ctype, class, &tree);
    ...
    // 将局部声明中的任何 AST 树粘合在一起
    // 构建一个要执行的赋值序列
    if (*gluetree== NULL)
      *gluetree= tree;
    else
      *gluetree = mkastnode(A_GLUE, P_NONE, *gluetree, NULL, tree, NULL, 0);
    ...
  }
```

所以 `gluetree` 被设置为一个包含一堆 A_GLUE 节点的 AST 树，每个 A_GLUE 节点都有一个带有 A_IDENT 子节点和表达式子节点的 A_ASSIGN 子节点。

然后，在 `stmt.c` 的 `single_statement()` 中：

```c
    ...
    case T_IDENT:
      // 我们必须检查标识符是否匹配 typedef。
      // 如果不是，就当作表达式处理。
      // 否则，继续到 parse_type() 调用。
      if (findtypedef(Text) == NULL) {
        stmt= binexpr(0); semi(); return(stmt);
      }
    case T_CHAR:
    case T_INT:
    case T_LONG:
    case T_STRUCT:
    case T_UNION:
    case T_ENUM:
    case T_TYPEDEF:
      // 变量声明列表的开始
      declaration_list(&ctype, C_LOCAL, T_SEMI, T_EOF, &stmt);
      semi();
      return (stmt);            // 来自声明的任何赋值
    ...
```

## 测试新代码

上面的修改非常简短且简单，所以第一次编译就工作了。这不是经常发生的事！我们的测试程序 `tests/input100.c` 如下：

```c
#include <stdio.h>
int main() {
  int x= 3, y=14;
  int z= 2 * x + y;
  char *str= "Hello world";
  printf("%s %d %d\n", str, x+y, z);
  return(0);
}
```

它产生了以下正确的输出：`Hello world 17 20`。

## 结论与下一步

在这段旅程中偶尔有一个简单的部分真是太好了。我现在开始和自己打赌：

  + 这段旅程总共有多少部分，以及
  + 我能否在年底前完成

目前我猜测大约 60 部分，有 75% 的把握能在年底前完成。但我们仍然需要为编译器添加一些小但可能很复杂的功能。

在我们编译器编写旅程的下一部分，我将向编译器添加类型转换解析。[下一步](../42_Casting/Readme_zh.md)