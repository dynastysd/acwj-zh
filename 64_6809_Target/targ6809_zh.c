// 6809目标特定的函数
// 由解析器和代码生成器使用。
// Copyright (c) 2024 Warren Toomey, GPL3

// 给定一个标量类型值，返回
// 该类型的大小（以字节为单位）。
int cgprimsize(int type) {
  if (ptrtype(type))
    return (2);
  switch (type) {
  case P_VOID:
    return (0);
  case P_CHAR:
    return (1);
  case P_INT:
    return (2);
  case P_LONG:
    return (4);
  default:
    fatald("Bad type in cgprimsize:", type);
  }
  return (0);                   // 保持-Wall满意
}

int cgalign(int type, int offset, int direction) {
  return (offset);
}

int genprimsize(int type) {
  return (cgprimsize(type));
}

int genalign(int type, int offset, int direction) {
  return (cgalign(type, offset, direction));
}

// 返回可以保存地址的原始类型。
// 当我们需要将INTLIT添加到指针时使用。
int cgaddrint(void) {
  return(P_INT);
}