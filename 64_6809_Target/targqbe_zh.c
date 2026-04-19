// QBE目标特定的函数
// 由解析器和代码生成器使用。
// Copyright (c) 2024 Warren Toomey, GPL3

// 给定一个标量类型值，返回
// QBE类型的大小（以字节为单位）。
int cgprimsize(int type) {
  if (ptrtype(type))
    return (8);
  switch (type) {
    case P_CHAR:
      return (1);
    case P_INT:
      return (4);
    case P_LONG:
      return (8);
    default:
      fatald("Bad type in cgprimsize:", type);
  }
  return (0);                   // 保持-Wall满意
}

// 给定一个标量类型、一个现有的内存偏移量
// （尚未分配给任何东西）和一个方向（1表示向上，-1表示向下），
// 计算并返回此标量类型的适当对齐内存偏移量。
// 这可能是原始偏移量，也可能是原始偏移量的上方或下方。
int cgalign(int type, int offset, int direction) {
  int alignment;

  // 在x86-64上我们不需要这样做，但让我们
  // 在任何偏移量上对齐char，并对int/指针
  // 进行4字节对齐
  switch (type) {
    case P_CHAR:
      break;
    default:
      // 在4字节对齐上对齐我们当前的任何东西。
      // 我把通用代码放在这里以便在其他地方重用。
      alignment = 4;
      offset = (offset + direction * (alignment - 1)) & ~(alignment - 1);
  }
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
  return(P_LONG);
}