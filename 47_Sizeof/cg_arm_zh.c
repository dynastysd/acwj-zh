#include "defs.h"
#include "data.h"
#include "decl.h"

// ARMv6（Raspberry Pi）代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3


// 可用寄存器列表及其名称。
static int freereg[4];
static char *reglist[4] = { "r4", "r5", "r6", "r7" };

// 将所有寄存器设置为可用
void freeall_registers(void) {
  freereg[0] = freereg[1] = freereg[2] = freereg[3] = 1;
}

// 分配一个空闲寄存器。返回
// 寄存器的编号。如果没有可用寄存器则报错。
static int alloc_register(void) {
  for (int i = 0; i < 4; i++) {
    if (freereg[i]) {
      freereg[i] = 0;
      return (i);
    }
  }
  fatal("Out of registers");
  return (NOREG);		// 保持 -Wall 高兴
}

// 将寄存器返回到可用寄存器列表。
// 检查它是否已经在那里。
static void free_register(int reg) {
  if (freereg[reg] != 0)
    fatald("Error trying to free register", reg);
  freereg[reg] = 1;
}

// 我们必须将大整数字面量值存储在内存中。
// 保留一个列表，它们将在后导码中输出
#define MAXINTS 1024
int Intlist[MAXINTS];
static int Intslot = 0;

// 确定大整数字面量
// 相对于 .L3 标签的偏移量。如果整数
// 不在列表中，则添加它。
static void set_int_offset(int val) {
  int offset = -1;

  // 查看它是否已经在那里
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
  // 将 r3 加载此偏移量
  fprintf(Outfile, "\tldr\tr3, .L3+%d\n", offset);
}

// 输出汇编前导码
void cgpreamble() {
  freeall_registers();
  fputs("\t.text\n", Outfile);
}

// 输出汇编后导码
void cgpostamble() {

  // 输出全局变量
  fprintf(Outfile, ".L2:\n");
  for (int i = 0; i < Globs; i++) {
    if (Symtable[i].stype == S_VARIABLE)
      fprintf(Outfile, "\t.word %s\n", Symtable[i].name);
  }

  // 输出整数字面量
  fprintf(Outfile, ".L3:\n");
  for (int i = 0; i < Intslot; i++) {
    fprintf(Outfile, "\t.word %d\n", Intlist[i]);
  }
}

// 输出函数前导码
void cgfuncpreamble(int id) {
  char *name = Symtable[id].name;
  fprintf(Outfile,
	  "\t.text\n"
	  "\t.globl\t%s\n"
	  "\t.type\t%s, \%%function\n"
	  "%s:\n" "\tpush\t{fp, lr}\n"
	  "\tadd\tfp, sp, #4\n"
	  "\tsub\tsp, sp, #8\n" "\tstr\tr0, [fp, #-8]\n", name, name, name);
}

// 输出函数后导码
void cgfuncpostamble(int id) {
  cglabel(Symtable[id].endlabel);
  fputs("\tsub\tsp, fp, #4\n" "\tpop\t{fp, pc}\n" "\t.align\t2\n", Outfile);
}

// 将整数字面量值加载到寄存器中。
// 返回寄存器的编号。
int cgloadint(int value, int type) {
  // 获取一个新寄存器
  int r = alloc_register();

  // 如果字面量值很小，用一条指令完成
  if (value <= 1000)
    fprintf(Outfile, "\tmov\t%s, #%d\n", reglist[r], value);
  else {
    set_int_offset(value);
    fprintf(Outfile, "\tldr\t%s, [r3]\n", reglist[r]);
  }
  return (r);
}

// 确定变量相对于 .L2
// 标签的偏移量。是的，这是低效代码。
static void set_var_offset(int id) {
  int offset = 0;
  // 向上遍历符号表直到 id。
  // 找到 S_VARIABLE 并加 4，直到
  // 我们到达变量

  for (int i = 0; i < id; i++) {
    if (Symtable[i].stype == S_VARIABLE)
      offset += 4;
  }
  // 将 r3 加载此偏移量
  fprintf(Outfile, "\tldr\tr3, .L2+%d\n", offset);
}


// 将变量值加载到寄存器中。
// 返回寄存器的编号
int cgloadglob(int id) {
  // 获取一个新寄存器
  int r = alloc_register();

  // 获取到变量的偏移量
  set_var_offset(id);

  switch (Symtable[id].type) {
    case P_CHAR:
      fprintf(Outfile, "\tldrb\t%s, [r3]\n", reglist[r]);
      break;
    case P_INT:
    case P_LONG:
    case P_CHARPTR:
    case P_INTPTR:
    case P_LONGPTR:
      fprintf(Outfile, "\tldr\t%s, [r3]\n", reglist[r]);
      break;
    default:
      fatald("Bad type in cgloadglob:", Symtable[id].type);
  }
  return (r);
}

// 将两个寄存器相加并返回
// 包含结果的寄存器编号
int cgadd(int r1, int r2) {
  fprintf(Outfile, "\tadd\t%s, %s, %s\n", reglist[r2], reglist[r1],
	  reglist[r2]);
  free_register(r1);
  return (r2);
}

// 从第一个寄存器减去第二个寄存器并
// 返回包含结果的寄存器编号
int cgsub(int r1, int r2) {
  fprintf(Outfile, "\tsub\t%s, %s, %s\n", reglist[r1], reglist[r1],
	  reglist[r2]);
  free_register(r2);
  return (r1);
}

// 将两个寄存器相乘并返回
// 包含结果的寄存器编号
int cgmul(int r1, int r2) {
  fprintf(Outfile, "\tmul\t%s, %s, %s\n", reglist[r2], reglist[r1],
	  reglist[r2]);
  free_register(r1);
  return (r2);
}

// 用第一个寄存器除以第二个寄存器并
// 返回包含结果的寄存器编号
int cgdiv(int r1, int r2) {

  // 做除法：r0 持有被除数，r1 持有除数。
  // 商在 r0 中。
  fprintf(Outfile, "\tmov\tr0, %s\n", reglist[r1]);
  fprintf(Outfile, "\tmov\tr1, %s\n", reglist[r2]);
  fprintf(Outfile, "\tbl\t__aeabi_idiv\n");
  fprintf(Outfile, "\tmov\t%s, r0\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

// 使用给定寄存器中的一个参数调用函数
// 返回包含结果的寄存器
int cgcall(int r, int id) {
  fprintf(Outfile, "\tmov\tr0, %s\n", reglist[r]);
  fprintf(Outfile, "\tbl\t%s\n", Symtable[id].name);
  fprintf(Outfile, "\tmov\t%s, r0\n", reglist[r]);
  return (r);
}

// 将寄存器左移常量
int cgshlconst(int r, int val) {
  fprintf(Outfile, "\tlsl\t%s, %s, #%d\n", reglist[r], reglist[r], val);
  return (r);
}

// 将寄存器的值存储到变量
int cgstorglob(int r, int id) {

  // 获取到变量的偏移量
  set_var_offset(id);

  switch (Symtable[id].type) {
    case P_CHAR:
      fprintf(Outfile, "\tstrb\t%s, [r3]\n", reglist[r]);
      break;
    case P_INT:
    case P_LONG:
    case P_CHARPTR:
    case P_INTPTR:
    case P_LONGPTR:
      fprintf(Outfile, "\tstr\t%s, [r3]\n", reglist[r]);
      break;
    default:
      fatald("Bad type in cgstorglob:", Symtable[id].type);
  }
  return (r);
}

// 给定 P_XXX 类型值，返回
// 基本类型的大小（以字节为单位）。
int cgprimsize(int type) {
  if (ptrtype(type))
    return (4);
  switch (type) {
    case P_CHAR:
      return (1);
    case P_INT:
    case P_LONG:
      return (4);
    default:
      fatald("Bad type in cgprimsize:", type);
  }
  return (0);			// 保持 -Wall 高兴
}

// 生成全局符号
void cgglobsym(int id) {
  int typesize;
  // 获取类型的大小
  typesize = cgprimsize(Symtable[id].type);

  fprintf(Outfile, "\t.data\n" "\t.globl\t%s\n", Symtable[id].name);
  switch (typesize) {
    case 1:
      fprintf(Outfile, "%s:\t.byte\t0\n", Symtable[id].name);
      break;
    case 4:
      fprintf(Outfile, "%s:\t.long\t0\n", Symtable[id].name);
      break;
    default:
      fatald("Unknown typesize in cgglobsym: ", typesize);
  }
}

// 比较指令列表，
// 按 AST 顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *cmplist[] =
  { "moveq", "movne", "movlt", "movgt", "movle", "movge" };

// 反转跳转指令列表，
// 按 AST 顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *invcmplist[] =
  { "movne", "moveq", "movge", "movle", "movgt", "movlt" };

// 比较两个寄存器并在为真时设置。
int cgcompare_and_set(int ASTop, int r1, int r2) {

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  fprintf(Outfile, "\tcmp\t%s, %s\n", reglist[r1], reglist[r2]);
  fprintf(Outfile, "\t%s\t%s, #1\n", cmplist[ASTop - A_EQ], reglist[r2]);
  fprintf(Outfile, "\t%s\t%s, #0\n", invcmplist[ASTop - A_EQ], reglist[r2]);
  fprintf(Outfile, "\tuxtb\t%s, %s\n", reglist[r2], reglist[r2]);
  free_register(r1);
  return (r2);
}

// 生成标签
void cglabel(int l) {
  fprintf(Outfile, "L%d:\n", l);
}

// 生成跳转到标签
void cgjump(int l) {
  fprintf(Outfile, "\tb\tL%d\n", l);
}

// 反转分支指令列表，
// 按 AST 顺序：A_EQ, A_NE, A_LT, A_GT, A_LE, A_GE
static char *brlist[] = { "bne", "beq", "bge", "ble", "bgt", "blt" };

// 比较两个寄存器并在为假时跳转。
int cgcompare_and_jump(int ASTop, int r1, int r2, int label) {

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  fprintf(Outfile, "\tcmp\t%s, %s\n", reglist[r1], reglist[r2]);
  fprintf(Outfile, "\t%s\tL%d\n", brlist[ASTop - A_EQ], label);
  freeall_registers();
  return (NOREG);
}

// 将寄存器中的值从旧类型
// 加宽到新类型，并返回带有
// 此新值的寄存器
int cgwiden(int r, int oldtype, int newtype) {
  // 什么也不做
  return (r);
}

// 生成从函数返回值的代码
void cgreturn(int reg, int id) {
  fprintf(Outfile, "\tmov\tr0, %s\n", reglist[reg]);
  cgjump(Symtable[id].endlabel);
}

// 生成将全局标识符的地址加载到
// 变量的代码。返回新寄存器
int cgaddress(int id) {
  // 获取一个新寄存器
  int r = alloc_register();

  // 获取到变量的偏移量
  set_var_offset(id);
  fprintf(Outfile, "\tmov\t%s, r3\n", reglist[r]);
  return (r);
}

// 解引用指针以获取其指向的值
// 到同一寄存器
int cgderef(int r, int type) {
  switch (type) {
    case P_CHARPTR:
      fprintf(Outfile, "\tldrb\t%s, [%s]\n", reglist[r], reglist[r]);
      break;
    case P_INTPTR:
    case P_LONGPTR:
      fprintf(Outfile, "\tldr\t%s, [%s]\n", reglist[r], reglist[r]);
      break;
  }
  return (r);
}

// 通过解引用指针存储
int cgstorderef(int r1, int r2, int type) {
  switch (type) {
    case P_CHAR:
      fprintf(Outfile, "\tstrb\t%s, [%s]\n", reglist[r1], reglist[r2]);
      break;
    case P_INT:
    case P_LONG:
      fprintf(Outfile, "\tstr\t%s, [%s]\n", reglist[r1], reglist[r2]);
      break;
    default:
      fatald("Can't cgstoderef on type:", type);
  }
  return (r1);
}