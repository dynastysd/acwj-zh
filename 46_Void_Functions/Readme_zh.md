# 第 46 章：Void 函数形参和扫描变更

在编译器编写旅程的这一部分，我做了几个涉及扫描器和解析器的修改。

## Void 函数形参

我们从 C 语言中这个常见的结构开始，它用来表示函数没有形参：

```c
int fred(void);         // Void 表示没有形参，但
int fred();             // 没有形参也意味着没有形参
```

这看起来确实有点奇怪，我们已经有了一种表示没有形参的方式，但不管怎样，这是一个常见的功能，所以我们需要支持它。

问题是，一旦我们遇到左括号，就会进入 `decl.c` 中的 `declaration_list()` 函数。这个函数的设置是用来解析一个类型及其后续的标识符。要修改它来处理一个类型但没有标识符的情况并不容易。所以我们需要回到 `param_declaration_list()` 函数，在那里解析 'void' ')' 词法单元。

我在扫描器中已经有一个叫做 `reject_token()` 的函数，在 `scan.c` 中。我们应该能够扫描一个词法单元，看看它，决定不想要它，然后拒绝它。那么，下一个扫描的词法单元就是我们拒绝的那个。

我从来没有用过这个函数，事实证明它是坏的。不管怎样，我退后一步，决定偷看一下下一个词法单元会更容易。如果我们决定喜欢它，就可以正常扫描进来。如果我们不喜欢它，我们不需要做任何事情：它会在下一次真正的词法单元扫描时被扫描进来。

现在，为什么我们需要这个？这是因为我们处理形参列表中 'void' 的伪代码是：

```
  解析 '('
  如果下一个词法单元是 'void' {
    偷看它后面的那个
    如果 'void' 后面是 ')'，
    则返回零个形参
  }
  调用 declaration_list() 来获取真正的形参
  这样 'void' 仍然是当前词法单元
```

我们需要做这个偷看，因为以下两种都是合法的：

```c
int fred(void);
int jane(void *ptr, int x, int y);
```

如果我们在 'void' 后面扫描并解析下一个词法单元，看到它是星号，那么我们就丢失了 'void' 词法单元。当我们调用 `declaration_list()` 时，它看到的第一个词法单元将是星号，这会让它出问题。因此，我们需要能够在保持当前词法单元完整的同时，偷看当前词法单元后面的内容。

## 新的扫描器代码

在 `data.h` 中我们有一个新的词法单元变量：

```c
extern_ struct token Token;             // 上一个扫描的词法单元
extern_ struct token Peektoken;         // 一个前瞻词法单元
```

并且 `Peektoken.token` 在 `main.c` 的代码中被初始化为零。我们按如下方式修改 `scan.c` 中的主 `scan()` 函数：

```c
// 扫描并返回在输入中找到的下一个词法单元。
// 如果词法单元有效则返回 1，如果没有词法单元剩余则返回 0。
int scan(struct token *t) {
  int c, tokentype;

  // 如果我们有前瞻词法单元，返回这个词法单元
  if (Peektoken.token != 0) {
    t->token = Peektoken.token;
    t->tokstr = Peektoken.tokstr;
    t->intvalue = Peektoken.intvalue;
    Peektoken.token = 0;
    return (1);
  }
  ...
}
```

如果 `Peektoken.token` 保持为零，我们就获取下一个词法单元。但是一旦有东西存储在 `Peektoken` 中，那么它将是我们返回的下一个词法单元。

## 声明修改

现在我们能够偷看下一个词法单元了，让我们把它付诸行动。我们按如下方式修改 `param_declaration_list()` 中的代码：

```c
  // 循环获取任何形参
  while (Token.token != T_RPAREN) {

    // 如果第一个词法单元是 'void'
    if (Token.token == T_VOID) {
      // 偷看下一个词法单元。如果是 ')'，函数
      // 没有形参，所以离开循环。
      scan(&Peektoken);
      if (Peektoken.token == T_RPAREN) {
        // 将 Peektoken 移入 Token
        paramcnt= 0; scan(&Token); break;
      }
    }
    ...
    // 获取下一个形参的类型
    type = declaration_list(&ctype, C_PARAM, T_COMMA, T_RPAREN, &unused);
    ...
  }
```

假设我们已经扫描进了 'void'。我们现在 `scan(&Peektoken);` 来看看接下来是什么，而不改变当前的 `Token`。如果那是右括号，我们可以在跳过 'void' 词法单元后将 `paramcnt` 设置为零后离开。

但如果下一个词法单元不是右括号，我们仍然有 `Token` 设置为 'void'，现在我们可以调用 `declaration_list()` 来获取实际的形参列表。

## 十六进制和八进制整型常量

我之所以发现上述问题，是因为我开始将编译器的源代码喂给编译器本身。一旦我修复了 'void' 形参问题，接下来发现的就是编译器无法解析十六进制和八进制常量，比如 `0x314A` 和 `0073`。

幸运的是，Nils M Holm 编写的 [SubC](http://www.t3x.org/subc/) 编译器有做这个的代码，我可以完整地借用它来添加到我们的编译器中。我们需要修改 `scan.c` 中的 `scanint()` 函数来做到这一点：

```c
// 从输入文件中扫描并返回一个整型字面量值。
static int scanint(int c) {
  int k, val = 0, radix = 10;

  // 新代码：假设 radix 是 10，但如果以 0 开头
  if (c == '0') {
    // 且下一个字符是 'x'，则是 radix 16
    if ((c = next()) == 'x') {
      radix = 16;
      c = next();
    } else
      // 否则，是 radix 8
      radix = 8;

  }

  // 将每个字符转换为一个 int 值
  while ((k = chrpos("0123456789abcdef", tolower(c))) >= 0) {
    if (k >= radix)
      fatalc("invalid digit in integer literal", c);
    val = val * radix + k;
    c = next();
  }

  // 我们遇到了一个非整数字符，把它放回去。
  putback(c);
  return (val);
}
```

我们已经在函数中有了处理十进制字面量值的 `k= chrpos("0123456789")` 代码。这段新代码现在扫描前导的 '0' 数字。如果看到它，就检查后面的字符。如果是 'x'，radix 是 16；如果不是，radix 是 8。

另一个改变是我们乘以 radix 而不是常量 10。这是一个非常优雅的解决问题的方式，非常感谢 Nils 编写了这个代码。

## 更多字符常量

我遇到的下一个问题是编译器中的这段代码：

```c
   if (*posn == '\0')
```

这是我们的编译器无法识别的字符字面量。我们需要修改 `scan.c` 中的 `scanch()` 来处理以八进制值指定的字符字面量。但也可以有以十六进制值指定的字符字面量，例如 '\0x41'。同样，来自 SubC 的代码帮助我们解决了问题：

```c
// 从输入中读取一个十六进制常量
static int hexchar(void) {
  int c, h, n = 0, f = 0;

  // 循环获取字符
  while (isxdigit(c = next())) {
    // 从 char 转换为 int 值
    h = chrpos("0123456789abcdef", tolower(c));
    // 加到运行的十六进制值中
    n = n * 16 + h;
    f = 1;
  }
  // 我们遇到了一个非十六进制字符，把它放回去
  putback(c);
  // 标志告诉我们从未看到任何十六进制字符
  if (!f)
    fatal("missing digits after '\\x'");
  if (n > 255)
    fatal("value out of range after '\\x'");
  return n;
}

// 从字符或字符串字面量返回下一个字符
static int scanch(void) {
  int i, c, c2;

  // 获取下一个输入字符并解释
  // 以反斜杠开头的元字符
  c = next();
  if (c == '\\') {
    switch (c = next()) {
      ...
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':			// 来自 SubC 的代码
        for (i = c2 = 0; isdigit(c) && c < '8'; c = next()) {
          if (++i > 3)
            break;
          c2 = c2 * 8 + (c - '0');
        }
        putback(c);             // 把第一个非八进制字符放回去
        return (c2);
      case 'x':
        return hexchar();	// 来自 SubC 的代码
      default:
        fatalc("unknown escape sequence", c);
    }
  }
  return (c);                   // 只是一个普通的字符！
}
```

同样，这是漂亮而优雅的代码。然而，我们现在有两个做十六进制转换的代码片段和三个做 radix 转换的代码片段，所以这里仍然有一些潜在的重构工作。

## 结论与下一步

在这部分旅程中，我们主要对扫描器做了修改。这些不是惊天动地的修改，但它们是我们需要完成的一些小事，让编译器能够自编译。

我们需要解决的两个大事是静态函数和变量，以及 `sizeof()` 运算符。

在我们编译器编写旅程的下一部分，我可能会研究 `sizeof()` 运算符，因为 `static` 仍然让我有点害怕！[下一步](../47_Sizeof/Readme_zh.md)