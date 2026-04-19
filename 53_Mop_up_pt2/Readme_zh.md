# Part 53: 清理工作，第二部分

在编译器编写之旅的这一部分，我修复了几个在编译器源代码中经常令人烦恼的问题。

## 连续字符串字面量

C语言允许通过跨多行或多个字符串来声明字符串字面量，例如：

```c
  char *c= "hello " "there, "
           "how " "are " "you?";
```

现在，我们可以选择在词法扫描器中解决这个问题。然而，我花了很多时间尝试这样做。问题是，由于处理C预处理器，代码现在变得很复杂，我找不到一种简洁的方法来处理连续字符串字面量。

我的解决方案是在解析器中完成这项工作，并借助代码生成器的一点帮助。在`expr.c`的`primary()`中，处理字符串字面量的代码现在如下所示：

```c
  case T_STRLIT:
    // For a STRLIT token, generate the assembly for it.
    id = genglobstr(Text, 0);   // 0 means generate a label

    // For successive STRLIT tokens, append their contents
    // to this one
    while (1) {
      scan(&Peektoken);
      if (Peektoken.token != T_STRLIT) break;
      genglobstr(Text, 1);      // 1 means don't generate a label
      scan(&Token);             // Skip it
    }

    // Now make a leaf AST node for it. id is the string's label.
    genglobstrend();
    n = mkastleaf(A_STRLIT, pointer_to(P_CHAR), NULL, NULL, id);
    break;
```

`genglobstr()`现在接受第二个参数，告诉它是字符串的第一部分还是后续部分。另外，`genglobstrend()`现在负责NUL终止字符串字面量。

## 空语句

C语言允许空语句和空复合语句，例如：

```c
  while ((c=getc()) != 'x') ;           // ';' is an empty statement

  int fred() { }                        // Function with empty body
```

我在编译器中使用了这两者，所以我们需要支持它们。在`stmt.c`中，代码现在这样做：

```c
static struct ASTnode *single_statement(void) {
  struct ASTnode *stmt;
  struct symtable *ctype;

  switch (Token.token) {
    case T_SEMI:
      // An empty statement
      semi();
      break;
    ...
  }
  ...
}

struct ASTnode *compound_statement(int inswitch) {
  struct ASTnode *left = NULL;
  struct ASTnode *tree;

  while (1) {
    // Leave if we've hit the end token. We do this first to allow
    // an empty compound statement
    if (Token.token == T_RBRACE)
      return (left);
    ...
  }
  ...
}
```

这样就解决了两个缺点。

## 重新声明的符号

C语言允许全局变量后来声明为extern，也允许extern变量后来声明为全局变量，反之亦然。但是，两个声明的类型必须匹配。我们还希望确保符号表中只有一个版本的符号：我们不希望同时有C_GLOBAL和C_EXTERN条目！

在`stmt.c`中，我添加了一个名为`is_new_symbol()`的新函数。我们在解析变量名之后以及尝试在符号表中查找它之后调用它。因此，`sym`可能是NULL（没有现有变量）或不是NULL（是现有变量）。

如果符号存在，确保它是安全的重新声明实际上相当复杂。

```c
// Given a pointer to a symbol that may already exist
// return true if this symbol doesn't exist. We use
// this function to convert externs into globals
int is_new_symbol(struct symtable *sym, int class,
                  int type, struct symtable *ctype) {

  // There is no existing symbol, thus is new
  if (sym==NULL) return(1);

  // global versus extern: if they match that it's not new
  // and we can convert the class to global
  if ((sym->class== C_GLOBAL && class== C_EXTERN)
      || (sym->class== C_EXTERN && class== C_GLOBAL)) {

      // If the types don't match, there's a problem
      if (type != sym->type)
        fatals("Type mismatch between global/extern", sym->name);

      // Struct/unions, also compare the ctype
      if (type >= P_STRUCT && ctype != sym->ctype)
        fatals("Type mismatch between global/extern", sym->name);

      // If we get to here, the types match, so mark the symbol
      // as global
      sym->class= C_GLOBAL;
      // Return that symbol is not new
      return(0);
  }

  // It must be a duplicate symbol if we get here
  fatals("Duplicate global variable declaration", sym->name);
  return(-1);   // Keep -Wall happy
}
```

代码直接但不优雅。还要注意，任何重新声明的extern符号都会转换为全局符号。这意味着我们不必从符号表中删除该符号并添加一个新的全局符号。

## 逻辑运算的操作数类型

我遇到的下一个错误是这样的：

```c
  int *x;
  int y;

  if (x && y > 12) ...
```

编译器在`binexpr()`中计算`&&`操作。为此，它确保二元运算符两边的类型兼容。好吧，如果上面的操作符是`+`，那么类型肯定是不兼容的。但对于逻辑比较，我们可以将它们*AND*在一起。

我在`types.c`的`modify_type()`顶部添加了一些代码来修复这个问题。如果我们要进行`&&`或`||`操作，那么操作两边需要整数或指针类型。

```c
struct ASTnode *modify_type(struct ASTnode *tree, int rtype,
                            struct symtable *rctype, int op) {
  int ltype;
  int lsize, rsize;

  ltype = tree->type;

  // For A_LOGOR and A_LOGAND, both types have to be int or pointer types
  if (op==A_LOGOR || op==A_LOGAND) {
    if (!inttype(ltype) && !ptrtype(ltype))
      return(NULL);
    if (!inttype(ltype) && !ptrtype(rtype))
      return(NULL);
    return (tree);
  }
  ...
}
```

我还意识到我的`&&`和`||`实现不正确，所以我必须修复它。现在不行，但很快。

## 无值的返回

另一个缺失的C特性是从void函数返回的能力，即只是离开而不返回任何值。然而，当前的解析器期望在`return`关键字后面看到括号和表达式。

所以，在`stmt.c`的`return_statement()`中，我们现在有：

```c
// Parse a return statement and return its AST
static struct ASTnode *return_statement(void) {
  struct ASTnode *tree= NULL;

  // Ensure we have 'return'
  match(T_RETURN, "return");

  // See if we have a return value
  if (Token.token == T_LPAREN) {
    // Code to parse the parentheses and the expression
    ...
  } else {
    if (Functionid->type != P_VOID)
      fatal("Must return a value from a non-void function");
  }


    // Add on the A_RETURN node
  tree = mkastunary(A_RETURN, P_NONE, NULL, tree, NULL, 0);

  // Get the ';'
  semi();
  return (tree);
}
```

如果`return`令牌后面不是左括号，我们将表达式`tree`设置为NULL。我们还要检查这是否是void返回函数，如果不是，则打印致命错误。

现在我们已经解析了`return`函数，我们可能创建一个带有NULL子节点的A_RETURN AST节点。所以现在我们必须在代码生成器中处理这个问题。`cg.c`中`cgreturn()`的顶部现在有：

```c
// Generate code to return a value from a function
void cgreturn(int reg, struct symtable *sym) {

  // Only return a value if we have a value to return
  if (reg != NOREG) {
    ..
  }

  cgjump(sym->st_endlabel);
}
```

如果没有子AST树，则没有包含表达式值的寄存器。因此，我们只输出跳转到函数结束标签。


## 结论和下一步

我们修复了编译器中的五个小问题：我们需要努力让编译器能够编译自身。

我确实发现了`&&`和`||`的问题。然而，在我解决这个问题之前，我需要解决一个重要而紧迫的问题：我们只有有限的CPU寄存器集，而且对于大型源文件，我们正在用完它们。

在编译器编写之旅的下一部分，我将不得不致力于实现寄存器溢出。我一直推迟这个问题，但现在编译器（编译自身时）的大多数致命错误都是寄存器问题。所以现在是时候解决这个问题了。[下一步](../54_Reg_Spills/Readme_zh.md)