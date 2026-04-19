# 第64部分：在8位CPU上的自编译

我又一次踏上了编译器编写之旅的篇章。这一次，
目标是让编译器在1980年代的8位CPU上自己编译自己。
这是一项有趣、有时有趣、有时痛苦的任务。以下是
我所做工作的总结。

对于CPU，我选择了
[Motorola 6809](https://en.wikipedia.org/wiki/Motorola_6809)。这可能是
1980年代最复杂的8位CPU之一，拥有大量有用的寻址模式，而且重要的是，有一个有用的堆栈指针。

编写6809编译器困难的原因在于地址空间
限制。像许多8位CPU一样，只有64K的内存
（是的，65,536 _字节_！），在大多数复古6809系统中，很大一部分被ROM占据。

我之所以朝这个方向努力，是因为在2023年，我决定尝试使用6809作为CPU构建一台
单板计算机（SBC）。特别是，
我想要一台至少拥有半兆字节内存、
类似磁盘的存储设备和类Unix操作系统的机器。

结果是[MMU09 SBC](https://github.com/DoctorWkt/MMU09)。
该项目是半不完整的；它确实有类Unix系统，
确实做多任务，但没有抢先式多任务。
每个进程获得63.5K的可用地地址空间（即RAM）。

当我从事MMU09工作时，我需要找到一个合适的C编译器来
为操作系统、库和应用程序编译代码。
我开始使用[CMOC](http://perso.b2b2c.ca/~sarrazip/dev/cmoc.html)，但
最终切换到了[vbcc](http://www.compilers.de/vbcc.html)。
在此过程中，我发现了Alan Cox的
[Fuzix编译器工具包](https://github.com/EtchedPixels/Fuzix-Compiler-Kit)，
这是一个面向许多8位和16位CPU的C编译器开发中版本。

所有这些让我思考：是否可以让C编译器
在6809上运行，而不仅仅是从更强大的系统进行交叉编译？
我认为Fuzix编译器工具包可能是一个竞争者，但是，不，它只是
太大了，无法安装在6809本身上。

所以我们面临的问题是/目标："acwj"编译器能否被修改为
适合并在6809平台上运行？

## 6809 CPU

让我们从编译器编写者的角度了解6809 CPU。
我已经提到了64K地址空间限制：这将需要"acwj"编译器被完全重构以适应。现在让我们
看看6809的架构。

![](docs/6809_Internal_Registers.png)

知识共享CC0许可证，
[Wikipedia](https://commons.wikimedia.org/wiki/File:6809_Internal_Registers.svg)

对于8位CPU，6809有相当多的寄存器。当然，它不像
x64或具有一堆通用寄存器的RISC CPU。有一个
单一的16位`D`寄存器，我们可以在其上执行逻辑和算术
操作。它也可以作为两个8位寄存器`A`和`B`访问，其中
`B`是`D`寄存器中的最低有效字节。

在执行逻辑和算术操作时，第二个操作数是
通过某种寻址模式访问的内存位置，或字面值。
操作的结果放回`D`寄存器：因此，它
_累积_操作的结果。

要访问内存，有大量寻址模式可以这样做。事实上，
比编译器需要的要多得多！我们有索引
寄存器`X`和`Y`，例如，当我们知道基地址且`X`保存元素的索引时，
用于访问数组中的元素。
我们还可以使用带符号常量和堆栈指针
`S`作为索引来访问内存；这允许我们将`S`视为
[帧指针](https://en.wikipedia.org/wiki/Call_stack#FRAME-POINTER)。
我们可以在帧指针下方找到函数的局部变量，
在帧指针上方找到函数参数。

让我们看一些例子来使以上内容更清楚：

```
    ldd #2         # 用常数2加载D
    ldd 2          # 从地址2和3加载D（16位）
    ldd _x         # 从称为_x的位置加载D
    ldd 2,s        # 从堆栈上的参数加载D
    std -20,s      # 将D存储到堆栈上的局部变量
    leax 8,s       # 获取S+8的（有效）地址
                   # 并将其存储到X寄存器
    ldd 4,x        # 现在将其用作int数组的指针
                   # 并加载索引2处的值 - 记住
                   # D是16位（2字节），所以4字节
                   # 是两个16位"字"
    addd -6,s      # 将我们刚获取的int加到局部
                   # 变量并保存到D寄存器
```

更多详情，我建议您浏览
[6809数据表](docs/6809Data.pdf)。
第5-6页涵盖寄存器，第16-18页涵盖寻址模式，
第25-27页列出可用指令。

回到针对"acwj"编译器的6809目标。有大量
寻址模式是很好的。我们可以处理8位值和16位
值，但没有32位寄存器。好吧，我们可以以某种方式解决。

但除了64K地址空间，
最大的问题是"acwj"编译器是为具有两个
或三个操作数指令以及大量可用寄存器的架构编写的，例如

```
   load R1, _x		# 将_x和_y带入寄存器
   load R2, _y
   add  R3, R1, R2	# R3= R1 + R2
   save R3, _z		# 将结果存储到_z
```

6809通常将`D`寄存器作为一个指令操作数，内存或字面值作为另一个操作数；结果总是保存在`D`寄存器中。

## 保留QBE后端

我也想保留编译器中现有的QBE后端。我知道
当我修改编译器时这将是非常宝贵的 - 我
可以使用QBE和6809后端运行测试并比较
结果。而且我总是可以尝试使用QBE后端进行三重测试来对编译器进行压力测试。

所以现在的完整目标是：我能否获取编译器解析器生成的抽象语法树（AST），并使用它为两种完全不同的架构生成汇编代码：QBE（RISC类，三操作数指令）
和6809（只有一个寄存器，隐式源和目标的二操作数指令）？我能否让编译器在这两种架构上都实现自编译？

这将是一段有趣的旅程！

## 代码生成器契约

现在我们将有兩個不同的后端，我们需要在架构无关的代码生成器部分
（[gen.c](gen.c)）和每个架构相关部分之间建立"契约"
或API。这现在是[gen.h](gen.h)中定义的函数列表。

基本API与之前相同。我们传入一个或多个"寄存器号"
并返回一个保存结果的寄存器号。这一次的一个区别是
许多函数接收操作数的架构无关的`type`；
这在[defs.h](defs.h)中定义：

```
// 原始类型。低4位是一个整数
// 表示间接级别，
// 例如0=无指针，1=指针，2=指针的指针等。
enum {
  P_NONE, P_VOID = 16, P_CHAR = 32, P_INT = 48, P_LONG = 64,
  P_STRUCT=80, P_UNION=96
};
```

如果你看[cgqbe.c](cgqbe.c)中的QBE代码生成器，它与这个"acwj"旅程中上一章基本相同。需要注意的一件事是，我将一些函数抽象到了一个单独的文件[targqbe.c](targqbe.c)，因为解析器和代码生成器现在位于不同的程序中。

现在让我们看看6809代码生成器。

## 6809特定类型和D寄存器

最大的问题是如何在6809上处理多个寄存器的概念。
我将在下一节中介绍，但我需要先绕一个小弯。

每个架构相关的代码生成器都会获得操作数类型：P_CHAR、P_INT等。对于6809代码生成器，我们将这些转换为6809特定类型，如[cg6809.c](cg6809.c)中所定义：

```
#define PR_CHAR         1	// 大小1字节
#define PR_INT          2	// 大小2字节
#define PR_POINTER      3	// 大小2字节
#define PR_LONG         4	// 大小4字节
```

在这个文件中，你会看到很多这样的代码：

```
  int primtype= cgprimtype(type);

  switch(primtype) {
    case PR_CHAR:
      // 生成char操作的代码
    case PR_INT:
    case PR_POINTER:
      // 生成int操作的代码
    case PR_LONG:
      // 生成long操作的代码
  }
```

尽管`PR_INT`和`PR_POINTER`大小相同且生成相同的
代码，但我将它们分开保留。这是因为指针实际上是
无符号的，而`int`是有符号的。以后，如果我要向编译器添加有符号和无符号类型，
我在6809后端已经有了一个良好的开端。

## 当没有寄存器时如何使用寄存器？

现在，回到主要问题：
如果代码生成器API使用寄存器号，当这个CPU只有一个累加器`D`时，我们如何编写6809后端？

当我开始编写6809后端时，我从一个称为`R0、R1、R2`等的4字节内存位置集开始。你仍然可以在[lib/6809/crt0.s](lib/6809/crt0.s)中看到它们：

```
R0:     .word   0
        .word   0
R1:     .word   0
        .word   0
...
```

这帮助我启动并运行了6809后端，但生成的代码很糟糕。例如，这个C代码：

```
  int x, y, z;
  ...
  z= x + y;
```

被翻译成：

```
  ldd  _x
  std  R0
  ldd  _y
  std  R1
  ldd  R0
  addd R1
  std  R2
  ldd  R2
  std  _z
```

然后我意识到：6809非常"地址"导向：有大量寻址模式，大多数指令都有地址（或字面值）作为操作数。所以，让我们保留一个"_位置"列表。

位置是以下之一，在[cg6809.c](cg6809.c)中定义：

```
enum {
  L_FREE,               // 此位置未使用
  L_SYMBOL,             // 具有可选偏移量的全局符号
  L_LOCAL,              // 局部变量或参数
  L_CONST,              // 整数字面值
  L_LABEL,              // 标签
  L_SYMADDR,            // 符号、本地或参数的地址
  L_TEMP,               // 临时存储的值：R0、R1、R2 ...
  L_DREG                // D位置，即B、D或Y/D
};
```

我们保留一个空闲或使用中位置的列表，它们有这个结构：

```
struct Location {
  int type;             // L_值之一
  char *name;           // 符号名称
  long intval;          // 偏移量、常量值、标签id等
  int primtype;         // 6809原始类型
};
```

示例：

 - 全局`int x`将是L_SYMBOL，`name`设为"x"，
   `primtype`设为PR_INT。
 - 局部`char *ptr`将是L_LOCAL，没有名称，但
   `intval`将设置为堆栈帧中的偏移量，例如-8。
   `primtype`将为PR_POINTER。
   如果是函数参数，偏移量将为正。
 - 如果操作数类似于`&x`（`x`的地址），
   则位置将是L_SYMADDR，`name`设为"x"。
 - 像456这样的字面值将是L_CONST，`intval`
   设为456，`primtype`设为PR_INT。
 - 最后，如果操作数已经在`D`寄存器中，我们
   将有一个具有特定PR_类型的L_DREG位置。

所以，位置代替寄存器。我们有一个16个位置的数组：

```
#define NUMFREELOCNS 16
static struct Location Locn[NUMFREELOCNS];
```

让我们看看在6809上生成加法的代码。

```
// 将两个位置相加并返回
// 包含结果的位置编号
int cgadd(int l1, int l2, int type) {
  int primtype= cgprimtype(type);

  load_d(l1);

  switch(primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\taddb "); printlocation(l2, 0, 'b'); break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\taddd "); printlocation(l2, 0, 'd'); break;
      break;
    case PR_LONG:
      fprintf(Outfile, "\taddd "); printlocation(l2, 2, 'd');
      fprintf(Outfile, "\texg y,d\n");
      fprintf(Outfile, "\tadcb "); printlocation(l2, 1, 'f');
      fprintf(Outfile, "\tadca "); printlocation(l2, 0, 'e');
      fprintf(Outfile, "\texg y,d\n");
  }
  cgfreelocn(l2);
  Locn[l1].type= L_DREG;
  d_holds= l1;
  return(l1);
}
```

我们首先从通用操作数类型确定6809类型。
然后我们将第一个位置`l1`的值加载到`D`寄存器中。
然后，根据6809类型，输出正确的指令集
并在每个指令后打印第二个位置`l2`。

加法完成后，我们释放第二个位置并标记
第一个位置`l1`现在是`D`寄存器。我们还记录
`D`现在正在使用中，然后返回。

使用位置的概念，C代码`z= x + y`现在被翻译成：

```
  ldd  _x	; 即load_x(l1);
  addd _y	; 即fprintf(Outfile, "\taddd "); printlocation(l2, 2, 'd');
  std  _z	; 在另一个函数cgstorglob()中执行
```

## 处理Long

6809有8位和16位操作，但编译器需要
合成32位long的操作。而且没有32位寄存器。

> 题外话：6809是大端的。如果`long`值0x12345678
> 存储在名为`foo`的`long`变量中，那么0x12将在
> `foo`偏移0处，0x34在偏移1处，0x56在偏移
> 2处，0x78在偏移3处。

我借用了Alan Cox在[Fuzix编译器工具包](https://github.com/EtchedPixels/Fuzix-Compiler-Kit)中使用的long处理方法。
我们使用`Y`寄存器保存32位long的高半部分，`D`寄存器保存低半部分：

![](docs/long_regs.png)

6809已经将`D`寄存器的低半部分称为`B`寄存器，用于8位操作。还有`A`寄存器，它是`D`寄存器的上半部分。

看上面的`cgadd()`代码，你可以看到，如果`x`、`y`
和`z`是`long`而不是`int`，我们会生成：

```
  ldd  _x+2	; 获取_x的低半部分到D
  ldy  _x+0	; 获取_x的高半部分到Y
  addd _y+2	; 将_y的低半部分加到D
  exg  y,d	; 交换Y和D寄存器
  adcb _y+1	; 将_y偏移1加到B寄存器并带进位
  adca _y+0	; 将_y偏移0加到A寄存器并带进位
  exg  y,d	; 再次交换Y和D寄存器
  std  _z+2	; 最后将D（低半部分）存储到_z偏移2
  sty  _z	; Y（高半部分）存储到_z偏移0
```

有点痛苦：有16位`addd`操作不带进位，但没有带进位的16位加法操作。相反，我们需要执行两个8位加法带进位来获得相同的结果。

这种与可用6809操作的不一致性使得6809代码生成器代码在某些地方非常丑陋。

# printlocation()

处理位置的大量工作由`printlocation()`函数执行。让我们分几个阶段来分解它。

```
// 打印一个位置。对于内存位置
// 使用偏移量。对于常量，使用
// 寄存器字母来确定使用哪部分。
static void printlocation(int l, int offset, char rletter) {
  int intval;

  if (Locn[l].type == L_FREE)
    fatald("Error trying to print location", l);

  switch(Locn[l].type) {
    case L_SYMBOL: fprintf(Outfile, "_%s+%d\n", Locn[l].name, offset); break;
    case L_LOCAL: fprintf(Outfile, "%ld,s\n",
                Locn[l].intval + offset + sp_adjust);
        break;
    case L_LABEL: fprintf(Outfile, "#L%ld\n", Locn[l].intval); break;
    case L_SYMADDR: fprintf(Outfile, "#_%s\n", Locn[l].name); break;
    case L_TEMP: fprintf(Outfile, "R%ld+%d\n", Locn[l].intval, offset);
        break;
    ...
```

如果位置是L_FREE，那么尝试打印它就没有意义了！
对于符号，我们打印符号名称，后跟偏移量。
这样，对于`int`和`long`，我们可以访问构成符号的所有2或4个字节：`_x+0`、`_x+1`、`_x+2`、`_x+3`。

对于局部变量和函数参数，我们打印堆栈帧中的位置（即`intval`加上偏移量）。所以如果局部`long`变量`fred`在堆栈上的位置-12处，我们可以使用`-12,s`、`-11,s`、`-10,s`、`-9,s`访问所有四个字节。

是的，这里有叫做`sp_adjust`的东西。我马上会谈到它！

现在，L_TEMP位置。与所有以前版本的编译器一样，
有时我们必须将中间结果存储在某个地方，例如

```
  int z= (a + b) * (c - d) / (e + f) * (g + h - i) * (q - 3);
```

我们在括号中有五个需要先处理的中间结果，然后才能进行乘法和除法。好吧，那些最初的假寄存器R0、R1、R2 ...现在有用了！当我需要临时存储中间结果时，我只需分配这些位置并将中间结果存储在这里。[cg6809.c](cg6809.c)中有`cgalloctemp()`和`cgfreealltemps()`函数来执行此操作。

# printlocation()和字面值

对于大多数位置，我们可以简单地打印位置名称或堆栈上的位置，加上我们需要的偏移量。代码生成器已经打印了要运行的指令，所以：

```
  ldb _x+0	; 将从_x加载一个字节到B
  ldd _x+0	; 将从_x加载两个字节到D
```

但对于字面值，例如0x12345678，我们需要在末尾打印0x78，还是0x5678？或者（在加法代码中）我们需要访问0x34和0x12吗？

这就是为什么`printlocation()`有`rletter`参数：

```
static void printlocation(int l, int offset, char rletter);
```

当我们打印字面值时，我们使用它来选择字面值的哪一部分和多少。我选择的值反映了6809的寄存器名称，但我自己也造了一些。对于字面值0x12345678：

 - 'b'打印0x78部分
 - 'a'打印0x56部分
 - 'd'打印0x5678部分
 - 'y'打印0x1234部分
 - 'f'打印0x34部分
 - 'e'打印0x12部分

## 辅助函数

有几个编译器需要执行但6809没有指令的操作：乘法、除法、多位移位等。

为了解决这个问题，我从[Fuzix编译器工具包](https://github.com/EtchedPixels/Fuzix-Compiler-Kit)借用了几个辅助函数。它们在归档文件`lib/6809/lib6809.a`中。[cg6809.c](cg6809.c)中的函数`cgbinhelper()`：

```
// 在两个位置上运行辅助子程序
// 并返回包含结果的位置编号
static int cgbinhelper(int l1, int l2, int type,
                                char *cop, char *iop, char *lop);
```

从两个位置l1和l2获取值，将它们压入堆栈，然后调用`cop`、`iop`和`lop`中命名的三个char/int/long辅助函数之一。因此，代码生成器中进行乘法的函数就是：

```
// 将两个位置相乘并返回
// 包含结果的位置编号
int cgmul(int r1, int r2, int type) {
  return(cgbinhelper(r1, r2, type, "__mul", "__mul", "__mull"));
}
```

# 跟踪局部变量和参数的位置

函数的局部变量或参数保存在堆栈上，我们通过相对于堆栈指针的偏移量来访问它们，例如

```
  ldd -12,s     ; 加载比堆栈指针低12字节的
                ; 局部整数变量
```

但有一个问题。如果堆栈指针移动了怎么办？考虑代码：

```
int main() {
 int x;
 
 x= 2; printf("%d %d %d\n", x, x, x);
 return(0);
}

```

`x`可能相对于堆栈指针在偏移0处。但当我们调用`printf()`时，我们将`x`的副本压入堆栈。现在真正的`x`在位置2等。所以我们实际上必须生成代码：

```
  ldd 0,s	; 获取x的值
  pshs d	; 将其压入堆栈
  ldd 2,s	; 获取x的值，注意新的偏移量
  pshs d	; 将其压入堆栈
  ldd 4,s	; 获取x的值，注意新的偏移量
  pshs d	; 将其压入堆栈
  ldd #L2	; 获取字符串"%d %d %d\n"的地址
  pshs d	; 将其压入堆栈
  lbsr _printf	; 调用printf()
  leas 8,s	; 从堆栈中弹出8字节的参数
```

我们如何跟踪局部变量和参数的当前偏移量？
答案是[cg6809.c](cg6809.c)中的`sp_adjust`变量。每次我们将某些东西压入堆栈时，我们将压入的字节数加到`sp_adjust`。类似地，当我们从堆栈中弹出或向上移动堆栈指针时，我们从`sp_adjust`中减去该量。例子：

```
// 将一个位置压入堆栈
static void pushlocn(int l) {
  load_d(l);

  switch(Locn[l].primtype) {
    ...
    case PR_INT:
      fprintf(Outfile, "\tpshs d\n");
      sp_adjust += 2;
      break;
    ...
  }
  ...
}
```

在`printlocation()`中当我们打印局部变量和参数时：

```
    case L_LOCAL: fprintf(Outfile, "%ld,s\n",
                Locn[l].intval + offset + sp_adjust);
```

当我们到达生成函数汇编代码的末尾时，还有一些错误检查：

```
// 打印函数尾声
void cgfuncpostamble(struct symtable *sym) {
  ...
  if (sp_adjust !=0 ) {
    fprintf(Outfile, "; DANGER sp_adjust is %d not 0\n", sp_adjust);
    fatald("sp_adjust is not zero", sp_adjust);
  }
}
```

这就是我想要介绍的关于6809汇编代码生成的全部内容。是的，[cg6809.c](cg6809.c)中的代码必须处理6809指令集的各种细节，这就是为什么[cg6809.c](cg6809.c)比[cgqbe.c](cgqbe.c)大得多。但是我（希望我）在[cg6809.c](cg6809.c)中放了足够的注释，以便你可以跟随并理解它在做什么。

有一些棘手的事情，比如跟踪`D`寄存器是正在使用还是空闲，我确信我仍然没有完全掌握所有`long`操作的综合。

现在我们需要讨论一个更大的话题，即6809的64K地址限制。

## 将编译器装入65,536字节

原始的"acwj"编译器是一个单一的可执行文件。它从C预处理器输出读取，完成扫描、解析和代码生成，输出汇编代码。它将符号表和每个函数的AST树保存在内存中，一旦使用数据结构就从不费心释放它们。

这些都不会帮助将编译器装入64K内存！所以，我针对6809自编译的方法是：

1. 将编译器分解为多个阶段。每个阶段
   做整体编译任务的一部分，阶段之间使用中间文件通信。
2. 在内存中尽可能少地保留符号表和AST树。
   相反，这些保存在文件中，我们有函数来读取/写入它们。
3. 尝试使用`free()`到处收集未使用的数据结构。

让我们依次看这三个。

## 七个编译器阶段

编译器现在被安排有七个阶段，每个阶段有自己的可执行文件：

1. 外部C预处理器解释#include、#ifdef和预处理器宏。
2. 词法分析器读取预处理器输出并生成令牌流。
3. 解析器读取令牌流并创建符号表以及一组AST树。
4. 代码生成器使用AST树和符号表并生成汇编代码。
5. 外部窥孔优化器改进汇编代码。
6. 外部汇编器生成目标文件。
7. 外部链接器获取`crt0.o`、目标文件和多个库并产生最终的可执行文件。

我们现在有一个前端程序[wcc.c](wcc.c)来协调所有阶段。词法分析器是名为`cscan`的程序。解析器是`cparse6809`或`cparseqbe`。代码生成器是`cgen6809`或`cgenqbe`，窥孔优化器是`cpeep`。所有这些（通过`make install`）都安装在`/opt/wcc/bin`。

可以理解有两个代码生成器，但为什么有两个解析器？答案是`sizeof(int)`、`sizeof(long)`等在每个架构上都不同，所以解析器也需要这些信息以及代码生成器。因此，[targ6809.c](targ6809.c)和[targqbe.c](targqbe.c)被编译到解析器和代码生成器中。

> 题外话：6809有一个窥孔优化器。QBE后端使用`qbe`程序将QBE代码转换为x64代码。我想这也是一种优化 :-)

## 中间文件

在这七个阶段之间，我们需要中间文件来保存阶段的输出。它们通常在编译结束时被删除，但如果你使用`wcc`的`-X`命令行标志，你可以保留它们。

C预处理器的输出存储在以`_cpp`结尾的临时文件中，例如如果我们正在编译`fred.c`，则为`foo.c_cpp`。

词法分析器的输出存储在以`_tok`结尾的临时文件中。我们有一个名为[detok.c](detok.c)的程序，你可以用它将令牌文件转储为可读格式。

解析器生成以`_sym`结尾的符号表文件，以及存储在以`_ast`结尾的文件中的AST树集合。我们有[desym.c](desym.c)和[detree.c](detree.c)程序来转储符号表和AST树文件。

无论CPU如何，代码生成器总是在以`_qbe`结尾的文件中输出未优化的汇编代码。这被`qbe`或`cpeep`读取，生成以`_s`结尾的临时文件中的优化汇编代码。

汇编器然后汇编这个文件生成以`.o`结尾的目标文件，然后链接器将它们链接起来生成最终的可执行文件。

像其他编译器一样，`wcc`有`-S`标志来输出汇编到以`.s`结尾的文件（然后停止），以及`-c`标志来输出目标文件然后停止。

## 符号表和AST文件的格式

我对这些文件采取了简单的方法，我相信可以改进。我只是使用`fwrite()`将每个`struct symtable`和`struct ASTnode`节点（见[defs.h](defs.h)）直接写入文件。

其中许多都有相关的字符串：例如符号名称，以及保存字符串字面量的AST节点。对于这些，我只是`fwrite()`输出字符串，包括末尾的NUL字节。

读取节点回来很简单：我只是`fread()`每个结构的大小。但随后我必须读取回来的NUL终止字符串（如果有的话）。没有好的C库函数来执行此操作，所以在[misc.c](misc.c)有一个名为`fgetstr()`的函数来执行此操作。

将内存结构转储到磁盘的一个问题是结构中的指针失去其含义：当结构被重新加载时，它将位于内存的另一部分。任何指针值都变得无效。

为了解决这个问题，符号表结构和ASTnode结构现在都有数字ID，用于节点本身和它指向的节点。

```
// 符号表结构
struct symtable {
  char *name;                   // 符号名称
  int id;                       // 符号的数字ID
  ...
  struct symtable *ctype;       // 如果是struct/union，指向该类型的指针
  int ctypeid;                  // 该类型的数字ID
};

// 抽象语法树结构
struct ASTnode {
  ...
  struct ASTnode *left;         // 左、中、右子树
  struct ASTnode *mid;
  struct ASTnode *right;
  int nodeid;                   // 树序列化时的节点ID
  int leftid;                   // 序列化时的数字ID
  int midid;
  int rightid;
  ...
};
```

两者的读取代码都很棘手，因为我们必须找到并重新附加节点。更大的问题是：我们将每个文件的多少内容读入并保存在内存中？

## 内存中与磁盘上的结构

这里的紧张关系是：如果我们在内存中保留太多符号表和AST节点，我们将耗尽内存。但如果我们将它们放入文件，那么当我们需要访问节点时，我们可能需要进行大量文件读取/写入操作。

与大多数此类问题一样，我们只是选择一个做得足够好的启发式方法。这里有一个额外的约束：我们可能选择一个做得很好的启发式方法，但它需要大量代码，而代码本身会占用可用内存的压力。

所以，这是我选择的。它可以被替换，但这是我现在拥有的。

## 写入符号表节点

解析阶段找到符号并确定它们的类型等。所以它负责将符号写入文件。

编译器的一个大变化是现在只有一个符号表，而不是一组表。统一表中每个符号现在都有一个结构类型和一个可见性（在[defs.h](defs.h)中）：

```
// 符号表中的符号是以下结构类型之一。
enum {
  S_VARIABLE, S_FUNCTION, S_ARRAY, S_ENUMVAL, S_STRLIT,
  S_STRUCT, S_UNION, S_ENUMTYPE, S_TYPEDEF, S_NOTATYPE
};

// 符号的可见性类
enum {
  V_GLOBAL,                     // 全局可见符号
  V_EXTERN,                     // 外部全局可见符号
  V_STATIC,                     // 静态符号，在一个文件中可见
  V_LOCAL,                      // 局部可见符号
  V_PARAM,                      // 局部可见函数参数
  V_MEMBER                      // 结构或联合的成员
};
```

好吧，我撒了点小谎 :-) 实际上有三个符号表：一个用于通用符号，一个用于类型（结构、联合、枚举、typedef），还有一个临时符号表，用于在将成员列表附加到符号的成员字段之前构建它。这用于结构、联合和枚举。它也保存当前正在解析的函数的参数和局部变量。

在[sym.c](sym.c)中，`serialiseSym()`函数将符号表节点和任何相关字符串写入文件。一个优化是，因为节点被赋予单调递增的ID，我们可以记录我们已经写出的最高符号ID，而不（重新）写入该ID及以下的符号。

同一文件中的函数`flushSymtable()`遍历类型列表和通用符号列表，并调用`serialiseSym()`来写出每个节点。

在同一文件中，`freeSym()`释放符号条目占用的内存。这包括节点本身、任何相关名称以及任何初始化列表（即对于全局符号，例如`int x= 27;`）。像结构、联合和函数这样的符号也有一系列成员符号——结构中的字段和联合中的字段，以及函数的局部变量和参数。这些也被释放。

[sym.c](sym.c)中的函数`freeSymtable()`遍历这些列表并调用`freeSym()`来释放每个节点。

现在的问题是：在解析器中什么时候可以安全地刷新和释放符号表？答案是：我们可以在每个函数之后将符号表刷新出来。但是我们不能释放符号表，因为解析器需要查找预定义类型和预定义符号，例如

```
  z= x + y;
```

这些是什么类型，它们兼容吗？它们是局部变量、参数还是全局变量？它们甚至被声明了吗？我们需要完整的符号表。

所以在[decl.c](decl.c)的`function_declaration()`末尾：

```
  ...
  flushSymtable();
  Functionid= NULL;
  return (oldfuncsym);
}
```

## 读取符号表节点

6809代码生成器在代码方面相当大。它占用约30K的RAM，所以我们必须努力不浪费剩余的RAM。在代码生成器中，我们只在需要时才加载符号。并且，每个符号可能需要一个或多个其他符号的知识，例如一个变量可能是类型`struct foo`，所以现在我们需要加载`struct foo`符号以及作为该结构字段的所有符号。

一个问题是我们按解析顺序写出符号，但我们需要通过名称或ID来查找符号。例子：

```
  struct foo x;
```

我们必须通过名称搜索`x`符号。那个节点有`foo`符号的`ctypeid`，所以我们需要通过ID搜索该符号。

这里大部分工作由[sym.c](sym.c)中的`loadSym()`完成：

```
// 给定一个指向symtable节点的指针，从磁盘上的符号表中读取下一个条目。
// 如果loadit为真，始终执行此操作。
// 如果recurse为零，只读取一个节点。
// 如果loadit为假，加载数据并返回真如果符号
// a) 匹配给定的名称和stype或b) 匹配id。
// 当没有剩余内容可读时返回-1。
static int loadSym(struct symtable *sym, char *name,
                   int stype, int id, int loadit, int recurse) {
 ...
}
```

我不会详细讲解代码，但有几点需要注意。我们可以通过`stype`和`name`搜索，例如名为`printf()`的S_FUNCTION。我们可以通过数字ID搜索。有时我们想递归获取节点：这是因为具有成员的符号（例如结构）在写出后立即跟着成员。最后，如果`loadit`被设置，我们总是可以读入下一个符号，例如当读入成员时。

`findSyminfile()`函数只是每次返回符号文件的开头，并循环调用`loadSym()`，直到找到所需的符号或到达文件末尾。效率不高，不是吗？

旧编译器代码有函数

```
struct symtable *findlocl(char *name, int id);
struct symtable *findSymbol(char *name, int stype, int id);
struct symtable *findmember(char *s);
struct symtable *findstruct(char *s);
struct symtable *findunion(char *s);
struct symtable *findenumtype(char *s);
struct symtable *findenumval(char *s);
struct symtable *findtypedef(char *s);
```

它们仍然在这里，但有所不同。我们首先在内存中搜索所需的符号，然后在内存中找不到符号时调用`findSyminfile()`。当从文件加载符号时，它被链接到内存中的符号表。因此，随着代码生成器需要它们，我们构建了一个符号缓存。

为了减轻内存压力，我们应该定期在代码生成器中刷新和释放符号表。在包含代码生成器主循环的[cgen.c](cgen.c)中：

```
  while (1) {
    // 从文件读取下一个函数的顶部节点
    node= loadASTnode(0, 1);
    if (node==NULL) break;

    // 为树生成汇编代码
    genAST(node, NOLABEL, NOLABEL, NOLABEL, 0);

    // 释放内存中符号表中的符号。
    freeSymtable();
  }
```

当我重写编译器时，有一个小问题困扰着我：有全局符号被初始化，需要为它们生成汇编指令。所以，就在上述循环之上，调用了一个名为`allocateGlobals()`的函数。这又调用[sym.c](sym.c)中名为`loadGlobals()`的函数，读取任何全局符号。现在当我们遍历全局符号列表时，我们可以调用适当的代码生成器函数。在`allocateGlobals()`结束时，我们可以`freeSymtable()`。

我还有最后一个评论。所有这些都有效，因为任何C程序中都没有那么多符号，还要考虑包含的头文件。但如果这是一个真正的生产编译器在真正的类Unix系统上，哎呀！！一个典型的程序会引入十几个头文件，每个都有几十个typedef、结构、枚举值等。我们会很快耗尽内存。

所以这一切都有效，但不可扩展。

## 写入AST节点

现在谈谈AST节点。我需要说明的第一点是，内存根本不足以构建一个函数的AST树，然后写出它（或读入它）。我们需要处理的最大函数有3000个或更多AST节点。它们自己根本装不进64K的RAM。

我们只能在内存中保留有限数量的AST节点，但怎么做？毕竟它是一棵树。对于任何节点，我们什么时候需要它下面的子树，什么时候可以修剪树？

在顶级解析器文件[parse.c](parse.c)中有一个名为`serialiseAST()`的函数，它将给定的节点及其子节点写入磁盘。这个函数在几个地方被调用。

在[stmt.c](stmt.c)的`compound_statement()`中：

```
  while (1) {
    ...
    // 解析单个语句
    tree = single_statement();

    ...
        left = mkastnode(A_GLUE, P_NONE, NULL, left, NULL, tree, NULL, 0);

        // 为了节省内存，我们尝试优化单个语句树。
        // 然后我们将树序列化并释放它。我们将right指针
        // 在left中设置为NULL；这将阻止序列化器进入
        // 我们已经序列化过的树。
        tree = optimise(tree);
        serialiseAST(tree);
        freetree(tree, 0);
    ...
  }
```

所以，每次有一个单独的语句，我们解析这个语句，构建它的AST树，然后将其转储到磁盘。

在[decl.c](decl.c)的`function_declaration()`末尾：

```
  // 序列化树
  serialiseAST(tree);
  freetree(tree, 0);

  // 将内存中的符号表刷新出来。
  // 我们不再处于函数中。
  flushSymtable();
  Functionid= NULL;

  return (oldfuncsym);
```

这写出标识函数顶部AST节点的S_FUNCTION节点。

上面的代码片段引用了`freetree()`。它在[tree.c](tree.c)中：

```
// 释放树的内容。可能
// 因为树优化，有时
// left和right是相同的子节点。
// 如果要求，释放名称。
void freetree(struct ASTnode *tree, int freenames) {
  if (tree==NULL) return;

  if (tree->left!=NULL) freetree(tree->left, freenames);
  if (tree->mid!=NULL) freetree(tree->mid, freenames);
  if (tree->right!=NULL && tree->right!=tree->left)
                                        freetree(tree->right, freenames);
  if (freenames && tree->name != NULL) free(tree->name);
  free(tree);
}
```

## 读取AST节点

我奋斗了相当长的时间才找到读取AST节点回代码生成器的好方法。我们必须做两件事：

1. 找到每个函数的顶部节点并读入。
2. 一旦我们有了一个AST节点，使用其ID读入其子节点。

我的第一个方法与符号表一样，每次搜索时都倒回到文件开头。好的，这让编译一个1000行的文件花费了大约45分钟。不，那不好。

我确实考虑过尝试将数字ID、类型（S_FUNCTION与否）和文件偏移量缓存到内存中。这也不可行。对于每个AST节点，那将是：

 - 2字节ID
 - 1字节S_FUNCTION布尔值
 - 4字节文件偏移量

一个有3000个节点的AST文件现在在内存中需要一个21000字节的缓存。荒谬！

相反，我在单独的临时文件中构建节点文件偏移量列表。这由[tree.c](tree.c)中的`mkASTidxfile()`函数完成。该文件只是一系列偏移值，每个4字节。位置0保存id 0的偏移量，位置4保存id 1的偏移量，等等。

由于我们需要依次找到每个函数的顶部节点，而且一个文件中通常没有很多函数，我选择将所有S_FUNCTION节点的偏移量记录在一个内存列表中：

在[tree.c](tree.c)中，我们有：

```
// 我们保存一个AST节点偏移数组，代表AST文件中的函数
long *Funcoffset;

```

这被`malloc()`和`realloc()`，增长到包含所有函数偏移量。最后一个值为0，因为id值0在解析器中从未被分配。

现在，我们如何使用所有这些信息？在同一文件中有一个名为`loadASTnode()`的函数：

```
// 给定一个AST节点ID，从AST文件中加载该AST节点。
// 如果设置了nextfunc，找到下一个是函数的AST节点。
// 分配并返回节点，如果找不到则返回NULL。
struct ASTnode *loadASTnode(int id, int nextfunc) {
  ...
}
```

我们可以根据ID加载节点，或者只加载下一个S_FUNCTION节点。我们使用带有偏移量的临时文件快速找到主AST文件中我们想要的节点的位置。简单而巧妙！

## 使用loadASTnode()和释放AST节点

不幸的是，没有单一的地方我们可以调用`loadASTnode()`。在[gen.c](gen.c)的架构无关生成代码中，以前使用指针`n->left`、`n->mid`或`n->right`的任何地方，我们现在必须调用`loadASTnode()`，例如

```
// 给定一个AST、一个可选标签和父级的AST操作码，
// 递归生成汇编代码。
// 返回树最终值的寄存器ID。
int genAST(struct ASTnode *n, int iflabel, int looptoplabel,
           int loopendlabel, int parentASTop) {
  struct ASTnode *nleft, *nmid, *nright;

  // 加载子节点
  nleft=loadASTnode(n->leftid,0);
  nmid=loadASTnode(n->midid,0);
  nright=loadASTnode(n->rightid,0);
  ...
}
```

你会在[gen.c](gen.c)中找到大约十五个对`loadASTnode()`的调用。

回到解析器，我们可以解析单个语句，然后在写出到磁盘后调用`freetree()`。在代码生成器中，我决定更具体。一旦我确定完成使用AST节点，我就调用[tree.c](tree.c)中定义的函数`freeASTnode()`来释放其内存。你会在代码生成器中找到大约十二次对这个函数的调用。

这就是符号表和AST节点处理的全部更改。

## 常规内存释放

回溯到我开始谈论尝试将编译器装入64K时，我的第三点是：尽可能使用`free()`到处收集未使用的数据结构。

好吧，C可能是尝试做垃圾收集最糟糕的语言！有一段时间我尝试在我认为它们会工作的地方撒`free()`，但后来编译器要么段错误，要么更糟的是，使用被覆盖的节点进入疯狂行为模式。

幸运的是，我已经能够降至四个主要垃圾收集函数：`freeSym()`、`freeSymtable()`、`freeASTnode()`和`freetree()`。

这并没有解决所有垃圾收集问题。我最近求助于使用[Valgrind](https://valgrind.org/)来显示我哪里有内存泄漏。我试图找到最坏的情况，然后找出在哪里可以插入一个`free()`来提供帮助。这已经让编译器达到了可以在6809上自编译的程度，但肯定还有改进空间！

## 窥孔优化器

窥孔优化器[cpeep.c](cpeep.c)最初由Christian W. Fraser于1984年编写。查看[文档](docs/copt.1)，此后有多人对其进行了工作。我从[Fuzix编译器工具包](https://github.com/EtchedPixels/Fuzix-Compiler-Kit)导入并更改了名称。我还将规则终止符更改为`====`而不是空行；我发现这样更容易看到规则在哪里结束。

6809后端可能吐出一些糟糕的代码。优化器有助于消除其中一些。看看[rules.6809](lib/6809/rules.6809)文件了解规则是什么；我认为我已经记录得足够好了。有一个[测试文件](tests/input.rules.6809)用来检查规则是否正常工作。

## 构建和运行编译器 - QBE

在Linux机器上构建编译器以输出x68代码，首先需要下载[QBE 1.2](https://c9x.me/compile/releases.html)，构建它并安装`qbe`二进制文件到你的`$PATH`上的某个地方。

接下来，你需要创建`/opt/wcc`目录并使其对你可写。

现在你可以`make; make install`，这将构建编译器并将可执行文件放入`/opt/wcc/bin`，头文件放入`/opt/wcc/include`，6809库放入`/opt/wcc/lib/6809`。

现在确保`/opt/wcc/bin/wcc`（编译器前端）在你的`$PATH`上。我通常将它符号链接到我的私人`bin`文件夹。

从这里，你可以`make test`，进入`tests/`目录并运行其中的所有测试。

## 构建和运行编译器 - 6809

这有点复杂。

首先，你需要下载[Fuzix Bintools](https://github.com/EtchedPixels/Fuzix-Bintools)，并至少构建汇编器`as6809`和链接器`ld6809`。现在将它们安装到你的`$PATH`上的某个地方。

接下来，下载我的[Fuzemsys](https://github.com/DoctorWkt/Fuzemsys)项目。这有一个我们需要运行6809二进制文件的6809模拟器。进入`emulators/`目录并`make emu6809`。构建完成后，将模拟器安装到你的`$PATH`上的某个地方。

如果你还没有，创建`/opt/wcc`目录如前，回到这个项目并`make; make install`来安装它。确保`/opt/wcc/bin/wcc`（编译器前端）在你的`$PATH`上。

从这里，你可以`make 6test`，进入`tests/`目录并运行其中的所有测试。这一次，我们构建6809二进制文件并使用6809模拟器运行它们。

## 执行QBE三重测试

安装了`qbe`并且你做了`make install; make test`来检查编译器工作后，你现在可以执行`make triple`。这：

 - 用你的原生编译器构建编译器，
 - 用它自己构建编译器到`L1`目录，
 - 再次用它自己构建编译器到`L2`目录，并且
 - 检查`L1`和`L2`可执行文件的校验和以确保它们相同：

```
0f14b990d9a48352c4d883cd550720b3  L1/detok
0f14b990d9a48352c4d883cd550720b3  L2/detok
3cc59102c6a5dcc1661b3ab3dcce5191  L1/cgenqbe
3cc59102c6a5dcc1661b3ab3dcce5191  L2/cgenqbe
3e036c748bdb5e3ffc0e03506ed00243  L2/wcc      <-- 不同
6fa26e506a597c9d9cfde7d168ae4640  L1/detree
6fa26e506a597c9d9cfde7d168ae4640  L2/detree
7f8e55a544400ab799f2357ee9cc4b44  L1/cscan
7f8e55a544400ab799f2357ee9cc4b44  L2/cscan
912ebc765c27a064226e9743eea3dd30  L1/wcc      <-- 不同
9c6a66e8b8bbc2d436266c5a3ca622c7  L1/cparseqbe
9c6a66e8b8bbc2d436266c5a3ca622c7  L2/cparseqbe
cb493abe1feed812fb4bb5c958a8cf83  L1/desym
cb493abe1feed812fb4bb5c958a8cf83  L2/desym
```

`wcc`二进制文件是不同的，因为一个在路径中有`L1`来找到各阶段的可执行文件，另一个有`L2`而不是。

## 执行6809三重测试

我没有使用`Makefile`来做这个，而是有一个名为`6809triple_test`的单独Bash脚本。运行它来：

 - 用你的原生编译器构建编译器，
 - 用它自己构建6809编译器到`L1`目录，并且
 - 再次用它自己构建6809编译器到`L2`目录。

这很慢！在我不错的笔记本电脑上大约需要45分钟。最终你可以做你自己的校验和来验证可执行文件是相同的：

```
$ md5sum L1/_* L2/_* | sort
01c5120e56cb299bf0063a07e38ec2b9  L1/_cgen6809
01c5120e56cb299bf0063a07e38ec2b9  L2/_cgen6809
0caee9118cb7745eaf40970677897dbf  L1/_detree
0caee9118cb7745eaf40970677897dbf  L2/_detree
2d333482ad8b4a886b5b78a4a49f3bb5  L1/_detok
2d333482ad8b4a886b5b78a4a49f3bb5  L2/_detok
d507bd89c0fc1439efe2dffc5d8edfe3  L1/_desym
d507bd89c0fc1439efe2dffc5d8edfe3  L2/_desym
e78da1f3003d87ca852f682adc4214e8  L1/_cscan
e78da1f3003d87ca852f682adc4214e8  L2/_cscan
e9c8b2c12ea5bd4f62091fafaae45971  L1/_cparse6809
e9c8b2c12ea5bd4f62091fafaae45971  L2/_cparse6809
```

目前我在将`wcc`作为6809可执行文件运行方面遇到问题，所以我使用x64`wcc`二进制文件代替。

## 示例命令行操作

以下是我用来完成上述所有操作的命令捕获：

```
# 下载acwj仓库
cd /usr/local/src
git clone https://github.com/DoctorWkt/acwj

# 创建目标目录
sudo mkdir /opt/wcc
sudo chown wkt:wkt /opt/wcc

# 安装QBE
cd /usr/local/src
wget https://c9x.me/compile/release/qbe-1.2.tar.xz
xz -d qbe-1.2.tar.xz 
tar vxf qbe-1.2.tar 
cd qbe-1.2/
make
sudo make install

# 安装wcc编译器
cd /usr/local/src/acwj/64_6809_Target
make install

# 将wcc放到我的$PATH上
cd ~/.bin
ln -s /opt/wcc/bin/wcc .

# 使用QBE在x64上执行三重测试
cd /usr/local/src/acwj/64_6809_Target
make triple

# 获取Fuzix-Bintools并构建
# 6809汇编器和链接器
cd /usr/local/src
git clone https://github.com/EtchedPixels/Fuzix-Bintools
cd Fuzix-Bintools/
make as6809 ld6809
cp as6809 ld6809 ~/.bin

# 获取Fuzemsys并构建6809模拟器。
# 我需要安装readline库。
sudo apt-get install libreadline-dev
cd /usr/local/src
git clone https://github.com/DoctorWkt/Fuzemsys
cd Fuzemsys/emulators/
make emu6809
cp emu6809 ~/.bin

# 回到编译器并使用
# 6809模拟器执行三重测试
cd /usr/local/src/acwj/64_6809_Target
./6809triple_test 
```

## 这是自编译吗？

我们可以用6809 CPU通过三重测试。但是，这真的能自编译吗？嗯，是的，但它绝对不是自托管的。

这个C编译器不能构建的东西包括：

 - C预处理器
 - 窥孔优化器
 - 6809汇编器
 - 6809链接器
 - 6809的`ar`归档器
 - 编译器辅助函数和C库。目前，我使用Fuzix编译器工具包来构建这些函数。Fuzix编译器说"真正的"C；这个编译器只说C的一个子集，所以它无法构建这些函数。

所以，如果我想将所有这些转移到我的[MMU09 SBC](https://github.com/DoctorWkt/MMU09)上，那么我需要使用Fuzix编译器来构建汇编器、链接器、辅助函数和C库。

因此，"acwj"编译器绝对可以获取预处理后的C源代码，并使用扫描器、解析器和代码生成器输出6809汇编代码。而"acwj"编译器可以自行完成上述操作。

这使我们的编译器成为一个自编译编译器，但不是自托管编译器！

## 未来工作

现在，这不是一个生产编译器。它甚至不是一个合适的C编译器——它只知道C语言的一个子集。

一些要做的事情是：

 - 使其更健壮
 - 掌握垃圾收集
 - 添加无符号类型
 - 添加浮点数和双精度数
 - 添加更多真正的C语言以成为自托管
 - 提高6809代码生成器的质量
 - 提高6809编译器的速度
 - 也许，退一步，利用通过整个旅程学到的教训，从头开始重写一个新编译器！

## 结论

这部分之后我非常精疲力竭——这花费了几个月的工作，有我的[笔记](docs/NOTES.md)为证。而且我们现在到了"acwj"旅程的第64部分；这是一个好的2的幂 :-)

所以我不会明确地说不会，但我认为这将是我"acwj"旅程的终点。如果你通过一些/大部分/所有部分跟随了，那么感谢你花时间阅读我的笔记。我希望它有用。

而且，现在，如果你需要一个面向8位或16位CPU（具有有限寄存器的）的还算可以的C编译器，这可能对你来说是一个起点！

Cheers, Warren