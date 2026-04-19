#include "defs.h"
#include "data.h"
#include "decl.h"

// 符号表函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 确定符号 s 是否在全局符号表中。
// 返回其槽位置，如果未找到则返回 -1。
// 跳过 C_PARAM 条目
int findglob(char *s) {
  int i;

  for (i = 0; i < Globs; i++) {
    if (Symtable[i].class == C_PARAM)
      continue;
    if (*s == *Symtable[i].name && !strcmp(s, Symtable[i].name))
      return (i);
  }
  return (-1);
}

// 获取新全局符号槽的位置，
// 如果用完了位置则报错
static int newglob(void) {
  int p;

  if ((p = Globs++) >= Locls)
    fatal("Too many global symbols");
  return (p);
}

// 确定符号 s 是否在局部符号表中。
// 返回其槽位置，如果未找到则返回 -1。
int findlocl(char *s) {
  int i;

  for (i = Locls + 1; i < NSYMBOLS; i++) {
    if (*s == *Symtable[i].name && !strcmp(s, Symtable[i].name))
      return (i);
  }
  return (-1);
}

// 获取新局部符号槽的位置，
// 如果用完了位置则报错
static int newlocl(void) {
  int p;

  if ((p = Locls--) <= Globs)
    fatal("Too many local symbols");
  return (p);
}

// 清除
// 局部符号表中的所有条目
void freeloclsyms(void) {
  Locls = NSYMBOLS - 1;
}

// 更新符号表中给定槽号的符号。设置其：
// + type: char, int 等。
// + 结构类型: var, function, array 等。
// + size: 元素数量
// + endlabel: 如果是函数
// + posn: 局部符号的位置信息
static void updatesym(int slot, char *name, int type, int stype,
		      int class, int endlabel, int size, int posn) {
  if (slot < 0 || slot >= NSYMBOLS)
    fatal("Invalid symbol slot number in updatesym()");
  Symtable[slot].name = strdup(name);
  Symtable[slot].type = type;
  Symtable[slot].stype = stype;
  Symtable[slot].class = class;
  Symtable[slot].endlabel = endlabel;
  Symtable[slot].size = size;
  Symtable[slot].posn = posn;
}

// 将全局符号添加到符号表。设置其：
// + type: char, int 等。
// + 结构类型: var, function, array 等。
// + 符号的 class
// + size: 元素数量
// + endlabel: 如果是函数
// 返回符号表中的槽号
int addglob(char *name, int type, int stype, int class, int endlabel,
	    int size) {
  int slot;

  // 如果这已经在符号表中，返回现有槽
  if ((slot = findglob(name)) != -1)
    return (slot);

  // 否则获取一个新槽并填充它
  slot = newglob();
  updatesym(slot, name, type, stype, class, endlabel, size, 0);
  // 如果是全局的，为符号生成汇编
  if (class == C_GLOBAL)
    genglobsym(slot);
  // 返回槽号
  return (slot);
}

// 将局部符号添加到符号表。设置其：
// + type: char, int 等。
// + 结构类型: var, function, array 等。
// + size: 元素数量
// 返回符号表中的槽号，
// 如果是重复条目则返回 -1
int addlocl(char *name, int type, int stype, int class, int size) {
  int localslot;

  // 如果这已经在符号表中，返回错误
  if ((localslot = findlocl(name)) != -1)
    return (-1);

  // 否则获取一个新的符号槽，
  // 并为此局部变量设置一个位置。
  // 更新局部符号表条目。
  localslot = newlocl();
  updatesym(localslot, name, type, stype, class, 0, size, 0);

  // 返回局部变量的槽号
  return (localslot);
}

// 给定函数的槽号，
// 将其原型中的全局参数
// 复制为局部参数
void copyfuncparams(int slot) {
  int i, id = slot + 1;

  for (i = 0; i < Symtable[slot].nelems; i++, id++) {
    addlocl(Symtable[id].name, Symtable[id].type, Symtable[id].stype,
	    Symtable[id].class, Symtable[id].size);
  }
}


// 确定符号 s 是否在符号表中。
// 返回其槽位置，如果未找到则返回 -1。
int findsymbol(char *s) {
  int slot;

  slot = findlocl(s);
  if (slot == -1)
    slot = findglob(s);
  return (slot);
}