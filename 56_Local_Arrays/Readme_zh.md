# 第 56 部分：局部数组

哎呀，让我惊讶的是，实现局部数组并不困难。原来，编译器中已经有所有需要的组件，我们只需要将它们连接起来。

## 局部数组解析

让我们从解析端开始。我想允许局部数组声明，但只能指定元素数量，不能赋值。

声明方面很简单，只需在 `decl.c` 的 `array_declaration()` 中添加以下行：

```c
  // 将其添加为已知的数组。我们将数组
  // 视为指向其元素类型的指针
  switch (class) {
    ...
    case C_LOCAL:
      sym = addlocl(varname, pointer_to(type), ctype, S_ARRAY, 0);
      break;
    ...
  }
```

现在，我们必须禁止对局部数组赋值：

```c
  // 数组初始化
  if (Token.token == T_ASSIGN) {
    if (class != C_GLOBAL && class != C_STATIC)
      fatals("Variable can not be initialised", varname);
```

我还添加了一些更多的错误检查：

```c
  // 设置数组的大小和元素数量
  // 只有 extern 可以没有元素。
  if (class != C_EXTERN && nelems<=0)
    fatals("Array must have non-zero elements", sym->name);
```

这就是局部数组声明端的全部内容。

## 代码生成

在 `cg.c` 中，有一个 `newlocaloffset()` 函数，用于计算局部变量相对于栈帧顶部的偏移量。
它的参数是原始类型，因为编译器以前只允许 int 和指针类型作为局部变量。

现在每个符号都有其大小（`sizeof()` 使用的大小），我们可以更改此函数中的代码以使用符号的大小：

```c
// 创建新局部变量的位置。
static int newlocaloffset(int size) {
  // 偏移量至少减少 4 个字节
  // 并在栈上分配
  localOffset += (size > 4) ? size : 4;
  return (-localOffset);
}
```

在生成函数前导码的代码 `cgfuncpreamble()` 中，我们只需要做以下更改：

```c
  // 将任何在寄存器中的参数复制到栈上，最多六个。
  // 其余参数已经在栈上。
  for (parm = sym->member, cnt = 1; parm != NULL; parm = parm->next, cnt++) {
    if (cnt > 6) {
      parm->st_posn = paramOffset;
      paramOffset += 8;
    } else {
      parm->st_posn = newlocaloffset(parm->size);       // 这里
      cgstorlocal(paramReg--, parm);
    }
  }

  // 对于其余的，如果是参数那么它们
  // 已经在栈上。如果是局部变量，则在栈上分配空间。
  for (locvar = Loclhead; locvar != NULL; locvar = locvar->next) {
    locvar->st_posn = newlocaloffset(locvar->size);     // 这里
  }
```

就这样！这可能意味着我们也可以允许 struct 和 union 作为局部变量。
我还没有考虑这个问题，但这是以后需要探索的内容。

## 测试更改

`test/input140.c` 声明了：

```c
int main() {
  int  i;
  int  ary[5];
  char z;
  ...
```

数组用 FOR 循环填充，`i` 作为索引。
`z` 局部变量也被初始化。这检查任何变量是否会踩踏其他变量。
它还检查我们是否可以赋值数组的所有元素并获取它们的值。

文件 `test/input141.c` 和 `test/input142.c` 检查编译器是否能够发现并拒绝作为参数的数组和没有元素的数组声明。

## 结论和下一步

在我们编译器编写旅程的下一部分，我将回到清理工作。[下一步](../57_Mop_up_pt3/Readme_zh.md)