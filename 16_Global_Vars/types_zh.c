#include "defs.h"
#include "data.h"
#include "decl.h"

// 类型和类型处理
// Copyright (c) 2019 Warren Toomey, GPL3

// 给定两个原始类型，
// 如果它们兼容则返回真，
// 否则返回假。如果需要，
// 还返回 A_WIDEN 操作（当一个
// 类型需要加宽以匹配另一个时）。
// 如果 onlyright 为真，则只从左到右加宽。
int type_compatible(int *left, int *right, int onlyright) {
  int leftsize, rightsize;

  // 相同类型，它们兼容
  if (*left == *right) {
    *left = *right = 0;
    return (1);
  }
  // 获取每个类型的大小
  leftsize = genprimsize(*left);
  rightsize = genprimsize(*right);

  // 大小为 0 的类型与
  // 任何类型都不兼容
  if ((leftsize == 0) || (rightsize == 0))
    return (0);

  // 根据需要加宽类型
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
  // 剩余的任何具有相同大小
  // 的类型都是兼容的
  *left = *right = 0;
  return (1);
}

// 给定一个原始类型，返回
// 指向它的指针类型
int pointer_to(int type) {
  int newtype;
  switch (type) {
    case P_VOID:
      newtype = P_VOIDPTR;
      break;
    case P_CHAR:
      newtype = P_CHARPTR;
      break;
    case P_INT:
      newtype = P_INTPTR;
      break;
    case P_LONG:
      newtype = P_LONGPTR;
      break;
    default:
      fatald("Unrecognised in pointer_to: type", type);
  }
  return (newtype);
}

// 给定一个原始指针类型，返回
// 它指向的类型
int value_at(int type) {
  int newtype;
  switch (type) {
    case P_VOIDPTR:
      newtype = P_VOID;
      break;
    case P_CHARPTR:
      newtype = P_CHAR;
      break;
    case P_INTPTR:
      newtype = P_INT;
      break;
    case P_LONGPTR:
      newtype = P_LONG;
      break;
    default:
      fatald("Unrecognised in value_at: type", type);
  }
  return (newtype);
}