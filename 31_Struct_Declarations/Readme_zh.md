# 第 31 章：实现 Struct，第一部分

在编译器编写旅程的这一部分，我开始了在语言中实现结构体的过程。虽然这些还不能正常工作，但为了能够声明结构体和结构体类型的全局变量，我已经对代码进行了大量修改。

## 符号表的变化

正如我在上一部分提到的，当符号是复合类型时，我们需要修改符号表结构以包含指向复合类型节点的指针。我们还添加了一个 `next` 指针来支持链表，以及一个 `member` 指针。函数节点的 `member` 指针保存函数的参数列表。我们将使用结构体的 `member` 节点来保存结构体的成员字段。

所以我们现在有：

```c
struct symtable {
  char *name;                   // 符号的名称
  int type;                     // 符号的原始类型
  struct symtable *ctype;       // 如果需要，指向复合类型的指针
  ...
  struct symtable *next;        // 链表中下一个符号
  struct symtable *member;      // 函数、结构体、
};                              // 联合或枚举的第一个成员
```

我们在 `data.h` 中还有两个新的符号列表：

```c
// 符号表列表
struct symtable *Globhead, *Globtail;     // 全局变量和函数
struct symtable *Loclhead, *Locltail;     // 局部变量
struct symtable *Parmhead, *Parmtail;     // 局部参数
struct symtable *Membhead, *Membtail;     // 结构体/联合成员的临时列表
struct symtable *Structhead, *Structtail; // 结构体类型列表
```

## 对 `sym.c` 的修改

在整个 `sym.c` 以及代码的其他地方，我们以前只接收 `int type` 参数来确定某物的类型。现在我们有了复合类型，仅有这个参数就不够了：P_STRUCT 整数值告诉我们某物是一个结构体，但不能确定是哪一个。

因此，许多函数现在除了接收 `int type` 参数外还接收一个 `struct symtable *ctype` 参数。当 `type` 是 P_STRUCT 时，`ctype` 指向定义这个特定结构体类型的节点。

在 `sym.c` 中，所有的 `addXX()` 函数都已被修改以添加这个额外参数。还有一个新的 `addmemb()` 函数和一个新的 `addstruct()` 函数，用于向这两个新列表添加节点。它们的功能与其他 `addXX()` 函数完全相同，只是操作的是不同的列表。我稍后会回到这些函数。

## 一个新的词法单元

我们有一个相当长时间以来的第一个新词法单元 P_STRUCT。它与匹配的 `struct` 关键字一起使用。我将省略对 `scan.c` 的修改，因为这些修改很小。

## 在我们的语法中解析 Struct

有很多地方我们需要解析 `struct` 关键字：

  + 命名结构体的定义
  + 无名结构体的定义，后面跟着一个该类型的变量
  + 在另一个结构体或联合内部定义结构体
  + 之前定义的结构体类型的变量定义

一开始，我不确定在哪里处理结构体的解析。我应该假设我们正在解析一个新的结构体定义，但在看到变量标识符时退出，还是假设是变量声明？

最后，我意识到，在看到 `struct <标识符>` 之后，我必须假设这只是一个类型的命名，就像 `int` 是 `int` 类型的命名一样。我们必须解析下一个词法单元来确定情况。

因此，我修改了 `decl.c` 中的 `parse_type()` 来解析标量类型（如 `int`）和复合类型（如 `struct foo`）。既然它可以返回复合类型，我就必须找到一种方法返回指向定义这个复合类型的节点的指针：

```c
// 解析当前词法单元并返回
// 一个原始类型枚举值和一个指向
// 任何复合类型的指针。
// 同时扫描下一个词法单元
int parse_type(struct symtable **ctype) {
  int type;
  switch (Token.token) {
    ...         // T_VOID、T_CHAR 等的现有代码
    case T_STRUCT:
      type = P_STRUCT;
      *ctype = struct_declaration();
      break;
    ...
```

我们调用 `struct_declaration()` 来查找现有的结构体类型或解析新结构体类型的声明。

## 重构变量列表的解析

在我们的旧代码中，有一个名为 `param_declaration()` 的函数，它解析用逗号分隔的参数列表，例如：

```c
int fred(int x, char y, long z);
```

就像你在函数声明的参数列表中找到的那样。结构体和联合声明也有一个变量列表，只不过它们用分号分隔并用花括号包围，例如：

```c
struct fred { int x; char y; long z; };
```

重构这个函数来解析两种列表是合理的。它现在接收两个词法单元：分隔词法单元（如 T_SEMI）和结束词法单元（如 T_RBRACE）。因此，我们可以用它来解析两种风格的列表。

```c
// 解析变量列表。
// 将它们作为符号添加到符号表列表之一，并返回
// 变量数量。如果 funcsym 不为 NULL，则存在现有函数
// 原型，因此将每个变量的类型与此原型进行比较。
static int var_declaration_list(struct symtable *funcsym, int class,
                                int separate_token, int end_token) {
    ...
    // 获取类型和标识符
    type = parse_type(&ctype);
    ...
    // 根据类别将新参数添加到正确的符号表列表
    var_declaration(type, ctype, class);
}
```

当我们解析函数参数列表时，我们调用：

```c
    var_declaration_list(oldfuncsym, C_PARAM, T_COMMA, T_RPAREN);
```

当我们解析结构体成员列表时，我们调用：

```c
    var_declaration_list(NULL, C_MEMBER, T_SEMI, T_RBRACE);
```

还要注意，对 `var_declaration()` 的调用现在给出了变量的类型、复合类型指针（如果它是结构体或联合），以及变量的类别。

现在我们可以解析结构体的成员列表了。让我们看看如何解析整个结构体。

## `struct_declaration()` 函数

让我们分阶段进行。

```c
static struct symtable *struct_declaration(void) {
  struct symtable *ctype = NULL;
  struct symtable *m;
  int offset;

  // 跳过 struct 关键字
  scan(&Token);

  // 查看是否有后续的结构体名称
  if (Token.token == T_IDENT) {
    // 查找任何匹配的复合类型
    ctype = findstruct(Text);
    scan(&Token);
  }
```

此时我们已经看到了 `struct`，后面可能跟着一个标识符。如果这是一个现有的结构体类型，`ctype` 现在指向现有的类型节点。否则，`ctype` 为 NULL。

```c
  // 如果下一个词法单元不是 LBRACE，这就是
  // 一个现有结构体类型的使用。
  // 返回指向该类型的指针。
  if (Token.token != T_LBRACE) {
    if (ctype == NULL)
      fatals("unknown struct type", Text);
    return (ctype);
  }
```

我们没有看到 '{'，所以这只是一个现有类型的命名。`ctype` 不能为 NULL，所以我们首先检查它，然后简单地返回指向这个现有结构体类型的指针。当我们执行：

```c
      type = P_STRUCT; *ctype = struct_declaration();
```

时，这将返回到 `parse_type()`。

但是，假设我们没有返回，我们一定找到了一个 '{'，这表示结构体类型的定义。让我们继续...

```c
  // 确保这个结构体类型之前没有被定义
  if (ctype)
    fatals("previously defined struct", Text);

  // 构建结构体节点并跳过左花括号
  ctype = addstruct(Text, P_STRUCT, NULL, 0, 0);
  scan(&Token);
```

我们不能两次声明同名结构体，所以要防止这种情况。然后在符号表中构建新结构体类型节点的开始部分。到目前为止，我们只有它的名称和它是 P_STRUCT 类型。

```c
  // 扫描成员列表并附加
  // 到结构体类型的节点
  var_declaration_list(NULL, C_MEMBER, T_SEMI, T_RBRACE);
  rbrace();
```

这会解析成员列表。对于每个成员，一个新的符号节点被附加到 `Membhead` 和 `Membtail` 指向的列表。这个列表只是临时的，因为接下来的几行代码将成员列表移动到新的结构体类型节点中：

```c
  ctype->member = Membhead;
  Membhead = Membtail = NULL;
```

我们现在有一个结构体类型节点，包含名称和结构体中的成员列表。接下来需要做什么？嗯，我们需要确定：

  + 结构体的总大小，以及
  + 每个成员相对于结构体基址的偏移量

其中一些由于内存中标量值的对齐方式而非常依赖于硬件。所以我将按原样给出代码，然后再跟进函数调用结构。

```c
  // 设置初始成员的偏移量
  // 并找到它之后的第一个可用字节
  m = ctype->member;
  m->posn = 0;
  offset = typesize(m->type, m->ctype);
```

我们有一个新函数 `typesize()` 来获取任何类型的大小：标量、指针或复合类型。第一个成员的位置被设置为零，我们用其大小来确定下一个成员可以存储的第一个可能的字节。但现在我们需要担心对齐问题。

举个例子，在 32 位架构中，4 字节标量值必须对齐到 4 字节边界：

```c
struct {
  char x;               // 偏移量为 0
  int y;                // 偏移量为 4，而不是 1
};
```

以下是计算每个后续成员偏移量的代码：

```c
  // 设置结构体中每个后续成员的位置
  for (m = m->next; m != NULL; m = m->next) {
    // 为这个成员设置偏移量
    m->posn = genalign(m->type, offset, 1);

    // 获取这个成员之后下一个可用字节的偏移量
    offset += typesize(m->type, m->ctype);
  }
```

我们有一个新函数 `genalign()`，它接收当前偏移量和我们需要对齐的类型，并返回适合这个类型对齐的第一个偏移量。例如，如果 P_INT 必须 4 对齐，`genalign(P_INT, 3, 1)` 可能返回 4。我将很快讨论最后的参数 1。

所以，`genalign()` 计算出这个成员的正确对齐，然后我们加上这个成员的大小来得到下一个可用的（未对齐的）位置，供下一个成员使用。

一旦我们对列表中的所有成员完成了上述操作，`offset` 就是整个结构体的字节大小。所以：

```c
  // 设置结构体的总大小
  ctype->size = offset;
  return (ctype);
}
```

## `typesize()` 函数

是时候跟踪所有新函数，看看它们做什么以及如何做。我们从 `types.c` 中的 `typesize()` 开始：

```c
// 给定一个类型和一个复合类型指针，返回
// 这个类型占用的字节大小
int typesize(int type, struct symtable *ctype) {
  if (type == P_STRUCT)
    return(ctype->size);
  return(genprimsize(type));
}
```

如果类型是结构体，则从结构体的类型节点返回大小。否则它是标量或指针类型，所以请 `genprimsize()`（它调用硬件特定的 `cgprimsize()`）来获取类型的大小。非常简单。

## `genalign()` 和 `cgalign()` 函数

现在我们进入一些不太好的代码。给定一个类型和一个现有的未对齐偏移量，我们需要知道将给定类型的值放在下一个对齐位置是哪个。

我还担心我们可能需要在栈上执行这个操作，栈是向下增长而不是向上增长的。所以函数有第三个参数：我们需要找到下一个对齐位置的*方向*。

同样，对齐的知识是硬件特定的，所以：

```c
int genalign(int type, int offset, int direction) {
  return (cgalign(type, offset, direction));
}
```

我们将注意力转向 `cg.c` 中的 `cgalign()`：

```c
// 给定一个标量类型、一个现有的内存偏移量
//（尚未分配给任何东西）和一个方向（1 向上，-1 向下），
// 计算并返回适合这个标量类型的内存偏移量。
// 这可能是原始偏移量，也可能在原始偏移量的上方或下方
int cgalign(int type, int offset, int direction) {
  int alignment;

  // 我们在 x86-64 上不需要这样做，但让我们
  // 在任何偏移量上对齐 char，并在
  // 4 字节对齐上对齐 int/指针
  switch(type) {
    case P_CHAR: return (offset);
    case P_INT:
    case P_LONG: break;
    default:     fatald("Bad type in calc_aligned_offset:", type);
  }

  // 这里我们有一个 int 或 long。在 4 字节偏移量上对齐它
  // 我把通用代码放在这里，以便可以在其他地方重用。
  alignment= 4;
  offset = (offset + direction * (alignment-1)) & ~(alignment-1);
  return (offset);
}
```

首先，是的，我知道我们不必在 x86-64 架构中担心对齐问题。但我认为我们应该完成处理对齐的练习，所以有一个可以借鉴的例子来为其他可能编写的后端使用。

代码对 `char` 类型返回给定的偏移量，因为它们可以存储在任何对齐位置。但我们对 `int` 和 `long` 强制执行 4 字节对齐。

让我们分解这个大的偏移量表达式。第一个 `alignment-1` 将 `offset` 从 0 变成 3，从 1 变成 4，从 2 变成 5，以此类推。然后，最后我们用它与 3 的取反（即 ...111111100）进行 AND 运算，丢弃最后两位并将值降低到正确的对齐位置。

因此：

| 偏移量 | 加值 | 新偏移量 |
|:------:|:---------:|:----------:|
|   0    |    3      |    0       |
|   1    |    4      |    4       |
|   2    |    5      |    4       |
|   3    |    6      |    4       |
|   4    |    7      |    4       |
|   5    |    8      |    8       |
|   6    |    9      |    8       |
|   7    |   10      |    8       |

偏移量 0 保持在零，但值 1 到 3 被推到 4。4 保持在 4 对齐，但 5 到 7 被推到 8。

现在神奇的部分。方向 1 执行我们目前看到的所有操作。方向 -1 将偏移量发送到相反方向，以确保值的高端不会碰到上面的东西：

| 偏移量 | 加值 | 新偏移量 |
|:------:|:---------:|:----------:|
|   0    |   -3      |   -4       |
|  -1    |   -4      |   -4       |
|  -2    |   -5      |   -8       |
|  -3    |   -6      |   -8       |
|  -4    |   -7      |   -8       |
|  -5    |   -8      |   -8       |
|  -6    |   -9      |   -12      |
|  -7    |  -10      |   -12      |

## 创建全局结构体变量

所以现在我们可以解析结构体类型，并声明一个指向该类型的全局变量。现在让我们修改代码来为全局变量分配内存空间：

```c
// 生成一个全局符号但不是函数
void cgglobsym(struct symtable *node) {
  int size;

  if (node == NULL) return;
  if (node->stype == S_FUNCTION) return;

  // 获取类型的大小
  size = typesize(node->type, node->ctype);

  // 生成全局标识和标签
  cgdataseg();
  fprintf(Outfile, "\t.globl\t%s\n", node->name);
  fprintf(Outfile, "%s:", node->name);

  // 为这个类型生成空间
  switch (size) {
    case 1: fprintf(Outfile, "\t.byte\t0\n"); break;
    case 4: fprintf(Outfile, "\t.long\t0\n"); break;
    case 8: fprintf(Outfile, "\t.quad\t0\n"); break;
    default:
      for (int i=0; i < size; i++)
        fprintf(Outfile, "\t.byte\t0\n");
  }
}

```

## 尝试这些更改

除了解析结构体、在符号表中存储新节点和为全局结构体变量生成存储空间外，我们没有任何新功能。

我有这个测试程序 `z.c`：

```c
struct fred { int x; char y; long z; };
struct foo { char y; long z; } var1;
struct { int x; };
struct fred var2;
```

它应该创建两个全局变量 `var1` 和 `var2`。我们创建了两个命名结构体类型 `fred` 和 `foo`，以及一个无名结构体。第三个结构体应该导致错误（或至少是警告），因为没有与该结构体关联的变量，所以结构体本身是无用的。

我添加了一些测试代码来打印上述结构体的成员偏移量和结构体大小，结果是：

```
Offset for fred.x is 0
Offset for fred.y is 4
Offset for fred.z is 8
Size of struct fred is 13

Offset for foo.y is 0
Offset for foo.z is 4
Size of struct foo is 9

Offset for struct.x is 0
Size of struct struct is 4
```

最后，当我执行 `./cwj -S z.c` 时，得到这个汇编输出：

```
        .globl  var1
var1:   .byte   0       // 九个字节
        ...

        .globl  var2    // 十三个字节
var2:   .byte   0
        ...
```

## 结论与下一步

在这一部分，我不得不将很多现有代码从只处理 `int type` 改为处理 `int type; struct symtable *ctype` 对。我相信我必须在更多地方这样做。

我们已经添加了结构体定义的解析以及结构体变量的声明，并且可以为全局结构体变量生成空间。目前，我们无法使用我们创建的结构体变量。但这是一个好的开始。我甚至还没有尝试处理局部结构体变量，因为这涉及栈，我相信这会很复杂。

在我们编译器编写旅程的下一部分，我将尝试添加解析 '.' 词法单元的代码，以便我们可以访问结构体变量中的成员。[下一步](../32_Struct_Access_pt1/Readme_zh.md)