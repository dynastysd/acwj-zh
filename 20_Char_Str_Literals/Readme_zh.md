# 第 20 章：字符和字符串字面量

我一直想用我们的编译器输出"Hello world"，既然现在我们有了指针和数组，
是时候在这一部分给编译器添加字符和字符串字面量了。

这些当然是字面量（即直接可见的值）。字符字面量的定义是
用单引号包围的单个字符。字符串字面量是用
双引号包围的字符序列。

说真的，C语言中的字符和字符串字面量简直疯狂。我只打算实现最明显的
反斜杠转义字符。我还会从SubC借用字符和字符串字面量
扫描代码来简化工作。

这一部分旅程会很短，但它会以"Hello world"结束。

## 一个新的词法单元

我们需要为语言添加一个新的词法单元：T_STRLIT。它和T_IDENT非常相似，
因为与该词法单元关联的文本存储在全局变量`Text`中，
而不是存储在词法单元结构本身。

## 扫描字符字面量

字符字面量以单引号开头，后跟单个字符的定义，
再以另一个单引号结束。解释这个单个字符的代码比较复杂，
所以让我们修改`scan.c`中的`scan()`来调用它：

```c
      case '\'':
      // If it's a quote, scan in the
      // literal character value and
      // the trailing quote
      t->intvalue = scanch();
      t->token = T_INTLIT;
      if (next() != '\'')
        fatal("Expected '\\'' at end of char literal");
      break;
```

我们可以将字符字面量当作类型为`char`的整数字面量来处理；
也就是说，假设我们只处理ASCII而不尝试处理Unicode。这正是我在这里的做法。

### `scanch()`函数的代码

`scanch()`函数的代码来自SubC，只做了一些简化：

```c
// Return the next character from a character
// or string literal
static int scanch(void) {
  int c;

  // Get the next input character and interpret
  // metacharacters that start with a backslash
  c = next();
  if (c == '\\') {
    switch (c = next()) {
      case 'a':  return '\a';
      case 'b':  return '\b';
      case 'f':  return '\f';
      case 'n':  return '\n';
      case 'r':  return '\r';
      case 't':  return '\t';
      case 'v':  return '\v';
      case '\\': return '\\';
      case '"':  return '"' ;
      case '\'': return '\'';
      default:
        fatalc("unknown escape sequence", c);
    }
  }
  return (c);                   // Just an ordinary old character!
}
```

该代码能识别大多数转义字符序列，但不会尝试识别八进制字符编码
或其他难以处理的序列。

## 扫描字符串字面量

字符串字面量以双引号开头，后跟零个或多个字符，
再以另一个双引号结束。与字符字面量一样，
我们需要在`scan()`中调用一个单独的函数：

```c
    case '"':
      // Scan in a literal string
      scanstr(Text);
      t->token= T_STRLIT;
      break;
```

我们创建一个新的T_STRLIT词法单元，并将字符串扫描到`Text`缓冲区中。
以下是`scanstr()`的代码：

```c
// Scan in a string literal from the input file,
// and store it in buf[]. Return the length of
// the string.
static int scanstr(char *buf) {
  int i, c;

  // Loop while we have enough buffer space
  for (i=0; i<TEXTLEN-1; i++) {
    // Get the next char and append to buf
    // Return when we hit the ending double quote
    if ((c = scanch()) == '"') {
      buf[i] = 0;
      return(i);
    }
    buf[i] = c;
  }
  // Ran out of buf[] space
  fatal("String literal too long");
  return(0);
}
```

我认为这段代码是直接明了的。它对扫描进来的字符串进行NUL终止，
并确保不会溢出`Text`缓冲区。注意，我们使用`scanch()`函数
来扫描各个字符。

## 解析字符串字面量

正如我之前提到的，字符字面量被当作整数字面量来处理，
这是我们已经能处理的。字符串字面量可以出现在哪里呢？让我们回到
Jeff Lee在1985年编写的
[BNF Grammar for C](https://www.lysator.liu.se/c/ANSI-C-grammar-y.html)：

```
primary_expression
        : IDENTIFIER
        | CONSTANT
        | STRING_LITERAL
        | '(' expression ')'
        ;
```

由此我们知道应该修改`expr.c`中的`primary()`：

```c
// Parse a primary factor and return an
// AST node representing it.
static struct ASTnode *primary(void) {
  struct ASTnode *n;
  int id;


  switch (Token.token) {
  case T_STRLIT:
    // For a STRLIT token, generate the assembly for it.
    // Then make a leaf AST node for it. id is the string's label.
    id= genglobstr(Text);
    n= mkastleaf(A_STRLIT, P_CHARPTR, id);
    break;
```

现在，我要创建一个匿名的全局字符串。它需要将字符串中的所有字符
存储在内存中，同时我们还需要一种引用它的方式。我不想用这个
新字符串污染符号表，所以我选择为字符串分配一个标签，
并将该标签的编号存储在字符串字面量的抽象语法树节点中。
我们还需要一个新的抽象语法树节点类型：A_STRLIT。
这个标签实际上就是字符串中字符数组的基地址，
因此它应该是P_CHARPTR类型。

稍后我会回来说明由`genglobstr()`完成的汇编输出生成。

### 一个抽象语法树示例

目前，字符串字面量被视为一个匿名指针。以下是
该语句的抽象语法树：

```c
  char *s;
  s= "Hello world";

  A_STRLIT rval label L2
  A_IDENT s
A_ASSIGN
```

它们类型相同，所以不需要进行缩放或加宽。

## 生成汇编输出

在通用代码生成器中，变化很少。我们需要一个函数来为新字符串
生成存储空间。我们需要为它分配一个标签，然后输出字符串的内容（在`gen.c`中）：

```c
int genglobstr(char *strvalue) {
  int l= genlabel();
  cgglobstr(l, strvalue);
  return(l);
}
```

我们需要识别A_STRLIT抽象语法树节点类型并为其生成汇编代码。
在`genAST()`中：

```c
    case A_STRLIT:
        return (cgloadglobstr(n->v.id));
```

## 生成x86-64汇编输出

我们终于来到了真正的新汇编输出函数。有两个：
一个用于生成字符串的存储，另一个用于加载字符串的基地址。

```c
// Generate a global string and its start label
void cgglobstr(int l, char *strvalue) {
  char *cptr;
  cglabel(l);
  for (cptr= strvalue; *cptr; cptr++) {
    fprintf(Outfile, "\t.byte\t%d\n", *cptr);
  }
  fprintf(Outfile, "\t.byte\t0\n");
}

// Given the label number of a global string,
// load its address into a new register
int cgloadglobstr(int id) {
  // Get a new register
  int r = alloc_register();
  fprintf(Outfile, "\tleaq\tL%d(\%%rip), %s\n", id, reglist[r]);
  return (r);
}
```

回到我们的示例：
```c
  char *s;
  s= "Hello world";
```

对应的汇编输出是：

```
L2:     .byte   72              # Anonymous string
        .byte   101
        .byte   108
        .byte   108
        .byte   111
        .byte   32
        .byte   119
        .byte   111
        .byte   114
        .byte   108
        .byte   100
        .byte   0
        ...
        leaq    L2(%rip), %r8   # Load L2's address
        movq    %r8, s(%rip)    # and store in s
```

## 其他修改

在为这部分旅程编写测试程序时，我发现了现有代码中的另一个bug。
在将整数值缩放以匹配指针所指向的类型大小时，
我忘记了在缩放值为1时什么都不做。`types.c`中`modify_type()`的代码现在是：

```c
    // Left is int type, right is pointer type and the size
    // of the original type is >1: scale the left
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = genprimsize(value_at(rtype));
      if (rsize > 1)
        return (mkastunary(A_SCALE, rtype, tree, rsize));
      else
        return (tree);          // Size 1, no need to scale
    }
```

我之前漏掉了`return (tree)`，因此在尝试缩放`char *`指针时
返回了NULL树。

## 结论与下一步

我很高兴我们现在可以输出文本了：

```
$ make test
./comp1 tests/input21.c
cc -o out out.s lib/printint.c
./out
10
Hello world
```

这次的大部分工作是扩展我们的词法扫描器，以处理字符和字符串字面量
定界符以及其中字符的转义。但代码生成器方面也有一些工作。

在编译器编写旅程的下一部分，我们将添加更多的二元运算符到
编译器识别的语言中。[下一步](../21_More_Operators/Readme_zh.md)