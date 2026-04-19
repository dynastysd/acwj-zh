# 第 42 章：类型转换和 NULL

在编译器编写的旅程的这一部分，我实现了类型转换。我本来以为这就能让我做到：

```c
#define NULL (void *)0
```

但我之前没有做足够的工作来让 `void *` 正常工作。所以我添加了类型转换，同时也让 `void *` 工作了。

## 什么是类型转换？

类型转换是指你强制将表达式的类型更改为其他类型。常见的原因是将整数值缩小到更小范围的类型，或者将一种类型的指针赋值到另一种类型的指针存储中，例如：

```c
  int   x= 65535;
  char  y= (char)x;     // y 现在是 255，即低 8 位
  int  *a= &x;
  char *b= (char *)a;   // b 指向 x 的地址
  long *z= (void *)0;   // z 是一个 NULL 指针，不指向任何东西
```

注意上面我在赋值语句中使用了转换。对于函数内的表达式，我们需要向 AST 树添加一个 A_CAST 节点来表示"将原始表达式类型转换为此新类型"。

对于全局变量赋值，我们需要修改赋值解析器以允许在字面量值之前进行转换。

## 一个新函数，`parse_cast()`

我在 `decl.c` 中添加了这个新函数：

```c
// Parse a type which appears inside a cast
int parse_cast(void) {
  int type, class;
  struct symtable *ctype;

  // Get the type inside the parentheses
  type= parse_stars(parse_type(&ctype, &class));

  // Do some error checking. I'm sure more can be done
  if (type == P_STRUCT || type == P_UNION || type == P_VOID)
    fatal("Cannot cast to a struct, union or void type");
  return(type);
}
```

周围 '(' ... ')' 的解析在其他地方完成。我们获取类型标识符和后续的 '*' 词法单元来得到转换的类型。然后我们阻止向结构体、联合体和 `void` 进行转换。

我们需要这样一个函数，因为转换既要在表达式中进行，也要在全局变量赋值中进行。我不想写任何[重复代码](https://en.wikipedia.org/wiki/Don%27t_repeat_yourself)。

## 表达式中的转换解析

我们已经在表达式代码中解析括号了，所以需要修改它。在 `expr.c` 的 `primary()` 中，我们现在这样做：

```c
static struct ASTnode *primary(void) {
  int type=0;
  ...
  switch (Token.token) {
  ...
    case T_LPAREN:
    // Beginning of a parenthesised expression, skip the '('.
    scan(&Token);


    // If the token after is a type identifier, this is a cast expression
    switch (Token.token) {
      case T_IDENT:
        // We have to see if the identifier matches a typedef.
        // If not, treat it as an expression.
        if (findtypedef(Text) == NULL) {
          n = binexpr(0); break;
        }
      case T_VOID:
      case T_CHAR:
      case T_INT:
      case T_LONG:
      case T_STRUCT:
      case T_UNION:
      case T_ENUM:
        // Get the type inside the parentheses
        type= parse_cast();

        // Skip the closing ')' and then parse the following expression
        rparen();

      default: n = binexpr(0); // Scan in the expression
    }

    // We now have at least an expression in n, and possibly a non-zero type in type
    // if there was a cast. Skip the closing ')' if there was no cast.
    if (type == 0)
      rparen();
    else
      // Otherwise, make a unary AST node for the cast
      n= mkastunary(A_CAST, type, n, NULL, 0);
    return (n);
  }
}
```

这些内容很多，让我们分阶段来理解。所有这些 case 都确保在 '(' 词法单元之后有一个类型标识符。我们调用 `parse_cast()` 来获取转换类型并解析 ')' 词法单元。

我们还没有 AST 树可以返回，因为我们还不知道要转换哪个表达式。所以我们跳到 default case，在那里解析下一个表达式。

此时，`type` 要么仍为 0（没有转换），要么非零（有转换）。如果没有转换，必须跳过右括号，然后直接返回括号中的表达式。

如果有转换，我们使用新的 `type` 和后续表达式作为子节点来构建一个 A_CAST 节点。

## 生成转换的汇编代码

幸运的是，表达式的值将存储在寄存器中。所以如果我们做：

```c
  int   x= 65535;
  char  y= (char)x;     // y 现在是 255，即低 8 位
```

那么我们可以简单地将 65535 放入寄存器。但当我们保存到 y 时，左值的类型将被调用以生成正确大小的保存代码：

```
        movq    $65535, %r10            # Store 65535 in x
        movl    %r10d, -4(%rbp)
        movslq  -4(%rbp), %r10          # Get x into %r10
        movb    %r10b, -8(%rbp)         # Store one byte into y
```

所以，在 `gen.c` 的 `genAST()` 中，我们有这段处理转换的代码：

```c
  ...
  leftreg = genAST(n->left, NOLABEL, NOLABEL, NOLABEL, n->op);
  ...
  switch (n->op) {
    ...
    case A_CAST:
      return (leftreg);         // Not much to do
    ...
  }
```

## 全局赋值中的转换

当变量是局部变量时，上述方法没问题，因为编译器将上述赋值作为表达式来处理。
对于全局变量，我们必须手动解析转换并将其应用于后续的字面量值。

例如，在 `decl.c` 的 `scalar_declaration` 中，我们需要这段代码：

```c
    // Globals must be assigned a literal value
    if (class == C_GLOBAL) {
      // If there is a cast
      if (Token.token == T_LPAREN) {
        // Get the type in the cast
        scan(&Token);
        casttype= parse_cast();
        rparen();

        // Check that the two types are compatible. Change
        // the new type so that the literal parse below works.
        // A 'void *' casstype can be assigned to any pointer type.
        if (casttype == type || (casttype== pointer_to(P_VOID) && ptrtype(type)))
          type= P_NONE;
        else
          fatal("Type mismatch");
      }

      // Create one initial value for the variable and
      // parse this value
      sym->initlist= (int *)malloc(sizeof(int));
      sym->initlist[0]= parse_literal(type);
      scan(&Token);
    }
```

首先，注意当有转换时我们设置 `type= P_NONE`，并且当有转换时用 P_NONE 调用 `parse_literal()`。为什么？
因为这个函数以前要求被解析的字面量正是作为参数的类型，即字符串字面量必须是 `char *` 类型，`char` 必须匹配范围 0 ... 255 内的字面量等等。

现在我们有了转换，应该能够接受：

```c
  char a= (char)65536;
```

所以 `decl.c` 中 `parse_literal()` 的代码现在这样做：

```c
int parse_literal(int type) {

  // We have a string literal. Store in memory and return the label
  if (Token.token == T_STRLIT) {
    if (type == pointer_to(P_CHAR) || type == P_NONE)
    return(genglobstr(Text));
  }

  // We have an integer literal. Do some range checking.
  if (Token.token == T_INTLIT) {
    switch(type) {
      case P_CHAR: if (Token.intvalue < 0 || Token.intvalue > 255)
                     fatal("Integer literal value too big for char type");
      case P_NONE:
      case P_INT:
      case P_LONG: break;
      default: fatal("Type mismatch: integer literal vs. variable");
    }
  } else
    fatal("Expecting an integer literal value");
  return(Token.intvalue);
}
```

P_NONE 用于放宽类型限制。

## 处理 `void *`

`void *` 指针是一个可以替代任何其他指针类型使用的指针。所以我们必须实现这一点。

我们已经在上面的全局变量赋值中这样做了：

```c
   if (casttype == type || (casttype== pointer_to(P_VOID) && ptrtype(type)))
```

即如果类型相等，或者如果一个 `void *` 指针被赋值给一个指针。这允许以下全局赋值：

```c
  char *str= (void *)0;
```

即使 `str` 的类型是 `char *` 而不是 `void *`。

现在我们需要处理表达式中的 `void *`（以及其他指针/指针操作）。为此，我必须修改 `types.c` 中的 `modify_type()`。作为回顾，这里是这个函数的用途：

```c
// Given an AST tree and a type which we want it to become,
// possibly modify the tree by widening or scaling so that
// it is compatible with this type. Return the original tree
// if no changes occurred, a modified tree, or NULL if the
// tree is not compatible with the given type.
// If this will be part of a binary operation, the AST op is not zero.
struct ASTnode *modify_type(struct ASTnode *tree, int rtype, int op);
```

这是加宽值的代码，例如 `int x= 'Q';` 将 `x` 变成 32 位值。我们也用它来进行缩放：当我们做：

```c
  int x[4];
  int y= x[2];
```

"2" 索引按 `int` 的大小缩放为相对于 `x[]` 数组基址的八个字节偏移。

所以，当我们在函数内部写：

```c
  char *str= (void *)0;
```

我们得到 AST 树：

```
          A_ASSIGN
           /    \
       A_CAST  A_IDENT
         /      str
     A_INTLIT
         0
```

左边 `tree` 的类型将是 `void *`，而 `rtype` 将是 `char *`。我们最好确保该操作可以执行。

我修改了 `modify_type()` 来处理指针：

```c
  // For pointers
  if (ptrtype(ltype) && ptrtype(rtype)) {
    // We can compare them
    if (op >= A_EQ && op <= A_GE)
      return(tree);

    // A comparison of the same type for a non-binary operation is OK,
    // or when the left tree is of  `void *` type.
    if (op == 0 && (ltype == rtype || ltype == pointer_to(P_VOID)))
      return (tree);
  }
```

现在，指针比较是可以的，但其他二元运算（如加法）是不行的。
"非二元操作"指的是类似赋值的东西。我们当然可以在两个相同类型之间进行赋值。现在，我们也可以从 `void *` 指针赋值给任何指针。

## 添加 NULL

既然我们能够处理 `void *` 指针，我们可以将 NULL 添加到我们的包含文件中。我已经将它添加到 `stdio.h` 和 `stddef.h` 中：

```c
#ifndef NULL
# define NULL (void *)0
#endif
```

但还有一个最后的问题。当我尝试这个全局声明时：

```c
#include <stdio.h>
char *str= NULL;
```

我得到了：

```
str:
        .quad   L0
```

因为 `char *` 指针的每个初始化值都被当作标签编号处理。所以 NULL 中的 "0" 被转换成了 "L0" 标签。我们需要修复这个问题。现在，在 `cg.c` 的 `cgglobsym()` 中：

```c
      case 8:
        // Generate the pointer to a string literal. Treat a zero value
        // as actually zero, not the label L0
        if (node->initlist != NULL && type== pointer_to(P_CHAR) && initvalue != 0)
          fprintf(Outfile, "\t.quad\tL%d\n", initvalue);
        else
          fprintf(Outfile, "\t.quad\t%d\n", initvalue);
```

是的这很丑但它能工作！

## 测试更改

我不会详细讲解所有测试，但文件 `tests/input101.c` 到 `tests/input108.c` 测试了上述功能以及编译器的错误检查。

## 结论与下一步

我以为转换会很容易，的确也很容易。但我没有考虑到的是围绕 `void *` 的问题。我觉得我已经覆盖了这里的大部分情况，但不是全部，所以期待看到一些我还没有发现的 `void *` 边缘情况。

在我们编译器编写旅程的下一部分，我们将添加一些缺失的运算符。[下一步](../43_More_Operators/Readme_zh.md)