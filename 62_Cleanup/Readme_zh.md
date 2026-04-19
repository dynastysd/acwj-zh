# 第62部分：代码清理

本版本的编译器与第60部分基本相同。
我利用这一部分来修复注释、修复bug、做一点代码清理、
重命名一些函数和变量等。

## 一些小的错误修复

由于我计划对编译器进行一些修改，我需要能够将结构体嵌套在结构体中。
因此，我应该能够做到：

```c
   printf("%d\n", thing.member1.age_in_years);
```

其中 `thing` 是一个结构体，但它有一个 `member1`，其类型也是结构体。
要做到这一点，我们需要找到 `member1` 相对于 `thing` 基址的偏移量，
然后找到 `age_in_years` 相对于上一个偏移量的偏移量。

然而，现有的代码期望 '.' 令牌左侧的内容是一个变量，该变量有符号表条目，
因此在内存中有固定位置。我们需要修复这一点，以处理 '.' 令牌左侧是已计算偏移量的情况。

幸运的是，这很容易做到。我们不需要改变解析器代码，
但让我们看看已有的代码。在 `expr.c` 的 `member_access()` 中：

```c
  // 检查左侧AST树是否是结构体或联合体。
  // 如果是，将其从 A_IDENT 改为 A_ADDR，以便
  // 获取基地址，而不是该地址处的值。
  if (!withpointer) {
    if (left->type == P_STRUCT || left->type == P_UNION)
      left->op = A_ADDR;
```

我们将左侧AST树标记为 A_ADDR（而不是 A_IDENT），
表示我们需要它的基地址，而不是该地址处的值。

现在我们需要修复代码生成。当我们遇到 A_ADDR AST节点时，
我们可能有一个需要获取其地址的变量（例如 `thing.member1` 中的 `thing`），
或者我们的子树的子节点已经有预先计算好的偏移量（例如 `member1.age_in_years` 中 `member1` 的偏移量）。
因此，在 `gen.c` 的 `genAST()` 中，我们执行：

```c
  case A_ADDR:
    // 如果我们有符号，获取其地址。否则，
    // 左侧寄存器已经有地址，因为它是成员访问
    if (n->sym != NULL)
      return (cgaddress(n->sym));
    else
      return (leftreg);
```

应该就这些了，但我们还有一点需要修复。
计算类型对齐的代码没有处理结构体内部嵌套结构体的情况，
只处理了结构体内部的标量类型。因此，我修改了 `cg.c` 中的 `cgalign()` 如下：

```c
// 给定一个标量类型、一个现有的内存偏移量
// （尚未分配给任何对象）和一个方向（1向上，-1向下），
// 计算并返回适合该标量类型的内存偏移量。
// 这可能是原始偏移量，也可能是原始偏移量的上方或下方
int cgalign(int type, int offset, int direction) {
  int alignment;

  // 在 x86-64 上我们不需要这样做，但让我们
  // 在任何偏移量上对齐 char，在 4 字节对齐上对齐 int/指针
  switch (type) {
  case P_CHAR:
    break;
  default:
    // 将我们现在拥有的任何内容对齐到 4 字节对齐。
    // 我把通用代码放在这里，以便在其他地方重用。
    alignment = 4;
    offset = (offset + direction * (alignment - 1)) & ~(alignment - 1);
  }
  return (offset);
}
```

除了 P_CHAR 之外的所有类型都对齐到 4 字节，包括结构体和联合体。

## 已知但未修复的 bug

现在这个 GitHub 仓库已经上线并获得了一些关注，
几个人报告了 bug 和不完善的特性。
开放和关闭的问题列表在这里：
![https://github.com/DoctorWkt/acwj/issues](https://github.com/DoctorWkt/acwj/issues)。
如果你发现任何 bug 或不完善的特性，欢迎报告。
但是，我不能保证会有时间修复它们！

## 下一步

我一直在阅读有关寄存器分配的资料，我认为我会在编译器中加入
线性扫描寄存器分配机制。但是，要做到这一点，
我需要添加一个中间表示阶段。
这将是接下来几个阶段的目标，但到目前为止，
我还没有做任何具体的事情。[下一步](../63_QBE/Readme_zh.md)