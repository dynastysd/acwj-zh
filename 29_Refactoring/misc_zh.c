#include "defs.h"
#include "data.h"
#include "decl.h"
#include <unistd.h>

// 杂项函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 确保当前token是t,
// 并获取下一个token。否则报错
void match(int t, char *what) {
  if (Token.token == t) {
    scan(&Token);
  } else {
    fatals("期望", what);
  }
}

// 匹配分号并获取下一个token
void semi(void) {
  match(T_SEMI, ";");
}

// 匹配左花括号并获取下一个token
void lbrace(void) {
  match(T_LBRACE, "{");
}

// 匹配右花括号并获取下一个token
void rbrace(void) {
  match(T_RBRACE, "}");
}

// 匹配左括号并获取下一个token
void lparen(void) {
  match(T_LPAREN, "(");
}

// 匹配右括号并获取下一个token
void rparen(void) {
  match(T_RPAREN, ")");
}

// 匹配标识符并获取下一个token
void ident(void) {
  match(T_IDENT, "identifier");
}

// 打印致命错误消息
void fatal(char *s) {
  fprintf(stderr, "%s 在第 %d 行\n", s, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}

void fatals(char *s1, char *s2) {
  fprintf(stderr, "%s:%s 在第 %d 行\n", s1, s2, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}

void fatald(char *s, int d) {
  fprintf(stderr, "%s:%d 在第 %d 行\n", s, d, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}

void fatalc(char *s, int c) {
  fprintf(stderr, "%s:%c 在第 %d 行\n", s, c, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}