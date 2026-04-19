# 第 30 章：设计 Struct、Union 和 Enum

在这一部分编译器编写的旅程中，我将概述实现结构体、联合体和枚举的设计思路。和函数一样，这需要多个后续步骤才能完成全部实现。

我还决定将符号表从单个数组重写为多个单链表。我之前已经提到过这个意图：关于如何实现复合类型的设计思路，使我现在必须重写符号表的实现。

在深入代码变更之前，让我们先看看复合类型到底是什么。

## 复合类型、枚举和typedef

在 C 语言中，[structs](https://en.wikipedia.org/wiki/Struct_(C_programming_language))
和[unions](https://en.wikipedia.org/wiki/Union_type#C/C++)被称为*复合类型*。结构体或联合体变量可以包含多个成员。区别在于，结构体中的成员保证不会在内存中重叠，而在联合体中，我们希望所有成员共享相同的内存位置。

以下是结构体类型的一个例子：

```c
struct foo {
  int  a;
  int  b;
  char c;
};

struct foo fred;
```

变量`fred`的类型是`struct foo`，它有三个成员`a`、`b`和`c`。我们现在可以对`fred`进行这三个赋值：

```c
  fred.a= 4;
  fred.b= 7;
  fred.c= 'x';
```

所有三个值都存储在`fred`中相应的成员里。

另一方面，以下是联合体类型的一个例子：

```c
union bar {
  int  a;
  int  b;
  char c;
};

union bar jane;
```

如果我们执行这些语句：

```c
  jane.a= 5;
  printf("%d\n", jane.b);
```

那么会打印出值 5，因为`a`和`b`成员在`jane`联合体中占用相同的内存位置。

### 枚举

这里我也谈谈枚举，尽管它们并不像结构体和联合体那样定义复合类型。
在 C 语言中，[enums](https://en.wikipedia.org/wiki/Enumerated_type#C)本质上是给整数值起名字的一种方式。枚举代表一个命名的整数值的列表。

例如，我们可以定义这些新的标识符：

```c
enum { apple=1, banana, carrot, pear=10, peach, mango, papaya };
```

我们现在有了这些命名的整数值：

|  名称  |  值  |
|:------:|:-----:|
| apple  |   1   |
| banana |   2   |
| carrot |   3   |
| pear   |  10   |
| peach  |  11   |
| mango  |  12   |
| papaya |  13   |

关于枚举有一些有趣的问题我之前并不知道，我将在下面介绍。

### Typedef

此时我也应该提一下 typedef，尽管要让我们的编译器能够自行编译，我不需要实现它们。
[typedef](https://en.wikipedia.org/wiki/Typedef)是为现有类型提供另一个名称的一种方式。它通常用于简化结构体和联合体的命名。

使用前面的例子，我们可以这样写：

```c
typedef struct foo Husk;
Husk kim;
```

`kim`的 类型是`Husk`，这与说`kim`的类型是`struct foo`是一样的。

## 类型与符号？

那么，如果 struct、union 和 typedef 是新类型，它们与存放变量和函数定义的符号表有什么关系？枚举只是整数字面量的名称，同样不是变量或函数。

问题是，所有这些东西都有*名字*：结构体或联合体的名称、它们成员的名字、成员的类型、枚举值的名称，以及 typedef 的名称。

我们需要把这些名字存储在某个地方，并且需要能够找到它们。对于结构体/联合体成员，我们需要找到它们的基本类型。对于枚举名称，我们需要查找它们的整数字面量值。

这就是为什么我将使用符号表来存储所有这些东西。但是，我们需要将表分成几个特定的列表，这样我们才能找到特定的东西，避免找到我们不想要的东西。

## 重设计符号表结构

让我们从以下开始：

 + 一个单链表用于全局变量和函数
 + 一个单链表用于当前函数的局部变量
 + 一个单链表用于当前函数的参数

在使用旧的基于数组的符号表时，我们在搜索全局变量和函数时必须跳过函数参数。所以，让我们也为函数的参数准备一个单独方向的列表：

```c
struct symtable {
  char *name;                   // 符号的名称
  int stype;                    // 符号的结构类型
  ...
  struct symtable *next;        // 链表中下一个符号
  struct symtable *member;      // 函数的第一个参数
};
```

让我们图形化地看看下面这段代码会如何存储：

```c
  int a;
  char b;
  void func1(int x, int y);
  void main(int argc, char **argv) {
    int loc1;
    int loc2;
  }
```

这将存储在三个符号表列表中，如下所示：

![](Figs/newsymlists.png)

注意，我们有三个列表"头"，它们指向三个列表。
我们现在可以遍历全局符号列表，而不必跳过参数，因为每个函数都把自己的参数放在自己的列表上。

当需要解析函数体时，我们可以将参数列表指向函数的参数列表。然后，当声明局部变量时，它们只是被追加到局部变量列表中。

然后，一旦函数体被解析完毕并且其汇编代码生成完毕，我们就可以将参数和局部列表重新设置为空，而不会干扰全局可见函数中的参数列表。
这就是我目前对符号表重写的进度。但它没有展示我们如何实现结构体、联合体和枚举。

## 有趣的问题和考虑

在我们看到如何用现有的符号表节点加上单链表来支持结构体、联合体和枚举之前，我们首先必须考虑它们的一些更有趣的问题。

### 联合体

我们从联合体开始。首先，我们可以把一个联合体放入结构体中。其次，联合体不需要名称。第三，不需要在结构体中声明一个变量来持有这个联合体。例如：

```c
#include <stdio.h>
struct fred {
  int x;
  union {
    int a;
    int b;
  };            // 无需声明此联合体类型的变量
};

int main() {
  struct fred foo;
  foo.x= 5;
  foo.a= 12;                            // a 被当作结构体成员处理
  foo.b= 13;                            // b 被当作结构体成员处理
  printf("%d %d\n", foo.x, foo.a);      // 打印 5 和 13
}
```

我们需要能够支持这一点。匿名联合体（和结构体）很容易：我们只需将符号表节点中的`name`设置为 NULL。但是这个联合体没有变量名：我认为我们可以通过让结构体的成员名称也设置为 NULL 来实现这一点，即

![](Figs/structunion1.png)

### 枚举

以前我用过枚举，但没有真正考虑过实现它们太多次。所以我写了以下 C 程序来看看我是否能"破坏"枚举：

```c
#include <stdio.h>

enum fred { bill, mary, dennis };
int fred;
int mary;
enum fred { chocolate, spinach, glue };
enum amy { garbage, dennis, flute, amy };
enum fred x;
enum { pie, piano, axe, glyph } y;

int main() {
  x= bill;
  y= pie;
  y= bill;
  x= axe;
  x= y;
  printf("%d %d %ld\n", x, y, sizeof(x));
}
```

问题是：

 + 我们能否用不同的元素重新声明枚举列表，例如`enum fred`和`enum fred`？
 + 我们能否用与枚举列表相同的名称声明变量，例如`fred`？
 + 我们能否用与枚举值相同的名称声明变量，例如`mary`？
 + 我们能否在另一个枚举列表中重用一个枚举值的名称，例如`dennis`和`dennis`？
 + 我们能否将一个枚举列表的值赋值给声明为不同枚举列表的变量？
 + 我们能否在不同枚举列表声明的变量之间赋值？

以下是`gcc`产生的错误和警告：

```c
z.c:4:5: error: 'mary' redeclared as different kind of symbol
 int mary;
     ^~~~
z.c:2:19: note: previous definition of 'mary' was here
 enum fred { bill, mary, dennis };
                   ^~~~
z.c:5:6: error: nested redefinition of 'enum fred'
 enum fred { chocolate, spinach, glue };
      ^~~~
z.c:5:6: error: redeclaration of 'enum fred'
z.c:2:6: note: originally defined here
 enum fred { bill, mary, dennis };
      ^~~~
z.c:6:21: error: redeclaration of enumerator 'dennis'
 enum amy { garbage, dennis, flute, amy };
                     ^~~~~~
z.c:2:25: note: previous definition of 'dennis' was here
 enum fred { bill, mary, dennis };
                         ^~~~~~
```

在修改和编译上述程序几次之后，答案是：

 + 我们不能重新声明`enum fred`。这似乎是我们需要记住枚举列表名称的唯一地方。
 + 我们可以重用枚举列表标识符`fred`作为变量名。
 + 我们不能在其他枚举列表中重用枚举值标识符`mary`，也不能作为变量名。
 + 我们可以随时赋值枚举值：它们似乎只是被当作整数字面量值的名称。
 + 似乎我们也可以用单词`int`替换`enum`和`enum X`作为类型。

## 设计考虑

好的，我想我们已经到了可以开始列出我们需求的地步：

 + 一个用于命名和无名结构体的列表，包括每个结构体中的成员名称以及每个成员的类型详细信息。此外，我们还需要成员相对于结构体"基地址"的内存偏移量。
 + 有名和无名结构体的同样内容，不过偏移量始终为零。
 + 枚举列表名称以及实际枚举名称及其相关值的列表。
 + 在符号表中，我们需要非复合类型的现有`type`信息，但如果符号是结构体或联合体，我们还需要指向相关复合类型的指针。
 + 鉴于结构体可以有一个指向自身的成员的指针，我们将需要能够将成员的类型指回同一个结构体。

## 符号表节点结构的变更

下面，**粗体**显示了我对当前单链表符号表节点的变更：

<pre>
struct symtable {
  char *name;                   // 符号的名称
  int type;                     // 符号的原始类型
  <b>struct symtable *ctype;       // 如需要，指向复合类型的指针</b>
  int stype;                    // 符号的结构类型
  int class;                    // 符号的存储类别
  union {
    int size;                   // 符号中元素的数量
    int endlabel;               // 对于函数，结束标签
    <b>int intvalue;               // 对于枚举符号，关联的值</b>
  };
  union {
    int nelems;                 // 对于函数，参数数量
    int posn;                   // 对于局部变量，相对于栈基址的负偏移
                                // 从栈基址指针
  };
  struct symtable *next;        // 链表中下一个符号
  struct symtable *member;      // 函数、结构体、的第一个成员
};                              // 联合体或枚举
</pre>

除了这个新节点结构，我们还将有六个链表：

 + 一个单链表用于全局变量和函数
 + 一个单链表用于当前函数的局部变量
 + 一个单链表用于当前函数的参数
 + 一个单链表用于已定义的结构体类型
 + 一个单链表用于已定义的联合体类型
 + 一个单链表用于已定义的枚举名称和枚举值

## 新符号表节点的用例

让我们看看上述结构中的每个字段如何被上面列举的六个列表使用。

### 新类型

我们将有两个新类型 P_STRUCT 和 P_UNION，我将在下面描述它们。

### 全局变量和函数、参数变量、局部变量

 + *name*：变量或函数的名称。
 + *type*：变量的类型，或函数的返回值，加上 4 位间接级别。
 + *ctype*：如果变量是 P_STRUCT 或 P_UNION，此字段指向相关单链表中关联的结构体或联合体定义。
 + *stype*：变量或函数的结构类型：S_VARIABLE、S_FUNCTION 或 S_ARRAY。
 + *class*：变量的存储类别：C_GLOBAL、C_LOCAL 或 C_PARAM。
 + *size*：对于变量，以字节为单位的总大小。对于数组，数组中的元素数量。我们稍后将使用它来实现`sizeof()`。
 + *endlabel*：对于函数，我们可以`return`到的结束标签。
 + *nelems*：对于函数，参数数量。
 + *posn*：对于局部变量和参数，变量相对于栈基址的负偏移。
 + *next*：此列表中的下一个符号。
 + *member*：对于函数，指向第一个参数节点的指针。对于变量为 NULL。

### 结构体类型

 + *name*：结构体类型的名称，如果是匿名的则为 NULL。
 + *type*：始终为 P_STRUCT，并非真正需要。
 + *ctype*：未使用。
 + *stype*：未使用。
 + *class*：未使用。
 + *size*：结构体的总大小（以字节为单位），供以后`sizeof()`使用。
 + *nelems*：结构体中的成员数量。
 + *next*：下一个已定义的结构体类型。
 + *member*：指向第一个结构体成员节点的指针。

### 联合体类型

 + *name*：联合体类型的名称，如果是匿名的则为 NULL。
 + *type*：始终为 P_UNION，并非真正需要。
 + *ctype*：未使用。
 + *stype*：未使用。
 + *class*：未使用。
 + *size*：联合体的总大小（以字节为单位），供以后`sizeof()`使用。
 + *nelems*：联合体中的成员数量。
 + *next*：下一个已定义的联合体类型。
 + *member*：指向第一个联合体成员节点的指针。

### 结构体和联合体成员

每个成员本质上都是一个变量，因此与普通变量有很强的相似性。

 + *name*：成员的名称。
 + *type*：变量的类型加上 4 位间接级别。
 + *ctype*：如果成员是 P_STRUCT 或 P_UNION，此字段指向相关单链表中关联的结构体或联合体定义。
 + *stype*：成员的结构类型：S_VARIABLE 或 S_ARRAY。
 + *class*：未使用。
 + *size*：对于变量，以字节为单位的总大小。对于数组，数组中的元素数量。我们稍后将使用它来实现`sizeof()`。
 + *posn*：成员相对于结构体/联合体基址的正偏移。
 + *next*：结构体/联合体中的下一个成员。
 + *member*：NULL。

### 枚举列表名称和值

我想存储下面所有的符号和隐式值：

```c
  enum fred { chocolate, spinach, glue };
  enum amy  { garbage, dennis, flute, couch };
```

我们可以简单地将`fred`然后`amy`链接起来，并使用`fred`中的`member`字段来链接`chocolate`、`spinach`、`glue`列表。同样的还有`garbage`等列表。

然而，我们实际上只关心`fred`和`amy`名称，以防止它们被重用为枚举列表名称。我们真正关心的是实际的枚举名称及其值。

因此，我建议使用几个"虚拟"类型值：P_ENUMLIST 和 P_ENUMVAL。然后我们构建一个单维度列表，如下所示：

```c
     fred  -> chocolate-> spinach ->   glue  ->    amy  -> garbage -> dennis -> ...
  P_ENUMLIST  P_ENUMVAL  P_ENUMVAL  P_ENUMVAL  P_ENUMLIST  P_ENUMVAL  P_ENUMVAL
```

因此，当我们使用单词`glue`时，我们只需遍历这一个列表。否则，我们需要找到`fred`，遍历`fred`的成员列表，然后对`amy`做同样的处理。我认为单一列表会更简单。

## 已经做了什么变更

在本文档顶部，我提到我已经将符号表从单个数组重写为多个单链表，并在`struct symtable`节点中添加了这些新字段：

```c
  struct symtable *next;        // 链表中下一个符号
  struct symtable *member;      // 函数的第一个参数
```

所以，让我们快速浏览一下变更。首先，绝对没有任何功能性变更。

### 三个符号表列表

我们现在在`data.h`中有三个符号表列表：

```c
// 符号表列表
struct symtable *Globhead, *Globtail;   // 全局变量和函数
struct symtable *Loclhead, *Locltail;   // 局部变量
struct symtable *Parmhead, *Parmtail;   // 局部参数
```

并且`sym.c`中的所有函数都已重写以使用它们。我写了一个通用函数来追加到列表：

```c
// 将节点追加到由 head 或 tail 指向的单链表
void appendsym(struct symtable **head, struct symtable **tail,
               struct symtable *node) {

  // 检查有效指针
  if (head == NULL || tail == NULL || node == NULL)
    fatal("Either head, tail or node is NULL in appendsym");

  // 追加到列表
  if (*tail) {
    (*tail)->next = node; *tail = node;
  } else *head = *tail = node;
  node->next = NULL;
}
```

现在有一个函数`newsym()`，它接收符号表节点的所有字段值。它`malloc()`一个新节点，填充它并返回它。我不会在这里给出代码。

对于每个列表，都有一个函数来构建并追加节点到列表。一个例子是：

```c
// 添加符号到全局符号列表
struct symtable *addglob(char *name, int type, int stype, int class, int size) {
  struct symtable *sym = newsym(name, type, stype, class, size, 0);
  appendsym(&Globhead, &Globtail, sym);
  return (sym);
}
```

有一个通用函数可以在列表中查找符号，其中`list`指针是列表的头：

```c
// 在特定列表中搜索符号。
// 返回找到的节点的指针，如果未找到则返回 NULL。
static struct symtable *findsyminlist(char *s, struct symtable *list) {
  for (; list != NULL; list = list->next)
    if ((list->name != NULL) && !strcmp(s, list->name))
      return (list);
  return (NULL);
}
```

有三个特定于列表的`findXXX()`函数。

有一个函数`findsymbol()`，它首先尝试在函数的参数列表中查找符号，然后是函数的局部变量，最后是全局变量。

有一个函数`findlocl()`，它只搜索函数的参数列表和局部变量。当我们声明局部变量并需要防止重复声明时使用这个。

最后，有一个函数`clear_symtable()`将所有三个列表的头和尾重置为 NULL，即清除所有三个列表。

### 参数和局部列表

全局符号列表只在解析每个源文件时被清除一次。但是每次开始解析新函数的函数体时，我们需要 a) 设置参数列表，和 b) 清除局部符号列表。

下面是它的工作原理。当我们在`expr.c`的`param_declaration()`中解析参数列表时，我们为每个参数调用`var_declaration()`。这会创建一个符号表节点并将其追加到参数列表，即`Parmhead`和`Parmtail`。当`param_declaration()`返回时，`Parmhead`指向参数列表。

回到正在解析整个函数（其名称、参数列表*和*任何函数体）的`function_declaration()`，参数列表被复制到函数的符号表节点中：

```c
    newfuncsym->nelems = paramcnt;
    newfuncsym->member = Parmhead;

    // 清除参数列表
    Parmhead = Parmtail = NULL;
```

我们通过将`Parmhead`和`Parmtail`设为 NULL 来清除参数列表，如图所示。这意味着所有这些都不能再通过全局参数列表进行搜索。

解决方案是将全局变量`Functionid`设置为函数的符号表条目：

```c
  Functionid = newfuncsym;
```

所以当我们调用`compound_statement()`来解析函数体时，我们仍然可以通过`Functionid->member`访问参数列表，以便执行以下操作：

 + 防止声明与参数名称匹配的局部变量
 + 将参数名称用作普通局部变量等

最终，`function_declaration()`返回覆盖整个函数的 AST 树给`global_declarations()`，然后传递给`gen.c`中的`genAST()`来生成汇编代码。当`genAST()`返回时，`global_declarations()`调用`freeloclsyms()`来清除局部和参数列表，并将`Functionid`重置为 NULL。

### 其他值得注意的变更

实际上，由于符号表变为多个链表，大量的代码不得不被重写。我不会遍历整个代码库。但是有些东西你可以很容易地发现。例如，符号节点以前用`Symtable[n->id]`这样的代码引用。现在是`n->sym`。

同样，`cg.c`中的大量代码引用符号名称，所以你现在看到的是`n->sym->name`。类似地，`tree.c`中转储 AST 树的代码现在也有很多`n->sym->name`。

## 结论与下一步

这段旅程部分是设计部分部分是重新实现。我们花了很多时间来弄清楚实现结构体、联合体和枚举时会面临什么问题。然后我们重新设计了符号表以支持这些新概念。最后，我们将符号表重写为三个链表（目前），为实现这些新概念做准备。

在我们编译器编写旅程的下一部分，我可能会实现结构体类型的声明，但不会实际编写使用它们的代码。我会在下一部分做这个。完成了这两部分之后，我可能会在第三部分实现联合体。然后在第四部分实现枚举。走着瞧！[下一步](../31_Struct_Declarations/Readme_zh.md)
