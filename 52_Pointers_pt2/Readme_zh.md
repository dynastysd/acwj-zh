# 第52部分：指针，第2部分

在我编写编译器的旅程中，我从一个需要修复的指针问题开始，最终重构了大约一半的`expr.c`，并更改了编译器中大约四分之一函数的API。因此，从触及的代码行数来看，这是很大的一步，但从修复或改进的角度来看，这并不是很大的一步。

## 问题

我们从导致这一切的问题开始。当我用编译器自己的源代码进行测试时，我意识到无法解析指针链，例如像下面这样的表达式：

```c
  ptr->next->next->next
```

原因是`primary()`被调用并获取表达式开头标识符的值。如果它看到后面的后缀运算符，就会调用`postfix()`来处理它。`postfix()`处理一个`->`运算符然后返回。就这样。没有循环来跟踪`->`运算符链。

更糟糕的是，`primary()`查找单个标识符。这意味着它也无法解析以下内容：

```c
  ptrarray[4]->next     或者
  unionvar.member->next
```

因为这些都不是`->`运算符之前的单个标识符。

## 这一切是如何发生的？

这是因为我们开发的快速原型性质。我每次只添加一小步功能，通常不会太超前考虑未来的需求。所以，时不时地，我们必须撤销已写的内容，使其更加通用和灵活。

## 如何修复？

如果我们查看[C的BNF语法](https://www.lysator.liu.se/c/ANSI-C-grammar-y.html)，会看到：

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
        | postfix_expression '(' ')'
        | postfix_expression '(' argument_expression_list ')'
        | postfix_expression '.' IDENTIFIER
        | postfix_expression '->' IDENTIFIER
        | postfix_expression '++'
        | postfix_expression '--'
        ;
```

换句话说，我们的做法是反的。`postfix`应该调用`primary()`来获取表示标识符的AST节点。然后，我们可以循环查找任何后缀标记，解析它们并将新的AST父节点添加到从`primary()`返回的标识符节点上。

一切听起来都很简单，除了一件事。当前的`primary()`不构建AST节点；它只解析标识符并将其留在`Text`中。构建标识符及其任何后缀操作的AST节点或AST树是`postfix()`的工作。

同时，`defs.h`中的AST节点结构只知道原始类型：

```c
// Abstract Syntax Tree structure
struct ASTnode {
  int op;           // "Operation" to be performed on this tree
  int type;         // Type of any expression this tree generates
  int rvalue;       // NOTE: no ctype
  ...
};
```

原因是我们最近才添加了结构体和联合体。这意味着，加上`postfix()`做了大部分解析工作，我们不需要为struct或union标识符存储指向struct或union符号的指针。

因此，要修复，我们需要：

  1. 在`struct ASTnode`中添加一个`ctype`指针，以便完整类型存储在每个AST节点中。
  2. 找到并修复所有构建AST节点的函数，以及所有调用这些函数的地方，以便存储节点的`ctype`。
  3. 将`primary()`移到`expr.c`顶部附近，并让它构建一个AST节点。
  4. 让`postfix()`调用`primary()`来获取标识符的朴素AST节点（A_IDENT）。
  5. 让`postfix()`循环处理后缀运算符。

这有很多工作，而且由于AST节点调用遍布各处，编译器的每个源文件都需要被修改。唉。

## AST节点函数的更改

我不会用所有细节来烦你，但我们可以从`defs.h`中AST节点结构的更改开始，以及`tree.c`中构建AST节点的主函数：

```c
// Abstract Syntax Tree structure
struct ASTnode {
  int op;                       // "Operation" to be performed on this tree
  int type;                     // Type of any expression this tree generates
  struct symtable *ctype;       // If struct/union, ptr to that type
  ...
};

// Build and return a generic AST node
struct ASTnode *mkastnode(int op, int type,
                          struct symtable *ctype, ...) {
  ...
  // Copy in the field values and return it
  n->op = op;
  n->type = type;
  n->ctype = ctype;
  ...
}
```

`mkastleaf()`和`mkastunary()`也有更改：它们现在接收一个`ctype`并用这个参数调用`mkastnode()`。

在编译器中大约有40个调用这三个函数的地方，所以我不会逐一介绍。对于大多数调用，都有原始的`type`和`ctype`指针可用。有些调用将AST节点类型设置为P_INT，因此`ctype`为NULL。有些调用将AST节点类型设置为P_NONE，同样`ctype`为NULL。

## `modify_type()`的更改

`modify_type()`用于确定AST节点的类型是否与另一种类型兼容，并在必要时加宽节点以匹配另一种类型。它调用`mkastunary()`，因此我们也需要为其提供一个`ctype`参数。我已经完成了这个操作，因此，调用`modify_type()`的六个地方也不得不修改，以传入我们要将AST节点比较的类型的`ctype`。

## `expr.c`的更改

现在我们进入更改的核心部分，即`primary()`和`postfix()`的重构。我已经在上面概述了我们必须做的事情。与我们所做的很多事情一样，沿途有一些褶皱需要熨平。

## `postfix()`的更改

`postfix()`现在实际上看起来更清晰了：

```c
// Parse a postfix expression and return
// an AST node representing it. The
// identifier is already in Text.
static struct ASTnode *postfix(void) {
  struct ASTnode *n;

  // Get the primary expression
  n = primary();

  // Loop until there are no more postfix operators
  while (1) {
    switch (Token.token) {
    ...
    default:
      return (n);
    }
  }
```

我们现在调用`primary()`来获取标识符或常量。然后循环将对从`primary()`获得的AST节点应用任何后缀运算符。我们调用辅助函数如`array_access()`和`member_access()`来处理`[..]`、`.`和`->`运算符。

我们在这里做后增和后减。既然有了一个循环，我们必须检查不要尝试多次执行这些操作。我们还检查从`primary()`收到的AST是左值而不是右值，因为我们需要一个内存地址来递增或递减。

## 一个新函数，`paren_expression()`

我意识到新的`primary()`函数变得有点太大了，所以我把其中一些代码分离到一个新函数`paren_expression()`中。这解析包含在`(..)`中的表达式：强制转换和普通带括号的表达式。代码几乎与旧代码相同，所以我不在这里详细介绍。它返回一个AST节点，树表示强制转换表达式或带括号的表达式。

## `primary()`的更改

这是发生最大变化的地方。首先，这里是它查找的标记：

 + 'static'、'extern'，它会报错，因为我们只能在局部上下文中解析表达式。
 + 'sizeof()'
 + 整数和字符串字面量
 + 标识符：这些可能是已知类型（如'int'）、枚举名称、typedef名称、函数名称、数组名称和/或标量变量名称。这一部分是`primary()`最大的部分，回想起来，也许我应该把它做成自己的函数。
 + `(..)`，这是调用`paren_expression()`的地方。

看代码，`primary()`现在为上述每个构建AST节点返回给`postfix()`。这以前是在`postfix`中完成的，但现在我在`primary()`中做了。

## `member_access()`的更改

在之前的`member_access()`中，全局变量`Text`仍然保存着标识符，而`member_access()`构建AST节点来表示struct/union标识符。

在当前的`member_access()`中，我们接收struct/union标识符的AST节点，而这可能是一个数组元素或另一个struct/union的成员。

所以代码的不同之处在于我们不再为原始标识符构建叶子AST节点。我们仍然构建AST节点来添加相对于基址的偏移量并取消引用指向成员的指针。

另一个区别是这段代码：

```c
  // Check that the left AST tree is a struct or union.
  // If so, change it from an A_IDENT to an A_ADDR so that
  // we get the base address, not the value at this address.
  if (!withpointer) {
    if (left->type == P_STRUCT || left->type == P_UNION)
      left->op = A_ADDR;
    else
      fatal("Expression is not a struct/union");
  }
```

考虑表达式`foo.bar`。`foo`是结构体的名称，例如，`bar`是该结构体的成员。

在`primary()`中，我们会为`foo`创建一个A_IDENT AST节点，因为我们无法判断这是标量变量（如`int foo`）还是结构体（如`struct fred foo`）。现在我们知道它是结构体或联合体，我们需要结构体的基址，而不是基址处的值。所以，代码将A_IDENT AST节点操作转换为标识符上的A_ADDR操作。

## 测试代码

我认为我花了大约两个小时运行我们一百多个回归测试，找到我遗漏的东西并修复它们。再次通过所有测试确实感觉很好。

`tests/input128.c`现在检查我们可以跟随指针链，这正是本次练习的全部目的：

```c
struct foo {
  int val;
  struct foo *next;
};

struct foo head, mid, tail;

int main() {
  struct foo *ptr;
  tail.val= 20; tail.next= NULL;
  mid.val= 15; mid.next= &tail;
  head.val= 10; head.next= &mid;

  ptr= &head;
  printf("%d %d\n", head.val, ptr->val);
  printf("%d %d\n", mid.val, ptr->next->val);
  printf("%d %d\n", tail.val, ptr->next->next->val);
  return(0);
}
```

而`tests/input129.c`检查我们不能连续两次后递增。

## 另一个更改：`Linestart`

作为我们努力让编译器自我编译的一部分，我做的另一个更改。

扫描器正在寻找'#'标记。当它看到这个标记时，它假定我们遇到了C预处理行并解析这一行。不幸的是，我没有把扫描器限制在每一行的第一列。所以，当我们的编译器遇到这行源代码时：

```c
  while (c == '#') {
```

它会对')' '{'不是C预处理行感到不安。

我们现在有一个`Linestart`变量，标志扫描器是否在新行的第一列。修改的主要函数是`scan.c`中的`next()`。我认为更改有点丑陋，但它们有效；我应该找个时间来清理一下。无论如何，我们只有在第一列看到'#'时期望C预处理行。

## 结论与下一步

在我们编译器编写旅程的下一部分，我将回到用编译器源代码喂给编译器本身，看看出现什么错误，然后选择一个或多个来修复。[下一步](../53_Mop_up_pt2/Readme_zh.md)