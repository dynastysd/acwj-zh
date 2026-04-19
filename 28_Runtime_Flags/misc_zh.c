#include "defs.h"
#include "data.h"
#include "decl.h"
#include <unistd.h>

// 杂项函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 确保当前词法单元是 t，
// 并获取下一个词法单元。
// 否则报错
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

// 匹配左花括号并获取下一个词法单元
void lbrace(void) {
  match(T_LBRACE, "{");
}

// 匹配右花括号并获取下一个词法单元
void rbrace(void) {
  match(T_RBRACE, "}");
}

// 匹配左括号并获取下一个词法单元
void lparen(void) {
  match(T_LPAREN, "(");
}

// 匹配右括号并获取下一个词法单元
void rparen(void) {
  match(T_RPAREN, ")");
}

// 匹配标识符并获取下一个词法单元
void ident(void) {
  match(T_IDENT, "identifier");
}

// 打印致命消息
void fatal(char *s) {
  fprintf(stderr, "%s on line %d\n", s, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}

void fatals(char *s1, char *s2) {
  fprintf(stderr, "%s:%s on line %d\n", s1, s2, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}

void fatald(char *s, int d) {
  fprintf(stderr, "%s:%d on line %d\n", s, d, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}

void fatalc(char *s, int c) {
  fprintf(stderr, "%s:%c on line %d\n", s, c, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}