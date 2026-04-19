# 第 32 章：访问 Struct 中的成员

编译器编写旅程的这一部分实现起来相当简单。我在语言中添加了 `.` 和 `->` 词法单元，并实现了对全局结构体变量的一级成员访问。

我先给出测试程序 `tests/input58.c`，这样你可以看到我实现的所有语言特性：

```c
int printf(char *fmt);

struct fred {                   // 结构体声明，上一章完成
  int x;
  char y;
  long z;
};

struct fred var2;               // 变量声明，上一章完成
struct fred *varptr;            // 指针变量声明，上一章完成

int main() {
  long result;

  var2.x= 12;   printf("%d\n", var2.x);         // 作为左值的成员访问，新增
  var2.y= 'c';  printf("%d\n", var2.y);
  var2.z= 4005; printf("%d\n", var2.z);

  result= var2.x + var2.y + var2.z;             // 作为右值的成员访问，新增
  printf("%d\n", result);

  varptr= &var2;                                // 原有行为
  result= varptr->x + varptr->y + varptr->z;    // 通过指针访问成员，新增
  printf("%d\n", result);
  return(0);
}
```

## 新的词法单元

我们有两个新的词法单元 T_DOT 和 T_ARROW，分别对应输入中的 `.` 和 `->`。一如既往，我不会给出 `scan.c` 中识别这些词法单元的代码。

## 解析成员引用

这与我们现有的数组元素访问代码非常相似。让我们看看两者的相似之处和不同之处。对于这段代码：

```c
  int x[5];
  int y;
  ...
  y= x[3];
```

我们获取 `x` 数组的基地址，将 3 乘以 `int` 类型占用的字节数（例如 3*4 等于 12），然后将结果加到基地址上，并把得到的地址视为我们要访问的 `int` 的地址。接着我们解引用这个地址来获取数组位置的值。

访问结构体成员与此类似：

```c
  struct fred { int x; char y; long z; };
  struct fred var2;
  char y;
  ...
  y= var2.y;
```

我们获取 `var2` 的基地址。我们获取 `y` 成员在 `fred` 结构体中的偏移量，将其加到基地址上，并把得到的地址视为我们要访问的 `char` 的地址。接着我们解引用这个地址来获取其中的值。

## 后缀运算符

T_DOT 和 T_ARROW 是后缀运算符，就像数组引用中的 `[` 一样，因为它们出现在标识符名称之后。所以在 `expr.c` 中的 `postfix()` 函数中添加它们的解析是合理的：

```c
static struct ASTnode *postfix(void) {
  ...
    // 访问结构体或联合体的成员
  if (Token.token == T_DOT)
    return (member_access(0));
  if (Token.token == T_ARROW)
    return (member_access(1));
  ...
}
```

`expr.c` 中新的 `member_access()` 函数的参数指示我们是通过指针还是直接访问成员。现在让我们分阶段看看新的 `member_access()` 函数。

```c
// 解析结构体（或联合体，稍后）的成员引用
// 并返回对应的 AST 树。如果 withpointer 为真，
// 则通过指针访问成员。
static struct ASTnode *member_access(int withpointer) {
  struct ASTnode *left, *right;
  struct symtable *compvar;
  struct symtable *typeptr;
  struct symtable *m;

  // 检查标识符是否已声明为结构体（或联合体，稍后），
  // 或者是结构体/联合体指针
  if ((compvar = findsymbol(Text)) == NULL)
    fatals("Undeclared variable", Text);
  if (withpointer && compvar->type != pointer_to(P_STRUCT))
    fatals("Undeclared variable", Text);
  if (!withpointer && compvar->type != P_STRUCT)
    fatals("Undeclared variable", Text);
```

首先是一些错误检查。我知道我需要在这里添加对联合体的检查，所以我暂时不重构代码。

```c
  // 如果是指向结构体的指针，获取指针的值。
  // 否则，创建一个指向基地址的叶子节点。
  // 两种情况下都是右值
  if (withpointer) {
    left = mkastleaf(A_IDENT, pointer_to(P_STRUCT), compvar, 0);
  } else
    left = mkastleaf(A_ADDR, compvar->type, compvar, 0);
  left->rvalue = 1;
```

此时我们需要获取复合变量的基地址。如果我们得到的是一个指针，我们只需通过创建一个 A_IDENT AST 节点来加载指针的值。否则，标识符本身就是结构体或联合体，所以我们最好用 A_ADDR AST 节点获取它的地址。

这个节点不能是左值，即我们不能说 `var2. = 5`。它必须是右值。

```c
  // 获取复合类型的详细信息
  typeptr = compvar->ctype;

  // 跳过 '.' 或 '->' 词法单元并获取成员名称
  scan(&Token);
  ident();
```

我们获取一个指向复合类型的指针，这样我们就可以遍历类型中的成员列表，然后我们获取 `.` 或 `->` 之后的成员名称（并确认它是一个标识符）。

```c
  // 在类型中查找匹配的成员名称
  // 如果找不到就报错
  for (m = typeptr->member; m != NULL; m = m->next)
    if (!strcmp(m->name, Text))
      break;

  if (m == NULL)
    fatals("No member found in struct/union: ", Text);
```

我们遍历成员列表以找到匹配的成员名称。

```c
  // 创建一个带有偏移量的 A_INTLIT 节点
  right = mkastleaf(A_INTLIT, P_INT, NULL, m->posn);

  // 将成员的偏移量加到结构体的基地址上并解引用。
  // 此时仍然是左值
  left = mkastnode(A_ADD, pointer_to(m->type), left, NULL, right, NULL, 0);
  left = mkastunary(A_DEREF, m->type, left, NULL, 0);
  return (left);
}

```

成员的偏移量（以字节为单位）存储在 `m->posn` 中，所以我们用这个值创建一个 A_INTLIT 节点，并使用 A_ADD 将其加到存储在 `left` 中的基地址上。此时我们有了成员的地址，所以我们解引用它（A_DEREF）来获取成员的 value。此时这仍然是一个左值，使我们能够同时执行 `5 + var2.x` 和 `var2.x= 6`。

### 运行测试代码

`tests/input58.c` 的输出毫不令人意外：

```
12
99
4005
4116
4116
```

让我们看看生成的汇编代码：

```
                                        # var2.y= 'c';
        movq    $99, %r10               # Load 'c' into %r10
        leaq    var2(%rip), %r11        # Get base address of var2 into %r11
        movq    $4, %r12
        addq    %r11, %r12              # Add 4 to this base address
        movb    %r10b, (%r12)           # Write 'c' into this new address

                                        # printf("%d\n", var2.z);
        leaq    var2(%rip), %r10        # Get base address of var2 into %r11
        movq    $4, %r11
        addq    %r10, %r11              # Add 4 to this base address
        movzbq  (%r11), %r11            # Load byte value from this address into %r11
        movq    %r11, %rsi              # Copy it into %rsi
        leaq    L4(%rip), %r10
        movq    %r10, %rdi
        call    printf@PLT              # and call printf()
```

## 结论与下一步

没想到结构体能这么容易就实现工作了，这真是一个愉快的惊喜！我相信未来的部分会弥补这一点的。我也知道我们目前的编译器仍然相当有限。例如，它无法处理这样的情况：

```c
struct foo {
  int x;
  struct foo *next;
};

struct foo *listhead;
struct foo *l;

int main() {
  ...
  l= listhead->next->next;
```

因为这需要跟踪两个指针层级。现有的代码只能跟踪一个指针层级。以后我们必须修复这个问题。

现在可能也是指出我们必须花大量时间让编译器"做对"的好时机。我一直在添加功能，但只够让一个特定的功能工作。在某个时候，这些特定的功能必须变得更加通用。所以在这段旅程中会有一个"收尾"阶段。

现在我们基本让结构体工作了，在编译器编写旅程的下一部分，我将尝试添加联合体。[下一步](../33_Unions/Readme_zh.md)