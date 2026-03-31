#include "defs.h"
#include "data.h"
#include "decl.h"

// 辅助函数
// Copyright (c) 2019 Warren Toomey, GPL3

// 确保当前 token 是 t，
// 并获取下一个 token。否则
// 抛出一个错误 
void match(int t, char *what) {
  if (Token.token == t) {
    scan(&Token);
  } else {
    printf("%s expected on line %d\n", what, Line);
    exit(1);
  }
}

// 匹配一个分号并获取下一个 token
void semi(void) {
  match(T_SEMI, ";");
}
