# 第 19 章：数组，第一部分

> *我大学一年级的讲师是个苏格兰人，口音很重。大约在第一学期的第三或第四周，
  他开始在课堂上很多地说"Hurray!"。我花了大约二十分钟才搞清楚他实际上是在说"array"。*

因此，在这段旅程的这一部分，我们开始向编译器添加数组的工作。我坐下来写了一个小的C程序，
来看看应该尝试实现什么样的功能：

```c
  int ary[5];               // Array of five int elements
  int *ptr;                 // Pointer to an int

  ary[3]= 63;               // Set ary[3] (lvalue) to 63
  ptr   = ary;              // Point ptr to base of ary
  // ary= ptr;              // error: assignment to expression with array type
  ptr   = &ary[0];          // Also point ptr to base of ary, ary[0] is lvalue
  ptr[4]= 72;               // Use ptr like an array, ptr[4] is an lvalue
```

数组和指针类似，我们都可以用"[ ]"语法来解引用，以访问特定元素。
我们可以用数组名作为"指针"，并将数组的基地址保存到指针中。
我们可以获取数组中某个元素的地址。
但有一件事我们做不到：用指针"覆盖"数组的基地址：
数组的元素是可变的，但数组的基地址是不可变的。

在这段旅程中，我将添加：

 + 数组声明，具有固定大小但没有初始化列表
 + 数组下标作为表达式中的右值
 + 数组下标作为赋值中的左值

我也不打算在每个数组中实现多维。

## 表达式中的括号

有时候我想试试这个： `*(ptr + 2)`，它应该最终等同于 `ptr[2]`。
但我们还没有允许在表达式中使用括号，所以现在是时候添加它们了。

### C 语法的 BNF 表示

在网上有一个页面，包含 Jeff Lee 于 1985 年编写的
[BNF Grammar for C](https://www.lysator.liu.se/c/ANSI-C-grammar-y.html)。
我喜欢参考它来给我灵感，并确认我没有犯太多错误。

需要注意的是，C 中二元表达式的优先级不是通过实现来处理的，
而是通过递归定义来明确表示优先级的。因此：

```
additive_expression
        : multiplicative_expression
        | additive_expression '+' multiplicative_expression
        | additive_expression '-' multiplicative_expression
        ;
```

这表明当我们解析"additive_expression"时，会深入到"multiplicative_expression"中，
从而使 '*' 和 '/' 运算符比 '+' 和 '-' 运算符具有更高的优先级。

在表达式优先级层次结构的顶部是：

```
primary_expression
        : IDENTIFIER
        | CONSTANT
        | STRING_LITERAL
        | '(' expression ')'
        ;
```

我们已经有了 `primary()` 函数，用于查找 T_INTLIT 和 T_IDENT 标记，
这符合 Jeff Lee 的 C 语法。因此，这是添加表达式中括号解析的完美位置。

我们已经有了 T_LPAREN 和 T_RPAREN 作为语言中的标记，所以词法扫描器不需要做任何工作。

相反，我们只需要修改 `primary()` 来做额外的解析：

```c
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  ...

  switch (Token.token) {
  case T_INTLIT:
  ...
  case T_IDENT:
  ...
  case T_LPAREN:
    // Beginning of a parenthesised expression, skip the '('.
    // Scan in the expression and the right parenthesis
    scan(&Token);
    n = binexpr(0);
    rparen();
    return (n);

    default:
    fatald("Expecting a primary expression, got token", Token.token);
  }

  // Scan in the next token and return the leaf node
  scan(&Token);
  return (n);
}
```

就这样！只需添加几行代码就可以在表达式中支持括号了。
你会注意到我在新代码中明确调用了 `rparen()` 而不是跳出 switch 语句。
如果代码离开了 switch 语句，在最终返回之前的 `scan(&Token);`
不会严格地要求 ')' 标记与 '(' 标记相匹配。

`test/input19.c` 测试检查括号是否正常工作：

```c
  a= 2; b= 4; c= 3; d= 2;
  e= (a+b) * (c+d);
  printint(e);
```

它应该输出 30，即 `6 * 5`。

## 符号表的变化

我们的符号表中有标量变量（只有一个值）和函数。是时候添加数组了。
以后，我们还需要使用 `sizeof()` 运算符获取每个数组的元素数量。
以下是 `defs.h` 中的变化：

```c
// Structural types
enum {
  S_VARIABLE, S_FUNCTION, S_ARRAY
};

// Symbol table structure
struct symtable {
  char *name;                   // Name of a symbol
  int type;                     // Primitive type for the symbol
  int stype;                    // Structural type for the symbol
  int endlabel;                 // For S_FUNCTIONs, the end label
  int size;                     // Number of elements in the symbol
};
```

目前，我们将数组视为指针，因此数组的类型是"指向...的指针"，
例如，如果数组中的元素是 `int`，则是"指向 int 的指针"。
我们还需要在 `sym.c` 的 `addglob()` 中添加一个参数：

```c
int addglob(char *name, int type, int stype, int endlabel, int size) {
  ...
}
```

## 解析数组声明

目前，我只允许声明具有大小的数组。变量声明的 BNF 语法现在是：

```
 variable_declaration: type identifier ';'
        | type identifier '[' P_INTLIT ']' ';'
        ;
```

因此，我们需要查看 `decl.c` 中 `var_declaration()` 的下一个标记，
并处理标量变量声明或数组声明：

```c
// Parse the declaration of a scalar variable or an array
// with a given size.
// The identifier has been scanned & we have the type
void var_declaration(int type) {
  int id;

  // Text now has the identifier's name.
  // If the next token is a '['
  if (Token.token == T_LBRACKET) {
    // Skip past the '['
    scan(&Token);

    // Check we have an array size
    if (Token.token == T_INTLIT) {
      // Add this as a known array and generate its space in assembly.
      // We treat the array as a pointer to its elements' type
      id = addglob(Text, pointer_to(type), S_ARRAY, 0, Token.intvalue);
      genglobsym(id);
    }

    // Ensure we have a following ']'
    scan(&Token);
    match(T_RBRACKET, "]");
  } else {
    ...      // Previous code
  }

    // Get the trailing semicolon
  semi();
}
```

我认为这段代码相当直接。稍后，我们将向数组声明中添加初始化列表。

## 生成数组存储

既然我们知道了数组的大小，就可以修改 `cgglobsym()` 在汇编器中分配这个空间：

```c
void cgglobsym(int id) {
  int typesize;
  // Get the size of the type
  typesize = cgprimsize(Gsym[id].type);

  // Generate the global identity and the label
  fprintf(Outfile, "\t.data\n" "\t.globl\t%s\n", Gsym[id].name);
  fprintf(Outfile, "%s:", Gsym[id].name);

  // Generate the space
  for (int i=0; i < Gsym[id].size; i++) {
    switch(typesize) {
      case 1: fprintf(Outfile, "\t.byte\t0\n"); break;
      case 4: fprintf(Outfile, "\t.long\t0\n"); break;
      case 8: fprintf(Outfile, "\t.quad\t0\n"); break;
      default: fatald("Unknown typesize in cgglobsym: ", typesize);
    }
  }
}
```

有了这个，我们现在可以声明这样的数组：

```c
  char a[10];
  int  b[25];
  long c[100];
```

## 解析数组下标

在这一部分，我不想太冒险。我只想让基本的数组下标作为右值和左值工作。
`test/input20.c` 程序具有我想实现的功能：

```c
int a;
int b[25];

int main() {
  b[3]= 12; a= b[3];
  printint(a); return(0);
}
```

回到 C 的 BNF 语法，我们可以看到数组下标的优先级略低于括号：

```
primary_expression
        : IDENTIFIER
        | CONSTANT
        | STRING_LITERAL
        | '(' expression ')'
        ;

postfix_expression
        : primary_expression
        | postfix_expression '[' expression ']'
          ...
```

但目前，我也将在 `primary()` 函数中解析数组下标。
做语义分析的代码最终变得足够大，需要一个新函数：


```c
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;


  switch (Token.token) {
  case T_IDENT:
    // This could be a variable, array index or a
    // function call. Scan in the next token to find out
    scan(&Token);

    // It's a '(', so a function call
    if (Token.token == T_LPAREN) return (funccall());

    // It's a '[', so an array reference
    if (Token.token == T_LBRACKET) return (array_access());
```

这里是 `array_access()` 函数：

```c
// Parse the index into an array and
// return an AST tree for it
static struct ASTnode *array_access(void) {
  struct ASTnode *left, *right;
  int id;

  // Check that the identifier has been defined as an array
  // then make a leaf node for it that points at the base
  if ((id = findglob(Text)) == -1 || Gsym[id].stype != S_ARRAY) {
    fatals("Undeclared array", Text);
  }
  left = mkastleaf(A_ADDR, Gsym[id].type, id);

  // Get the '['
  scan(&Token);

  // Parse the following expression
  right = binexpr(0);

  // Get the ']'
  match(T_RBRACKET, "]");

  // Ensure that this is of int type
  if (!inttype(right->type))
    fatal("Array index is not of integer type");

  // Scale the index by the size of the element's type
  right = modify_type(right, left->type, A_ADD);

  // Return an AST tree where the array's base has the offset
  // added to it, and dereference the element. Still an lvalue
  // at this point.
  left = mkastnode(A_ADD, Gsym[id].type, left, NULL, right, 0);
  left = mkastunary(A_DEREF, value_at(left->type), left, 0);
  return (left);
}
```

对于数组 `int x[20];` 和数组下标 `x[6]`，我们需要将下标（6）乘以 `int` 的大小（4），
然后将其加到数组基址上。然后必须对这个元素进行解引用。
我们将其标记为左值，因为我们可能尝试做：

```c
  x[6] = 100;
```

如果它确实变成了右值，`binexpr()` 将在 A_DEREF AST 节点中设置 `rvalue` 标志。

### 生成的 AST 树

回到我们的测试程序 `tests/input20.c`，将产生带有数组下标的 AST 树的代码是：

```c
  b[3]= 12; a= b[3];
```

运行 `comp1 -T tests/input20.c`，我们得到：

```
    A_INTLIT 12
  A_WIDEN
      A_ADDR b
        A_INTLIT 3    # 3 is scaled by 4
      A_SCALE 4
    A_ADD             # and then added to b's address
  A_DEREF             # and derefenced. Note, stll an lvalue
A_ASSIGN

      A_ADDR b
        A_INTLIT 3    # As above
      A_SCALE 4
    A_ADD
  A_DEREF rval        # but the dereferenced address will be an rvalue
  A_IDENT a
A_ASSIGN
```

### 其他小的解析变化

`expr.c` 中解析器有一些小的变化，我花了一些时间来调试。
我需要对运算符优先级查找函数的输入更加严格：

```c
// Check that we have a binary operator and
// return its precedence.
static int op_precedence(int tokentype) {
  int prec;
  if (tokentype >= T_VOID)
    fatald("Token with no precedence in op_precedence:", tokentype);
  ...
}
```

在我正确解析之前，我发送了一个不在优先级表中的标记，
而 `op_precedence()` 正在读取表末尾之外的内容。哎呀！C 语言真是让人又爱又恨！

另一个变化是，既然我们可以使用表达式作为数组下标（例如 `x[ a+2 ]`），
我们必须期望 ']' 标记可以结束一个表达式。所以在 `binexpr()` 的末尾：

```c
    // Update the details of the current token.
    // If we hit a semicolon, ')' or ']', return just the left node
    tokentype = Token.token;
    if (tokentype == T_SEMI || tokentype == T_RPAREN
        || tokentype == T_RBRACKET) {
      left->rvalue = 1;
      return (left);
    }
  }
```

## 代码生成器的变化

没有任何变化。我们已经在编译器中拥有了所有必要的组件：
缩放整数值、获取变量的地址等。对于我们的测试代码：

```c
  b[3]= 12; a= b[3];
```

我们生成 x86-64 汇编代码：

```
        movq    $12, %r8
        leaq    b(%rip), %r9    # Get b's address
        movq    $3, %r10
        salq    $2, %r10        # Shift 3 by 2, i.e. 3 * 4
        addq    %r9, %r10       # Add to b's address
        movq    %r8, (%r10)     # Save 12 into b[3]

        leaq    b(%rip), %r8    # Get b's address
        movq    $3, %r9
        salq    $2, %r9         # Shift 3 by 2, i.e. 3 * 4
        addq    %r8, %r9        # Add to b's address
        movq    (%r9), %r9      # Load b[3] into %r9
        movl    %r9d, a(%rip)   # and store in a
```

## 结论与下一步

添加基本数组声明和数组表达式（就处理语法而言）的解析变化相当容易。
我发现困难的是正确设置 AST 树节点来缩放、加到基地址，并设置为左值/右值。
一旦这一步正确，现有的代码生成器就会产生正确的汇编输出。

在编写编译器的下一段旅程中，我们将向语言添加字符和字符串字面量，并找到一种方法来输出它们。
[下一步](../20_Char_Str_Literals/Readme_zh.md)