#include "defs.h"
#include "data.h"
#include "decl.h"

// 符号表函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 确定符号 s 是否在全局符号表中。
// 返回其槽位位置，如果未找到则返回 -1。
int findglob(char *s) {
  int i;

  for (i = 0; i < Globs; i++) {
    if (*s == *Gsym[i].name && !strcmp(s, Gsym[i].name))
      return (i);
  }
  return (-1);
}

// 获取新全局符号槽的位置，
// 如果我们已经用完了位置则报错。
static int newglob(void) {
  int p;

  if ((p = Globs++) >= NSYMBOLS)
    fatal("Too many global symbols");
  return (p);
}

// 将全局符号添加到符号表。
// 同时设置其类型和结构类型。
// 返回符号表中的槽号
int addglob(char *name, int type, int stype, int endlabel) {
  int y;

  // 如果已经在符号表中，返回现有槽
  if ((y = findglob(name)) != -1)
    return (y);

  // 否则获取一个新槽，填充它，
  // 并返回槽号
  y = newglob();
  Gsym[y].name = strdup(name);
  Gsym[y].type = type;
  Gsym[y].stype = stype;
  Gsym[y].endlabel = endlabel;
  return (y);
}