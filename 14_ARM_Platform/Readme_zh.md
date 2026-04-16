# 第 14 章：生成 ARM 汇编代码

在本章的编译器编写旅程中，我已经将编译器移植到了
[Raspberry Pi 4](https://en.wikipedia.org/wiki/Raspberry_Pi) 上的 ARM CPU。

在开始本节之前，我应该说明一下：虽然我对 MIPS 汇编语言非常熟悉，
但在开始这段旅程时，我只了解一点 x86-32 汇编语言，
对 x86-64 和 ARM 汇编语言一无所知。

一路上，我做的事情是：用各种 C 编译器将示例 C 程序编译成汇编代码，
看看它们会产生什么样的汇编语言。这就是我在这里为这个编译器编写 ARM
输出所做的工作。

## 主要差异

首先，ARM 是 RISC CPU，而 x86-64 是 CISC CPU。与 x86-64 相比，
ARM 的寻址模式更少。在生成 ARM 汇编代码时还有其他一些有趣的约束。
所以我将从主要差异开始讲述，相同之处留到后面再谈。

### ARM 寄存器

ARM 的寄存器比 x86-64 多得多。不过，我坚持使用四个寄存器来分配：
`r4`、`r5`、`r6` 和 `r7`。我们将看到 `r0` 和 `r3` 会被用于其他用途。

### 全局变量的寻址

在 x86-64 上，我们只需要用这样的行声明一个全局变量：

```
        .comm   i,4,4        # int 变量
        .comm   j,1,1        # char 变量
```

之后，我们就可以轻松地加载和存储这些变量：

```
        movb    %r8b, j(%rip)    # 存储到 j
        movl    %r8d, i(%rip)    # 存储到 i
        movzbl  i(%rip), %r8     # 从 i 加载
        movzbq  j(%rip), %r8     # 从 j 加载
```

在 ARM 上，我们必须在程序的后置码中手动为所有全局变量分配空间：

```
        .comm   i,4,4
        .comm   j,1,1
...
.L2:
        .word i
        .word j
```

要访问这些变量，我们需要将一个寄存器加载为每个变量的地址，
然后从该地址加载第二个寄存器：

```
        ldr     r3, .L2+0
        ldr     r4, [r3]        # 加载 i
        ldr     r3, .L2+4
        ldr     r4, [r3]        # 加载 j
```

对变量的存储操作类似：

```
        mov     r4, #20
        ldr     r3, .L2+4
        strb    r4, [r3]        # i = 20
        mov     r4, #10
        ldr     r3, .L2+0
        str     r4, [r3]        # j = 10
```

现在 `cgpostamble()` 中有了这段代码来生成 .word 表：

```c
  // 输出全局变量
  fprintf(Outfile, ".L2:\n");
  for (int i = 0; i < Globs; i++) {
    if (Gsym[i].stype == S_VARIABLE)
      fprintf(Outfile, "\t.word %s\n", Gsym[i].name);
  }
```

这也意味着我们需要为每个全局变量确定相对于 `.L2` 的偏移量。
遵循 KISS 原则，每次我想将 `r3` 加载为变量地址时，都是手动计算偏移量。
是的，我应该一次计算好每个偏移量并存放在某处；那是以后的事了！

```c
// 确定变量相对于 .L2 标签的偏移量。
// 是的，这是低效的代码。
static void set_var_offset(int id) {
  int offset = 0;
  // 遍历符号表直到 id。
  // 找到 S_VARIABLE 并加 4，直到
  // 我们到达目标变量

  for (int i = 0; i < id; i++) {
    if (Gsym[i].stype == S_VARIABLE)
      offset += 4;
  }
  // 将 r3 加载为此偏移量
  fprintf(Outfile, "\tldr\tr3, .L2+%d\n", offset);
}
```

### 加载整数字面量

加载指令中整数字面量的大小限制为 11 位，
我认为这是一个有符号值。因此，我们不能将大的整数字面量
放入单条指令中。解决办法是将字面量值像变量一样存储在内存中。
所以我保留了一个先前使用过的字面量值列表。在后置码中，
我会在 `.L3` 标签之后输出它们。和变量一样，我遍历这个列表
来确定任何字面量相对于 `.L3` 标签的偏移量：

```c
// 我们必须将大的整数字面量值存储在内存中。
// 保留一个列表，这些值将在后置码中输出
#define MAXINTS 1024
int Intlist[MAXINTS];
static int Intslot = 0;

// 确定大整数字面量相对于 .L3 标签的偏移量。
// 如果整数不在列表中，就添加它。
static void set_int_offset(int val) {
  int offset = -1;

  // 检查它是否已经在列表中
  for (int i = 0; i < Intslot; i++) {
    if (Intlist[i] == val) {
      offset = 4 * i;
      break;
    }
  }

  // 不在列表中，所以添加它
  if (offset == -1) {
    offset = 4 * Intslot;
    if (Intslot == MAXINTS)
      fatal("Out of int slots in set_int_offset()");
    Intlist[Intslot++] = val;
  }
  // 将 r3 加载为此偏移量
  fprintf(Outfile, "\tldr\tr3, .L3+%d\n", offset);
}
```

### 函数前导码

我将给你函数前导码，但我不完全确定每条指令的作用。
对于 `int main(int x)`，它是这样的：

```
  .text
  .globl        main
  .type         main, %function
  main:         push  {fp, lr}          # 保存帧和栈指针
                add   fp, sp, #4        # 将 sp+4 加到栈指针
                sub   sp, sp, #8        # 将栈指针降低 8
                str   r0, [fp, #-8]     # 将参数保存为局部变量？
```

这是返回单个值的函数后置码：

```
                sub   sp, fp, #4        # ???
                pop   {fp, pc}          # 弹出帧和栈指针
```

### 返回 0 或 1 的比较

在 x86-64 上，有一条指令可以根据比较结果将寄存器设置为 0 或 1，
例如 `sete`，但随后我们必须用 `movzbq` 将寄存器的其余部分零填充。
在 ARM 上，我们运行两条单独的指令，根据我们想要的条件
将寄存器设置为真或假的值，例如

```
                moveq r4, #1            # 如果值相等则将 r4 设为 1
                movne r4, #0            # 如果值不相等则将 r4 设为 0
```

## x86-64 和 ARM 汇编输出的比较

我认为主要的差异都已经讨论完了。下面的表格比较了 `cgXXX()` 操作、
该操作的特定类型，以及执行该操作的示例 x86-64 和 ARM 指令序列。

| 操作(类型) | x86-64 版本 | ARM 版本 |
|-----------------|----------------|-------------|
| cgloadint() | movq $12, %r8 | mov r4, #13 |
| cgloadglob(char) | movzbq foo(%rip), %r8 | ldr r3, .L2+#4 |
| | | ldr r4, [r3] |
| cgloadglob(int) | movzbl foo(%rip), %r8 | ldr r3, .L2+#4 |
| | | ldr r4, [r3] |
| cgloadglob(long) | movq foo(%rip), %r8 | ldr r3, .L2+#4 |
| | | ldr r4, [r3] |
| int cgadd() | addq %r8, %r9 | add r4, r4, r5 |
| int cgsub() | subq %r8, %r9 | sub r4, r4, r5 |
| int cgmul() | imulq %r8, %r9 | mul r4, r4, r5 |
| int cgdiv() | movq %r8,%rax | mov r0, r4 |
| | | cqo | mov r1, r5 |
| | | idivq %r8 | bl __aeabi_idiv |
| | | movq %rax,%r8 | mov r4, r0 |
| cgprintint() | movq %r8, %rdi | mov r0, r4 |
| | | call printint | bl printint |
| | | | nop |
| cgcall() | movq %r8, %rdi | mov r0, r4 |
| | | call foo | bl foo |
| | | movq %rax, %r8 | mov r4, r0 |
| cgstorglob(char) | movb %r8, foo(%rip) | ldr r3, .L2+#4 |
| | | | strb r4, [r3] |
| cgstorglob(int) | movl %r8, foo(%rip) | ldr r3, .L2+#4 |
| | | | str r4, [r3] |
| cgstorglob(long) | movq %r8, foo(%rip) | ldr r3, .L2+#4 |
| | | | str r4, [r3] |
| cgcompare_and_set() | cmpq %r8, %r9 | cmp r4, r5 |
| | | sete %r8 | moveq r4, #1 |
| | | movzbq %r8, %r8 | movne r4, #1 |
| cgcompare_and_jump() | cmpq %r8, %r9 | cmp r4, r5 |
| | | je L2 | beq L2 |
| cgreturn(char) | movzbl %r8, %eax | mov r0, r4 |
| | | jmp L2 | b L2 |
| cgreturn(int) | movl %r8, %eax | mov r0, r4 |
| | | jmp L2 | b L2 |
| cgreturn(long) | movq %r8, %rax | mov r0, r4 |
| | | jmp L2 | b L2 |

## 测试 ARM 代码生成器

如果你把本部分中的编译器复制到树莓派 3 或 4 上，你应该能够执行：

```
$ make armtest
cc -o comp1arm -g -Wall cg_arm.c decl.c expr.c gen.c main.c misc.c
      scan.c stmt.c sym.c tree.c types.c
cp comp1arm comp1
(cd tests; chmod +x runtests; ./runtests)
input01: OK
input02: OK
input03: OK
input04: OK
input05: OK
input06: OK
input07: OK
input08: OK
input09: OK
input10: OK
input11: OK
input12: OK
input13: OK
input14: OK

$ make armtest14
./comp1 tests/input14
cc -o out out.s lib/printint.c
./out
10
20
30
```

## 结论和下一步

让 ARM 版本的代码生成器 `cg_arm.c` 正确编译所有测试输入，
确实花了我不少脑筋。这件事大部分是直截了当的，
我只是不熟悉这个架构和指令集。

将编译器移植到一个有 3 或 4 个寄存器、2 种左右数据大小
和一个栈（以及栈帧）的平台上，应该是相对容易的。
随着我们继续前进，我将努力保持 `cg.c` 和 `cg_arm.c`
的功能同步。

在编译器编写旅程的下一部分，我们将把 `char`
指针以及 '*' 和 '&' 一元运算符添加到语言中。
[下一步](../15_Pointers_pt1/Readme_zh.md)

(文件结束 - 共 288 行)