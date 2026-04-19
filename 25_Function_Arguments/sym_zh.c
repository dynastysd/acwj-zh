#include "defs.h"
#include "data.h"
#include "decl.h"

// 符号表函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 判断符号 s 是否在全局符号表中。
// 返回其槽位置，如果未找到则返回 -1。
// 跳过 C_PARAM 条目
int findglob(char *s) {
  int i;

  for (i = 0; i < Globs; i++) {
    if (Symtable[i].class == C_PARAM) continue;
    if (*s == *Symtable[i].name && !strcmp(s, Symtable[i].name))
      return (i);
  }
  return (-1);
}

// 获取新全局符号槽的位置，如果用完则报错。
static int newglob(void) {
  int p;

  if ((p = Globs++) >= Locls)
    fatal("Too many global symbols");
  return (p);
}

// 判断符号 s 是否在局部符号表中。
// 返回其槽位置，如果未找到则返回 -1。
int findlocl(char *s) {
  int i;

  for (i = Locls + 1; i < NSYMBOLS; i++) {
    if (*s == *Symtable[i].name && !strcmp(s, Symtable[i].name))
      return (i);
  }
  return (-1);
}

// 获取新局部符号槽的位置，如果用完则报错。
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

// 在符号表的给定槽号处更新符号。设置其：
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

// 向符号表添加全局符号。设置其：
// + type: char, int 等。
// + 结构类型: var, function, array 等。
// + size: 元素数量
// + endlabel: 如果是函数
// 返回符号表中的槽号
int addglob(char *name, int type, int stype, int endlabel, int size) {
  int slot;

  // 如果已在符号表中，返回现有槽
  if ((slot = findglob(name)) != -1)
    return (slot);

  // 否则获取新槽，填充它并
  // 返回槽号
  slot = newglob();
  updatesym(slot, name, type, stype, C_GLOBAL, endlabel, size, 0);
  genglobsym(slot);
  return (slot);
}

// 向符号表添加局部符号。设置其：
// + type: char, int 等。
// + 结构类型: var, function, array 等。
// + size: 元素数量
// + isparam: 如果为 true，这是函数的形参
// 返回符号表中的槽号，如果重复条目则返回 -1
int addlocl(char *name, int type, int stype, int isparam, int size) {
  int localslot, globalslot;

  // 如果已在符号表中，返回错误
  if ((localslot = findlocl(name)) != -1)
    return (-1);

  // 否则获取新符号槽和此局部的位置。
  // 更新局部符号表条目。如果这是形参，
  // 还要创建全局 C_PARAM 条目来构建函数的原型。
  localslot = newlocl();
  if (isparam) {
    updatesym(localslot, name, type, stype, C_PARAM, 0, size, 0);
    globalslot = newglob();
    updatesym(globalslot, name, type, stype, C_PARAM, 0, size, 0);
  } else {
    updatesym(localslot, name, type, stype, C_LOCAL, 0, size, 0);
  }

  // 返回局部符号的槽
  return (localslot);
}

// 判断符号 s 是否在符号表中。
// 返回其槽位置，如果未找到则返回 -1。
int findsymbol(char *s) {
  int slot;

  slot = findlocl(s);
  if (slot == -1)
    slot = findglob(s);
  return (slot);
}