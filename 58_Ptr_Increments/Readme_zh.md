# 第58部分：修复指针增量/减量

在我们编译器编写旅程的最后一部分，我提到指针增量
和减量存在问题。让我们看看问题是什么，以及我如何修复它。

在使用 AST 操作 A_ADD、A_SUBTRACT、A_ASPLUS 和 A_ASMINUS 时，
当一个操作数是指针，另一个是整数类型时，我们需要根据指针
所指向的类型的大小来缩放整数值。在 `types.c` 的 `modify_type()` 中：

```c
  // 我们只能在加法和减法操作时进行缩放
  if (op == A_ADD || op == A_SUBTRACT ||
      op == A_ASPLUS || op == A_ASMINUS) {

    // 左侧是 int 类型，右侧是指针类型且原始类型
    // 的大小 >1：缩放左侧
    if (inttype(ltype) && ptrtype(rtype)) {
      rsize = genprimsize(value_at(rtype));
      if (rsize > 1)
        return (mkastunary(A_SCALE, rtype, rctype, tree, NULL, rsize));
      else
        return (tree);          // 大小为1，无需缩放
    }
  }
```

但是，当我们使用 `++` 或 `--` 时，无论是作为
前置增量/减量还是后置增量/减量运算符，都不会进行这种缩放。在这里，
我们只是将 A_PREINC、A_PREDEC、A_POSTINC 或 A_POSTDEC AST 节点
附加到我们正在操作的 AST 树上，然后让代码生成器来处理这种情况。

到目前为止，当我们调用 `cg.c` 中的 `cgloadglob()` 或
`cgloadlocal()` 来加载全局变量或局部变量的值时，这个问题会得到解决。
例如：

```c
int cgloadglob(struct symtable *sym, int op) {
  ...
  if (cgprimsize(sym->type) == 8) {
    if (op == A_PREINC)
      fprintf(Outfile, "\tincq\t%s(%%rip)\n", sym->name);
    ...
    fprintf(Outfile, "\tmovq\t%s(%%rip), %s\n", sym->name, reglist[r]);

    if (op == A_POSTINC)
      fprintf(Outfile, "\tincq\t%s(%%rip)\n", sym->name);
  }
  ...
}
```

请注意，`incq` 递增1。如果我们要递增的变量是整数类型，这没问题，
但它无法处理指针类型的变量。

此外，`cgloadglob()` 和 `cgloadlocal()` 函数非常相似。
它们的区别在于我们用来访问变量的指令：是固定位置，还是
相对于栈帧的位置。

## 修复问题

有一段时间我认为可以让解析器构建一个类似于 `modify_type()` 的 AST 树，
但我放弃了。感谢上帝。我决定，既然 `++` 和 `--` 已经在 `cgloadglob()` 中完成了，
我应该从这里入手解决这个问题。

做到一半时，我意识到我可以把 `cgloadglob()` 和 `cgloadlocal()` 合并成一个函数。
让我们分阶段来看这个解决方案。

```c
// 从变量加载值到寄存器。
// 返回寄存器的编号。如果操作是前置或后置
// 增量/减量，也要执行此操作。
int cgloadvar(struct symtable *sym, int op) {
  int r, postreg, offset=1;

  // 获取一个新寄存器
  r = alloc_register();

  // 如果符号是指针，使用它所指向的
  // 类型的大小作为任何增量或减量。如果不是，则为1。
  if (ptrtype(sym->type))
    offset= typesize(value_at(sym->type), sym->ctype);
```

我们首先假设将进行 +1 的增量。但是，
一旦我们意识到可能要递增指针，我们就将其更改
为它所指向的类型的大小。

```c
  // 对于减量，取偏移量的负值
  if (op==A_PREDEC || op==A_POSTDEC)
    offset= -offset;
```

现在，如果我们要进行减量，`offset` 就是负数。

```c
  // 如果我们有前置操作
  if (op==A_PREINC || op==A_PREDEC) {
    // 加载符号的地址
    if (sym->class == C_LOCAL || sym->class == C_PARAM)
      fprintf(Outfile, "\tleaq\t%d(%%rbp), %s\n", sym->st_posn, reglist[r]);
    else
      fprintf(Outfile, "\tleaq\t%s(%%rip), %s\n", sym->name, reglist[r]);
```

这正是我们的算法与旧代码不同的地方。旧代码使用
`incq` 指令，但那将变量更改限制为正好1。
现在我们在寄存器中有了变量的地址...

```c
    // 并更改该地址处的值
    switch (sym->size) {
      case 1: fprintf(Outfile, "\taddb\t$%d,(%s)\n", offset, reglist[r]); break;
      case 4: fprintf(Outfile, "\taddl\t$%d,(%s)\n", offset, reglist[r]); break;
      case 8: fprintf(Outfile, "\taddq\t$%d,(%s)\n", offset, reglist[r]); break;
    }
  }
```

我们可以将偏移量加到变量上，使用寄存器作为
指向变量的指针。我们必须根据变量的大小使用不同的指令。

我们已经完成了任何前置增量或前置减量操作。现在我们可以将
变量的值加载到寄存器中：

```c
  // 现在将输出寄存器的值加载进来
  if (sym->class == C_LOCAL || sym->class == C_PARAM) {
    switch (sym->size) {
      case 1: fprintf(Outfile, "\tmovzbq\t%d(%%rbp), %s\n", sym->st_posn, reglist[r]); break;
      case 4: fprintf(Outfile, "\tmovslq\t%d(%%rbp), %s\n", sym->st_posn, reglist[r]); break;
      case 8: fprintf(Outfile, "\tmovq\t%d(%%rbp), %s\n", sym->st_posn, reglist[r]);
    }
  } else {
    switch (sym->size) {
      case 1: fprintf(Outfile, "\tmovzbq\t%s(%%rip), %s\n", sym->name, reglist[r]); break;
      case 4: fprintf(Outfile, "\tmovslq\t%s(%%rip), %s\n", sym->name, reglist[r]); break;
      case 8: fprintf(Outfile, "\tmovq\t%s(%%rip), %s\n", sym->name, reglist[r]);
    }
  }
```

根据符号是局部的还是全局的，
我们从命名位置或从相对于
帧指针的位置加载。我们根据符号的大小
选择一条指令来零扩展结果。

值安全地保存在寄存器 `r` 中。但是现在我们需要做任何后置增量
或后置减量。我们可以重用预操作代码，但我们需要一个新的
寄存器：

```c
  // 如果我们有后置操作，获取一个新寄存器
  if (op==A_POSTINC || op==A_POSTDEC) {
    postreg = alloc_register();

    // 与之前相同的代码，但使用 postreg

    // 释放寄存器
    free_register(postreg);
  }

  // 返回带有值的寄存器
  return(r);
}
```

所以 `cgloadvar()` 的代码与旧代码一样复杂，但现在
它可以处理指针增量。`tests/input145.c` 测试程序
验证了这个新代码的工作：

```c
int list[]= {3, 5, 7, 9, 11, 13, 15};
int *lptr;

int main() {
  lptr= list;
  printf("%d\n", *lptr);
  lptr= lptr + 1; printf("%d\n", *lptr);
  lptr += 1; printf("%d\n", *lptr);
  lptr += 1; printf("%d\n", *lptr);
  lptr -= 1; printf("%d\n", *lptr);
  lptr++   ; printf("%d\n", *lptr);
  lptr--   ; printf("%d\n", *lptr);
  ++lptr   ; printf("%d\n", *lptr);
  --lptr   ; printf("%d\n", *lptr);
}
```

## 我怎么会遗漏取模？

修复了这个问题后，我回去让编译器源代码编译自己，
惊讶地发现取模运算符 `%` 和 `%=` 缺失了。
我不知道为什么之前没有加入它们。

### 新的标记和 AST 运算符

现在向编译器添加新运算符是很棘手的，因为我们必须在
多个地方同步更改。让我们看看在哪里。在 `defs.h` 中
我们需要添加标记：

```c
// 标记类型
enum {
  T_EOF,

  // 二元运算符
  T_ASSIGN, T_ASPLUS, T_ASMINUS,
  T_ASSTAR, T_ASSLASH, T_ASMOD,
  T_QUESTION, T_LOGOR, T_LOGAND,
  T_OR, T_XOR, T_AMPER,
  T_EQ, T_NE,
  T_LT, T_GT, T_LE, T_GE,
  T_LSHIFT, T_RSHIFT,
  T_PLUS, T_MINUS, T_STAR, T_SLASH, T_MOD,
  ...
};
```

T_ASMOD 和 T_MOD 是新的标记。现在我们需要创建匹配的 AST 操作：

```c
 // AST 节点类型。前几个与
// 相关的标记对应
enum {
  A_ASSIGN = 1, A_ASPLUS, A_ASMINUS, A_ASSTAR,                  //  1
  A_ASSLASH, A_ASMOD, A_TERNARY, A_LOGOR,                       //  5
  A_LOGAND, A_OR, A_XOR, A_AND, A_EQ, A_NE, A_LT,               //  9
  A_GT, A_LE, A_GE, A_LSHIFT, A_RSHIFT,                         // 16
  A_ADD, A_SUBTRACT, A_MULTIPLY, A_DIVIDE, A_MOD,               // 21
  ...
};
```

现在我们需要添加扫描器更改来扫描这些标记。我不会展示
代码，但我会展示 `scan.c` 中标记字符串表的更改：

```c
// 标记字符串列表，用于调试目的
char *Tstring[] = {
  "EOF", "=", "+=", "-=", "*=", "/=", "%=",
  "?", "||", "&&", "|", "^", "&",
  "==", "!=", ",", ">", "<=", ">=", "<<", ">>",
  "+", "-", "*", "/", "%",
  ...
};
```

### 运算符优先级

现在我们需要在 `expr.c` 中设置运算符的优先级。T_SLASH 曾经是
最高运算符，但已被 T_MOD 取代：

```c
// 将二元运算符标记转换为二元 AST 操作。
// 我们依赖于从标记到 AST 操作的1:1映射
static int binastop(int tokentype) {
  if (tokentype > T_EOF && tokentype <= T_MOD)
    return (tokentype);
  fatals("Syntax error, token", Tstring[tokentype]);
  return (0);                   // 保持 -Wall 愉快
}

// 每个标记的运算符优先级。必须
// 与 defs.h 中标记的顺序相匹配
static int OpPrec[] = {
  0, 10, 10,                    // T_EOF, T_ASSIGN, T_ASPLUS,
  10, 10,                       // T_ASMINUS, T_ASSTAR,
  10, 10,                       // T_ASSLASH, T_ASMOD,
  15,                           // T_QUESTION,
  20, 30,                       // T_LOGOR, T_LOGAND
  40, 50, 60,                   // T_OR, T_XOR, T_AMPER 
  70, 70,                       // T_EQ, T_NE
  80, 80, 80, 80,               // T_LT, T_GT, T_LE, T_GE
  90, 90,                       // T_LSHIFT, T_RSHIFT
  100, 100,                     // T_PLUS, T_MINUS
  110, 110, 110                 // T_STAR, T_SLASH, T_MOD
};

// 检查我们是否有二元运算符并
// 返回其优先级。
static int op_precedence(int tokentype) {
  int prec;
  if (tokentype > T_MOD)
    fatals("Token with no precedence in op_precedence:", Tstring[tokentype]);
  prec = OpPrec[tokentype];
  if (prec == 0)
    fatals("Syntax error, token", Tstring[tokentype]);
  return (prec);
}
```

### 代码生成

我们已经有了一个 `cgdiv()` 函数来生成 x86-64 指令
进行除法。查看 `idiv` 指令的手册：

> idivq S: 有符号除法 `%rdx:%rax` 除以 S。
> 商存储在 `%rax` 中。余数存储在 `%rdx` 中。

因此我们可以修改 `cgdiv()` 来接受正在执行的 AST 操作，
它可以同时进行除法和取模。`cg.c` 中的新函数是：

```c
// 第一个寄存器除以第二个寄存器并
// 返回带有结果的寄存器编号
int cgdivmod(int r1, int r2, int op) {
  fprintf(Outfile, "\tmovq\t%s,%%rax\n", reglist[r1]);
  fprintf(Outfile, "\tcqo\n");
  fprintf(Outfile, "\tidivq\t%s\n", reglist[r2]);
  if (op== A_DIVIDE)
    fprintf(Outfile, "\tmovq\t%%rax,%s\n", reglist[r1]);
  else
    fprintf(Outfile, "\tmovq\t%%rdx,%s\n", reglist[r1]);
  free_register(r2);
  return (r1);
}
```

`tests/input147.c` 确认上述更改有效：

```c
#include <stdio.h>

int a;

int main() {
  printf("%d\n", 24 % 9);
  printf("%d\n", 31 % 11);
  a= 24; a %= 9; printf("%d\n",a);
  a= 31; a %= 11; printf("%d\n",a);
  return(0);
}
```

## 为什么它不能链接

我们现在处于编译器可以解析其自身的每个源文件的地步。
但是当我尝试链接它们时，我收到关于缺少 `L0` 标记的警告。

经过一番调查，我发现我没有正确传播
`gen.c` 中 `genIF()` 内循环和 switch 的结束标签。
修复在第49行：

```c
// 为 IF 语句生成代码
// 以及可选的 ELSE 子句。
static int genIF(struct ASTnode *n, int looptoplabel, int loopendlabel) {
  ...
  // 可选的 ELSE 子句：生成
  // 假的复合语句和
  // 结束标签
  if (n->right) {
    genAST(n->right, NOLABEL, NOLABEL, loopendlabel, n->op);
    genfreeregs(NOREG);
    cglabel(Lend);
  }
  ...
}
```

现在 `loopendlabel` 正在被传播，我可以在 shell 脚本中执行此操作
（我称之为 `memake`）：

```
#!/bin/sh
make install

rm *.s *.o

for i in cg.c decl.c expr.c gen.c main.c misc.c \
        opt.c scan.c stmt.c sym.c tree.c types.c
do echo "./cwj -c $i"; ./cwj -c $i ; ./cwj -S $i
done

cc -o cwj0 cg.o decl.o expr.o gen.o main.o misc.o \
        opt.o scan.o stmt.o sym.o tree.o types.o
```

我们最终得到了一个二进制文件 `cwj0`，这是编译器
编译自身的结果。

```
$ size cwj0
   text    data     bss     dec     hex filename
 106540    3008      48  109596   1ac1c cwj0

$ file cwj0
cwj0: ELF 64-bit LSB shared object, x86-64, version 1 (SYSV), dynamically
      linked, interpreter /lib64/l, for GNU/Linux 3.2.0, not stripped
```

## 结论与下一步

对于指针增量问题，我肯定花了很多时间思考并寻找
几种可能的替代解决方案。我确实尝试过构建带有 A_SCALE 的新 AST 树，
做到一半就放弃了。然后我把所有东西都扔掉，
转向 `cgloadvar()` 的更改。那更简洁。

取模运算符在理论上很简单添加（理论上），
但实际上却很难同步。有可能
有一些重构的空间，使同步更加容易。

然后，在尝试链接编译器自己生成的所有目标文件时，
我发现我们没有正确传播循环/switch 的结束标签。

我们现在达到了编译器可以解析其每个源文件、为它们生成
汇编代码并可以链接它们的程度。我们已经到达了旅程的最后阶段，
这可能将是最痛苦的阶段：**WDIW** 阶段：为什么不工作？

在这里，我们没有调试器，我们将不得不查看大量
汇编输出。我们将不得不单步执行汇编并查看
寄存器值。

在我们编译器编写旅程的下一部分，我将开始
**WDIW** 阶段。我们需要一些策略来使我们的工作
更有效。[下一步](../59_WDIW_pt1/Readme_zh.md)