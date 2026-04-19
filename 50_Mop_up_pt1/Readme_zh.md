# 第50部分: 清理工作, 第1部分

我们无疑已经进入了"清理"阶段,因为在这一部分
的编译器编写旅程中,我没有引入任何主要特性。
相反,我修复了一些问题并添加了一些小函数。

## 连续的Case

目前,编译器无法解析

```c
  switch(x) {
    case 1:
    case 2: printf("Hello\n");
  }
```

因为解析器期望在':'标记后有一个复合语句。
在`stmt.c`的`switch_statement()`中:

```c
        // Scan the ':' and get the compound expression
        match(T_COLON, ":");
        left= compound_statement(1); casecount++;
        ...
        // Build a sub-tree with the compound statement as the left child
        casetail->right= mkastunary(ASTop, 0, left, NULL, casevalue);
```

我们想要的是允许一个空的复合语句,以便任何缺少
复合语句的case都会落入下一个已存在的复合语句。

`switch_statement()`中的更改是:

```c
        // Scan the ':' and increment the casecount
        match(T_COLON, ":");
        casecount++;

        // If the next token is a T_CASE, the existing case will fall
        // into the next case. Otherwise, parse the case body.
        if (Token.token == T_CASE) 
          body= NULL;
        else
          body= compound_statement(1);
```

然而,这只是故事的一半。现在在代码生成部分,
我们必须处理NULL复合语句并做出相应的操作。
在`gen.c`的`genSWITCH()`中:

```c
  // Walk the right-child linked list to
  // generate the code for each case
  for (i = 0, c = n->right; c != NULL; i++, c = c->right) {
    ...
    // Generate the case code. Pass in the end label for the breaks.
    // If case has no body, we will fall into the following body.
    if (c->left) genAST(c->left, NOLABEL, NOLABEL, Lend, 0);
    genfreeregs(NOREG);
  }
```

所以,这是一个简单而漂亮的修复。`tests/input123.c`是测试程序,
用于确认此更改是否有效。

## 转储符号表

当我试图找出为什么全局`Text`变量对编译器不可见时,
我在`sym.c`中添加了代码,在每个源文件末尾转储符号表。
有一个`-M`命令行参数来启用此功能。我不会详细讲解代码,
但这里是其输出的一个示例:

```
Symbols for misc.c
Global
--------
void exit(): global, 1 params
    int status: param, size 4
void _Exit(): global, 1 params
    int status: param, size 4
void *malloc(): global, 1 params
    int size: param, size 4
...
int Line: extern, size 4
int Putback: extern, size 4
struct symtable *Functionid: extern, size 8
char **Infile: extern, size 8
char **Outfile: extern, size 8
char *Text[]: extern, 513 elems, size 513
struct symtable *Globhead: extern, size 8
struct symtable *Globtail: extern, size 8
...
struct mkastleaf *mkastleaf(): global, 4 params
    int op: param, size 4
    int type: param, size 4
    struct symtable *sym: param, size 8
    int intvalue: param, size 4
...
Enums
--------
int (null): enumtype, size 0
int TEXTLEN: enumval, value 512
int (null): enumtype, size 0
int T_EOF: enumval, value 0
int T_ASSIGN: enumval, value 1
int T_ASPLUS: enumval, value 2
int T_ASMINUS: enumval, value 3
int T_ASSTAR: enumval, value 4
int T_ASSLASH: enumval, value 5
...
Typedefs
--------
long size_t: typedef, size 0
char *FILE: typedef, size 0
```

## 作为参数传递数组

我做了以下更改,但事后看来,我意识到可能需要
重新思考我处理数组的方式... 当我用编译器编译`decl.c`时,
收到错误:

```
Unknown variable:Text on line 87 of decl.c
```

这促使我编写了符号转储代码。`Text`在全局符号表中,
那为什么解析器抱怨它缺失呢?

答案是`expr.c`中的`postfix()`,在找到一个标识符后,
会查看下一个标记。如果是'[',那么该标识符必须是一个数组。
如果没有'[',那么该标识符必须是一个变量:

```c
  // A variable. Check that the variable exists.
  if ((varptr = findsymbol(Text)) == NULL || varptr->stype != S_VARIABLE)
    fatals("Unknown variable", Text);
```

这阻止了将数组引用作为函数参数传递。
提示错误消息的"冒犯"行在`decl.c`中:

```c
      type = type_of_typedef(Text, ctype);
```

我们正在传递`Text`基址作为参数。但是由于没有后续的'[',
我们的编译器认为它是一个标量变量,并抱怨没有标量变量`Text`。

我做了更改,在这里也允许S_ARRAY以及S_VARIABLE,但这只是
更大问题的冰山一角:数组和指针在我们的编译器中
不像它们应该的那样可互换。我将在下一部分处理这个问题。

## 缺失的运算符

自旅程的第21部分以来,我们的编译器中就有这些标记和AST运算符:

 + <code>&#124;&#124;</code>, T_LOGOR, A_LOGOR
 + `&&`, T_LOGAND, A_LOGAND

不知何故,我从未实现过它们!所以,是时候做它们了。

对于A_LOGAND,我们有两个表达式。如果两个都求值为true,
我们需要将寄存器设置为rvalue 1,否则为0。
对于A_LOGOR,如果任一求值为true,我们需要将寄存器设置为rvalue 1,否则为0。

`expr.c`中的`binexpr()`代码已经解析了这些标记并构建了
A_LOGOR和A_LOGAND AST节点。所以我们需要修复代码生成器。

在`gen.c`的`genAST()`中,我们现在有:

```c
  case A_LOGOR:
    return (cglogor(leftreg, rightreg));
  case A_LOGAND:
    return (cglogand(leftreg, rightreg));
```

以及`cg.c`中两个对应的函数。在我们查看`cg.c`函数之前,
让我们先看一个C表达式示例和将产生的汇编代码。

```c
int x, y, z;
  ...
  z= x || y;
```

编译时,结果为:

```
        movslq  x(%rip), %r10           # Load x's rvalue
        movslq  y(%rip), %r11           # Load y's rvalue
        test    %r10, %r10              # Test x's boolean value
        jne     L13                     # True, jump to L13
        test    %r11, %r11              # Test y's boolean value
        jne     L13                     # True, jump to L13
        movq    $0, %r10                # Neither true, set %r10 to false
        jmp     L14                     # and jump to L14
L13:
        movq    $1, %r10                # Set %r10 to true
L14:
        movl    %r10d, z(%rip)          # Save boolean result to z
```

我们测试每个表达式,根据布尔结果跳转,并将0或1存储到
输出寄存器中。A_LOGAND的汇编代码类似,只是条件跳转是`je`
(如果等于零则跳转),并且`movq $0`和`movq $1`被交换了。

所以,以下是新的`cg.c`函数,不再赘述:

```c
// Logically OR two registers and return a
// register with the result, 1 or 0
int cglogor(int r1, int r2) {
  // Generate two labels
  int Ltrue = genlabel();
  int Lend = genlabel();

  // Test r1 and jump to true label if true
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r1], reglist[r1]);
  fprintf(Outfile, "\tjne\tL%d\n", Ltrue);

  // Test r2 and jump to true label if true
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r2], reglist[r2]);
  fprintf(Outfile, "\tjne\tL%d\n", Ltrue);

  // Didn't jump, so result is false
  fprintf(Outfile, "\tmovq\t$0, %s\n", reglist[r1]);
  fprintf(Outfile, "\tjmp\tL%d\n", Lend);

  // Someone jumped to the true label, so result is true
  cglabel(Ltrue);
  fprintf(Outfile, "\tmovq\t$1, %s\n", reglist[r1]);
  cglabel(Lend);
  free_register(r2);
  return(r1);
}
```

```c
// Logically AND two registers and return a
// register with the result, 1 or 0
int cglogand(int r1, int r2) {
  // Generate two labels
  int Lfalse = genlabel();
  int Lend = genlabel();

  // Test r1 and jump to false label if not true
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r1], reglist[r1]);
  fprintf(Outfile, "\tje\tL%d\n", Lfalse);

  // Test r2 and jump to false label if not true
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r2], reglist[r2]);
  fprintf(Outfile, "\tje\tL%d\n", Lfalse);

  // Didn't jump, so result is true
  fprintf(Outfile, "\tmovq\t$1, %s\n", reglist[r1]);
  fprintf(Outfile, "\tjmp\tL%d\n", Lend);

  // Someone jumped to the false label, so result is false
  cglabel(Lfalse);
  fprintf(Outfile, "\tmovq\t$0, %s\n", reglist[r1]);
  cglabel(Lend);
  free_register(r2);
  return(r1);
}
```

程序`tests/input122.c`是测试,用于确认这个新功能是否有效。

## 结论与下一步

所以那就是我们旅程这一部分修复的一些小东西。
我现在要做的是退后一步,重新思考数组/指针的设计,
并在编译器编写旅程的下一部分尝试解决这个问题。 [下一步](../51_Arrays_pt2/Readme_zh.md)