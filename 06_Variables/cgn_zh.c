#include "defs.h"
#include "data.h"
#include "decl.h"

// x86-64 的代码生成器
// Copyright (c) 2019 Warren Toomey, GPL3


// 可用寄存器及其名称的列表
static int freereg[4];
static char *reglist[4] = { "r8", "r9", "r10", "r11" };

// 将所有寄存器设置为可用
void freeall_registers(void) {
  freereg[0] = freereg[1] = freereg[2] = freereg[3] = 1;
}

// 分配一个空闲寄存器。返回
// 寄存器的编号。如果没有可用寄存器则终止。
static int alloc_register(void) {
  for (int i = 0; i < 4; i++) {
    if (freereg[i]) {
      freereg[i] = 0;
      return (i);
    }
  }
  fatal("寄存器用完了");
}

// 将一个寄存器返还到可用寄存器列表中。
// 检查它是否已经在列表中了。
static void free_register(int reg) {
  if (freereg[reg] != 0)
    fatald("尝试释放寄存器时出错", reg);
  freereg[reg] = 1;
}

// 输出汇编前导代码
void cgpreamble() {
  freeall_registers();
  fputs("\tglobal\tmain\n"
	"\textern\tprintf\n"
	"\tsection\t.text\n"
	"LC0:\tdb\t\"%d\",10,0\n"
	"printint:\n"
	"\tpush\trbp\n"
	"\tmov\trbp, rsp\n"
	"\tsub\trsp, 16\n"
	"\tmov\t[rbp-4], edi\n"
	"\tmov\teax, [rbp-4]\n"
	"\tmov\tesi, eax\n"
	"\tlea\trdi, [rel LC0]\n"
	"\tmov\teax, 0\n"
	"\tcall\tprintf\n"
	"\tnop\n"
	"\tleave\n"
	"\tret\n"
	"\n"
	"main:\n" "\tpush\trbp\n" "\tmov\trbp, rsp\n", Outfile);
}

// 输出汇编后导代码
void cgpostamble() {
  fputs("\tmov\teax, 0\n" "\tpop\trbp\n" "\tret\n", Outfile);
}

// 将整数字面量值加载到寄存器中。
// 返回寄存器的编号
int cgloadint(int value) {
  // 获取一个新的寄存器
  int r = alloc_register();

  // 输出初始化代码
  fprintf(Outfile, "\tmov\t%s, %d\n", reglist[r], value);
  return (r);
}

// 从变量加载值到寄存器中。
// 返回寄存器的编号
int cgloadglob(char *identifier) {
  // 获取一个新的寄存器
  int r = alloc_register();

  // 输出初始化代码
  fprintf(Outfile, "\tmov\t%s, [%s]\n", reglist[r], identifier);
  return (r);
}

// 将两个寄存器相加并返回
// 持有结果的寄存器的编号
int cgadd(int r1, int r2) {
  fprintf(Outfile, "\tadd\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

// 用第一个寄存器减去第二个寄存器并返回
// 持有结果的寄存器的编号
int cgsub(int r1, int r2) {
  fprintf(Outfile, "\tsub\t%s, %s\n", reglist[r1], reglist[r2]);
  free_register(r2);
  return (r1);
}

// 将两个寄存器相乘并返回
// 持有结果的寄存器的编号
int cgmul(int r1, int r2) {
  fprintf(Outfile, "\timul\t%s, %s\n", reglist[r2], reglist[r1]);
  free_register(r1);
  return (r2);
}

// 用第一个寄存器除以第二个寄存器并返回
// 持有结果的寄存器的编号
int cgdiv(int r1, int r2) {
  fprintf(Outfile, "\tmov\trax, %s\n", reglist[r1]);
  fprintf(Outfile, "\tcqo\n");
  fprintf(Outfile, "\tidiv\t%s\n", reglist[r2]);
  fprintf(Outfile, "\tmov\t%s, rax\n", reglist[r1]);
  free_register(r2);
  return (r1);
}

// 使用给定寄存器调用 printint()
void cgprintint(int r) {
  fprintf(Outfile, "\tmov\trdi, %s\n", reglist[r]);
  fprintf(Outfile, "\tcall\tprintint\n");
  free_register(r);
}

// 将寄存器的值存储到变量中
int cgstorglob(int r, char *identifier) {
  fprintf(Outfile, "\tmov\t[%s], %s\n", identifier, reglist[r]);
  return (r);
}

// 生成一个全局符号
void cgglobsym(char *sym) {
  fprintf(Outfile, "\tcommon\t%s 8:8\n", sym);
}
