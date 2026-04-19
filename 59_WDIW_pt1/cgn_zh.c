#include "defs.h"
#include "data.h"
#include "decl.h"

// x86-64 代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3

// 标志我们正在输出到哪个段
enum { no_seg, text_seg, data_seg } currSeg = no_seg;

void cgtextseg() {
  if (currSeg != text_seg) {
    fputs("\tsection .text\n", Outfile);
    currSeg = text_seg;
  }
}

void cgdataseg() {
  if (currSeg != data_seg) {
    fputs("\tsection .data\n", Outfile);
    currSeg = data_seg;
  }
}

// 给定一个标量类型值,返回
// 该类型的大小(字节)。
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

// 给定一个标量类型、一个现有的内存偏移
// (尚未分配给任何东西)和一个方向(1 表示向上,-1 表示向下),
// 计算并返回一个适合此标量类型的对齐内存偏移。
// 这可能是原始偏移,也可能是原始偏移的上方或下方
int cgalign(int type, int offset, int direction) {
  int alignment;

  // 在 x86-64 上我们不需要这样做,但让我们
  // 在任何偏移上对齐 char,并在 4 字节对齐上对齐 int/指针
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

  // 在这里我们有一个 int 或 long。在 4 字节偏移上对齐它。
  // 我把通用代码放在这里以便可以在其他地方重用。
  alignment = 4;
  offset = (offset + direction * (alignment - 1)) & ~(alignment - 1);
  return (offset);
}

// 下一个局部变量相对于栈基指针的位置。
// 我们将偏移存储为正数,以便对齐栈指针更容易
static int localOffset;
static int stackOffset;

// 创建新局部变量的位置。
static int newlocaloffset(int size) {
  // 偏移至少递减 4 字节
  // 并在栈上分配
  localOffset += (size > 4) ? size : 4;
  return (-localOffset);
}

// 可用寄存器列表及其名称。
// 我们还需要字节寄存器和双字寄存器的列表。
// 列表还包括用于保存函数参数的寄存器
#define NUMFREEREGS 4
#define FIRSTPARAMREG 9		// 第一个参数寄存器在上述寄存器列表中的位置
static int freereg[NUMFREEREGS];
static char *reglist[] =
 { "r10",  "r11", "r12", "r13", "r9", "r8", "rcx", "rdx", "rsi",
   "rdi"
};
static char *breglist[] =
 { "r10b",  "r11b", "r12b", "r13b", "r9b", "r8b", "cl", "dl", "sil",
   "dil"
};
static char *dreglist[] =
 { "r10d",  "r11d", "r12d", "r13d", "r9d", "r8d", "ecx", "edx",
  "esi", "edi"
};

// 在栈上推送和弹出寄存器
static void pushreg(int r) {
  fprintf(Outfile, "\tpush\t%s\n", reglist[r]);
}

static void popreg(int r) {
  fprintf(Outfile, "\tpop\t%s\n", reglist[r]);
}

// 将所有寄存器设置为可用。
// 但如果 reg 为正,则不释放该寄存器。

void freeall_registers(int keepreg) {
  int i;
  // fprintf(Outfile, "; freeing all registers\n");
  for (i = 0; i < NUMFREEREGS; i++)
    if (i != keepreg)
      freereg[i] = 1;
}

// 当我们需要溢出寄存器时,选择以下寄存器,
// 然后循环遍历剩余的寄存器。spillreg 递增,
// 所以我们需要对其取模 NUMFREEREGS。
static int spillreg = 0;

// 分配一个空闲寄存器。返回
// 寄存器号。如果没有可用寄存器则终止。
int alloc_register(void) {
  int reg;

  for (reg = 0; reg < NUMFREEREGS; reg++) {
    if (freereg[reg]) {
      freereg[reg] = 0;
      // fprintf(Outfile, "; allocated register %s\n", reglist[reg]);
      return (reg);
    }
  }
  // 我们没有寄存器,所以必须溢出其中一个
  reg = (spillreg % NUMFREEREGS);
  spillreg++;
  // fprintf(Outfile, "; spilling reg %s\n", reglist[reg]);
  pushreg(reg);
  return (reg);
}

// 将寄存器返回到可用寄存器列表。
// 检查它是否已经在那里。
static void free_register(int reg) {
  if (freereg[reg] != 0) {
    //fprintf(Outfile, "# error trying to free register %s\n", reglist[reg]);
    fatald("Error trying to free register", reg);
  }
  // 如果这是溢出的寄存器,则取回它
  if (spillreg > 0) {
    spillreg--;
    reg = (spillreg % NUMFREEREGS);
    // fprintf(Outfile, "; unspilling reg %s\n", reglist[reg]);
    popreg(reg);
  } else {
    // fprintf(Outfile, "; freeing reg %s\n", reglist[reg]);
    freereg[reg] = 1;
  }
}

// 在栈上溢出所有寄存器
void spill_all_regs(void) {
  int i;

  for (i = 0; i < NUMFREEREGS; i++)
    pushreg(i);
}

// 从栈上恢复所有寄存器
static void unspill_all_regs(void) {
  int i;

  for (i = NUMFREEREGS - 1; i >= 0; i--)
    popreg(i);
}

// 打印汇编前导码
void cgpreamble(char *filename) {
  freeall_registers(NOREG);
  cgtextseg();
  fprintf(Outfile, ";\t%s\n", filename);
  fprintf(Outfile,
	  "; internal switch(expr) routine\n"
	  "; rsi = switch table, rax = expr\n"
	  "; from SubC: http://www.t3x.org/subc/\n"
	  "\n"
	  "__switch:\n"
	  "        push   rsi\n"
	  "        mov    rsi, rdx\n"
	  "        mov    rbx, rax\n"
	  "        cld\n"
	  "        lodsq\n"
	  "        mov    rcx, rax\n"
	  "__next:\n"
	  "        lodsq\n"
	  "        mov    rdx, rax\n"
	  "        lodsq\n"
	  "        cmp    rbx, rdx\n"
	  "        jnz    __no\n"
	  "        pop    rsi\n"
	  "        jmp    rax\n"
	  "__no:\n"
	  "        loop   __next\n"
	  "        lodsq\n"
	  "        pop    rsi\n" "        jmp     rax\n\n");
}

// 无操作
void cgpostamble() {
}

// 打印函数前导码
void cgfuncpreamble(struct symtable *sym) {
  char *name = sym->name;
  struct symtable *parm, *locvar;
  int cnt;
  int paramOffset = 16;		// 任何推送的参数从此栈偏移开始
  int paramReg = FIRSTPARAMREG;	// 上述寄存器列表中第一个参数寄存器的索引

  // 在文本段中输出,重置局部偏移
  cgtextseg();
  localOffset = 0;

  // 输出函数开始,保存 rsp 和 rbp
//  if (sym->class == C_GLOBAL)
  if(!sym->extinit) {
    fprintf(Outfile, "\tglobal\t%s\n", name);
    sym->extinit = 1;
  }
  fprintf(Outfile,
	  "%s:\n" "\tpush\trbp\n"
	  "\tmov\trbp, rsp\n", name);

  // 将任何寄存器中的参数复制到栈上,最多六个。
  // 剩余参数已在栈上
  for (parm = sym->member, cnt = 1; parm != NULL; parm = parm->next, cnt++) {
    if (cnt > 6) {
      parm->st_posn = paramOffset;
      paramOffset += 8;
    } else {
      parm->st_posn = newlocaloffset(parm->size);
      cgstorlocal(paramReg--, parm);
    }
  }

  // 对于其余的,如果是参数则它们已在栈上。
  // 如果只是局部变量,则创建一个栈位置。
  for (locvar = Loclhead; locvar != NULL; locvar = locvar->next) {
    locvar->st_posn = newlocaloffset(locvar->size);
  }

  // 将栈指针对齐为其原值的 16 的倍数
  stackOffset = (localOffset + 15) & ~15;
  fprintf(Outfile, "\tadd\trsp, %d\n", -stackOffset);
}

// 打印函数后置码
void cgfuncpostamble(struct symtable *sym) {
  cglabel(sym->st_endlabel);
  fprintf(Outfile, "\tadd\trsp, %d\n", stackOffset);
  fputs("\tpop\trbp\n" "\tret\n", Outfile);
  freeall_registers(NOREG);
}

// 将整数字面量值加载到寄存器中。
// 返回寄存器号。
// 对于 x86-64,我们不需要担心类型。
int cgloadint(int value, int type) {
  // 获取一个新寄存器
  int r = alloc_register();

  fprintf(Outfile, "\tmov\t%s, %d\n", reglist[r], value);
  return (r);
}

// 将变量值加载到寄存器中。
// 返回寄存器号。如果操作是预或后递增/递减,
// 也执行此操作。
int cgloadvar(struct symtable *sym, int op) {
  int r, postreg, offset = 1;

  if(!sym->extinit) {
    fprintf(Outfile, "extern\t%s\n", sym->name);
    sym->extinit = 1;
  }

  // 获取一个新寄存器
  r = alloc_register();

  // 如果符号是指针,使用其指向类型的大小
  // 作为任何递增或递减的大小。如果不是,则是 1。
  if (ptrtype(sym->type))
    offset = typesize(value_at(sym->type), sym->ctype);

  // 对于递减取偏移的负值
  if (op == A_PREDEC || op == A_POSTDEC)
    offset = -offset;

  // 如果我们有预操作
  if (op == A_PREINC || op == A_PREDEC) {
    // 加载符号的地址
    if (sym->class == C_LOCAL || sym->class == C_PARAM)
      fprintf(Outfile, "\tlea\t%s, [rbp+%d]\n", reglist[r], sym->st_posn);
    else
      fprintf(Outfile, "\tlea\t%s, [%s]\n", reglist[r], sym->name);

    // 然后更该该地址处的值
    switch (sym->size) {
      case 1:
        fprintf(Outfile, "\tadd\tbyte [%s], %d\n", reglist[r], offset);
        break;
      case 4:
        fprintf(Outfile, "\tadd\tdword [%s], %d\n", reglist[r], offset);
        break;
      case 8:
        fprintf(Outfile, "\tadd\tqword [%s], %d\n", reglist[r], offset);
        break;
    }
  }

  // 现在用值加载输出寄存器
  if (sym->class == C_LOCAL || sym->class == C_PARAM) {
    switch (sym->size) {
      case 1:
        fprintf(Outfile, "\tmovzx\t%s, byte [rbp+%d]\n", reglist[r], sym->st_posn);
        break;
      case 4:
        fprintf(Outfile, "\tmovsxd\t%s, dword [rbp+%d]\n", reglist[r], sym->st_posn);
        break;
      case 8:
        fprintf(Outfile, "\tmov\t%s, [rbp+%d]\n", reglist[r], sym->st_posn);
    }
  } else {
    switch (sym->size) {
      case 1:
        fprintf(Outfile, "\tmovzx\t%s, byte [%s]\n", reglist[r], sym->name);
        break;
      case 4:
        fprintf(Outfile, "\tmovsxd\t%s, dword [%s]\n", reglist[r], sym->name);
        break;
      case 8:
        fprintf(Outfile, "\tmov\t%s, [%s]\n", reglist[r], sym->name);
    }
  }

  // 如果我们有后操作,获取一个新寄存器
  if (op == A_POSTINC || op == A_POSTDEC) {
    postreg = alloc_register();

    // 加载符号的地址
    if (sym->class == C_LOCAL || sym->class == C_PARAM)
      fprintf(Outfile, "\tlea\t%s, [rbp+%d]\n", reglist[postreg], sym->st_posn);
    else
      fprintf(Outfile, "\tlea\t%s, [%s]\n", reglist[postreg], sym->name);
    // 然后更该该地址处的值

    switch (sym->size) {
      case 1:
        fprintf(Outfile, "\tadd\tbyte [%s], %d\n", reglist[postreg], offset);
        break;
      case 4:
        fprintf(Outfile, "\tadd\tdword [%s], %d\n", reglist[postreg], offset);
        break;
      case 8:
        fprintf(Outfile, "\tadd\tqword [%s], %d\n", reglist[postreg], offset);
        break;
    }
    // 释放寄存器
    free_register(postreg);
  }

  // 返回带有值的寄存器
  return (r);
}

// 给定全局字符串的标签号,
// 将其地址加载到新寄存器中
int cgloadglobstr(int label) {
  // 获取一个新寄存器
  int r = alloc_register();
  fprintf(Outfile, "\tmov\t%s, L%d\n", reglist[r], label);
  return (r);
}

// 将两个寄存器相加并返回
// 带有结果的寄存器号
int cgadd(int r1, int r2) {
  fprintf(Outfile, "\tadd\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

// 从第一个寄存器减去第二个并
// 返回带有结果的寄存器号
int cgsub(int r1, int r2) {
  fprintf(Outfile, "\tsub\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

// 将两个寄存器相乘并返回
// 带有结果的寄存器号
int cgmul(int r1, int r2) {
  fprintf(Outfile, "\timul\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

// 将第一个寄存器除以第二个并
// 返回带有结果的寄存器号
int cgdivmod(int r1, int r2, int op) {
  fprintf(Outfile, "\tmov\trax, %s\n", reglist[r1]);
  fprintf(Outfile, "\tcqo\n");
  fprintf(Outfile, "\tidiv\t%s\n", reglist[r2]);
  if (op == A_DIVIDE)
    fprintf(Outfile, "\tmov\t%s, rax\n", reglist[r1]);
  else
    fprintf(Outfile, "\tmov\t%s, rdx\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgand(int r1, int r2) {
  fprintf(Outfile, "\tand\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

int cgor(int r1, int r2) {
  fprintf(Outfile, "\tor\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

int cgxor(int r1, int r2) {
  fprintf(Outfile, "\txor\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

int cgshl(int r1, int r2) {
  fprintf(Outfile, "\tmov\tcl, %s\n", breglist[r2]);
  fprintf(Outfile, "\tshl\t%s, cl\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgshr(int r1, int r2) {
  fprintf(Outfile, "\tmov\tcl, %s\n", breglist[r2]);
  fprintf(Outfile, "\tshr\t%s, cl\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

// 取反寄存器的值
int cgnegate(int r) {
  fprintf(Outfile, "\tneg\t%s\n", reglist[r]);
  return (r);
}

// 反转寄存器的值
int cginvert(int r) {
  fprintf(Outfile, "\tnot\t%s\n", reglist[r]);
  return (r);
}

// 逻辑上否定寄存器的值
int cglognot(int r) {
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r], reglist[r]);
  fprintf(Outfile, "\tsete\t%s\n", breglist[r]);
  fprintf(Outfile, "\tmovzx\t%s, %s\n", reglist[r], breglist[r]);
  return (r);
}

// 将布尔值(仅 0 或 1)
// 加载到给定寄存器中
void cgloadboolean(int r, int val) {
  fprintf(Outfile, "\tmov\t%s, %d\n", reglist[r], val);
}

// 将整数值转换为布尔值。如果是 IF、WHILE、LOGAND 或 LOGOR 操作则跳转
int cgboolean(int r, int op, int label) {
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r], reglist[r]);
  switch (op) {
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
      fprintf(Outfile, "\tmovzx\t%s, byte %s\n", reglist[r], breglist[r]);
  }
  return (r);
}

// 使用给定符号 id 调用函数
// 弹出在栈上推送的任何参数
// 返回带有结果的寄存器
int cgcall(struct symtable *sym, int numargs) {
  int outr;

  // 调用函数
  if(!sym->extinit) {
    fprintf(Outfile, "extern\t%s\n", sym->name);
    sym->extinit = 1;
  }
  fprintf(Outfile, "\tcall\t%s\n", sym->name);

  // 删除在栈上推送的任何参数
  if (numargs > 6) 
    fprintf(Outfile, "\tadd\trsp, %d\n", 8 * (numargs - 6));
  // 恢复所有溢出的寄存器
  unspill_all_regs();
  // 获取一个新寄存器并将返回值复制到其中
  outr = alloc_register();
  fprintf(Outfile, "\tmov\t%s, rax\n", reglist[outr]);
  return (outr);
}

// 给定带有参数值的寄存器,
// 将此参数复制到第 argposn'th 个参数中,为将来的函数调用做准备。
// 注意 argposn 是 1, 2, 3, 4, ..., 从不为零。
void cgcopyarg(int r, int argposn) {

  // 如果大于第六个参数,只需将寄存器压入栈。
  // 我们依赖于以正确的顺序调用带有连续参数的函数,用于 x86-64
  if (argposn > 6) {
    fprintf(Outfile, "\tpush\t%s\n", reglist[r]);
  } else {
    // 否则,将值复制到用于保存参数值的六个寄存器之一
    fprintf(Outfile, "\tmov\t%s, %s\n", 
	    reglist[FIRSTPARAMREG - argposn + 1], reglist[r]);
  }
  free_register(r);
}

// 将寄存器左移一个常量
int cgshlconst(int r, int val) {
  fprintf(Outfile, "\tsal\t%s, %d\n", reglist[r], val);
  return (r);
}

// 将寄存器的值存储到变量中
int cgstorglob(int r, struct symtable *sym) {
  if(!sym->extinit) {
    fprintf(Outfile, "extern\t%s\n", sym->name);
    sym->extinit = 1;
  }
  if (cgprimsize(sym->type) == 8) {
    fprintf(Outfile, "\tmov\t[%s], %s\n", sym->name, reglist[r]);
  } else
  switch (sym->type) {
    case P_CHAR:
      fprintf(Outfile, "\tmov\t[%s], %s\n", sym->name, breglist[r]);
      break;
    case P_INT:
      fprintf(Outfile, "\tmov\t[%s], %s\n", sym->name, dreglist[r]);
      break;
    default:
      fatald("Bad type in cgloadglob:", sym->type);
  }
  return (r);
}

// 将寄存器的值存储到局部变量中
int cgstorlocal(int r, struct symtable *sym) {
  if (cgprimsize(sym->type) == 8) {
    fprintf(Outfile, "\tmov\tqword\t[rbp+%d], %s\n", sym->st_posn,
            reglist[r]);
  } else
  switch (sym->type) {
    case P_CHAR:
      fprintf(Outfile, "\tmov\tbyte\t[rbp+%d], %s\n", sym->st_posn,
              breglist[r]);
      break;
    case P_INT:
      fprintf(Outfile, "\tmov\tdword\t[rbp+%d], %s\n", sym->st_posn,
              dreglist[r]);
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

  // 获取变量的大小(或其元素如果是数组)
  // 以及变量的类型
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
    fprintf(Outfile, "\tglobal\t%s\n", node->name);
  if(!node->extinit) {
    node->extinit = 1;
  }
  fprintf(Outfile, "%s:\n", node->name);

  // 为一个或多个元素输出空间
  for (i = 0; i < node->nelems; i++) {

    // 获取任何初始值
    initvalue = 0;
    if (node->initlist != NULL)
      initvalue = node->initlist[i];

    // 为此类型生成空间
    // 原始版本
    switch(size) {
      case 1:
        fprintf(Outfile, "\tdb\t%d\n", initvalue);
        break;
      case 4:
        fprintf(Outfile, "\tdd\t%d\n", initvalue);
        break;
      case 8:
        // 生成指向字符串字面量的指针。 将零值视为
        // 实际零,而不是标签 L0
        if (node->initlist != NULL && type == pointer_to(P_CHAR) && initvalue != 0)
          fprintf(Outfile, "\tdq\tL%d\n", initvalue);
        else
          fprintf(Outfile, "\tdq\t%d\n", initvalue);
        break;
      default:
        for (i = 0; i < size; i++) 
          fprintf(Outfile, "\tdb\t0\n");
    }
  }

}

// 生成全局字符串及其开始标签
// 如果 append 为 true,则不输出标签。
void cgglobstr(int l, char *strvalue, int append) {
  char *cptr;
  if (!append)
    cglabel(l);
  for (cptr = strvalue; *cptr; cptr++) {
    fprintf(Outfile, "\tdb\t%d\n", *cptr);
  }
}

void cgglobstrend(void) {
  fprintf(Outfile, "\tdb\t0\n");
}

// 比较指令列表,
// 按 AST 顺序: A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *cmplist[] =
  { "sete", "setne", "setl", "setg", "setle", "setge" };

// 比较两个寄存器并在为真时设置。
int cgcompare_and_set(int ASTop, int r1, int r2, int type) {
  int size = cgprimsize(type);

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  switch (size) {
    case 1:
      fprintf(Outfile, "\tcmp\t%s, %s\n", breglist[r1], breglist[r2]);
      break;
    case 4:
      fprintf(Outfile, "\tcmp\t%s, %s\n", dreglist[r1], dreglist[r2]);
      break;
    default:
      fprintf(Outfile, "\tcmp\t%s, %s\n", reglist[r1], reglist[r2]);
  }
  fprintf(Outfile, "\t%s\t%s\n", cmplist[ASTop - A_EQ], breglist[r2]);
  fprintf(Outfile, "\tmovzx\t%s, %s\n", reglist[r2], breglist[r2]);
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

// 反转跳转指令列表,
// 按 AST 顺序: A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *invcmplist[] = { "jne", "je", "jge", "jle", "jg", "jl" };

// 比较两个寄存器并在为假时跳转。
int cgcompare_and_jump(int ASTop, int r1, int r2, int label, int type) {
  int size = cgprimsize(type);

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  switch (size) {
    case 1:
      fprintf(Outfile, "\tcmp\t%s, %s\n", breglist[r1], breglist[r2]);
      break;
    case 4:
      fprintf(Outfile, "\tcmp\t%s, %s\n", dreglist[r1], dreglist[r2]);
      break;
    default:
      fprintf(Outfile, "\tcmp\t%s, %s\n", reglist[r1], reglist[r2]);
  }
  fprintf(Outfile, "\t%s\tL%d\n", invcmplist[ASTop - A_EQ], label);
  freeall_registers(NOREG);
  return (NOREG);
}

// 将寄存器中的值从旧类型宽展到新类型,并返回带有此新值的寄存器
int cgwiden(int r, int oldtype, int newtype) {
  // 无操作
  return (r);
}

// 生成从函数返回值的代码
void cgreturn(int reg, struct symtable *sym) {

  // 只有当我们有要返回的值时才返回值
  if (reg != NOREG) {
    // 在这里处理指针,因为我们不能将它们放入 switch 语句
    if (ptrtype(sym->type))
      fprintf(Outfile, "\tmov\trax, %s\n", reglist[reg]);
    else {
      // 根据函数类型生成代码
      switch (sym->type) {
        case P_CHAR:
          fprintf(Outfile, "\tmovzx\teax, %s\n", breglist[reg]);
          break;
        case P_INT:
          fprintf(Outfile, "\tmov\teax, %s\n", dreglist[reg]);
          break;
        case P_LONG:
          fprintf(Outfile, "\tmov\trax, %s\n", reglist[reg]);
          break;
        default:
          fatald("Bad function type in cgreturn:", sym->type);
      }
    }
  }
  cgjump(sym->st_endlabel);
}

// 生成将标识符的地址加载到变量中的代码。返回一个新寄存器
int cgaddress(struct symtable *sym) {
  int r = alloc_register();

  if (!sym->extinit) {
    fprintf(Outfile, "extern\t%s\n", sym->name);
    sym->extinit = 1;
  }

  if (sym->class == C_GLOBAL ||
      sym->class == C_EXTERN || sym->class == C_STATIC)
    fprintf(Outfile, "\tmov\t%s, %s\n", reglist[r], sym->name);
  else
    fprintf(Outfile, "\tlea\t%s, [rbp+%d]\n", reglist[r],
            sym->st_posn);
  return (r);
}

// 解引用指针并将其指向的值获取到同一寄存器中
int cgderef(int r, int type) {
  // 获取我们指向的类型
  int newtype = value_at(type);
  // 现在获取此类型的大小
  int size = cgprimsize(newtype);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tmovzx\t%s, byte [%s]\n", reglist[r], reglist[r]);
      break;
    case 4:
      fprintf(Outfile, "\tmovsx\t%s, dword [%s]\n", reglist[r], reglist[r]);
      break;
    case 8:
      fprintf(Outfile, "\tmov\t%s, [%s]\n", reglist[r], reglist[r]);
      break;
    default:
      fatald("Can't cgderef on type:", type);
  }
  return (r);
}

// 通过解引用指针存储
int cgstorderef(int r1, int r2, int type) {
  // 获取类型的大小
  int size = cgprimsize(type);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tmov\t[%s], byte %s\n", reglist[r2], breglist[r1]);
      break;
    case 4:
      fprintf(Outfile, "\tmov\t[%s], dword %s\n", reglist[r2], dreglist[r1]);
      break;
    case 8:
      fprintf(Outfile, "\tmov\t[%s], %s\n", reglist[r2], reglist[r1]);
      break;
    default:
      fatald("Can't cgstoderef on type:", type);
  }
  return (r1);
}

// 生成 switch 跳转表和用于加载寄存器并调用 switch() 代码的代码
void cgswitch(int reg, int casecount, int toplabel,
	      int *caselabel, int *caseval, int defaultlabel) {
  int i, label;

  // 为 switch 表获取一个标签
  label = genlabel();
  cglabel(label);

  // 启发式。如果我们没有 case,创建一个指向 default case 的 case
  if (casecount == 0) {
    caseval[0] = 0;
    caselabel[0] = defaultlabel;
    casecount = 1;
  }
  // 生成 switch 跳转表。
  fprintf(Outfile, "\tdq\t%d\n", casecount);
  for (i = 0; i < casecount; i++)
    fprintf(Outfile, "\tdq\t%d, L%d\n", caseval[i], caselabel[i]);
  fprintf(Outfile, "\tdq\tL%d\n", defaultlabel);

  // 加载特定寄存器
  cglabel(toplabel);
  fprintf(Outfile, "\tmov\trax, %s\n", reglist[reg]);
  fprintf(Outfile, "\tmov\trdx, L%d\n", label);
  fprintf(Outfile, "\tjmp\t__switch\n");
}

// 在寄存器之间移动值
void cgmove(int r1, int r2) {
  fprintf(Outfile, "\tmov\t%s, %s\n", reglist[r2], reglist[r1]);
}

void cglinenum(int line) {
  //fprintf(Outfile, ";\t.loc 1 %d 0\n", line);
}