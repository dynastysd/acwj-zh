# 第 5 章：语句

是时候为我们的语言语法添加一些"真正的"语句了。我希望能写出这样的代码行：

```
   print 2 + 3 * 5;
   print 18 - 6/3 + 4*2;
```

当然，由于我们忽略空白符，一个语句的所有 token 不必都在同一行上。每个语句以关键字 `print` 开头，以分号结尾。因此，这些将成为我们语言中的新 token。

## 语法的 BNF 描述

我们已经见过表达式的 BNF 表示法。现在让我们定义上述类型语句的 BNF 语法：

```
statements: statement
     | statement statements
     ;

statement: 'print' expression ';'
     ;
```

一个输入文件由多个语句组成。它们可以是一个语句，或者是一个语句后跟更多语句。每个语句以关键字 `print` 开头，然后是一个表达式，然后是一个分号。

## 词法扫描器的变化

在我们开始解析上述语法之前，我们需要给现有代码添加一些更多的内容。让我们从词法扫描器开始。

添加分号的 token 很容易。现在是 `print` 关键字。 Later，我们会遇到语言中的很多关键字，加上变量的标识符，所以我们需要添加一些代码来处理它们。

在 `scan.c` 中，我添加了从 SubC 编译器借用的代码。它将字母数字字符读入缓冲区，直到遇到非字母数字字符。

```c
// 从输入文件扫描一个标识符并
// 存储到 buf[] 中。返回标识符的长度
static int scanident(int c, char *buf, int lim) {
  int i = 0;

  // 允许数字、字母和下划线
  while (isalpha(c) || isdigit(c) || '_' == c) {
    // 如果达到标识符长度限制则报错，
    // 否则追加到 buf[] 并获取下一个字符
    if (lim - 1 == i) {
      printf("identifier too long on line %d\n", Line);
      exit(1);
    } else if (i < lim - 1) {
      buf[i++] = c;
    }
    c = next();
  }
  // 遇到无效字符，将其放回。
  // NUL 终止 buf[] 并返回长度
  putback(c);
  buf[i] = '\0';
  return (i);
}
```

我们还需要一个函数来识别语言中的关键字。一种方法是有个关键字列表，然后遍历列表，用 `strcmp()` 与 `scanident()` 中的缓冲区逐一比较。SubC 的代码有一个优化：在做 `strcmp()` 之前先匹配第一个字母。这加快了对数十个关键字的比较。目前我们还不需要这个优化，但我为以后添加了它：

```c
// 给定输入中的一个单词，返回匹配的
// 关键字 token 编号，如果不是关键字则返回 0。
// 根据第一个字母 switch，这样我们就不必
//浪费时间去与所有关键字做 strcmp()。
static int keyword(char *s) {
  switch (*s) {
    case 'p':
      if (!strcmp(s, "print"))
        return (T_PRINT);
      break;
  }
  return (0);
}
```

现在，在 `scan()` 的 switch 语句底部，我们添加这段代码来识别分号和关键字：

```c
    case ';':
      t->token = T_SEMI;
      break;
    default:

      // 如果是数字，扫描
      // 字面整数值
      if (isdigit(c)) {
        t->intvalue = scanint(c);
        t->token = T_INTLIT;
        break;
      } else if (isalpha(c) || '_' == c) {
        // 读入关键字或标识符
        scanident(c, Text, TEXTLEN);

        // 如果是已识别关键字，返回该 token
        if (tokentype = keyword(Text)) {
          t->token = tokentype;
          break;
        }
        // 不是已识别关键字，所以是错误
        printf("Unrecognised symbol %s on line %d\n", Text, Line);
        exit(1);
      }
      // 该字符不属于任何已识别 token，报错
      printf("Unrecognised character %c on line %d\n", c, Line);
      exit(1);
```

我还添加了一个全局 `Text` 缓冲区来存储关键字和标识符：

```c
#define TEXTLEN         512             // 输入中符号的长度
extern_ char Text[TEXTLEN + 1];         // 最后扫描的标识符
```

## 表达式解析器的变化

到目前为止，我们的输入文件只包含一个表达式；因此，在 `expr.c` 的 Pratt 解析器代码 `binexpr()` 中，我们有这段代码来退出解析器：

```c
  // 如果没有剩余 token，仅返回左节点
  tokentype = Token.token;
  if (tokentype == T_EOF)
    return (left);
```

使用我们的新语法，每个表达式以分号终止。因此，我们需要修改表达式解析器中的代码来识别 `T_SEMI` token 并退出表达式解析：

```c
// 返回一个以二元运算符为根的 AST 树。
// 参数 ptp 是前一个 token 的优先级。
struct ASTnode *binexpr(int ptp) {
  struct ASTnode *left, *right;
  int tokentype;

  // 获取左边的整数字面量。
  // 同时获取下一个 token。
  left = primary();

  // 如果遇到分号，仅返回左节点
  tokentype = Token.token;
  if (tokentype == T_SEMI)
    return (left);

    while (op_precedence(tokentype) > ptp) {
      ...

          // 更新当前 token 的详情。
    // 如果遇到分号，仅返回左节点
    tokentype = Token.token;
    if (tokentype == T_SEMI)
      return (left);
    }
}
```

## 代码生成器的变化

我想把 `gen.c` 中的通用代码生成器与 `cg.c` 中的 CPU 专用代码分开。这也意味着编译器的其余部分只应调用 `gen.c` 中的函数，只有 `gen.c` 才能调用 `cg.c` 中的代码。

为此，我在 `gen.c` 中定义了一些新的"前端"函数：

```c
void genpreamble()        { cgpreamble(); }
void genpostamble()       { cgpostamble(); }
void genfreeregs()        { freeall_registers(); }
void genprintint(int reg) { cgprintint(reg); }
```

## 添加语句解析器

我们有一个新文件 `stmt.c`。这将保存我们语言中所有主要语句的解析代码。现在，我们需要解析上面给出的语句的 BNF 语法。这用一个函数完成。我把递归定义转换成了一个循环：

```c
// 解析一个或多个语句
void statements(void) {
  struct ASTnode *tree;
  int reg;

  while (1) {
    // 匹配 'print' 作为第一个 token
    match(T_PRINT, "print");

    // 解析后续表达式并
    // 生成汇编代码
    tree = binexpr(0);
    reg = genAST(tree);
    genprintint(reg);
    genfreeregs();

    // 匹配后续分号
    // 如果到达 EOF 则停止
    semi();
    if (Token.token == T_EOF)
      return;
  }
}
```

在每个循环中，代码找到一个 T_PRINT token。然后调用 `binexpr()` 来解析表达式。最后，找到 T_SEMI token。如果接下来是 T_EOF，我们就跳出循环。

在每个表达式树之后，调用 `gen.c` 中的代码将树转换为汇编代码，并调用汇编的 `printint()` 函数来打印最终值。

## 一些辅助函数

上述代码中有几个新的辅助函数，我把它们放到了新文件 `misc.c` 中：

```c
// 确保当前 token 是 t，
// 并获取下一个 token。否则
// 抛出一个错误 
void match(int t, char *what) {
  if (Token.token == t) {
    scan(&Token);
  } else {
    printf("%s expected on line %d\n", what, Line);
    exit(1);
  }
}

// 匹配一个分号并获取下一个 token
void semi(void) {
  match(T_SEMI, ";");
}
```

这些构成了解析器中语法检查的一部分。以后我会添加更多调用 `match()` 的简短函数来使语法检查更容易。

## `main()` 的变化

`main()` 以前直接调用 `binexpr()` 来解析旧输入文件中的单个表达式。现在它这样做：

```c
  scan(&Token);                 // 从输入中获取第一个 token
  genpreamble();                // 输出前导部分
  statements();                 // 解析输入中的语句
  genpostamble();               // 输出后置部分
  fclose(Outfile);              // 关闭输出文件并退出
  exit(0);
```

## 试用一下

这就是新代码和修改后的代码。让我们试一下新代码。这是新的输入文件 `input01`：

```
print 12 * 3;
print 
   18 - 2
      * 4; print
1 + 2 +
  9 - 5/2 + 3*5;
```

是的，我决定检查我们是否可以让 token 分布在多行上。编译和运行输入文件，做 `make test`：

```make
$ make test
cc -o comp1 -g cg.c expr.c gen.c main.c misc.c scan.c stmt.c tree.c
./comp1 input01
cc -o out out.s
./out
36
10
25
```

而且它工作了！

## 结论与下一步

我们已经为语言添加了第一个"真正的"语句语法。我用 BNF 表示法定义了它，但用循环实现比递归实现更容易。别担心，我们很快就会回到递归解析。

一路上，我们必须修改扫描器，添加对关键字和标识符的支持，并且更清晰地分离通用代码生成器和 CPU 专用生成器。

在我们编译器编写的下一步中，我们将向语言添加变量。这将需要大量工作。[下一步](../06_Variables/Readme.md)
