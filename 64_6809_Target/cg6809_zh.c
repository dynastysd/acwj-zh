#include "defs.h"
#include "data.h"
#include "gen.h"
#include "misc.h"
#include "types.h"
#include "target.h"

// 6809 代码生成器
// Copyright (c) 2024 Warren Toomey, GPL3

// 不使用寄存器，而是维护一个位置列表。
// 位置可以是以下类型之一：

enum {
  L_FREE,     // 此位置未使用
  L_SYMBOL,   // 具有可选偏移量的全局符号
  L_LOCAL,    // 局部变量或参数
  L_CONST,    // 整数字面量值
  L_LABEL,    // 标签
  L_SYMADDR,  // 符号的地址（局部或参数）
  L_TEMP,     // 临时存储的值
  L_DREG      // D 寄存器位置，即 B、D 或 Y/D
};

struct Location {
  int type;      // L_ 值之一
  char *name;    // 符号名称
  long intval;  // 偏移量、常量值、标签 ID 等
  int primtype;  // 6809 原始类型，见下面的 PR_POINTER
};

// 还跟踪 D 是否保存了某个位置的副本。
// 如果 D 可用，d_holds 可以是 NOREG
static int d_holds;

#define NUMFREELOCNS 16
static struct Location Locn[NUMFREELOCNS];

// 还需要一组内存中的临时位置。
// 它们在 crt0.s 中定义为 R0、R1 等。
// 可以增量分配
static int next_free_temp;

// 分配当前空闲的临时位置
static int cgalloctemp() {
  return(next_free_temp++);
}

// 释放所有临时位置
static void cgfreealltemps() {
  next_free_temp=0;
}

// 参数和局部变量位于栈上。
// 每次入栈/出栈时需要调整它们的偏移量。
// sp_adjust 保存栈上的额外字节数
static int sp_adjust;

// 将 C 类型转换为 6809 的类型：
// PR_CHAR、PR_INT、PR_LONG、PR_POINTER
#define PR_CHAR     1
#define PR_INT      2
#define PR_POINTER  3
#define PR_LONG     4

// 给定 C 类型，返回匹配的 6809 类型
static int cgprimtype(int type) {
  if (ptrtype(type))  return(PR_POINTER);
  if (type == P_CHAR) return(PR_CHAR);
  if (type == P_INT)  return(PR_INT);
  if (type == P_LONG) return(PR_LONG);
  fatald("Bad type in cgprimtype:", type);
  return(0); // 保持 -Wall 开心
}

// 打印位置。对于内存位置使用偏移量。
// 对于常量，使用寄存器字母确定使用哪部分
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
    case L_CONST:
      // 将 Locn[l].intval（long）转换为 intval（int）。
      // 例如，如果我们做 Locn[l].intval & 0xffff，
      // 在 6809 上 0xffff 被扩展为 32 位。
      // 但这是一个负值，所以被扩展为 0xffffffff 而不是 0x0000ffff
      switch(rletter) {
        case 'b':
          fprintf(Outfile, "#%ld\n", Locn[l].intval & 0xff); break;
        case 'a':
          fprintf(Outfile, "#%ld\n", (Locn[l].intval >> 8) & 0xff); break;
        case 'd':
          intval= (int)Locn[l].intval;
          fprintf(Outfile, "#%d\n", intval & 0xffff); break;
        case 'y':
          intval= (int)(Locn[l].intval >> 16);
          fprintf(Outfile, "#%d\n", intval & 0xffff); break;

        // 这些是 32 位值的高 16 位
        case 'f':
          fprintf(Outfile, "#%ld\n", (Locn[l].intval >> 16) & 0xff); break;
        case 'e':
          fprintf(Outfile, "#%ld\n", (Locn[l].intval >> 24) & 0xff); break;
      }
      break;
    default: fatald("Unknown type for location", l);
  }
}

// 将 D（B、D、Y/D）保存到位置
static void save_d(int l) {

  // 如果保存到自身，则无需操作
  if (Locn[l].type == L_DREG) return;

  switch (Locn[l].primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tstb "); printlocation(l, 0, 'b');
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tstd "); printlocation(l, 0, 'd');
      break;
    case PR_LONG:
      fprintf(Outfile, "\tstd "); printlocation(l, 2, 'd');
      fprintf(Outfile, "\tsty "); printlocation(l, 0, 'y');
  }
  d_holds= l;
}

// 如需要，将 D 存储到临时位置
static void stash_d() {
  // 如果 D 保存了值，需要将其存储到临时位置
  if (d_holds != NOREG && Locn[d_holds].type == L_DREG) {
    Locn[d_holds].type= L_TEMP;
    Locn[d_holds].intval= cgalloctemp();
    save_d(d_holds);
  }
}

// 将 D（B、D、Y/D）加载为位置
static void load_d(int l) {
  // 如果 l 已经是 L_DREG，什么都不做
  if (Locn[l].type== L_DREG) return;

  // 如果 D 保存了值，需要将其存储到临时位置
  stash_d();

  // 加载现有位置到 D 并标记为 L_DREG
  switch(Locn[l].primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tldb "); printlocation(l, 0, 'b'); break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tldd "); printlocation(l, 0, 'd'); break;
    case PR_LONG:
      fprintf(Outfile, "\tldd "); printlocation(l, 2, 'd');
      fprintf(Outfile, "\tldy "); printlocation(l, 0, 'y');
  }

  Locn[l].type= L_DREG;
  d_holds= l;
}

// 将所有位置设置为可用
// 如果 keepl 为正，则不释放该位置
static void cgfreeall_locns(int keepl) {
  int l;

  for (l = 0; l < NUMFREELOCNS; l++)
    if (l != keepl) {
      Locn[l].type= L_FREE;
    }

  if (keepl == NOREG)
    cgfreealltemps();
  fprintf(Outfile, ";\n");
  d_holds= NOREG;
}

// 分配空闲位置。返回位置编号。
// 如果没有可用位置则终止
static int cgalloclocn(int type, int primtype, char *name, long intval) {
  int l;

  for (l = 0; l < NUMFREELOCNS; l++) {
    if (Locn[l].type== L_FREE) {

      // 如果请求临时位置，获取一个
      if (type==L_TEMP)
        intval= cgalloctemp();
      if (type==L_DREG)
        d_holds= l;
      Locn[l].type= type;
      Locn[l].primtype= primtype;
      Locn[l].name= name;
      Locn[l].intval= intval;
      return(l);
    }
  }

  fatal("Out of locations in cgalloclocn");
  return(0); // 保持 -Wall 开心
}

// 释放位置。检查它是否已空闲
static void cgfreelocn(int l) {
  if (Locn[l].type== L_FREE)
    fatald("Error trying to free location", l);
  Locn[l].type= L_FREE;
  if (d_holds ==l) d_holds= NOREG;
}

// gen.c 调用我们就像有寄存器一样
void cgfreeallregs(int keepl) {
  cgfreeall_locns(keepl);
}

int cgallocreg(int type) {
  return(cgalloclocn(L_TEMP, cgprimtype(type), NULL, 0));
}

void cgfreereg(int reg) {
  cgfreelocn(reg);
}

// 将位置压入栈
static void pushlocn(int l) {
  load_d(l);

  switch(Locn[l].primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tpshs b\n");
      sp_adjust += 1;
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tpshs d\n");
      sp_adjust += 2;
      break;
    case PR_LONG:
      fprintf(Outfile, "\tpshs d\n");
      fprintf(Outfile, "\tpshs y\n");
      sp_adjust += 4;
  }

  cgfreelocn(l);
  d_holds= NOREG;
}

// 标志：当前输出的段
enum { no_seg, text_seg, data_seg, lit_seg } currSeg = no_seg;

// 切换到文本段
void cgtextseg() {
  if (currSeg != text_seg) {
    fputs("\t.code\n", Outfile);
    currSeg = text_seg;
  }
}

// 切换到数据段
void cgdataseg() {
  if (currSeg != data_seg) {
    fputs("\t.data\n", Outfile);
    currSeg = data_seg;
  }
}

// 切换到字面量段
void cglitseg() {
  if (currSeg != lit_seg) {
    fputs("\t.literal\n", Outfile);
    currSeg = lit_seg;
  }
}

// 下一个局部变量相对于栈基址的位置。
// 将偏移量存储为正数以简化栈指针对齐
static int localOffset;

// 创建新局部变量的位置
static int newlocaloffset(int size) {
  int o;

  // 返回当前的 localOffset，然后增加 localOffset
  o= localOffset;
  localOffset += size;
  return (o);
}

// 打印输出文件的汇编前导码
void cgpreamble() {
  cgfreeall_locns(NOREG);
  cgfreealltemps();
  cgtextseg();
}

// 文件结束时无事可做
void cgpostamble() {
}

// 生成标签
void cglabel(int l) {
  fprintf(Outfile, "L%d:\n", l);
}

// 打印函数前导码
void cgfuncpreamble(struct symtable *sym) {
  char *name = sym->name;
  struct symtable *parm, *locvar;
  int paramOffset = 2; // 任何推送的参数从此帧偏移开始

  // 在文本段输出，重置局部偏移量
  // 以及栈上的参数数量
  cgtextseg();
  localOffset = 0;
  next_free_temp = 0;
  sp_adjust = 0;

  // 输出函数开始
  if (sym->class == V_GLOBAL) {
    fprintf(Outfile, "\t.export _%s\n", name);
  }
  fprintf(Outfile, "_%s:\n", name);

  // 为局部变量创建帧位置
  // 首先跳过成员列表中的参数
  for (locvar = sym->member; locvar != NULL; locvar = locvar->next)
    if (locvar->class==V_LOCAL) break;

  for (; locvar != NULL; locvar = locvar->next) {
    locvar->st_posn = newlocaloffset(locvar->size);
    // fprintf(Oufile, "; placed local %s size %d at offset %d\n",
    //      locvar->name, locvar->size, locvar->st_posn);
  }

  // 计算参数的帧偏移量
  // 一旦知道局部变量的总大小就这样做
  // 一旦遇到局部变量就停止
  for (parm = sym->member; parm != NULL; parm = parm->next) {
    if (parm->class==V_LOCAL) break;
    parm->st_posn = paramOffset + localOffset;
    paramOffset += parm->size;
    // fprintf(Outfile, "; placed param %s size %d at offset %d\n",
    //        parm->name, parm->size, parm->st_posn);
  }

  // 将栈降低到局部变量以下
  if (localOffset!=0)
    fprintf(Outfile, "\tleas -%d,s\n", localOffset);
}

// 打印函数后导码
void cgfuncpostamble(struct symtable *sym) {
  cglabel(sym->st_endlabel);
  if (localOffset!=0)
    fprintf(Outfile, "\tleas %d,s\n", localOffset);
  fputs("\trts\n", Outfile);
  cgfreeall_locns(NOREG);
  cgfreealltemps();

  if (sp_adjust !=0 ) {
    fprintf(Outfile, "; DANGER sp_adjust is %d not 0\n", sp_adjust);
    fatald("sp_adjust is not zero", sp_adjust);
  }
}

// 将整数字面量值加载到位置中
// 返回位置编号
int cgloadint(int value, int type) {
  int primtype= cgprimtype(type);
  return(cgalloclocn(L_CONST, primtype, NULL, value));
}

// 按偏移量递增符号的值，偏移量可正可负
static void incdecsym(struct symtable *sym, int offset) {
    // 加载符号的地址
    if (sym->class == V_LOCAL || sym->class == V_PARAM)
      fprintf(Outfile, "\tleax %d,s\n", sym->st_posn + sp_adjust);
    else
      fprintf(Outfile, "\tldx #_%s\n", sym->name);

    // 现在改变该地址的值
    switch (sym->size) {
    case 1:
      fprintf(Outfile, "\tldb #%d\n", offset & 0xff);
      fprintf(Outfile, "\taddb 0,x\n");
      fprintf(Outfile, "\tstb 0,x\n");
      break;
    case 2:
      fprintf(Outfile, "\tldd #%d\n", offset & 0xffff);
      fprintf(Outfile, "\taddd 0,x\n");
      fprintf(Outfile, "\tstd 0,x\n");
      break;
    case 4:
      fprintf(Outfile, "\tldd #%d\n", offset);
      fprintf(Outfile, "\taddd 2,x\n");
      fprintf(Outfile, "\tstd 2,x\n");
      fprintf(Outfile, "\tldd 0,x\n");
      fprintf(Outfile, "\tadcb #0\n");
      fprintf(Outfile, "\tadca #0\n");
      fprintf(Outfile, "\tstb 0,x\n");
    }
}

// 从变量加载值到位置
// 返回位置编号。如果是预或后递增/递减操作，
// 也执行此操作
int cgloadvar(struct symtable *sym, int op) {
  int l, offset = 1;
  int primtype= cgprimtype(sym->type);

  // 如果符号是指针，使用其所指向类型的大小作为递增或递减的偏移量
  // 否则为 1
  if (ptrtype(sym->type))
    offset = typesize(value_at(sym->type), sym->ctype);

  // 对递减取负偏移量
  if (op == A_PREDEC || op == A_POSTDEC)
    offset = -offset;

  // 如果有预操作，执行它
  if (op == A_PREINC || op == A_PREDEC)
    incdecsym(sym, offset);

  // 获取新位置并设置它
  if (sym->class == V_LOCAL || sym->class == V_PARAM)
    l= cgalloclocn(L_LOCAL, primtype, NULL, sym->st_posn + sp_adjust);
  else
    l= cgalloclocn(L_SYMBOL, primtype, sym->name, 0);

  // 如果有后操作，执行它
  // 但将当前值获取到临时位置
  if (op == A_POSTINC || op == A_POSTDEC) {
    load_d(l);
    stash_d();
    incdecsym(sym, offset);
    load_d(l);
  }

  // 返回包含值的位置
  return (l);
}

// 给定全局字符串的标签号，
// 将其地址加载到新位置
int cgloadglobstr(int label) {
  // 获取新位置
  int l = cgalloclocn(L_LABEL, PR_INT, NULL, label);
  return (l);
}

// 将两个位置相加并返回包含结果的位置编号
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

// 从第一个位置减去第二个位置并返回包含结果的位置编号
int cgsub(int l1, int l2, int type) {
  int primtype= cgprimtype(type);

  load_d(l1);

  switch(primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tsubb "); printlocation(l2, 0, 'b'); break;
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tsubd "); printlocation(l2, 0, 'd'); break;
      break;
    case PR_LONG:
      fprintf(Outfile, "\tsubd "); printlocation(l2, 2, 'd');
      fprintf(Outfile, "\texg y,d\n");
      fprintf(Outfile, "\tsbcb "); printlocation(l2, 1, 'f');
      fprintf(Outfile, "\tsbca "); printlocation(l2, 0, 'e');
      fprintf(Outfile, "\texg y,d\n");
  }
  cgfreelocn(l2);
  Locn[l1].type= L_DREG;
  d_holds= l1;
  return (l1);
}

// 在两个位置上运行辅助子程序并返回包含结果的位置编号
static int cgbinhelper(int l1, int l2, int type,
                char *cop, char *iop, char *lop) {
  int primtype= cgprimtype(type);

  load_d(l1);

  switch(primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tclra\n");
      fprintf(Outfile, "\tpshs d\n");
      sp_adjust += 2;
      fprintf(Outfile, "\tldb "); printlocation(l2, 0, 'b');
      fprintf(Outfile, "\tlbsr %s\n", cop);
      sp_adjust -= 2;
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tpshs d\n");
      sp_adjust += 2;
      fprintf(Outfile, "\tldd "); printlocation(l2, 0, 'd');
      fprintf(Outfile, "\tlbsr %s\n", iop);
      sp_adjust -= 2;
      break;
    case PR_LONG:
      fprintf(Outfile, "\tpshs d\n");
      fprintf(Outfile, "\tpshs y\n");
      sp_adjust += 4;
      fprintf(Outfile, "\tldy "); printlocation(l2, 0, 'd');
      fprintf(Outfile, "\tldd "); printlocation(l2, 2, 'y');
      fprintf(Outfile, "\tlbsr %s\n", lop);
      sp_adjust -= 4;
  }
  cgfreelocn(l2);
  Locn[l1].type= L_DREG;
  d_holds= l1;
  return (l1);
}

// 将两个位置相乘并返回包含结果的位置编号
int cgmul(int r1, int r2, int type) {
  return(cgbinhelper(r1, r2, type, "__mul", "__mul", "__mull"));
}

// 用第一个位置除以第二个并返回包含结果的位置编号
int cgdiv(int r1, int r2, int type) {
  return(cgbinhelper(r1, r2, type, "__div", "__div", "__divl"));
}

// 用第一个位置除以第二个得到余数
// 返回包含结果的位置编号
int cgmod(int r1, int r2, int type) {
  return(cgbinhelper(r1, r2, type, "__rem", "__rem", "__reml"));
}

// 两个位置上的通用二元操作
static int cgbinop(int l1, int l2, int type, char *op) {
  int primtype= cgprimtype(type);

  load_d(l1);

  switch(primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\t%sb ", op); printlocation(l2, 0, 'b');
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\t%sa ", op); printlocation(l2, 0, 'a');
      fprintf(Outfile, "\t%sb ", op); printlocation(l2, 1, 'b');
      break;
    case PR_LONG:
      fprintf(Outfile, "\t%sa ", op); printlocation(l2, 2, 'a');
      fprintf(Outfile, "\t%sb ", op); printlocation(l2, 3, 'b');
      fprintf(Outfile, "\texg y,d\n");
      fprintf(Outfile, "\t%sa ", op); printlocation(l2, 0, 'e');
      fprintf(Outfile, "\t%sb ", op); printlocation(l2, 1, 'f');
      fprintf(Outfile, "\texg y,d\n");
      break;
  }
  cgfreelocn(l2);
  Locn[l1].type= L_DREG;
  d_holds= l1;
  return (l1);
}

// 两个位置按位与
int cgand(int r1, int r2, int type) {
  return(cgbinop(r1, r2, type, "and"));
}

// 两个位置按位或
int cgor(int r1, int r2, int type) {
  return(cgbinop(r1, r2, type, "or"));
}

// 两个位置按位异或
int cgxor(int r1, int r2, int type) {
  return(cgbinop(r1, r2, type, "eor"));
}

// 反转位置的值
int cginvert(int l, int type) {
  int primtype= cgprimtype(type);

  load_d(l);

  switch(primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tcomb\n");
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tcoma\n");
      fprintf(Outfile, "\tcomb\n");
      break;
    case PR_LONG:
      fprintf(Outfile, "\tcoma\n");
      fprintf(Outfile, "\tcomb\n");
      fprintf(Outfile, "\texg y,d\n");
      fprintf(Outfile, "\tcoma\n");
      fprintf(Outfile, "\tcomb\n");
      fprintf(Outfile, "\texg y,d\n");
  }

  Locn[l].type= L_DREG;
  d_holds= l;
  return(l);
}

// 将 r1 左移 r2 位
int cgshl(int r1, int r2, int type) {
  return(cgbinhelper(r1, r2, type, "__shl", "__shl", "__shll"));
}

// 将 r1 右移 8、16 或 24 位
int cgshrconst(int r1, int amount, int type) {
  int primtype= cgprimtype(type);
  int temp;

  load_d(r1);

  switch(primtype) {
    // 任何对 B 的移位都会清空它
    case PR_CHAR:
      fprintf(Outfile, "\tclrb\n"); return(r1);
    case PR_INT:
    case PR_POINTER:
      switch(amount) {
        case  8:
          fprintf(Outfile, "\ttfr a,b\n");
          fprintf(Outfile, "\tclra\n"); return(r1);
        case 16:
        case 24:
          fprintf(Outfile, "\tclra\n");
          fprintf(Outfile, "\tclrb\n"); return(r1);
      }
    case PR_LONG:
      switch(amount) {
        case  8:
          temp= cgalloctemp();
          fprintf(Outfile, "\tclr R%d ; long >> 8\n", temp);
          fprintf(Outfile, "\tsty R%d+1\n", temp);
          fprintf(Outfile, "\tsta R%d+3\n", temp);
          fprintf(Outfile, "\tldy R%d\n", temp);
          fprintf(Outfile, "\tldd R%d+2\n", temp); return(r1);
        case 16:
          fprintf(Outfile, "\ttfr y,d ; long >> 16\n");
          fprintf(Outfile, "\tldy #0\n"); return(r1);
        case 24:
          fprintf(Outfile, "\ttfr y,d ; long >> 24\n");
          fprintf(Outfile, "\ttfr a,b\n");
          fprintf(Outfile, "\tclra\n");
          fprintf(Outfile, "\tldy #0\n"); return(r1);
      }
  }
  return(0); // 保持 -Wall 开心
}

// 将 r1 右移 r2 位
int cgshr(int r1, int r2, int type) {
  int val;

  // 如果 r2 是常量 8、16 或 24
  // 可以用几条指令完成
  if (Locn[r2].type== L_CONST) {
    val= (int)Locn[r2].intval;
    if (val==8 || val==16 || val==24)
      return(cgshrconst(r1, val, type));
  }

  return(cgbinhelper(r1, r2, type, "__shr", "__shr", "__shrl"));
}

// 取反位置的值
int cgnegate(int l, int type) {
  int primtype= cgprimtype(type);

  load_d(l);

  switch(primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tnegb\n");
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tcoma\n");
      fprintf(Outfile, "\tcomb\n");
      fprintf(Outfile, "\taddd #1\n");
      break;
    case PR_LONG:
      fprintf(Outfile, "\tlbsr __negatel\n");
  }
  Locn[l].type= L_DREG;
  d_holds= l;
  return (l);
}

// 逻辑取反位置的值
int cglognot(int l, int type) {
  // 获取两个标签
  int label1 = genlabel();
  int label2 = genlabel();
  int primtype= cgprimtype(type);

  load_d(l);

  switch(primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tcmpb #0\n");
      fprintf(Outfile, "\tbne L%d\n", label1);
      fprintf(Outfile, "\tldd #1\n");
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tcmpd #0\n");
      fprintf(Outfile, "\tbne L%d\n", label1);
      fprintf(Outfile, "\tldd #1\n");
      break;
    case PR_LONG:
      fprintf(Outfile, "\tcmpd #0\n");
      fprintf(Outfile, "\tbne L%d\n", label1);
      fprintf(Outfile, "\tcmpy #0\n");
      fprintf(Outfile, "\tbne L%d\n", label1);
      fprintf(Outfile, "\tldd #1\n");
  }
  fprintf(Outfile, "\tbra L%d\n", label2);
  cglabel(label1);
  fprintf(Outfile, "\tldd #0\n");
  cglabel(label2);

  Locn[l].type= L_DREG;
  d_holds= l;
  return (l);
}

// 将布尔值（只有 0 或 1）加载到给定位置
// 如果 l 是 NOREG，则分配位置
int cgloadboolean(int l, int val, int type) {
  int primtype= cgprimtype(type);
  int templ;

  // 将值放入字面量位置
  // 加载到 D
  templ= cgalloclocn(L_CONST, primtype, NULL, val);
  load_d(templ);

  // 返回字面量位置或保存值并返回该位置
  if (l==NOREG) {
    return(templ);
  } else {
    save_d(l);
    return(l);
  }
  return(NOREG); // 保持 -Wall 开心
}

// 如果 D 已加载则设置 Z 标志。否则，
// 加载 D 寄存器，这会设置 Z 标志
static void load_d_z(int l, int type) {
  int primtype= cgprimtype(type);
  int label = genlabel();

  if (Locn[l].type== L_DREG) {
    switch(primtype) {
      case PR_CHAR:
        fprintf(Outfile, "\tcmpb #0\n");
        break;
      case PR_INT:
      case PR_POINTER:
        fprintf(Outfile, "\tcmpd #0\n");
        break;
      case PR_LONG:
        fprintf(Outfile, "\tcmpd #0\n");
        fprintf(Outfile, "\tbne L%d\n", label);
        fprintf(Outfile, "\tcmpy #0\n");
        cglabel(label);
    }
  } else
    load_d(l);
}

// 将整数值转换为 TOBOOL 操作的布尔值
// 如果是 IF、WHILE 操作则为真时跳转
// 如果是 LOGOR 操作则为假时跳转
int cgboolean(int l, int op, int label, int type) {
  int primtype= cgprimtype(type);
  char *jmpop= "beq";

  if (op== A_LOGOR) jmpop= "bne";

  load_d_z(l, type);

  switch(primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\t%s L%d\n", jmpop, label);
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\t%s L%d\n", jmpop, label);
      break;
    case PR_LONG:
      fprintf(Outfile, "\tpshs y\n");
      fprintf(Outfile, "\tora 0,s\n");
      fprintf(Outfile, "\torb 1,s\n");
      fprintf(Outfile, "\tleas 2,s\n");
      fprintf(Outfile, "\t%s L%d\n", jmpop, label);
  }

  // 如果操作是 A_TOBOOL，将位置设置为 1
  if (op == A_TOBOOL) {
    cgloadboolean(l, 1, type);
    return(l);
  }
  return(NOREG);
}

// 使用给定的符号 id 调用函数
// 之前将参数压入栈
// 之后弹出任何压入栈的参数
// 返回包含结果的位置
int cgcall(struct symtable *sym, int numargs, int *arglist, int *typelist) {
  int i, l, argamount;
  int gentype= sym->type;
  int primtype= 0;

  // 如果不是 void 函数，获取其 primtype
  // 还要将任何 D 值暂存到临时位置
  if (gentype!=P_VOID) {
    stash_d();
    primtype= cgprimtype(sym->type);
  }

  // 将函数参数压入栈
  argamount=0;
  for (i= 0; i< numargs; i++) {
    pushlocn(arglist[i]);
    argamount += cgprimsize(typelist[i]);
  }

  // 调用函数，调整栈
  fprintf(Outfile, "\tlbsr _%s\n", sym->name);
  fprintf(Outfile, "\tleas %d,s\n", argamount);
  sp_adjust -= argamount;

  // 如果不是 void 函数，标记 D 中的结果
  if (gentype!=P_VOID) {
    // 获取一个位置并说 D 正在使用中
    l = cgalloclocn(L_DREG, primtype, NULL, 0);
    return (l);
  }
  return(NOREG);
}

// 将位置左移常量位，只能是 1 或 2
int cgshlconst(int l, int val, int type) {
  int primtype= cgprimtype(type);

  load_d(l);

  switch(primtype) {
    case PR_CHAR:
      if (val==2) {
        fprintf(Outfile, "\taslb\n");
      }
      fprintf(Outfile, "\taslb\n");
      break;
    case PR_INT:
    case PR_POINTER:
      if (val==2) {
        fprintf(Outfile, "\taslb\n");
        fprintf(Outfile, "\trola\n");
      }
      fprintf(Outfile, "\taslb\n");
      fprintf(Outfile, "\trola\n");
      break;
    case PR_LONG:
      if (val==2) {
        fprintf(Outfile, "\taslb\n");
        fprintf(Outfile, "\trola\n");
      }
      fprintf(Outfile, "\texg y,d\n");
      fprintf(Outfile, "\trolb\n");
      fprintf(Outfile, "\trola\n");
      fprintf(Outfile, "\texg y,d\n");
  }
  Locn[l].type= L_DREG;
  d_holds= l;
  return (l);
}

// 将位置的值存储到变量
int cgstorglob(int l, struct symtable *sym) {
  int size= cgprimsize(sym->type);

  load_d(l);

  switch (size) {
    case 1:
      fprintf(Outfile, "\tstb _%s\n", sym->name);
      break;
    case 2:
      fprintf(Outfile, "\tstd _%s\n", sym->name);
      break;
    case 4:
      fprintf(Outfile, "\tstd _%s+2\n", sym->name);
      fprintf(Outfile, "\tsty _%s\n", sym->name);
  }
  return (l);
}

// 将位置的值存储到局部变量
int cgstorlocal(int l, struct symtable *sym) {
  int primtype= cgprimtype(sym->type);

  load_d(l);

  switch (primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tstb %d,s\n", sym->st_posn + sp_adjust);
      break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tstd %d,s\n", sym->st_posn + sp_adjust);
      break;
    case PR_LONG:
      fprintf(Outfile, "\tsty %d,s\n", sym->st_posn + sp_adjust);
      fprintf(Outfile, "\tstd %d,s\n", 2+sym->st_posn + sp_adjust);
  }
  return (l);
}

// 生成全局符号但不包括函数
void cgglobsym(struct symtable *node) {
  int size, type;
  int initvalue;
  int i,j;

  if (node == NULL)
    return;
  if (node->stype == S_FUNCTION)
    return;

  // 获取变量的大小（如果是数组则为其元素的大小）
  // 以及变量的类型
  if (node->stype == S_ARRAY) {
    size = typesize(value_at(node->type), node->ctype);
    type = value_at(node->type);
  } else {
    size = node->size;
    type = node->type;
  }

  // 生成全局标识符和标签
  cgdataseg();
  if (node->class == V_GLOBAL)
    fprintf(Outfile, "\t.export _%s\n", node->name);
  fprintf(Outfile, "_%s:\n", node->name);

  // 为一个或多个元素输出空间
  for (i = 0; i < node->nelems; i++) {

    // 获取任何初始值
    initvalue = 0;
    if (node->initlist != NULL)
      initvalue = node->initlist[i];

    // 为此类型生成空间
    switch (size) {
    case 1:
      fprintf(Outfile, "\t.byte\t%d\n", initvalue & 0xff);
      break;
    case 2:
      // 生成指向字符串字面量的指针。将零值视为实际的零，而不是标签 L0
      if (node->initlist != NULL && type == pointer_to(P_CHAR)
          && initvalue != 0)
        fprintf(Outfile, "\t.word\tL%d\n", initvalue);
      else
        fprintf(Outfile, "\t.word\t%d\n", initvalue & 0xffff);
      break;
    case 4:
      fprintf(Outfile, "\t.word\t%d\n", (initvalue >> 16) & 0xffff);
      fprintf(Outfile, "\t.word\t%d\n", initvalue & 0xffff);
      break;
    default:
      for (j = 0; j < size; j++)
        fprintf(Outfile, "\t.byte\t0\n");
    }
  }
}

// 生成全局字符串及其起始标签
void cgglobstr(int l, char *strvalue) {
  char *cptr;
  cglabel(l);
  for (cptr = strvalue; *cptr; cptr++) {
    fprintf(Outfile, "\t.byte\t%d\n", *cptr);
  }
  fprintf(Outfile, "\t.byte\t0\n");
}

// 比较指令列表，按 AST 顺序：A_EQ、A_NE、A_LT、A_GT、A_LE、A_GE
static char *cmplist[] =
  { "beq", "bne", "blt", "bgt", "ble", "bge" };

// 比较两个位置并在为真时设置
int cgcompare_and_set(int ASTop, int l1, int l2, int type) {
  int label1, label2;
  int primtype = cgprimtype(type);

  // 获取两个标签
  label1= genlabel();
  label2= genlabel();

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  load_d(l1);

  switch (primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tcmpb "); printlocation(l2, 0, 'b');
      break;
    case PR_INT:
    case PR_POINTER:
    case PR_LONG:
      fprintf(Outfile, "\tcmpd "); printlocation(l2, 0, 'd');
      break;
  }

  fprintf(Outfile, "\t%s L%d\n", cmplist[ASTop - A_EQ], label1);

  // XXX 这不正确，我需要修复它
  if (primtype==PR_LONG) {
    fprintf(Outfile, "\tbne L%d\n", label1);
    fprintf(Outfile, "\tcmpd "); printlocation(l2, 2, 'd');
  }
  fprintf(Outfile, "\tldd #0\n");
  fprintf(Outfile, "\tbra L%d\n", label2);
  cglabel(label1);
  fprintf(Outfile, "\tldd #1\n");
  cglabel(label2);
  cgfreelocn(l2);

  // 标记该位置为 D 寄存器
  Locn[l1].type= L_DREG;
  d_holds= l1;
  return (l1);
}

// 生成跳转到标签
void cgjump(int l) {
  fprintf(Outfile, "\tbra L%d\n", l);
  d_holds= NOREG;
}

// 反转跳转指令列表，按 AST 顺序：A_EQ、A_NE、A_LT、A_GT、A_LE、A_GE
static char *invcmplist[] = { "bne", "beq", "bge", "ble", "bgt", "blt" };

// long 函数使用的比较
static char *lcmplist1[] = { "bne", "beq", "bge", "ble", "bgt", "blt" };
static char *lcmplist2[] = { "bne", "beq", "bhs", "bls", "bhi", "blo" };

// 比较两个 long 位置并在为假时跳转
static void longcmp_and_jump(int ASTop, int parentASTop,
                int l1, int l2, int label) {
  int truelabel;

  // 生成新标签
  truelabel=genlabel();

  fprintf(Outfile, "\t%s L%d\n", lcmplist1[ASTop - A_EQ], label);
  switch(ASTop) {
    case A_EQ:
      fprintf(Outfile, "\tbne L%d\n", label);
      break;
    case A_NE:
      fprintf(Outfile, "\tbne L%d\n", truelabel);
      break;
    case A_LT:
      fprintf(Outfile, "\tblt L%d\n", truelabel);
      fprintf(Outfile, "\tbne L%d\n", label);
      break;
    case A_GT:
      fprintf(Outfile, "\tbgt L%d\n", truelabel);
      fprintf(Outfile, "\tbne L%d\n", label);
      break;
    case A_LE:
      fprintf(Outfile, "\tbgt L%d\n", label);
      fprintf(Outfile, "\tbne L%d\n", truelabel);
      break;
    case A_GE:
      fprintf(Outfile, "\tblt L%d\n", label);
      fprintf(Outfile, "\tbne L%d\n", truelabel);
  }

  fprintf(Outfile, "\tcmpd "); printlocation(l2, 2, 'd');
  fprintf(Outfile, "\t%s L%d\n", lcmplist2[ASTop - A_EQ], label);
  cglabel(truelabel);
}

// 比较两个位置并在为假时跳转
// 如果父操作是 A_LOGOR 则为真时跳转
int cgcompare_and_jump(int ASTop, int parentASTop,
                int l1, int l2, int label, int type) {
  int primtype = cgprimtype(type);
  char *jmpop;

  // 检查 AST 操作的范围
  if (ASTop < A_EQ || ASTop > A_GE)
    fatal("Bad ASTop in cgcompare_and_set()");

  load_d_z(l1, type);
  jmpop= invcmplist[ASTop - A_EQ];
  if (parentASTop==A_LOGOR)
    jmpop= cmplist[ASTop - A_EQ];

  switch (primtype) {
    case PR_CHAR:
      fprintf(Outfile, "\tcmpb "); printlocation(l2, 0, 'b'); break;
    case PR_INT:
    case PR_POINTER:
      fprintf(Outfile, "\tcmpd "); printlocation(l2, 0, 'd'); break;
    case PR_LONG:
      fprintf(Outfile, "\tcmpy "); printlocation(l2, 0, 'y'); break;
  }

  if (primtype==PR_LONG) longcmp_and_jump(ASTop, parentASTop, l1, l2, label);
  fprintf(Outfile, "\t%s L%d\n", jmpop, label);
  cgfreelocn(l1);
  cgfreelocn(l2);
  return (NOREG);
}

// 将位置中的值从旧类型扩展到新类型，
// 并返回包含此新值的位置
int cgwiden(int l, int oldtype, int newtype) {
  int how= cgprimsize(newtype) - cgprimsize(oldtype);
  int label1, label2;
  int l2;

  // 如果大小相同则无事可做
  if (how==0) return(l);

  load_d(l);

  // 获取一个作为 L_DREG 的位置
  l2= cgalloclocn(L_DREG, cgprimtype(newtype), NULL, 0);

  // 三种可能性：大小 1 到 2、2 到 4 和 1 到 4
  // 注意字符是无符号的，这使得事情更容易
  switch(how) {
      // 1 到 2
    case 1: fprintf(Outfile, "\tclra\n"); break;

      // 2 到 4
    case 2:
      // 获取两个标签
      label1 = genlabel();
      label2 = genlabel();
      fprintf(Outfile, "\tbge L%d\n", label1);
      fprintf(Outfile, "\tldy #65535\n");
      fprintf(Outfile, "\tbra L%d\n", label2);
      cglabel(label1);
      fprintf(Outfile, "\tldy #0\n");
      cglabel(label2);
      break;

      // 1 到 4
    case 3:
      fprintf(Outfile, "\tclra\n");
      fprintf(Outfile, "\tldy #0\n");
  }

  return (l2);
}

// 将位置从旧类型更改为新类型
int cgcast(int l, int oldtype, int newtype) {
  return(cgwiden(l,oldtype,newtype));
}

// 生成从函数返回值的代码
void cgreturn(int l, struct symtable *sym) {
  // 如果有返回值则加载 D
  if (l != NOREG)
    load_d(l);
  cgjump(sym->st_endlabel);
}

// 生成加载标识符地址的代码
// 返回新位置
int cgaddress(struct symtable *sym) {
  int l;

  // 对于不在栈上的东西很简单
  if (sym->class == V_GLOBAL ||
      sym->class == V_EXTERN || sym->class == V_STATIC) {
    l= cgalloclocn(L_SYMADDR, PR_POINTER, sym->name, 0);
    return(l);
  }

  // 对于栈上的东西，需要将地址获取到 X 寄存器，然后移动到 D
  // 如果 D 已在使用中，则将其暂存到临时位置
  stash_d();
  fprintf(Outfile, "\tleax %d,s\n", sym->st_posn + sp_adjust);
  fprintf(Outfile, "\ttfr x,d\n");
  l = cgalloclocn(L_DREG, PR_POINTER, NULL, 0);
  return(l);
}

// 解引用指针，将其指向的值加载到同一位置
int cgderef(int l, int type) {
  // 获取我们指向的类型
  int newtype = value_at(type);
  int primtype= cgprimtype(newtype);

  if (Locn[l].type==L_DREG)
    fprintf(Outfile, "\ttfr d,x\n");
  else {
    // 如果 D 已在使用中则暂存到临时位置
    stash_d();
    fprintf(Outfile, "\tldx "); printlocation(l, 0, 'd');
  }

  switch (primtype) {
  case PR_CHAR:
    fprintf(Outfile, "\tldb 0,x\n"); break;
  case PR_INT:
  case PR_POINTER:
    fprintf(Outfile, "\tldd 0,x\n"); break;
  case PR_LONG:
    fprintf(Outfile, "\tldd 2,x\n");
    fprintf(Outfile, "\tldy 0,x\n");
  }

  cgfreelocn(l);
  l= cgalloclocn(L_DREG, primtype, NULL, 0);
  return (l);
}

// 解引用并通过 l2（指针）存储
int cgstorderef(int l1, int l2, int type) {
  int primtype = cgprimtype(type);

  // 如果 l2 在 D 寄存器中，则执行传输
  if (d_holds== l2) {
    fprintf(Outfile, "\ttfr d,x\n");
  } else {
    fprintf(Outfile, "\tldx "); printlocation(l2, 0, 'd');
  }

  d_holds= NOREG;
  load_d(l1);

  switch (primtype) {
  case PR_CHAR:
    fprintf(Outfile, "\tstb 0,x\n"); break;
  case PR_INT:
  case PR_POINTER:
    fprintf(Outfile, "\tstd 0,x\n"); break;
  case PR_LONG:
    fprintf(Outfile, "\tsty 0,x\n");
    fprintf(Outfile, "\tstd 2,x\n"); break;
  }

  d_holds= l1;
  return (11);
}

// 生成 switch 跳转表和用于加载位置并调用 switch() 代码的代码
void cgswitch(int reg, int casecount, int toplabel,
          int *caselabel, int *caseval, int defaultlabel) {
  int i, label;

  // 获取 switch 跳转表的标签
  label= genlabel();

  // 生成 switch 跳转表
  cglitseg();
  cglabel(label);
  fprintf(Outfile, "\t.word %d\n", casecount);
  for (i = 0; i < casecount; i++)
    fprintf(Outfile, "\t.word %d\n\t.word L%d\n", caseval[i], caselabel[i]);
  fprintf(Outfile, "\t.word L%d\n", defaultlabel);

  // 在我们重新开始实际代码的地方输出标签
  cgtextseg();
  cglabel(toplabel);

  // 在用 reg（一个位置）中的值加载 D 后，
  // 调用辅助例程并提供跳转表位置
  load_d(reg);
  fprintf(Outfile, "\tldx #L%d\n", label);
  fprintf(Outfile, "\tbra __switch\n");
}

// 在位置之间移动值
void cgmove(int l1, int l2, int type) {

  load_d(l1);
  save_d(l2);
}

// 输出 gdb 指令，说明以下汇编代码来自哪个源代码行号
void cglinenum(int line) {
  fprintf(Outfile, ";\t\t\t\t\tline %d\n", line);
}