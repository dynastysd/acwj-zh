#include "defs.h"
#include "data.h"
#include "decl.h"

// 杂项函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 确保当前词法单元是 t，
// 并获取下一个词法单元。否则
// 抛出错误
void match(int t, char *what) {
  if (Token.token == t) {
    scan(&Token);
  } else {
    fatals("Expected", what);
  }
}

// 匹配分号并获取下一个词法单元
void semi(void) {
  match(T_SEMI, ";");
}

// 匹配标识符并获取下一个词法单元
void ident(void) {
  match(T_IDENT, "identifier");
}

// 输出致命错误消息
void fatal(char *s) {
  fprintf(stderr, "%s on line %d\n", s, Line); exit(1);
}

void fatals(char *s1, char *s2) {
  fprintf(stderr, "%s:%s on line %d\n", s1, s2, Line); exit(1);
}

void fatald(char *s, int d) {
  fprintf(stderr, "%s:%d on line %d\n", s, d, Line); exit(1);
}

void fatalc(char *s, int c) {
  fprintf(stderr, "%s:%c on line %d\n", s, c, Line); exit(1);
}