# 第 28 章：添加更多运行时标志

在编译器编写旅程的这一部分，与扫描、解析、语义分析或代码生成其实没有太大关系。在这一部分，我为编译器添加了 `-c`、`-S` 和 `-o` 运行时标志，使其行为更接近传统的 Unix C 编译器。

所以，如果这不够有趣，可以直接跳过旅程的下一部分。

## 编译步骤

到目前为止，我们的编译器只会输出汇编文件。但将高级语言源代码文件转换为可执行文件还有更多步骤：

 + 扫描和解析源代码文件以生成汇编输出
 + 将汇编输出汇编成[目标文件](https://en.wikipedia.org/wiki/Object_file)
 + [链接](https://en.wikipedia.org/wiki/Linker_(computing))一个或多个目标文件以生成可执行文件

我们一直在手动或使用 Makefile 执行最后两个步骤，但现在我要修改编译器来调用外部汇编器和链接器来完成最后两个步骤。

为此，我将重新安排 `main.c` 中的一些代码，并在 `main.c` 中编写更多函数来进行汇编和链接。大部分代码都是典型的 C 语言字符串和文件处理代码，所以我会浏览代码，但只有在你从未见过这种代码时才可能感兴趣。

## 解析命令行标志

我已经将编译器重命名为 `cwj`，以反映项目名称。当你不带命令行参数运行它时，现在会显示此用法消息：

```
$ ./cwj
Usage: ./cwj [-vcST] [-o outfile] file [file ...]
       -v give verbose output of the compilation stages
       -c generate object files but don't link them
       -S generate assembly files but don't link them
       -T dump the AST trees for each input file
       -o outfile, produce the outfile executable file
```

我们现在允许将多个源代码文件作为输入。我们有四个布尔标志：`-v`、`-c`、`-S` 和 `-T`，现在可以命名输出可执行文件。

`main()` 中的 `argv[]` 解析代码现在已更改以处理此问题，并且有更多选项变量来保存结果。

```c
  // Initialise our variables
  O_dumpAST = 0;        // If true, dump the AST trees
  O_keepasm = 0;        // If true, keep any assembly files
  O_assemble = 0;       // If true, assemble the assembly files
  O_dolink = 1;         // If true, link the object files
  O_verbose = 0;        // If true, print info on compilation stages

  // Scan for command-line options
  for (i = 1; i < argc; i++) {
    // No leading '-', stop scanning for options
    if (*argv[i] != '-')
      break;

    // For each option in this argument
    for (int j = 1; (*argv[i] == '-') && argv[i][j]; j++) {
      switch (argv[i][j]) {
      case 'o':
        outfilename = argv[++i]; break;         // Save & skip to next argument
      case 'T':
        O_dumpAST = 1; break;
      case 'c':
        O_assemble = 1; O_keepasm = 0; O_dolink = 0; break;
      case 'S':
        O_keepasm = 1; O_assemble = 0; O_dolink = 0; break;
      case 'v':
        O_verbose = 1; break;
      default:
        usage(argv[0]);
      }
    }
  }
```

请注意，某些选项是互斥的。例如，如果我们只想用 `-S` 输出汇编文件，那么我们不需要链接或创建目标文件。

## 执行编译阶段

有了命令行标志的解析，我们现在可以运行编译阶段。我们可以轻松地编译和汇编每个输入文件，但最后可能需要链接多个目标文件。因此，我们在 `main()` 中设置了一些局部变量来存储目标文件名：

```c
#define MAXOBJ 100
  char *objlist[MAXOBJ];        // List of object file names
  int objcnt = 0;               // Position to insert next name
```

我们首先依次处理所有输入源文件：

```c
  // Work on each input file in turn
  while (i < argc) {
    asmfile = do_compile(argv[i]);      // Compile the source file

    if (O_dolink || O_assemble) {
      objfile = do_assemble(asmfile);   // Assemble it to object format
      if (objcnt == (MAXOBJ - 2)) {
        fprintf(stderr, "Too many object files for the compiler to handle\n");
        exit(1);
      }
      objlist[objcnt++] = objfile;      // Add the object file's name
      objlist[objcnt] = NULL;           // to the list of object files
    }

    if (!O_keepasm)                     // Remove the assembly file if
      unlink(asmfile);                  // we don't need to keep it
    i++;
  }
```

`do_compile()` 包含以前在 `main()` 中的代码：打开文件、解析并生成汇编文件。但我们不能再打开硬编码的文件名 `out.s`；现在需要将 `filename.c` 转换为 `filename.s`。

## 修改输入文件名

我们有一个辅助函数来修改文件名。

```c
// Given a string with a '.' and at least a 1-character suffix
// after the '.', change the suffix to be the given character.
// Return the new string or NULL if the original string could
// not be modified
char *alter_suffix(char *str, char suffix) {
  char *posn;
  char *newstr;

  // Clone the string
  if ((newstr = strdup(str)) == NULL) return (NULL);

  // Find the '.'
  if ((posn = strrchr(newstr, '.')) == NULL) return (NULL);

  // Ensure there is a suffix
  posn++;
  if (*posn == '\0') return (NULL);

  // Change the suffix and NUL-terminate the string
  *posn++ = suffix; *posn = '\0';
  return (newstr);
}
```

只有 `strdup()`、`strrchr()` 和最后两行做实际工作；其余的都是错误检查。

## 执行编译

这是我们以前拥有的代码，现已重新打包成一个新函数。

```c
// Given an input filename, compile that file
// down to assembly code. Return the new file's name
static char *do_compile(char *filename) {
  Outfilename = alter_suffix(filename, 's');
  if (Outfilename == NULL) {
    fprintf(stderr, "Error: %s has no suffix, try .c on the end\n", filename);
    exit(1);
  }
  // Open up the input file
  if ((Infile = fopen(filename, "r")) == NULL) {
    fprintf(stderr, "Unable to open %s: %s\n", filename, strerror(errno));
    exit(1);
  }
  // Create the output file
  if ((Outfile = fopen(Outfilename, "w")) == NULL) {
    fprintf(stderr, "Unable to create %s: %s\n", Outfilename,
            strerror(errno));
    exit(1);
  }

  Line = 1;                     // Reset the scanner
  Putback = '\n';
  clear_symtable();             // Clear the symbol table
  if (O_verbose)
    printf("compiling %s\n", filename);
  scan(&Token);                 // Get the first token from the input
  genpreamble();                // Output the preamble
  global_declarations();        // Parse the global declarations
  genpostamble();               // Output the postamble
  fclose(Outfile);              // Close the output file
  return (Outfilename);
}
```

这里几乎没有新代码，只是调用 `alter_suffix()` 来获取正确的输出文件名。

有一个重要的变化：汇编输出文件现在是一个名为 `Outfilename` 的全局变量。这允许 `misc.c` 中的 `fatal()` 函数及相关函数在从未完全生成汇编文件时删除它们，例如：

```c
// Print out fatal messages
void fatal(char *s) {
  fprintf(stderr, "%s on line %d\n", s, Line);
  fclose(Outfile);
  unlink(Outfilename);
  exit(1);
}
```

## 汇编上述输出

现在我们有了汇编输出文件，可以调用外部汇编器来完成这项工作。这在 `defs.h` 中定义为 ASCMD。以下是执行此操作的函数：

```c
#define ASCMD "as -o "
// Given an input filename, assemble that file
// down to object code. Return the object filename
char *do_assemble(char *filename) {
  char cmd[TEXTLEN];
  int err;

  char *outfilename = alter_suffix(filename, 'o');
  if (outfilename == NULL) {
    fprintf(stderr, "Error: %s has no suffix, try .s on the end\n", filename);
    exit(1);
  }
  // Build the assembly command and run it
  snprintf(cmd, TEXTLEN, "%s %s %s", ASCMD, outfilename, filename);
  if (O_verbose) printf("%s\n", cmd);
  err = system(cmd);
  if (err != 0) { fprintf(stderr, "Assembly of %s failed\n", filename); exit(1); }
  return (outfilename);
}
```

我使用 `snprintf()` 来构建要运行的汇编命令。如果用户使用了 `-v` 命令行标志，此命令将显示给他们。然后我们使用 `system()` 执行此 Linux 命令。示例：

```
$ ./cwj -v -c tests/input54.c
compiling tests/input54.c
as -o  tests/input54.o tests/input54.s
```

## 链接目标文件

在 `main()` 中，我们构建了 `do_assemble()` 返回给我们的目标文件列表：

```c
      objlist[objcnt++] = objfile;      // Add the object file's name
      objlist[objcnt] = NULL;           // to the list of object files
```

因此，当我们需要将它们全部链接在一起时，需要将此列表传递给 `do_link()` 函数。代码与 `do_assemble()` 类似，都使用 `snprintf()` 和 `system()`。区别在于我们必须跟踪命令缓冲区中的当前位置，以及剩余多少空间来进行更多 `snprintf()` 操作。

```c
#define LDCMD "cc -o "
// Given a list of object files and an output filename,
// link all of the object filenames together.
void do_link(char *outfilename, char *objlist[]) {
  int cnt, size = TEXTLEN;
  char cmd[TEXTLEN], *cptr;
  int err;

  // Start with the linker command and the output file
  cptr = cmd;
  cnt = snprintf(cptr, size, "%s %s ", LDCMD, outfilename);
  cptr += cnt; size -= cnt;

  // Now append each object file
  while (*objlist != NULL) {
    cnt = snprintf(cptr, size, "%s ", *objlist);
    cptr += cnt; size -= cnt; objlist++;
  }

  if (O_verbose) printf("%s\n", cmd);
  err = system(cmd);
  if (err != 0) { fprintf(stderr, "Linking failed\n"); exit(1); }
}
```

一个麻烦是我仍在调用外部 C 编译器 `cc` 来进行链接。我们真的应该能够打破对这个其他编译器的依赖。

很久以前，可以通过以下方式手动链接一组目标文件：

```
  $ ln -o out /lib/crt0.o file1.o file.o /usr/lib/libc.a
```

我想在当前的 Linux 上应该可以执行类似的命令，但到目前为止，我的 Google 搜索能力还不足以解决这个问题。如果你读到这里并知道答案，请告诉我！

## 移除 `printint()` 和 `printchar()`

现在我们可以直接在编译的程序中调用 `printf()`，不再需要手写的 `printint()` 和 `printchar()` 函数。我已经删除了 `lib/printint.c`，并更新了 `tests/` 目录中的所有测试以使用 `printf()`。

我还更新了 `tests/mktests` 和 `tests/runtests` 脚本，使它们使用新的编译器命令行参数，顶层 `Makefile` 也是如此。因此 `make test` 仍然可以正常运行我们的回归测试。

## 结论与下一步

这就是我们旅程这一部分的内容。我们的编译器现在感觉像我熟悉的传统 Unix 编译器。

我确实承诺在这一步中添加对外部预处理器的支持，但我决定不这样做。主要原因是我需要解析预处理器在其输出中嵌入的文件名和行号，例如：

```c
# 1 "tests/input54.c"
# 1 "<built-in>"
# 1 "<command-line>"
# 31 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3 4
# 32 "<command-line>" 2
# 1 "tests/input54.c"
int printf(char *fmt);

int main()
{
  int i;
  for (i=0; i < 20; i++) {
    printf("Hello world, %d\n", i);
  }
  return(0);
}
```


在编译器编写旅程的下一部分，我们将探讨为编译器添加对结构体的支持。我想我们可能需要先再进行一步设计，然后再实现这些更改。[下一步](../29_Refactoring/Readme_zh.md)