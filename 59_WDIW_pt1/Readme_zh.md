# 第59部分: 为什么它不工作, 第一部分

我们已经到了 **WDIW** 阶段: 为什么它不工作? 这是这个阶段的第一部分,
我会找到一些容易发现的 bug 并修复它们。这意味着还有一些
更微妙的 bug 尚未被发现。

## `*argv[i]` 的代码生成错误

我正在使用 `cwj`(Gnu C 编译的版本) 来构建 `cwj0`。`cwj0` 中的汇编
代码是 *我们的* 汇编代码,而不是由 Gnu C 生成的汇编代码。所以,当我们运行 `cwj0` 时,
任何错误都是因为我们的汇编代码不正确。

我注意到的第一个 bug 是 `*argv[i]` 似乎在生成代码时好像它是 `(*argv)[i]`,
即总是 `*argv` 的第 *i* 个字符,而不是 `argv[i]` 处的第一个字符。

我最初认为这是一个解析错误,但事实并非如此。问题在于
我们在解引用之前没有将 `argv[i]` 设置为右值。
我通过使用 `cwj` 和 `cwj0` 转储 AST 树并观察它们之间的差异来解决了这个问题。
我们需要做的是在 `*` 标记 *之后* 将表达式标记为右值。现在这在 `expr.c` 的 `prefix()` 中完成:

```c
static struct ASTnode *prefix(int ptp) {
  struct ASTnode *tree;
  switch (Token.token) {
  ...
  case T_STAR:
    // 获取下一个标记并递归解析
    // 作为前缀表达式。
    // 将其设为右值
    scan(&Token);
    tree = prefix(ptp);
    tree->rvalue= 1;
```

## Extern 也是全局变量

这个会一直困扰我,我确定。我发现了另一个地方
我没有将 extern 符号当作全局符号处理。这是在 `gen.c` 的 `genAST()` 中,
我们在那里生成赋值汇编代码。修复如下:

```c
      // 现在进入赋值代码
      // 我们是赋值给标识符还是通过指针赋值?
      switch (n->right->op) {
        case A_IDENT:
          if (n->right->sym->class == C_GLOBAL ||
              n->right->sym->class == C_EXTERN ||
              n->right->sym->class == C_STATIC)
            return (cgstorglob(leftreg, n->right->sym));
          else
            return (cgstorlocal(leftreg, n->right->sym));
```

## 扫描正在工作

此时,`cwj0` 编译器正在读取源代码输入但没有生成任何输出。
以下是执行此操作的新 `Makefile` 规则:

```
# 尝试做三重测试
triple: cwj1

cwj1: cwj0 $(SRCS) $(HSRCS)
        ./cwj0 -o cwj1 $(SRCS)

cwj0: install $(SRCS) $(HSRCS)
        ./cwj -o cwj0 $(SRCS)
```

所以,`$ make triple` 将使用 Gnu C 构建 `cwj`,然后使用
`cwj` 构建 `cwj0`,最后使用 `cwj0` 构建 `cwj1`。我将在下面讨论这个。

现在,`cwj1` 无法创建,因为没有汇编输出!
问题是,编译器进行到哪里了? 为了找出答案,我
在 `scan()` 的底部添加了一个 `printf()`:

```c
  // 我们找到了一个标记
  t->tokstr = Tstring[t->token];
  printf("Scanned %d\n", t->token);
  return (1);
```

添加这个后,我看到 `cwj` 和 `cwj0` 都扫描了 50,404 个标记,并且生成的标记流是相同的。
因此,我们可以得出结论,直到 `scan()`,一切都在正常工作。

但是,`./cwj0 -S -T cg.c` 的输出没有显示 AST 树。如果我
运行 `gdb cwj0`,在 `dumpAST()` 设置断点,并使用 `-S -T cg.c` 参数运行它,
然后我们在到达 `dumpAST()` 之前就退出了。
我们也没有到达 `function_declaration()`。那么,为什么不工作呢?

啊,我发现了一个对 `0(%rbp)` 的内存访问。这永远不应该发生,
因为所有局部变量都位于帧指针的负位置相对处。在 `cg.c` 的 `cgaddress()` 中,
我们又有了一个遗漏的外部测试。现在我们有了:

```c
int cgaddress(struct symtable *sym) {
  int r = alloc_register();

  if (sym->class == C_GLOBAL ||
      sym->class == C_EXTERN ||
      sym->class == C_STATIC)
    fprintf(Outfile, "\tleaq\t%s(%%rip), %s\n", sym->name, reglist[r]);
  else
    fprintf(Outfile, "\tleaq\t%d(%%rbp), %s\n", sym->st_posn, reglist[r]);
  return (r);
}
```

该死的这些 extern 问题! 好吧,都是我的错,所以我需要承担这个责任。

## 错误的比较

添加上述更改后,我们现在失败了:

```
$ ./cwj0 -S tests/input001.c 
invalid digit in integer literal:e on line 1 of tests/input001.c
```

这是由 `scan.c()` 中 `scanint()` 里的这个循环引起的:

```c
static int scanint(int c) {
  int k;
  ...
  // 将每个字符转换为整数值
  while ((k = chrpos("0123456789abcdef", tolower(c))) >= 0) {
```

发生的事情是 `k=` 赋值不仅将结果存储在内存中,而且还被用作表达式。
在这种情况下 `k` 结果正在被比较,即 `k >= 0`。现在,`k` 是 `int` 类型,
我们将这个存储执行到内存中:

```
    movl    %r10d, -8(%rbp)
```

当 `chrpos()` 返回 `-1` 时,它被截断为 32 位(`0xffffffff`)并存储在 `-8(%rbp)` 中,
即存储在 `k` 中。但是在接下来的比较中:

```
    movslq  -8(%rbp), %r10    # 从 k 中加载值
    movq    $0, %r11          # 加载零
    cmpq    %r11, %r10        # 将 k 的值与零比较
```

我们将 `k` 的 *32 位* 值加载到 `%r10` 中,然后执行 *64 位* 比较。
好吧,作为 64 位值,`0xffffffff` 是一个正数,循环比较保持为真,
所以我们没有在应该的时候离开循环。

我们应该做的是根据比较中操作数的大小使用不同的 `cmp` 指令。
我对 `cg.c` 中的 `cgcompare_and_set()` 做了这个更改:

```c
int cgcompare_and_set(int ASTop, int r1, int r2, int type) {
  int size = cgprimsize(type);
  ...
  switch (size) {
  case 1:
    fprintf(Outfile, "\tcmpb\t%s, %s\n", breglist[r2], breglist[r1]);
    break;
  case 4:
    fprintf(Outfile, "\tcmpl\t%s, %s\n", dreglist[r2], dregist[r1]);
    break;
  default:
    fprintf(Outfile, "\tcmpq\t%s, %s\n", reglist[r2], reglist[r1]);
  }
  ...
}
```

现在使用的是正确的比较指令。有一个类似的函数 `cgcompare_and_jump()`,
在某个阶段我应该重构并合并这两个函数。

# 现在,如此接近!

我们非常接近通过非正式称为 **三重测试** 的测试。在三重测试中,
我们使用现有编译器从源代码构建我们的编译器(第1阶段)。
然后我们使用这个编译器构建自己(第2阶段)。现在,为了证明编译器是自编译的,
我们使用第2阶段编译器来构建自己,得到第3阶段编译器。

我们现在可以:

 + 使用 Gnu C 编译器构建 `cwj`(第1阶段)
 + 使用 `cwj` 编译器构建 `cwj0`(第2阶段)
 + 使用 `cwj0` 编译器构建 `cwj1`(第3阶段)

但是,`cwj0` 和 `cwj1` 的二进制大小不匹配:

```
$ size cwj[01]
   text    data     bss     dec     hex filename
 109636    3028      48  112712   1b848 cwj0
 109476    3028      48  112552   1b7a8 cwj1
```

它们应该 *完全* 匹配。只有当编译器能够连续多次编译自己
并产生相同的结果时,我们才知道它正在正确地自编译。

直到结果完全匹配,第2阶段和第3阶段之间存在一些微妙的行为差异,
因此编译器没有一致地自编译自己。

## 结论与下一步

我没有想到我能到达可以一步构建 `cwj`、`cwj0` 和 `cwj1` 的地步。
我预计在到达这一点之前，我们会有一堆 bug 需要修复。

下一个问题是找出为什么第2阶段和第3阶段编译器的大小不同。
查看 `size` 输出,data 和 bss 部分相同,但两个编译器之间的汇编代码量不同。

在编写编译器旅程的下一部分中,我们将尝试对不同阶段之间的汇编输出
进行逐行比较,并尝试找出导致差异的原因。

> 附注:在旅程的这一部分,我还开始添加一些汇编输出,
  这将允许 `gdb` 查看我们停止位置的源代码行号。它还不工作,
  但如果你看的话,你会看到 `cg.c` 中的一个新函数 `cglinenum()`。
  当我让它工作时,我会写一些关于它的评论。
  [下一步](../60_TripleTest/Readme_zh.md)