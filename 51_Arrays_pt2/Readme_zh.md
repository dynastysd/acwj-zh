# Part 51: Arrays, part 2（数组，第二部分）

在我上次编译器编写的旅程中，我意识到我对数组的实现并不完全正确。
在这次编译器编写旅程中，我将尝试纠正这个问题。

首先，我退后一步，思考了一下数组和指针的关系。
我意识到数组与指针类似，但有以下区别：

  1. 你不能将不带下标的数组标识符用作右值。
  2. 数组的大小是其所有元素大小的总和。
     指针的大小不包括它所指向的任何数组的元素。
  3. 数组的地址（例如 `&ary`）没有任何实际用途，
     与指针的地址（例如 `&ptr`）不同。

作为上述第一点的例子，请考虑：

```c
int ary[5];
int *ptr;

int main() {
   ptr= ary;            // OK, put base address of ary into ptr
   ary= ptr;            // Bad, can't change ary's base address
```

而且，对于那些C语言纯粹主义者，我知道第三点并不完全正确。
但我不需要在任何地方使用 `&ary`，所以我可以使我们的编译器拒绝它，
这意味着我不需要实现这个功能！

那么，我们具体需要修改什么呢？

  + 允许在 '[' 标记前使用标量或数组标识符
  + 允许不带下标的数组标识符，但将其标记为右值
  + 在我们尝试对数组做坏事时添加更多错误

大概就是这些了。我已经对这些编译器进行了修改。我希望它们能覆盖
所有的数组问题，但可能我忽略了一些其他的东西。如果是这样，
我们会再次回顾。

## 对 `postfix()` 的修改

在上一次中，我在 `expr.c` 的 `postfix()` 中放置了一个"权宜之计"修复，
但现在是时候回去正确修复它了。我们需要允许不带下标的数组标识符，
但将它们标记为右值。以下是修改内容：

```c
static struct ASTnode *postfix(void) {
  ...
  int rvalue=0;
  ...
  // An identifier, check that it exists. For arrays, set rvalue to 1.
  if ((varptr = findsymbol(Text)) == NULL)
    fatals("Unknown variable", Text);
  switch(varptr->stype) {
    case S_VARIABLE: break;
    case S_ARRAY: rvalue= 1; break;
    default: fatals("Identifier not a scalar or array variable", Text);
  }

  switch (Token.token) {
    // Post-increment: skip over the token. Also same for post-decrement
  case T_INC:
    if (rvalue == 1)
      fatals("Cannot ++ on rvalue", Text);
  ...
    // Just a variable reference. Ensure any arrays
    // cannot be treated as lvalues.
  default:
    if (varptr->stype == S_ARRAY) {
      n = mkastleaf(A_ADDR, varptr->type, varptr, 0);
      n->rvalue = rvalue;
    } else
      n = mkastleaf(A_IDENT, varptr->type, varptr, 0);
  }
  return (n);
}
```

现在标量或数组变量都可以不带下标使用，但数组不能作为左值。
此外，数组不能进行前置或后置递增。
我们可以加载数组基址，或加载标量变量中的值。

## 对 `array_access()` 的修改

现在我们需要修改 `expr.c` 中的 `array_access()`，
以允许指针与 '[' ']' 索引一起使用。以下是修改内容：

```c
static struct ASTnode *array_access(void) {
  struct ASTnode *left, *right;
  struct symtable *aryptr;

  // Check that the identifier has been defined as an array or a pointer.
  if ((aryptr = findsymbol(Text)) == NULL)
    fatals("Undeclared variable", Text);
  if (aryptr->stype != S_ARRAY &&
        (aryptr->stype == S_VARIABLE && !ptrtype(aryptr->type)))
    fatals("Not an array or pointer", Text);
  
  // Make a leaf node for it that points at the base of
  // the array, or loads the pointer's value as an rvalue
  if (aryptr->stype == S_ARRAY)
    left = mkastleaf(A_ADDR, aryptr->type, aryptr, 0);
  else {
    left = mkastleaf(A_IDENT, aryptr->type, aryptr, 0);
    left->rvalue= 1;
  }
  ...
}
```

现在我们检查符号是否存在，并且是数组或指针类型的标量变量。
一旦这个检查通过，我们就可以加载数组基址，或加载指针变量中的值。

## 测试代码修改

我不会详细介绍所有测试，而是总结一下：

  + `tests/input124.c` 检查 `ary++` 不能对数组执行。
  + `tests/input125.c` 检查我们可以赋值 `ptr= ary` 然后
     通过指针访问数组。
  + `tests/input126.c` 检查我们不能做 `&ary`。
  + `tests/input127.c` 使用 `fred(ary)` 调用函数，并确保
     我们可以将其作为指针参数接收。

## 结论与下一步

嗯，我曾担心我必须重写大量代码才能正确地实现数组。
就目前而言，代码几乎是正确的，只需要再做一些调整以覆盖我们需要的
所有功能。

在我们编译器编写旅程的下一部分中，我们将回去做一些收尾工作。
[下一步](../52_Pointers_pt2/Readme_zh.md)