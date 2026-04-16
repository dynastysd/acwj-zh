#include "defs.h"
#include "data.h"
#include "decl.h"

// 类型与类型处理
// Copyright (c) 2019 Warren Toomey, GPL3

// 给定两个基本类型，
// 如果兼容则返回真，否则返回假。
// 如果需要将一个扩展以匹配另一个，
// 还要返回零或 A_WIDEN 操作。
// 如果 onlyright 为真，则只将左边扩展到右边。
int type_compatible(int *left, int *right, int onlyright) {
  int leftsize, rightsize;

  // 相同类型，它们兼容
  if (*left == *right) {
    *left = *right = 0;
    return (1);
  }
  // 获取每种类型的大小
  leftsize = genprimsize(*left);
  rightsize = genprimsize(*right);

  // 大小为零的类型与任何类型都不
  // 兼容
  if ((leftsize == 0) || (rightsize == 0))
    return (0);

  // 按需扩展类型
  if (leftsize < rightsize) {
    *left = A_WIDEN;
    *right = 0;
    return (1);
  }
  if (rightsize < leftsize) {
    if (onlyright)
      return (0);
    *left = 0;
    *right = A_WIDEN;
    return (1);
  }
  // 剩余的任何情况都是相同大小
  // 因此兼容
  *left = *right = 0;
  return (1);
}