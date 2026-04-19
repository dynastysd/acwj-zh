# 第 26 章：函数原型

在编译器编写的旅程的这一部分，我添加了编写函数原型的能力。在这个过程中，我不得不重写在前面部分刚刚写的一些代码，对此我很抱歉。我没有看得足够远。

那么我们对函数原型的需求是什么：

 + 能够声明一个没有函数体的函数原型
 + 能够在之后声明一个完整的函数
 + 将原型保存在全局符号表部分，将形参作为局部变量保存在局部符号表部分
 + 根据之前的函数原型对形参的数量和类型进行错误检查

而这些是我不打算做的事情，至少现在还不做：

 + `function(void)`：这将与 `function()` 声明相同
 + 只用类型声明函数，例如 `function(int ,char, long);` 因为这会使解析更加困难。我们可以稍后再做。

## 需要重写的功能

在我们旅程的最近一部分，我添加了带形参和完整函数体的函数声明。当我们解析每个形参时，我立即将其添加到全局符号表（形成原型）和局部符号表（作为函数形参）中。

现在我们要实现函数原型，形参列表并不总是会成为实际函数的形参。考虑这个函数原型：

```c
  int fred(char a, int foo, long bar);
```

我们只能将 `fred` 定义为一个函数，将 `a`、`foo` 和 `bar` 作为三个形参加入全局符号表。我们必须等到完整的函数声明之后，才能将 `a`、`foo` 和 `bar` 添加到局部符号表。

我需要将 C_PARAM 条目在全局符号表和局部符号表上的定义分开。

## 新解析机制的设计

这是我为新的函数解析机制快速设计的内容，同时也处理原型。

```
获取标识符和 '('。
在符号表中搜索该标识符。
如果已存在，则已有一个原型：获取函数的 id 位置及其形参数量。

解析形参时：
  - 如果有之前的原型，将此形参的类型与现有的进行比较。
    如果这是一个完整函数，则更新符号的名称
  - 如果没有之前的原型，将形参添加到符号表

确保形参数量与任何现有原型匹配。
解析 ')'。如果下一个是 ';'，则完成。

如果下一个是 '{'，则将形参列表从全局符号表复制到
局部符号表。用循环复制它们，以便它们以相反顺序
放入局部符号表中。
```

我在过去几个小时完成了这些工作，下面是代码的更改。

## `sym.c` 的更改

我更改了 `sym.c` 中几个函数的形参列表：

```c
int addglob(char *name, int type, int stype, int class, int endlabel, int size);
int addlocl(char *name, int type, int stype, int class, int size);
```

以前，我们让 `addlocl()` 也调用 `addlocl()` 来将 C_PARAM 符号添加到两个符号表。现在我们要分开这个函数，将符号的实际类别传递给两个函数是有意义的。

这些函数的调用在 `main.c` 和 `decl.c` 中。我稍后会介绍 `decl.c` 中的那些。`main.c` 中的更改很简单。

一旦我们遇到真实函数的声明，就需要将其形参列表从全局符号表复制到局部符号表。由于这实际上是符号表特有的内容，我将其添加到了 `sym.c` 中：

```c
// Given a function's slot number, copy the global parameters
// from its prototype to be local parameters
void copyfuncparams(int slot) {
  int i, id = slot + 1;

  for (i = 0; i < Symtable[slot].nelems; i++, id++) {
    addlocl(Symtable[id].name, Symtable[id].type, Symtable[id].stype,
            Symtable[id].class, Symtable[id].size);
  }
}
```

## `decl.c` 的更改

几乎所有的编译器更改都限制在 `decl.c` 中。我们从小的更改开始，逐步到大的更改。

### `var_declaration()`

我以与 `sym.c` 函数相同的方式更改了 `var_declaration()` 的形参列表：

```c
void var_declaration(int type, int class) {
  ...
  addglob(Text, pointer_to(type), S_ARRAY, class, 0, Token.intvalue);
  ...
  if (addlocl(Text, type, S_VARIABLE, class, 1) == -1)
  ...
  addglob(Text, type, S_VARIABLE, class, 0, 1);
}
```

我们将在其他 `decl.c` 函数中使用传入类别的能力。

### `param_declaration()`

这里有很大的变化，因为我们可能已经在全局符号表中作为现有原型有了形参列表。如果是这样，我们需要根据原型检查新列表中的数量和类型。

```c
// Parse the parameters in parentheses after the function name.
// Add them as symbols to the symbol table and return the number
// of parameters. If id is not -1, there is an existing function
// prototype, and the function has this symbol slot number.
static int param_declaration(int id) {
  int type, param_id;
  int orig_paramcnt;
  int paramcnt = 0;

  // Add 1 to id so that it's either zero (no prototype), or
  // it's the position of the zeroth existing parameter in
  // the symbol table
  param_id = id + 1;

  // Get any existing prototype parameter count
  if (param_id)
    orig_paramcnt = Symtable[id].nelems;

  // Loop until the final right parentheses
  while (Token.token != T_RPAREN) {
    // Get the type and identifier
    // and add it to the symbol table
    type = parse_type();
    ident();

    // We have an existing prototype.
    // Check that this type matches the prototype.
    if (param_id) {
      if (type != Symtable[id].type)
        fatald("Type doesn't match prototype for parameter", paramcnt + 1);
      param_id++;
    } else {
      // Add a new parameter to the new prototype
      var_declaration(type, C_PARAM);
    }
    paramcnt++;

    // Must have a ',' or ')' at this point
    switch (Token.token) {
    case T_COMMA:
      scan(&Token);
      break;
    case T_RPAREN:
      break;
    default:
      fatald("Unexpected token in parameter list", Token.token);
    }
  }

  // Check that the number of parameters in this list matches
  // any existing prototype
  if ((id != -1) && (paramcnt != orig_paramcnt))
    fatals("Parameter count mismatch for function", Symtable[id].name);

  // Return the count of parameters
  return (paramcnt);
}
```

请记住，第一个形参的全局符号表槽位位置紧跟在函数名符号的槽位之后。我们接收到现有原型的槽位位置，如果没有原型则接收 -1。

很巧合的是，我们可以为此加 1 来获得第一个形参的槽位号，或者用 0 表示没有现有原型。

我们仍然循环解析每个新形参，但现在有了新代码来与现有原型进行比较，或将形参添加到全局符号表。

一旦退出循环，我们就可以将此列表中的形参数量与任何现有原型中的数量进行比较。

现在，代码感觉有点丑陋，我相信如果我过一段时间再来看，就能看到一些重构的方法。

### `function_declaration()`

以前，这是一个相当简单的函数：获取类型和名称，添加全局符号，读取形参，获取函数体并为函数代码生成 AST 树。

现在，我们必须处理这样一个事实：这可能只是一个原型，或者是一个完整函数。在解析了 ';'（用于原型）或 '{'（用于完整函数）之前，我们不会知道是哪一个。让我们分阶段来讲解代码。

```c
// Parse the declaration of function.
// The identifier has been scanned & we have the type.
struct ASTnode *function_declaration(int type) {
  struct ASTnode *tree, *finalstmt;
  int id;
  int nameslot, endlabel, paramcnt;

  // Text has the identifier's name. If this exists and is a
  // function, get the id. Otherwise, set id to -1
  if ((id = findsymbol(Text)) != -1)
    if (Symtable[id].stype != S_FUNCTION)
      id = -1;

  // If this is a new function declaration, get a
  // label-id for the end label, and add the function
  // to the symbol table,
  if (id == -1) {
    endlabel = genlabel();
    nameslot = addglob(Text, type, S_FUNCTION, C_GLOBAL, endlabel, 0);
  }
  // Scan in the '(', any parameters and the ')'.
  // Pass in any existing function prototype symbol slot number
  lparen();
  paramcnt = param_declaration(id);
  rparen();
```

这与之前版本的代码几乎相同，只是 `id` 现在在沒有先前原型时设置为 -1，在有先前原型时设置为正数。我们只在函数名尚不在全局符号表中时将其添加进去。

```c
  // If this is a new function declaration, update the
  // function symbol entry with the number of parameters
  if (id == -1)
    Symtable[nameslot].nelems = paramcnt;

  // Declaration ends in a semicolon, only a prototype.
  if (Token.token == T_SEMI) {
    scan(&Token);
    return (NULL);
  }
```

我们已经得到了形参的数量。如果没有先前的原型，用这个计数更新原型。现在我们可以查看形参列表末尾之后的词法单元。如果是分号，这只是一个原型。我们现在没有 AST 树可以返回，所以跳过词法单元并返回 NULL。我不得不对 `global_declarations()` 中的代码进行一些轻微的修改来处理这个 NULL 值：改动不大。

如果我们继续，就进入了带有函数体的完整函数声明。

```c
  // This is not just a prototype.
  // Copy the global parameters to be local parameters
  if (id == -1)
    id = nameslot;
  copyfuncparams(id);
```

现在我们需要将形参从原型复制到局部符号表。`id = nameslot` 代码用于当我们刚刚自己添加了全局符号而没有先前原型的情况。

`function_declaration()` 中其余的代码与之前相同，我就不赘述了。它检查非 void 函数是否返回值，并以 A_FUNCTION 根节点生成 AST 树。

## 测试新功能

`tests/runtests` 脚本的一个缺点是它假定编译器一定会产生可以汇编和运行的汇编输出文件 `out.s`。这阻止了我们测试编译器是否检测到语法和语义错误。

快速 *grep* `decl.c` 可以看到这些新的错误被检测到：

```c
fatald("Type doesn't match prototype for parameter", paramcnt + 1);
fatals("Parameter count mismatch for function", Symtable[id].name);
```

因此，我最好重写 `tests/runtests` 来验证编译器确实能在错误输入上检测到这些错误。

我们确实有两个新的工作测试程序，`input29.c` 和 `input30.c`。第一个与 `input28.c` 相同，只是我在程序顶部放置了所有函数的原型：

```c
int param8(int a, int b, int c, int d, int e, int f, int g, int h);
int fred(int a, int b, int c);
int main();
```

这个和所有之前的测试程序仍然有效。不过 `input30.c` 可能是我们的编译器收到的第一个 nontrivial 程序。它打开自己的源文件并将其打印到标准输出：

```c
int open(char *pathname, int flags);
int read(int fd, char *buf, int count);
int write(int fd, void *buf, int count);
int close(int fd);

char *buf;

int main() {
  int zin;
  int cnt;

  buf= "                                                             ";
  zin = open("input30.c", 0);
  if (zin == -1) {
    return (1);
  }
  while ((cnt = read(zin, buf, 60)) > 0) {
    write(1, buf, cnt);
  }
  close(zin);
  return (0);
}
```

我们还不能调用预处理器，所以我手动放置了 `open()`、`read()`、`write()` 和 `close()` 函数的原型。我们还必须在 `open()` 调用中使用 0 而不是 O_RDONLY。

目前，编译器允许我们声明 `char buf[60];` 但不能将 `buf` 本身用作 char 指针。所以我选择为 char 指针分配一个 60 字符的字面量字符串，并将其用作缓冲区。

我们仍然需要用 '{' ... '}' 包装 IF 和 WHILE 体以使它们成为复合语句：我仍然没有处理悬挂 else 问题。最后，我们还不能接受 `char *argv[]` 作为 main 的形参声明，所以我不得不硬编码输入文件的名称。

不过，我们现在有了一个非常原始的 *cat(1)* 程序，编译器可以编译它！这是进展。

## 结论与下一步

在我们编译器编写旅程的下一部分，我将延续上面的评论，改进编译器功能的测试。[下一步](../27_Testing_Errors/Readme_zh.md)