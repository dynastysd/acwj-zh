#include "defs.h"
#include "data.h"
#include "decl.h"

// x86-64 代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3

// 标志，指示我们当前输出到哪个段
enum { no_seg, text_seg, data_seg } currSeg = no_seg;

void cgtextseg() {
  if (currSeg != text_seg) {
    fputs("\t.text\n", Outfile);
    currSeg = text_seg;
  }
}

void cgdataseg() {
  if (currSeg != data_seg) {
    fputs("\t.data\n", Outfile);
    currSeg = data_seg;
  }
}

// 给定标量类型值，返回
// 该类型的大小（以字节为单位）。
int cgprimsize(int type) {
  if (ptrtype(type))
    return (8);
  switch (type) {
    case P_CHAR:
      return (1);
    case P_INT:
      return (4);
    case P_LONG:
      return (8);
    default:
      fatald("Bad type in cgprimsize:", type);
  }
  return (0);			// 保持 -Wall 高兴
}

// 给定标量类型、现有内存偏移量
// （尚未分配给任何内容）
// 和方向（1 是向上，-1 是向下），
// 计算并返回适合此标量类型的
// 内存偏移量。这可以是原始
// 偏移量，也可以在原始偏移量的上方或下方
int cgalign(int type, int offset, int direction) {
  int alignment;

  // 在 x86-64 上我们不需要这样做，但我们将
  // 在任何偏移量上对齐 char，在 4 字节对齐上
  // 对齐 int/指针
  switch (type) {
    case P_CHAR:
      return (offset);
    case P_INT:
    case P_LONG:
      break;
    default:
      if (!ptrtype(type))
	fatald("Bad type in cg_align:", type);
  }

  // 在这里我们有一个 int 或 long。在 4 字节偏移量上对齐它。
  // 我把通用代码放在这里以便在其他地方重用。
  alignment = 4;
  offset = (offset + direction * (alignment - 1)) & ~(alignment - 1);
  return (offset);
}

// 下一个局部变量相对于栈基址指针的位置。
// 我们将偏移量存储为正数，以使栈指针对齐更容易
static int localOffset;
static int stackOffset;

// 创建新局部变量的位置。
static int newlocaloffset(int size) {
  // 偏移量至少减少 4 个字节
  // 并在栈上分配
  localOffset += (size > 4) ? size : 4;
  return (-localOffset);
}

// 可用寄存器列表及其名称。
// 我们还需要字节和双字寄存器的列表。
// 列表还包括用于保存函数参数的寄存器
#define NUMFREEREGS 4
#define FIRSTPARAMREG 9		// 第一个参数寄存器在上述寄存器列表中的位置
static int freereg[NUMFREEREGS];
static char *reglist[] =
  { "%r10", "%r11", "%r12", "%r13", "%r9", "%r8", "%rcx", "%rdx", "%rsi",
  "%rdi"
};

static char *breglist[] =
  { "%r10b", "%r11b", "%r12b", "%r13b", "%r9b", "%r8b", "%cl", "%dl", "%sil",
  "%dil"
};

static char *dreglist[] =
  { "%r10d", "%r11d", "%r12d", "%r13d", "%r9d", "%r8d", "%ecx", "%edx",
  "%esi", "%edi"
};

// 将寄存器推入/弹出栈
static void pushreg(int r) {
  fprintf(Outfile, "\tpushq\t%s\n", reglist[r]);
}

static void popreg(int r) {
  fprintf(Outfile, "\tpopq\t%s\n", reglist[r]);
}


// 将所有寄存器设置为可用。
// 但如果 reg 为正，不要释放该寄存器。
void freeall_registers(int keepreg) {
  int i;
  for (i = 0; i < NUMFREEREGS; i++)
    if (i != keepreg)
      freereg[i] = 1;
}

// 当我们需要溢出寄存器时，我们选择
// 以下寄存器，然后循环遍历
// 其余寄存器。spillreg 持续递增，
// 所以我们需要对其取 NUMFREEREGS 的模
static int spillreg=0;

// 分配一个空闲寄存器。返回
// 寄存器的编号。如果没有可用寄存器则死亡。
int alloc_register(void) {
  int reg;

  for (reg = 0; reg < NUMFREEREGS; reg++) {
    if (freereg[reg]) {
      freereg[reg] = 0;
      return (reg);
    }
  }

  // 我们没有寄存器，所以必须溢出其中一个
  reg= (spillreg % NUMFREEREGS);
  spillreg++;
  fprintf(Outfile, "# spilling reg %d\n", reg);
  pushreg(reg);
  return (reg);
}

// 将寄存器返回到可用寄存器列表。
// 检查它是否已经在那里。
static void free_register(int reg) {
  if (freereg[reg] != 0) {
    fprintf(Outfile, "# error trying to free register %d\n", reg);
    fatald("Error trying to free register", reg);
  }

  // 如果这是溢出的寄存器，则恢复它
  if (spillreg > 0) {
    spillreg--;
    reg= (spillreg % NUMFREEREGS);
    fprintf(Outfile, "# unspilling reg %d\n", reg);
    popreg(reg);
  } else {
    freereg[reg] = 1;
  }
}

// 将所有寄存器溢出到栈
void spill_all_regs(void) {
  int i;

  for (i = 0; i < NUMFREEREGS; i++)
    pushreg(i);
}

// 从栈中恢复所有寄存器
static void unspill_all_regs(void) {
  int i;

  for (i = NUMFREEREGS-1; i >= 0; i--)
    popreg(i);
}

// 输出汇编前导码
void cgpreamble() {
  freeall_registers(NOREG);
  cgtextseg();
  fprintf(Outfile,
	  "# internal switch(expr) routine\n"
	  "# %%rsi = switch table, %%rax = expr\n"
	  "# from SubC: http://www.t3x.org/subc/\n"
	  "\n"
	  "switch:\n"
	  "        pushq   %%rsi\n"
	  "        movq    %%rdx, %%rsi\n"
	  "        movq    %%rax, %%rbx\n"
	  "        cld\n"
	  "        lodsq\n"
	  "        movq    %%rax, %%rcx\n"
	  "next:\n"
	  "        lodsq\n"
	  "        movq    %%rax, %%rdx\n"
	  "        lodsq\n"
	  "        cmpq    %%rdx, %%rbx\n"
	  "        jnz     no\n"
	  "        popq    %%rsi\n"
	  "        jmp     *%%rax\n"
	  "no:\n"
	  "        loop    next\n"
	  "        lodsq\n"
	  "        popq    %%rsi\n" "        jmp     *%%rax\n" "\n");
}

// 什么都不做
void cgpostamble() {
}

// 输出函数前导码
void cgfuncpreamble(struct symtable *sym) {
  char *name = sym->name;
  struct symtable *parm, *locvar;
  int cnt;
  int paramOffset = 16;		// 任何推送的参数从此栈偏移量开始
  int paramReg = FIRSTPARAMREG;	// 上述寄存器列表中第一个参数寄存器的索引

  // 在文本段输出，重置局部偏移量
  cgtextseg();
  localOffset = 0;

  // 输出函数开始，保存 %rsp 和 %rsp
  if (sym->class == C_GLOBAL)
    fprintf(Outfile, "\t.globl\t%s\n" "\t.type\t%s, @function\n", name, name);
  fprintf(Outfile, "%s:\n" "\tpushq\t%%rbp\n" "\tmovq\t%%rsp, %%rbp\n", name);

  // 将任何在寄存器中的参数复制到栈上，最多六个。
  // 其余参数已经在栈上。
  for (parm = sym->member, cnt = 1; parm != NULL; parm = parm->next, cnt++) {
    if (cnt > 6) {
      parm->st_posn = paramOffset;
      paramOffset += 8;
    } else {
      parm->st_posn = newlocaloffset(parm->size);
      cgstorlocal(paramReg--, parm);
    }
  }

  // 对于其余的，如果是参数那么它们
  // 已经在栈上。如果是局部变量，则在栈上分配空间。
  for (locvar = Loclhead; locvar != NULL; locvar = locvar->next) {
    locvar->st_posn = newlocaloffset(locvar->size);
  }

  // 将栈指针对齐到 16 的倍数
  // 小于其之前的值
  stackOffset = (localOffset + 15) & ~15;
  fprintf(Outfile, "\taddq\t$%d,%%rsp\n", -stackOffset);
}

// 输出函数后导码
void cgfuncpostamble(struct symtable *sym) {
  cglabel(sym->st_endlabel);
  fprintf(Outfile, "\taddq\t$%d,%%rsp\n", stackOffset);
  fputs("\tpopq	%rbp\n" "\tret\n", Outfile);
  freeall_registers(NOREG);
}

// 将整数常量值加载到寄存器中。
// 返回寄存器的编号。
// 对于 x86-64，我们不需要担心类型。
int cgloadint(int value, int type) {
  // 获取一个新寄存器
  int r = alloc_register();

  fprintf(Outfile, "\tmovq\t$%d, %s\n", value, reglist[r]);
  return (r);
}

// 将变量值加载到寄存器中。
// 返回寄存器的编号。如果操作是前或后递增/递减，
// 也要执行此操作。
int cgloadglob(struct symtable *sym, int op) {
  // 获取一个新寄存器
  int r = alloc_register();

  if (cgprimsize(sym->type) == 8) {
    if (op == A_PREINC)
      fprintf(Outfile, "\tincq\t%s(%%rip)\n", sym->name);
    if (op == A_PREDEC)
      fprintf(Outfile, "\tdecq\t%s(%%rip)\n", sym->name);
    fprintf(Outfile, "\tmovq\t%s(%%rip), %s\n", sym->name, reglist[r]);
    if (op == A_POSTINC)
      fprintf(Outfile, "\tincq\t%s(%%rip)\n", sym->name);
    if (op == A_POSTDEC)
      fprintf(Outfile, "\tdecq\t%s(%%rip)\n", sym->name);
  } else
    // 输出初始化它的代码
    switch (sym->type) {
      case P_CHAR:
	if (op == A_PREINC)
	  fprintf(Outfile, "\tincb\t%s(%%rip)\n", sym->name);
	if (op == A_PREDEC)
	  fprintf(Outfile, "\tdecb\t%s(%%rip)\n", sym->name);
	fprintf(Outfile, "\tmovzbq\t%s(%%rip), %s\n", sym->name, reglist[r]);
	if (op == A_POSTINC)
	  fprintf(Outfile, "\tincb\t%s(%%rip)\n", sym->name);
	if (op == A_POSTDEC)
	  fprintf(Outfile, "\tdecb\t%s(%%rip)\n", sym->name);
	break;
      case P_INT:
	if (op == A_PREINC)
	  fprintf(Outfile, "\tincl\t%s(%%rip)\n", sym->name);
	if (op == A_PREDEC)
	  fprintf(Outfile, "\tdecl\t%s(%%rip)\n", sym->name);
	fprintf(Outfile, "\tmovslq\t%s(%%rip), %s\n", sym->name, reglist[r]);
	if (op == A_POSTINC)
	  fprintf(Outfile, "\tincl\t%s(%%rip)\n", sym->name);
	if (op == A_POSTDEC)
	  fprintf(Outfile, "\tdecl\t%s(%%rip)\n", sym->name);
	break;
      default:
	fatald("Bad type in cgloadglob:", sym->type);
    }
  return (r);
}

// 将局部变量值加载到寄存器中。
// 返回寄存器的编号。如果操作是前或后递增/递减，
// 也要执行此操作。
int cgloadlocal(struct symtable *sym, int op) {
  // 获取一个新寄存器
  int r = alloc_register();

  // 输出初始化它的代码
  if (cgprimsize(sym->type) == 8) {
    if (op == A_PREINC)
      fprintf(Outfile, "\tincq\t%d(%%rbp)\n", sym->st_posn);
    if (op == A_PREDEC)
      fprintf(Outfile, "\tdecq\t%d(%%rbp)\n", sym->st_posn);
    fprintf(Outfile, "\tmovq\t%d(%%rbp), %s\n", sym->st_posn, reglist[r]);
    if (op == A_POSTINC)
      fprintf(Outfile, "\tincq\t%d(%%rbp)\n", sym->st_posn);
    if (op == A_POSTDEC)
      fprintf(Outfile, "\tdecq\t%d(%%rbp)\n", sym->st_posn);
  } else
    switch (sym->type) {
      case P_CHAR:
	if (op == A_PREINC)
	  fprintf(Outfile, "\tincb\t%d(%%rbp)\n", sym->st_posn);
	if (op == A_PREDEC)
	  fprintf(Outfile, "\tdecb\t%d(%%rbp)\n", sym->st_posn);
	fprintf(Outfile, "\tmovzbq\t%d(%%rbp), %s\n", sym->st_posn,
		reglist[r]);
	if (op == A_POSTINC)
	  fprintf(Outfile, "\tincb\t%d(%%rbp)\n", sym->st_posn);
	if (op == A_POSTDEC)
	  fprintf(Outfile, "\tdecb\t%d(%%rbp)\n", sym->st_posn);
	break;
      case P_INT:
	if (op == A_PREINC)
	  fprintf(Outfile, "\tincl\t%d(%%rbp)\n", sym->st_posn);
	if (op == A_PREDEC)
	  fprintf(Outfile, "\tdecl\t%d(%%rbp)\n", sym->st_posn);
	fprintf(Outfile, "\tmovslq\t%d(%%rbp), %s\n", sym->st_posn,
		reglist[r]);
	if (op == A_POSTINC)
	  fprintf(Outfile, "\tincl\t%d(%%rbp)\n", sym->st_posn);
	if (op == A_POSTDEC)
	  fprintf(Outfile, "\tdecl\t%d(%%rbp)\n", sym->st_posn);
	break;
      default:
	fatald("Bad type in cgloadlocal:", sym->type);
    }
  return (r);
}

// 给定全局字符串的标签号，
// 将其地址加载到新寄存器中
int cgloadglobstr(int label) {
  // 获取一个新寄存器
  int r = alloc_register();
  fprintf(Outfile, "\tleaq\tL%d(%%rip), %s\n", label, reglist[r]);
  return (r);
}

// 将两个寄存器相加并返回
// 包含结果的寄存器编号
int cgadd(int r1, int r2) {
  fprintf(Outfile, "\taddq\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r2);
  return (r1);
}

// 减去第二个寄存器减去第一个并返回
// 包含结果的寄存器编号
int cgsub(int r1, int r2) {
  fprintf(Outfile, "\tsubq\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r2);
  return (r1);
}

// 将两个寄存器相乘并返回
// 包含结果的寄存器编号
int cgmul(int r1, int r2) {
  fprintf(Outfile, "\timulq\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r2);
  return (r1);
}

// 第一个寄存器除以第二个并返回
// 包含结果的寄存器编号
int cgdiv(int r1, int r2) {
  fprintf(Outfile, "\tmovq\t%s,%%rax\n", reglist[r1]);
  fprintf(Outfile, "\tcqo\n");
  fprintf(Outfile, "\tidivq\t%s\n", reglist[r2]);
  fprintf(Outfile, "\tmovq\t%%rax,%s\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgand(int r1, int r2) {
  fprintf(Outfile, "\tandq\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgor(int r1, int r2) {
  fprintf(Outfile, "\torq\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgxor(int r1, int r2) {
  fprintf(Outfile, "\txorq\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgshl(int r1, int r2) {
  fprintf(Outfile, "\tmovb\t%s, %%cl\n", breglist[r2]);
  fprintf(Outfile, "\tshlq\t%%cl, %s\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgshr(int r1, int r2) {
  fprintf(Outfile, "\tmovb\t%s, %%cl\n", breglist[r2]);
  fprintf(Outfile, "\tshrq\t%%cl, %s\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

// 求反寄存器的值
int cgnegate(int r) {
  fprintf(Outfile, "\tnegq\t%s\n", reglist[r]);
  return (r);
}

// 反转寄存器的值
int cginvert(int r) {
  fprintf(Outfile, "\tnotq\t%s\n", reglist[r]);
  return (r);
}

// 逻辑求反寄存器的值
int cglognot(int r) {
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r], reglist[r]);
  fprintf(Outfile, "\tsete\t%s\n", breglist[r]);
  fprintf(Outfile, "\tmovzbq\t%s, %s\n", breglist[r], reglist[r]);
  return (r);
}

// 将布尔值（仅为 0 或 1）
// 加载到给定寄存器中
void cgloadboolean(int r, int val) {
  fprintf(Outfile, "\tmovq\t$%d, %s\n", val, reglist[r]);
}


// 将整数值转换为布尔值。如果是
// IF、WHILE、LOGAND 或 LOGOR 操作则跳转
int cgboolean(int r, int op, int label) {
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r], reglist[r]);
  switch(op) {
    case A_IF:
    case A_WHILE:
    case A_LOGAND:
      fprintf(Outfile, "\tje\tL%d\n", label);
      break;
    case A_LOGOR:
      fprintf(Outfile, "\tjne\tL%d\n", label);
      break;
    default:
      fprintf(Outfile, "\tsetnz\t%s\n", breglist[r]);
      fprintf(Outfile, "\tmovzbq\t%s, %s\n", breglist[r], reglist[r]);
  }
  return (r);
}

// 使用给定符号 id 调用函数
// 弹出推入栈的任何参数
// 返回包含结果的寄存器
int cgcall(struct symtable *sym, int numargs) {
  int outr;

  // 调用函数
  fprintf(Outfile, "\tcall\t%s@PLT\n", sym->name);

  // 移除推入栈的任何参数
  if (numargs > 6)
    fprintf(Outfile, "\taddq\t$%d, %%rsp\n", 8 * (numargs - 6));

  // 恢复所有寄存器
  unspill_all_regs();

  // 获取一个新寄存器并将返回值复制到其中
  outr = alloc_register();
  fprintf(Outfile, "\tmovq\t%%rax, %s\n", reglist[outr]);
  return (outr);
}

// 给定包含参数值的寄存器，
// 将此参数复制到第 argposn'th
// 参数中，为将来的函数调用做准备。
// 注意 argposn 是 1、2、3、4、...，永远不为零。
void cgcopyarg(int r, int argposn) {

  // 如果这大于第六个参数，只需将
  // 寄存器推入栈。我们依赖于以正确的顺序
  // 为 x86-64 调用连续的参数
  if (argposn > 6) {
    fprintf(Outfile, "\tpushq\t%s\n", reglist[r]);
  } else {
    // 否则，将值复制到用于保存参数值的
    // 六个寄存器之一
    fprintf(Outfile, "\tmovq\t%s, %s\n", reglist[r],
	    reglist[FIRSTPARAMREG - argposn + 1]);
  }
  free_register(r);
}

// 将寄存器左移常量
int cgshlconst(int r, int val) {
  fprintf(Outfile, "\tsalq\t$%d, %s\n", val, reglist[r]);
  return (r);
}

// 将寄存器的值存储到变量中
int cgstorglob(int r, struct symtable *sym) {

  if (cgprimsize(sym->type) == 8) {
    fprintf(Outfile, "\tmovq\t%s, %s(%%rip)\n", reglist[r], sym->name);
  } else
    switch (sym->type) {
      case P_CHAR:
	fprintf(Outfile, "\tmovb\t%s, %s(%%rip)\n", breglist[r], sym->name);
	break;
      case P_INT:
	fprintf(Outfile, "\tmovl\t%s, %s(%%rip)\n", dreglist[r], sym->name);
	break;
      default:
	fatald("Bad type in cgstorglob:", sym->type);
    }
  return (r);
}

// 将寄存器的值存储到局部变量中
int cgstorlocal(int r, struct symtable *sym) {

  if (cgprimsize(sym->type) == 8) {
    fprintf(Outfile, "\tmovq\t%s, %d(%%rbp)\n", reglist[r], sym->st_posn);
  } else
    switch (sym->type) {
      case P_CHAR:
	fprintf(Outfile, "\tmovb\t%s, %d(%%rbp)\n", breglist[r],
		sym->st_posn);
	break;
      case P_INT:
	fprintf(Outfile, "\tmovl\t%s, %d(%%rbp)\n", dreglist[r],
		sym->st_posn);
	break;
      default:
	fatald("Bad type in cgstorlocal:", sym->type);
    }
  return (r);
}

// 生成全局符号但不包括函数
void cgglobsym(struct symtable *node) {
  int size, type;
  int initvalue;
  int i;

  if (node == NULL)
    return;
  if (node->stype == S_FUNCTION)
    return;

  // 获取变量的大小（如果是数组则为其元素）
  // 和变量的类型
  if (node->stype == S_ARRAY) {
    size = typesize(value_at(node->type), node->ctype);
    type = value_at(node->type);
  } else {
    size = node->size;
    type = node->type;
  }

  // 生成全局标识和标签
  cgdataseg();
  if (node->class == C_GLOBAL)
    fprintf(Outfile, "\t.globl\t%s\n", node->name);
  fprintf(Outfile, "%s:\n", node->name);

  // 为一个或多个元素输出空间
  for (i = 0; i < node->nelems; i++) {

    // 获取任何初始值
    initvalue = 0;
    if (node->initlist != NULL)
      initvalue = node->initlist[i];

    // 为此类型生成空间
    switch (size) {
      case 1:
	fprintf(Outfile, "\t.byte\t%d\n", initvalue);
	break;
      case 4:
	fprintf(Outfile, "\t.long\t%d\n", initvalue);
	break;
      case 8:
	// 生成指向字符串常量的指针。将零值
	// 视为实际零，而不是标签 L0
	if (node->initlist != NULL && type == pointer_to(P_CHAR)
	    && initvalue != 0)
	  fprintf(Outfile, "\t.quad\tL%d\n", initvalue);
	else
	  fprintf(Outfile, "\t.quad\t%d\n", initvalue);
	break;
      default:
	for (i = 0; i < size; i++)
	  fprintf(Outfile, "\t.byte\t0\n");
    }
  }
}

// 生成全局字符串及其起始标签
// 如果 append 为真，不输出标签。
void cgglobstr(int l, char *strvalue, int append) {
  char *cptr;
  if (!append)
    cglabel(l);
  for (cptr = strvalue; *cptr; cptr++) {
    fprintf(Outfile, "\t.byte\t%d\n", *cptr);
  }
}

void cgglobstrend(void) {
  fprintf(Outfile, "\t.byte\t0\n");
}

// 比较指令列表，
// 按 AST 顺序：A_EQ、A_NE、A_LT、A_GT、A_LE、A_GE
static char *cmplist[] =
  { "sete", "setne", "setl", "setg", "setle", "setge" };

// 比较两个寄存器并在为真时设置。
int cgcompare_and_set(int ASTop, int r1, int r2) {

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  fprintf(Outfile, "\tcmpq\t%s, %s\n", reglist[r2], reglist[r1]);
  fprintf(Outfile, "\t%s\t%s\n", cmplist[ASTop - A_EQ], breglist[r2]);
  fprintf(Outfile, "\tmovzbq\t%s, %s\n", breglist[r2], reglist[r2]);
  free_register(r1);
  return (r2);
}

// 生成标签
void cglabel(int l) {
  fprintf(Outfile, "L%d:\n", l);
}

// 生成跳转到标签
void cgjump(int l) {
  fprintf(Outfile, "\tjmp\tL%d\n", l);
}

// 反转跳转指令列表，
// 按 AST 顺序：A_EQ、A_NE、A_LT、A_GT、A_LE、A_GE
static char *invcmplist[] = { "jne", "je", "jge", "jle", "jg", "jl" };

// 比较两个寄存器并在为假时跳转。
int cgcompare_and_jump(int ASTop, int r1, int r2, int label) {

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  fprintf(Outfile, "\tcmpq\t%s, %s\n", reglist[r2], reglist[r1]);
  fprintf(Outfile, "\t%s\tL%d\n", invcmplist[ASTop - A_EQ], label);
  freeall_registers(NOREG);
  return (NOREG);
}

// 将寄存器中的值从旧类型拓宽到新类型，
// 并返回具有此新值的寄存器
int cgwiden(int r, int oldtype, int newtype) {
  // 什么都不做
  return (r);
}

// 生成从函数返回值的代码
void cgreturn(int reg, struct symtable *sym) {

  // 只有当我们有返回值时才返回值
  if (reg != NOREG) {
    // 在这里处理指针，因为它们不能进入
    // switch 语句
    if (ptrtype(sym->type))
      fprintf(Outfile, "\tmovq\t%s, %%rax\n", reglist[reg]);
    else {
      // 根据函数的类型生成代码
      switch (sym->type) {
        case P_CHAR:
	  fprintf(Outfile, "\tmovzbl\t%s, %%eax\n", breglist[reg]);
	  break;
        case P_INT:
	  fprintf(Outfile, "\tmovl\t%s, %%eax\n", dreglist[reg]);
	  break;
        case P_LONG:
	  fprintf(Outfile, "\tmovq\t%s, %%rax\n", reglist[reg]);
	  break;
        default:
	  fatald("Bad function type in cgreturn:", sym->type);
      }
    }
  }

  cgjump(sym->st_endlabel);
}


// 生成将标识符的地址加载到变量中的代码。
// 返回一个新寄存器
int cgaddress(struct symtable *sym) {
  int r = alloc_register();

  if (sym->class == C_GLOBAL || sym->class == C_STATIC)
    fprintf(Outfile, "\tleaq\t%s(%%rip), %s\n", sym->name, reglist[r]);
  else
    fprintf(Outfile, "\tleaq\t%d(%%rbp), %s\n", sym->st_posn, reglist[r]);
  return (r);
}

// 解引用指针并获取其指向的值
// 到同一寄存器
int cgderef(int r, int type) {
  // 获取我们指向的类型
  int newtype = value_at(type);
  // 现在获取此类型的大小
  int size = cgprimsize(newtype);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tmovzbq\t(%s), %s\n", reglist[r], reglist[r]);
      break;
    case 4:
      fprintf(Outfile, "\tmovslq\t(%s), %s\n", reglist[r], reglist[r]);
      break;
    case 8:
      fprintf(Outfile, "\tmovq\t(%s), %s\n", reglist[r], reglist[r]);
      break;
    default:
      fatald("Can't cgderef on type:", type);
  }
  return (r);
}

// 通过解引用的指针存储
int cgstorderef(int r1, int r2, int type) {
  // 获取类型的大小
  int size = cgprimsize(type);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tmovb\t%s, (%s)\n", breglist[r1], reglist[r2]);
      break;
    case 4:
      fprintf(Outfile, "\tmovl\t%s, (%s)\n", dreglist[r1], reglist[r2]);
      break;
    case 8:
      fprintf(Outfile, "\tmovq\t%s, (%s)\n", reglist[r1], reglist[r2]);
      break;
    default:
      fatald("Can't cgstoderef on type:", type);
  }
  return (r1);
}

// 生成 switch 跳转表和用于
// 加载寄存器并调用 switch() 的代码
void cgswitch(int reg, int casecount, int toplabel,
	      int *caselabel, int *caseval, int defaultlabel) {
  int i, label;

  // 为 switch 表获取一个标签
  label = genlabel();
  cglabel(label);

  // 启发式。如果没有 case，创建一个指向
  // 默认 case 的 case
  if (casecount == 0) {
    caseval[0] = 0;
    caselabel[0] = defaultlabel;
    casecount = 1;
  }
  // 生成 switch 跳转表。
  fprintf(Outfile, "\t.quad\t%d\n", casecount);
  for (i = 0; i < casecount; i++)
    fprintf(Outfile, "\t.quad\t%d, L%d\n", caseval[i], caselabel[i]);
  fprintf(Outfile, "\t.quad\tL%d\n", defaultlabel);

  // 加载特定寄存器
  cglabel(toplabel);
  fprintf(Outfile, "\tmovq\t%s, %%rax\n", reglist[reg]);
  fprintf(Outfile, "\tleaq\tL%d(%%rip), %%rdx\n", label);
  fprintf(Outfile, "\tjmp\tswitch\n");
}

// 在寄存器之间移动值
void cgmove(int r1, int r2) {
  fprintf(Outfile, "\tmovq\t%s, %s\n", reglist[r1], reglist[r2]);
}