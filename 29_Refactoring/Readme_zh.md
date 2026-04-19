# 第 29 章：一些重构

我开始思考在编译器中实现结构体、联合体和枚举的设计方面，然后想到了一个改进符号表的好主意，这就让我对编译器代码进行了一些重构。因此，在这部分旅程中没有新的功能，但我对编译器中的一些代码感到更满意了。

如果你对我关于结构体、联合体和枚举的设计想法更感兴趣，可以直接跳到下一部分。

## 重构符号表

当我开始写编译器时，我刚刚读完 [SubC](http://www.t3x.org/subc/) 编译器的代码并添加了自己的注释。因此，我从这个代码库中借鉴了很多最初的思路。其中之一是使用元素数组来存储符号表，全局符号在一端，局部符号在另一端。

我们已经看到，对于函数原型和参数，我们必须将函数的原型从全局端复制到局部端，这样函数就有局部参数变量。我们还必须担心符号表的一端是否会撞到另一端。

所以，在某个时候，我们应该将符号表转换为多个单向链表：至少一个用于全局符号，一个用于局部符号。当我们实现枚举时，我可能还会有第三个用于枚举值。

现在，我在这部分旅程中没有做这个重构，因为看起来变化很大，所以我将等到真正需要的时候再做。但我还会做另一个改变。每个符号节点都有一个 `next` 指针来形成单向链表，但还有一个 `param` 指针。这样函数就可以有一个单独的单向链表来存储它们的参数，我们在搜索全局符号时可以直接跳过。然后，当我们需要"复制"函数的原型作为其参数列表时，只需复制指向原型参数列表的指针就行了。不管怎样，这个改变是未来的事。

## 类型，再谈

我从 SubC 借鉴的另一件事是类型的枚举（在 `defs.h` 中）：

```c
// Primitive types
enum {
  P_NONE, P_VOID, P_CHAR, P_INT, P_LONG,
  P_VOIDPTR, P_CHARPTR, P_INTPTR, P_LONGPTR
};
```

SubC 只允许一级间接寻址，因此有了上面的类型列表。我有一个想法，为什么不把间接寻址的级别编码到原始类型的值中呢？所以我改变了代码，使得 `type` 整数的低四位是间接寻址的级别，高位编码实际类型：

```c
// Primitive types. The bottom 4 bits is an integer
// value that represents the level of indirection,
// e.g. 0= no pointer, 1= pointer, 2= pointer pointer etc.
enum {
  P_NONE, P_VOID=16, P_CHAR=32, P_INT=48, P_LONG=64
};
```

我已经能够完全重构掉旧代码中所有旧的 `P_XXXPTR` 引用。让我们看看有哪些变化。

首先，我们必须在 `types.c` 中处理标量类型和指针类型。现在的代码实际上比以前更小了：

```c
// Return true if a type is an int type
// of any size, false otherwise
int inttype(int type) {
  return ((type & 0xf) == 0);
}

// Return true if a type is of pointer type
int ptrtype(int type) {
  return ((type & 0xf) != 0);
}

// Given a primitive type, return
// the type which is a pointer to it
int pointer_to(int type) {
  if ((type & 0xf) == 0xf)
    fatald("Unrecognised in pointer_to: type", type);
  return (type + 1);
}

// Given a primitive pointer type, return
// the type which it points to
int value_at(int type) {
  if ((type & 0xf) == 0x0)
    fatald("Unrecognised in value_at: type", type);
  return (type - 1);
}
```

而 `modify_type()` 完全没有改变。

在 `expr.c` 中处理字符串字面量时，我以前使用 `P_CHARPTR`，但现在我可以写：

```c
   n = mkastleaf(A_STRLIT, pointer_to(P_CHAR), id);
```

另一个大量使用 `P_XXXPTR` 值的领域是 `cg.c` 中硬件相关代码。我们首先重写 `cgprimsize()` 来使用 `ptrtype()`：

```c
// Given a P_XXX type value, return the
// size of a primitive type in bytes.
int cgprimsize(int type) {
  if (ptrtype(type)) return (8);
  switch (type) {
    case P_CHAR: return (1);
    case P_INT:  return (4);
    case P_LONG: return (8);
    default: fatald("Bad type in cgprimsize:", type);
  }
  return (0);                   // Keep -Wall happy
}
```

使用这段代码，`cg.c` 中的其他函数现在可以根据需要调用 `cgprimsize()`、`ptrtype()`、`inttype()`、`pointer_to()` 和 `value_at()`，而不是引用特定类型。以下是来自 `cg.c` 的一个例子：

```c
// Dereference a pointer to get the value it
// pointing at into the same register
int cgderef(int r, int type) {

  // Get the type that we are pointing to
  int newtype = value_at(type);

  // Now get the size of this type
  int size = cgprimsize(newtype);

  switch (size) {
  case 1:
    fprintf(Outfile, "\tmovzbq\t(%s), %s\n", reglist[r], reglist[r]);
    break;
  case 2:
    fprintf(Outfile, "\tmovslq\t(%s), %s\n", reglist[r], reglist[r]);
    break;
  case 4:
  case 8:
    fprintf(Outfile, "\tmovq\t(%s), %s\n", reglist[r], reglist[r]);
    break;
  default:
    fatald("Can't cgderef on type:", type);
  }
  return (r);
}
```

快速浏览一下 `cg.c` 并查找对 `cgprimsize()` 的调用。

### 双指针的使用示例

现在我们有了高达十六级的间接寻址，我写了一个测试程序来确认它们可以工作，`tests/input55.c`：

```c
int printf(char *fmt);

int main(int argc, char **argv) {
  int i;
  char *argument;
  printf("Hello world\n");

  for (i=0; i < argc; i++) {
    argument= *argv; argv= argv + 1;
    printf("Argument %d is %s\n", i, argument);
  }
  return(0);
}
```

注意 `argv++` 还不能工作，`argv[i]` 也还不能工作。但我们可以通过上面的方式绕过这些缺失的特性。

## 符号表结构的变更

虽然我没有将符号表重构为链表，但我确实调整了符号表结构本身，因为我意识到可以使用联合体而不必给联合体一个变量名：

```c
// Symbol table structure
struct symtable {
  char *name;                   // Name of a symbol
  int type;                     // Primitive type for the symbol
  int stype;                    // Structural type for the symbol
  int class;                    // Storage class for the symbol
  union {
    int size;                   // Number of elements in the symbol
    int endlabel;               // For functions, the end label
  };
  union {
    int nelems;                 // For functions, # of params
    int posn;                   // For locals, the negative offset
                                // from the stack base pointer
  };
};
```

我以前用 `#define` 来定义 `nelems`，但上面的方法得到相同的结果，并且防止了全局定义的 `nelems` 污染命名空间。我还意识到 `size` 和 `endlabel` 可以占据结构中的相同位置，并添加了这个联合体。对 `addglob()` 参数有一些 cosmetic 变化，但不多。

## AST 结构的变更

同样，我修改了 AST 节点结构，使联合体没有变量名：

```c
// Abstract Syntax Tree structure
struct ASTnode {
  int op;                       // "Operation" to be performed on this tree
  int type;                     // Type of any expression this tree generates
  int rvalue;                   // True if the node is an rvalue
  struct ASTnode *left;         // Left, middle and right child trees
  struct ASTnode *mid;
  struct ASTnode *right;
  union {                       // For A_INTLIT, the integer value
    int intvalue;               // For A_IDENT, the symbol slot number
    int id;                     // For A_FUNCTION, the symbol slot number
    int size;                   // For A_SCALE, the size to scale by
  };                            // For A_FUNCCALL, the symbol slot number
};
```

这意味着我可以，例如，写第二行而不是第一行：

```c
    return (cgloadglob(n->left->v.id, n->op));    // Old code
    return (cgloadglob(n->left->id,   n->op));    // New code
```

## 结论与下一步

这就是我们编译器编写旅程这一部分的全部内容。我可能在这里和那里做了一些更多的小代码更改，但我想不到还有什么其他主要的了。

我会将符号表改成链表；这可能会在我们实现枚举值的部分发生。

在我们编译器编写旅程的下一部分，我将回到我原来想在本部分讨论的内容：在编译器中实现结构体、联合体和枚举的设计方面。[下一步](../30_Design_Composites/Readme_zh.md)