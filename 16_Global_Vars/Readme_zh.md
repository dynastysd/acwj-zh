# 第 16 章：正确声明全局变量

我确实承诺过要研究给指针添加偏移量的问题，但首先我需要一些思考。
因此，我决定将全局变量声明从函数声明中移出。实际上，
我还保留了函数内部变量声明的解析，因为稍后我们会将其更改为局部变量声明。

我还希望扩展我们的语法，以便能够同时声明具有相同类型的多个变量，例如：

```c
  int x, y, z;
```

## 新的 BNF 语法

以下是全局声明（包括函数和变量）的新 BNF 语法：

```
 global_declarations : global_declarations 
      | global_declaration global_declarations
      ;

 global_declaration: function_declaration | var_declaration ;

 function_declaration: type identifier '(' ')' compound_statement   ;

 var_declaration: type identifier_list ';'  ;

 type: type_keyword opt_pointer  ;
 
 type_keyword: 'void' | 'char' | 'int' | 'long'  ;
 
 opt_pointer: <empty> | '*' opt_pointer  ;
 
 identifier_list: identifier | identifier ',' identifier_list ;
```

`function_declaration` 和 `global_declaration` 都以 `type` 开头。
这是一个 `type_keyword` 后跟 `opt_pointer`，后者是零个或多个 '*' 标记。之后，
`function_declaration` 和 `global_declaration` 都必须跟一个标识符。

但是，在 `type` 之后，`var_declaration` 跟的是一个 `identifier_list`，
即一个或多个由 ',' 标记分隔的 `identifier`。此外，`var_declaration` 必须以 ';' 标记结尾，
而 `function_declaration` 以 `compound_statement` 结尾，没有 ';' 标记。

## 新的标记

现在在 `scan.c` 中我们有了 ',' 字符的 T_COMMA 标记。

## 对 `decl.c` 的更改

现在我们将上述 BNF 语法转换为一组递归下降函数，
但是由于我们可以进行循环，因此可以将一些递归转换为内部循环。

### `global_declarations()`

由于有一个或多个全局声明，我们可以循环解析每一个。
当标记用完时，可以退出循环。

```c
// 解析一个或多个全局声明，
// 变量或函数
void global_declarations(void) {
  struct ASTnode *tree;
  int type;

  while (1) {

    // 我们必须跳过类型和标识符
    // 才能看到函数声明的 '('
    // 或变量声明的 ',' 或 ';'。
    // Text 由 ident() 调用填充。
    type = parse_type();
    ident();
    if (Token.token == T_LPAREN) {

      // 解析函数声明
      // 并为其生成汇编代码
      tree = function_declaration(type);
      genAST(tree, NOREG, 0);
    } else {

      // 解析全局变量声明
      var_declaration(type);
    }

    // 当到达 EOF 时停止
    if (Token.token == T_EOF)
      break;
  }
}
```

鉴于目前我们只有全局变量和函数，我们可以在这里扫描类型和第一个标识符。
然后，我们查看下一个标记。如果它是 '('，我们调用 `function_declaration()`。
如果不是，我们可以假定它是 `var_declaration()`。
我们将 `type` 传递给两个函数。

现在我们在这里从 `function_declaration()` 获取 AST `tree`，
我们可以立即从 AST 树生成代码。这段代码原来在 `main()` 中，
但现在已经移到这里。`main()` 现在只需要调用 `global_declarations()`：

```c
  scan(&Token);                 // 从输入获取第一个标记
  genpreamble();                // 输出前导代码
  global_declarations();        // 解析全局声明
  genpostamble();               // 输出后置代码
```

### `var_declaration()`

函数的解析与之前大致相同，只是扫描类型和标识符的代码在其他地方完成，
我们接收 `type` 作为参数。

变量的解析也失去了类型和标识符扫描代码。我们可以将标识符添加到全局符号中，
并为其生成汇编代码。但现在我们需要添加一个循环。如果后面跟着 ','，
则循环回来获取具有相同类型的下一个标识符。
如果后面跟着 ';'，则变量声明结束。

```c
// 解析一组变量的声明。
// 标识符已被扫描，我们有类型
void var_declaration(int type) {
  int id;

  while (1) {
    // Text 现在包含标识符的名称。
    // 将其添加为已知标识符
    // 并在汇编中为其生成空间
    id = addglob(Text, type, S_VARIABLE, 0);
    genglobsym(id);

    // 如果下一个标记是分号，
    // 跳过它并返回。
    if (Token.token == T_SEMI) {
      scan(&Token);
      return;
    }
    // 如果下一个标记是逗号，跳过它，
    // 获取标识符并循环
    if (Token.token == T_COMMA) {
      scan(&Token);
      ident();
      continue;
    }
    fatal("Missing , or ; after identifier");
  }
}
```

## 不完全是局部变量

`var_declaration()` 现在可以解析一组变量声明，
但它需要预先扫描类型和第一个标识符。

因此，我将 `var_declaration()` 的调用保留在 `stmt.c` 的 `single_statement()` 中。
稍后，我们将修改它来声明局部变量。但目前，此示例程序中的所有变量都是全局的：

```c
int   d, f;
int  *e;

int main() {
  int a, b, c;
  b= 3; c= 5; a= b + c * 10;
  printint(a);

  d= 12; printint(d);
  e= &d; f= *e; printint(f);
  return(0);
}
```

## 测试更改

上述代码是我们的 `tests/input16.c`。一如既往，我们可以测试它：

```
$ make test16
cc -o comp1 -g -Wall cg.c decl.c expr.c gen.c main.c misc.c scan.c
      stmt.c sym.c tree.c types.c
./comp1 tests/input16.c
cc -o out out.s lib/printint.c
./out
53
12
12
```


## 结论和下一步

在我们编写编译器的下一个部分中，
我承诺解决给指针添加偏移量的问题。[下一步](../17_Scaling_Offsets/Readme_zh.md)

（文件结束 - 共 205 行）