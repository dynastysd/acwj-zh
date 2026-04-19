#include "defs.h"
#include "data.h"
#include "decl.h"

// x86-64代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3

// 标志我们当前输出的段
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

// 给定标量类型值，返回
// 该类型的字节大小
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
  return (0);			// 保持-Wall愉快
}

// 给定标量类型，现有内存偏移量
// （尚未分配给任何东西）
// 和方向（1是向上，-1是向下），计算
// 并返回适合此标量类型的内存偏移量
// 这可能是原始偏移量，
// 也可能在原始偏移量的上方或下方
int cgalign(int type, int offset, int direction) {
  int alignment;

  // 在x86-64上我们不需要这样做，但让我们在任何偏移量上对齐char，
  // 在4字节对齐上对齐int/指针
  switch(type) {
    case P_CHAR: return (offset);
    case P_INT:
    case P_LONG: break;
    default:     fatald("Bad type in calc_aligned_offset:", type);
  }

  // 这里我们有int或long。在4字节偏移量上对齐它
  // 我把通用代码放在这里以便其他地方可以重用
  alignment= 4;
  offset = (offset + direction * (alignment-1)) & ~(alignment-1);
  return (offset);
}

// 下一个局部变量相对于栈基指针的位置
// 我们将偏移量存储为正数以使对齐栈指针更容易
static int localOffset;
static int stackOffset;

// 创建新局部变量的位置
int newlocaloffset(int type) {
  // 偏移量至少减少4字节
  // 并在栈上分配
  localOffset += (cgprimsize(type) > 4) ? cgprimsize(type) : 4;
  return (-localOffset);
}

// 可用寄存器列表及其名称
// 我们还需要字节和双字寄存器的列表
// 列表还包括用于保存函数参数的寄存器
#define NUMFREEREGS 4
#define FIRSTPARAMREG 9		// 第一个参数寄存器在上述寄存器列表中的位置
static int freereg[NUMFREEREGS];
static char *reglist[]  = { "r10",  "r11", "r12", "r13", "r9", "r8", "rcx", "rdx", "rsi",
"rdi"  };
static char *breglist[]  = { "r10b",  "r11b", "r12b", "r13b", "r9b", "r8b", "cl", "dl", "sil",
"dil"  };
static char *dreglist[]  = { "r10d",  "r11d", "r12d", "r13d", "r9d", "r8d", "ecx", "edx",
"esi", "edi"  };

// 将所有寄存器设置为可用
void freeall_registers(void) {
  freereg[0] = freereg[1] = freereg[2] = freereg[3] = 1;
}

// 分配一个空闲寄存器。返回
// 寄存器的编号。如果没有可用寄存器则报错
static int alloc_register(void) {
  for (int i = 0; i < NUMFREEREGS; i++) {
    if (freereg[i]) {
      freereg[i] = 0;
      return (i);
    }
  }
  fatal("Out of registers");
  return (NOREG);		// 保持-Wall愉快
}

// 将寄存器返回到可用寄存器列表
// 检查它是否已经在那里
static void free_register(int reg) {
  if (freereg[reg] != 0)
    fatald("Error trying to free register", reg);
  freereg[reg] = 1;
}

// 打印汇编前导码
void cgpreamble() {
  freeall_registers();
  fputs("\textern\tprintint\n", Outfile);
  fputs("\textern\tprintchar\n", Outfile);
  fputs("\textern\topen\n", Outfile);
  fputs("\textern\tclose\n", Outfile);
  fputs("\textern\tread\n", Outfile);
  fputs("\textern\twrite\n", Outfile);
  fputs("\textern\tprintf\n", Outfile);
}

// 无事可做
void cgpostamble() {
}

// 打印函数前导码
void cgfuncpreamble(struct symtable *sym) {
  char *name = sym->name;
  struct symtable *parm, *locvar;
  int cnt;
  int paramOffset = 16;		// 任何推送的参数从此栈偏移量开始
  int paramReg = FIRSTPARAMREG;	// 第一个参数寄存器在上述寄存器列表中的索引

  // 输出到文本段，重置局部偏移量
  cgtextseg();
  localOffset = 0;

  // 输出函数开始，保存rsp和rbp
  fprintf(Outfile,
	  "\tglobal\t%s\n"
	  "%s:\n" "\tpush\trbp\n"
	  "\tmov\trbp, rsp\n", name, name);

  // 将任何在寄存器中的参数复制到栈，最多六个
  // 其余参数已经在栈上
  for (parm = sym->member, cnt = 1; parm != NULL; parm = parm->next, cnt++) {
    if (cnt > 6) {
      parm->posn = paramOffset;
      paramOffset += 8;
    } else {
      parm->posn = newlocaloffset(parm->type);
      cgstorlocal(paramReg--, parm);
    }
  }

  // 对于其余的，如果是参数那么它们
  // 已经在栈上。如果是局部变量，则创建栈位置
  for (locvar = Loclhead; locvar != NULL; locvar = locvar->next) {
    locvar->posn = newlocaloffset(locvar->type);
  }

  // 将栈指针对齐到16的倍数
  // 小于其之前的值
  stackOffset = (localOffset + 15) & ~15;
  fprintf(Outfile, "\tadd\trsp, %d\n", -stackOffset);
}

// 打印函数后置码
void cgfuncpostamble(struct symtable *sym) {
  cglabel(sym->endlabel);
  fprintf(Outfile, "\tadd\trsp, %d\n", stackOffset);
  fputs("\tpop	rbp\n" "\tret\n", Outfile);
}

// 将整数字面量值加载到寄存器中
// 返回寄存器的编号
// 对于x86-64，我们不需要担心类型
int cgloadint(int value, int type) {
  // 获取新寄存器
  int r = alloc_register();

  fprintf(Outfile, "\tmov\t%s, %d\n", reglist[r], value);
  return (r);
}

// 将变量值加载到寄存器中
// 返回寄存器的编号。如果
// 操作是前置或后置递增/递减，
// 也要执行此操作
int cgloadglob(struct symtable *sym, int op) {
  // 获取新寄存器
  int r = alloc_register();

  if (cgprimsize(sym->type) == 8) {
    if (op == A_PREINC)
      fprintf(Outfile, "\tinc\tqword [%s]\n", sym->name);
    if (op == A_PREDEC)
      fprintf(Outfile, "\tdec\tqword [%s]\n", sym->name);
    fprintf(Outfile, "\tmov\t%s, [%s]\n", reglist[r], sym->name);
    if (op == A_POSTINC)
      fprintf(Outfile, "\tinc\tqword [%s]\n", sym->name);
    if (op == A_POSTDEC)
      fprintf(Outfile, "\tdec\tqword [%s]\n", sym->name);
  } else
  // 打印初始化代码
  switch (sym->type) {
    case P_CHAR:
      if (op == A_PREINC)
        fprintf(Outfile, "\tinc\tbyte [%s]\n", sym->name);
      if (op == A_PREDEC)
        fprintf(Outfile, "\tdec\tbyte [%s]\n", sym->name);
      fprintf(Outfile, "\tmovzx\t%s, byte [%s]\n", reglist[r], 
              sym->name);
      if (op == A_POSTINC)
        fprintf(Outfile, "\tinc\tbyte [%s]\n", sym->name);
      if (op == A_POSTDEC)
        fprintf(Outfile, "\tdec\tbyte [%s]\n", sym->name);
      break;
    case P_INT:
      if (op == A_PREINC)
        fprintf(Outfile, "\tinc\tdword [%s]\n", sym->name);
      if (op == A_PREDEC)
        fprintf(Outfile, "\tdec\tdword [%s]\n", sym->name);
      fprintf(Outfile, "\tmovsx\t%s, word [%s]\n", dreglist[r], 
              sym->name);
      fprintf(Outfile, "\tmovsxd\t%s, %s\n", reglist[r], dreglist[r]);
      if (op == A_POSTINC)
        fprintf(Outfile, "\tinc\tdword [%s]\n", sym->name);
      if (op == A_POSTDEC)
        fprintf(Outfile, "\tdec\tdword [%s]\n", sym->name);
      break;
    default:
      fatald("Bad type in cgloadglob:", sym->type);
  }
  return (r);
}

// 将局部变量值加载到寄存器中
// 返回寄存器的编号。如果
// 操作是前置或后置递增/递减，
// 也要执行此操作
int cgloadlocal(struct symtable *sym, int op) {
  // 获取新寄存器
  int r = alloc_register();

  // 打印初始化代码
  if (cgprimsize(sym->type) == 8) {
    if (op == A_PREINC)
      fprintf(Outfile, "\tinc\tqword\t[rbp+%d]\n", sym->posn);
    if (op == A_PREDEC)
      fprintf(Outfile, "\tdec\tqword\t[rbp+%d]\n", sym->posn);
    fprintf(Outfile, "\tmov\t%s, [rbp+%d]\n", reglist[r],
            sym->posn);
    if (op == A_POSTINC)
      fprintf(Outfile, "\tinc\tqword\t[rbp+%d]\n", sym->posn);
    if (op == A_POSTDEC)
      fprintf(Outfile, "\tdec\tqword\t[rbp+%d]\n", sym->posn);
  } else
  switch (sym->type) {
    case P_CHAR:
      if (op == A_PREINC)
        fprintf(Outfile, "\tinc\tbyte\t[rbp+%d]\n", sym->posn);
      if (op == A_PREDEC)
        fprintf(Outfile, "\tdec\tbyte\t[rbp+%d]\n", sym->posn);
      fprintf(Outfile, "\tmovzx\t%s, byte [rbp+%d]\n", reglist[r], 
              sym->posn);
      if (op == A_POSTINC)
        fprintf(Outfile, "\tinc\tbyte\t[rbp+%d]\n", sym->posn);
      if (op == A_POSTDEC)
        fprintf(Outfile, "\tdec\tbyte\t[rbp+%d]\n", sym->posn);
      break;
    case P_INT:
      if (op == A_PREINC)
        fprintf(Outfile, "\tinc\tdword\t[rbp+%d]\n", sym->posn);
      if (op == A_PREDEC)
        fprintf(Outfile, "\tdec\tdword\t[rbp+%d]\n", sym->posn);
      fprintf(Outfile, "\tmovsx\t%s, word [rbp+%d]\n", reglist[r], 
              sym->posn);
      fprintf(Outfile, "\tmovsxd\t%s, %s\n", reglist[r], dreglist[r]);
      if (op == A_POSTINC)
      if (op == A_POSTINC)
        fprintf(Outfile, "\tinc\tdword\t[rbp+%d]\n", sym->posn);
      if (op == A_POSTDEC)
        fprintf(Outfile, "\tdec\tdword\t[rbp+%d]\n", sym->posn);
      break;
    default:
      fatald("Bad type in cgloadlocal:", sym->type);
  }
  return (r);
}

// 给定全局字符串的标签号，
// 将其地址加载到新寄存器中
int cgloadglobstr(int label) {
  // 获取新寄存器
  int r = alloc_register();
  fprintf(Outfile, "\tmov\t%s, L%d\n", reglist[r], label);
  return (r);
}

// 将两个寄存器相加并返回
// 包含结果的寄存器编号
int cgadd(int r1, int r2) {
  fprintf(Outfile, "\tadd\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

// 从第一个寄存器减去第二个寄存器并
// 返回包含结果的寄存器编号
int cgsub(int r1, int r2) {
  fprintf(Outfile, "\tsub\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

// 将两个寄存器相乘并返回
// 包含结果的寄存器编号
int cgmul(int r1, int r2) {
  fprintf(Outfile, "\timul\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

// 用第一个寄存器除以第二个寄存器并
// 返回包含结果的寄存器编号
int cgdiv(int r1, int r2) {
  fprintf(Outfile, "\tmov\trax, %s\n", reglist[r1]);
  fprintf(Outfile, "\tcqo\n");
  fprintf(Outfile, "\tidiv\t%s\n", reglist[r2]);
  fprintf(Outfile, "\tmov\t%s, rax\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

int cgand(int r1, int r2) {
  fprintf(Outfile, "\tand\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

int cgor(int r1, int r2) {
  fprintf(Outfile, "\tor\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

int cgxor(int r1, int r2) {
  fprintf(Outfile, "\txor\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
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

// 求反寄存器的值
int cgnegate(int r) {
  fprintf(Outfile, "\tneg\t%s\n", reglist[r]);
  return (r);
}

// 反转寄存器的值
int cginvert(int r) {
  fprintf(Outfile, "\tnot\t%s\n", reglist[r]);
  return (r);
}

// 逻辑否定寄存器的值
int cglognot(int r) {
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r], reglist[r]);
  fprintf(Outfile, "\tsete\t%s\n", breglist[r]);
  fprintf(Outfile, "\tmovzx\t%s, %s\n", reglist[r], breglist[r]);
  return (r);
}

// 将整数值转换为布尔值。如果是
// IF或WHILE操作则跳转
int cgboolean(int r, int op, int label) {
  fprintf(Outfile, "\ttest\t%s, %s\n", reglist[r], reglist[r]);
  if (op == A_IF || op == A_WHILE)
    fprintf(Outfile, "\tje\tL%d\n", label);
  else {
    fprintf(Outfile, "\tsetnz\t%s\n", breglist[r]);
    fprintf(Outfile, "\tmovzx\t%s, byte %s\n", reglist[r], breglist[r]);
  }
  return (r);
}

// 使用给定的符号id调用函数
// 弹出栈上推送的任何参数
// 返回包含结果的寄存器
int cgcall(struct symtable *sym, int numargs) {
  // 获取新寄存器
  int outr = alloc_register();
  // 调用函数
  fprintf(Outfile, "\tcall\t%s\n", sym->name);
  // 移除栈上推送的任何参数
  if (numargs > 6) 
    fprintf(Outfile, "\tadd\trsp, %d\n", 8 * (numargs - 6));
  // 将返回值复制到我们的寄存器
  fprintf(Outfile, "\tmov\t%s, rax\n", reglist[outr]);
  return (outr);
}

// 给定带有参数值的寄存器，
// 将此参数复制到argposn'th参数中
// 为将来的函数调用做准备。注意argposn是1, 2, 3, 4, ...，永不为零
void cgcopyarg(int r, int argposn) {

  // 如果这超过第六个参数，只需将
  // 寄存器压入栈。我们依赖于以正确的顺序
  // 为x86-64调用连续参数
  if (argposn > 6) {
    fprintf(Outfile, "\tpush\t%s\n", reglist[r]);
  } else {
    // 否则，将值复制到六个用于
    // 保存参数值的寄存器之一
    fprintf(Outfile, "\tmov\t%s, %s\n", 
	    reglist[FIRSTPARAMREG - argposn + 1], reglist[r]);
  }
}

// 将寄存器左移一个常量
int cgshlconst(int r, int val) {
  fprintf(Outfile, "\tsal\t%s, %d\n", reglist[r], val);
  return (r);
}

// 将寄存器的值存储到变量中
int cgstorglob(int r, struct symtable *sym) {
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
    fprintf(Outfile, "\tmov\tqword\t[rbp+%d], %s\n", sym->posn,
            reglist[r]);
  } else
  switch (sym->type) {
    case P_CHAR:
      fprintf(Outfile, "\tmov\tbyte\t[rbp+%d], %s\n", sym->posn,
              breglist[r]);
      break;
    case P_INT:
      fprintf(Outfile, "\tmov\tdword\t[rbp+%d], %s\n", sym->posn,
              dreglist[r]);
      break;
    default:
      fatald("Bad type in cgstorlocal:", sym->type);
  }
  return (r);
}

// 生成全局符号但不是函数
void cgglobsym(struct symtable *node) {
  int size;

  if (node == NULL)
    return;
  if (node->stype == S_FUNCTION)
    return;
  // 获取类型的大小
  size = typesize(node->type, node->ctype);

  // 生成全局标识和标签
  cgdataseg();
  fprintf(Outfile, "\tsection\t.data\n" "\tglobal\t%s\n", node->name);
  fprintf(Outfile, "%s:", node->name);

  // 为此类型生成空间
  // 原始版本
  for (int i = 0; i < node->size; i++) {
    switch(size) {
      case 1:
        fprintf(Outfile, "\tdb\t0\n");
        break;
      case 4:
        fprintf(Outfile, "\tdd\t0\n");
        break;
      case 8:
        fprintf(Outfile, "\tdq\t0\n");
        break;
      default:
        for (int i = 0; i < size; i++) 
          fprintf(Outfile, "\tdb\t0\n");
    }
  }

  /* 使用times而不是loop的紧凑版本
  switch(size) {
    case 1:
      fprintf(Outfile, "\ttimes\t%d\tdb\t0\n", node->size);
      break;
    case 4:
      fprintf(Outfile, "\ttimes\t%d\tdd\t0\n", node->size);
      break;
    case 8:
      fprintf(Outfile, "\ttimes\t%d\tdq\t0\n", node->size);
      break;
    default:
      fprintf(Outfile, "\ttimes\t%d\tdb\t0\n", size);
  }
  */
}

// 生成全局字符串及其起始标签
void cgglobstr(int l, char *strvalue) {
  char *cptr;
  cglabel(l);
  for (cptr = strvalue; *cptr; cptr++) {
    fprintf(Outfile, "\tdb\t%d\n", *cptr);
  }
  fprintf(Outfile, "\tdb\t0\n");

  /* 将字符串以可读格式放在一行
  // 可能在错误检查方面有点过分
  int comma = 0, quote = 0, start = 1;
  fprintf(Outfile, "\tdb\t");
  for (cptr=strvalue; *cptr; cptr++) {
    if ( ! isprint(*cptr) )
      if (comma || start) {
        fprintf(Outfile, "%d, ", *cptr);
        start = 0;
        comma = 1;
      }
      else if (quote) {
        fprintf(Outfile, "\', %d, ", *cptr);
        comma = 1;
        quote = 0;
      }
      else {
        fprintf(Outfile, "%d, ", *cptr);
        comma = 1;
        quote = 0;
      }
    else
      if (start || comma) {
        fprintf(Outfile, "\'%c", *cptr);
        start = comma = 0;
        quote = 1;
      }
      else {
        fprintf(Outfile, "%c", *cptr);
        comma = 0;
        quote = 1;
      }
  }
  if (comma || start)
    fprintf(Outfile, "0\n");
  else
    fprintf(Outfile, "\', 0\n");
  */
}

// 比较指令列表，
// 按AST顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *cmplist[] =
  { "sete", "setne", "setl", "setg", "setle", "setge" };

// 比较两个寄存器并在为真时设置
int cgcompare_and_set(int ASTop, int r1, int r2) {

  // 检查AST操作的范围内
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  fprintf(Outfile, "\tcmp\t%s, %s\n", reglist[r1], reglist[r2]);
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

// 反转跳转指令列表，
// 按AST顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *invcmplist[] = { "jne", "je", "jge", "jle", "jg", "jl" };

// 比较两个寄存器并在为假时跳转
int cgcompare_and_jump(int ASTop, int r1, int r2, int label) {

  // 检查AST操作的范围内
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  fprintf(Outfile, "\tcmp\t%s, %s\n", reglist[r1], reglist[r2]);
  fprintf(Outfile, "\t%s\tL%d\n", invcmplist[ASTop - A_EQ], label);
  freeall_registers();
  return (NOREG);
}

// 将寄存器中的值从旧类型扩展到新类型，
// 并返回包含此新值的寄存器
int cgwiden(int r, int oldtype, int newtype) {
  // 无事可做
  return (r);
}

// 生成从函数返回值的代码
void cgreturn(int reg, struct symtable *sym) {
  // 根据函数的类型生成代码
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
  cgjump(sym->endlabel);
}

// 生成将标识符的地址加载到变量中的代码
// 返回一个新寄存器
int cgaddress(struct symtable *sym) {
  int r = alloc_register();

  if (sym->class == C_GLOBAL)
    fprintf(Outfile, "\tmov\t%s, %s\n", reglist[r], sym->name);
  else
    fprintf(Outfile, "\tlea\t%s, [rbp+%d]\n", reglist[r],
            sym->posn);
  return (r);
}

// 解引用指针以获取其指向的值
// 到同一寄存器
int cgderef(int r, int type) {
  // 获取我们指向的类型
  int newtype = value_at(type);
  // 现在获取此类型的大小
  int size = cgprimsize(newtype);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tmovzx\t%s, byte [%s]\n", reglist[r], reglist[r]);
      break;
    case 2:
      fprintf(Outfile, "\tmovsx\t%s, dword [%s]\n", reglist[r], reglist[r]);
      break;
    case 4:
    case 8:
      fprintf(Outfile, "\tmov\t%s, [%s]\n", reglist[r], reglist[r]);
      break;
    default:
      fatald("Can't cgderef on type:", type);
  }
  return (r);
}

// 通过解引用的指针存储
int cgstorderef(int r1, int r2, int type) {
  //获取类型的大小
  int size = cgprimsize(type);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tmov\t[%s], byte %s\n", reglist[r2], breglist[r1]);
      break;
    case 2:
    case 4:
    case 8:
      fprintf(Outfile, "\tmov\t[%s], %s\n", reglist[r2], reglist[r1]);
      break;
    default:
      fatald("Can't cgstoderef on type:", type);
  }
  return (r1);
}