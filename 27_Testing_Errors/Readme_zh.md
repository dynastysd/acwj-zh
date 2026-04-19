# 第 27 章：回归测试与惊喜

最近在编译器编写的旅程中，我们已经迈出了几个较大的步伐，所以我认为这一步应该让大家稍微喘口气。我们可以放慢脚步，回顾一下迄今为止的进展。

在上一步中，我注意到我们没有办法确认语法和语义错误检查是否正常工作。所以我刚刚重写了 `tests/` 文件夹中的脚本来实现这一点。

从 20 世纪 80 年代末开始使用 Unix，所以我最喜欢的自动化工具是 shell 脚本和 Makefile，如果需要更复杂的工具，就用 Python 或 Perl 编写脚本（是的，我就是那么老）。

让我们快速看一下 `tests/` 目录中的 `runtest` 脚本。虽然我说我使用 Unix 脚本已经很长时间了，但我绝对不是超级脚本编写者。

## `runtest` 脚本

这个脚本的任务是：获取一组输入程序，让我们的编译器编译它们，运行可执行文件，并将其输出与已知正确的输出进行比较。如果它们匹配，测试成功；否则失败。

我刚刚扩展了它，使得如果输入文件有关联的"错误"文件，我们就运行编译器并捕获其错误输出。如果这个错误输出与预期的错误输出匹配，测试就成功，因为编译器正确地检测到了错误的输入。

让我们分阶段查看 `runtest` 脚本的各个部分。

```
# Build our compiler if needed
if [ ! -f ../comp1 ]
then (cd ..; make)
fi
```

我在这里使用 '( ... )' 语法来创建一个*子 shell*。这可以改变其工作目录而不影响原始 shell，所以我们能够返回上级目录并重新构建编译器。


```
# Try to use each input source file
for i in input*
# We can't do anything if there's no file to test against
do if [ ! -f "out.$i" -a ! -f "err.$i" ]
   then echo "Can't run test on $i, no output file!"
```

'[' 实际上是一个外部 Unix 工具 *test(1)*。
哦，如果你从未见过这种语法，*test(1)* 表示 *test* 的手册页在手册的第一部分，你可以这样做：

```
$ man 1 test
```

来阅读手册第一部分中关于 *test* 的说明。`/usr/bin/[` 可执行文件通常链接到 `/usr/bin/test`，所以当你在 shell 脚本中使用 '[' 时，它与运行 *test* 命令是一样的。

我们可以将 `[ ! -f "out.$i" -a ! -f "err.$i" ]` 这行理解为：测试是否存在文件 "out.$i" 和 "err.$i"。如果两者都不存在，我们就给出错误消息。

```
   # Output file: compile the source, run it and
   # capture the output, and compare it against
   # the known-good output
   else if [ -f "out.$i" ]
        then
          # Print the test name, compile it
          # with our compiler
          echo -n $i
          ../comp1 $i

          # Assemble the output, run it
          # and get the output in trial.$i
          cc -o out out.s ../lib/printint.c
          ./out > trial.$i

          # Compare this agains the correct output
          cmp -s "out.$i" "trial.$i"

          # If different, announce failure
          # and print out the difference
          if [ "$?" -eq "1" ]
          then echo ": failed"
            diff -c "out.$i" "trial.$i"
            echo

          # No failure, so announce success
          else echo ": OK"
          fi
```

这是脚本的主体。我认为注释解释了正在发生的事情，但也许有一些细节需要补充说明。`cmp -s` 比较两个文本文件；`-s` 标志表示不产生输出，但设置 `cmp` 退出时的返回值：

> 0 表示输入相同，1 表示不同，2 表示
  出现问题。（来自手册页）

`if [ "$?" -eq "1" ]` 这行是说：如果上一个命令的退出值等于 1。因此，如果编译器的输出与已知正确的输出不同，我们就宣布这一点，并使用 `diff` 工具来显示两个文件之间的差异。

```
   # Error file: compile the source and
   # capture the error messages. Compare
   # against the known-bad output. Same
   # mechanism as before
   else if [ -f "err.$i" ]
        then
          echo -n $i
          ../comp1 $i 2> "trial.$i"
          cmp -s "err.$i" "trial.$i"
          ...
```

当存在错误文档 "err.$i" 时，会执行此部分。这次，我们使用 shell 语法 `2>` 将编译器的标准错误输出捕获到文件 "trial.$i" 中，并将其与正确的错误输出进行比较。这之后的逻辑与之前相同。

## 我们在做什么：回归测试

之前我没有太多谈论测试，但现在时候到了。我以前教过软件开发，所以不在某个时候讲解测试就是我的失职了。

我们在这里做的是[**回归测试**](https://en.wikipedia.org/wiki/Regression_testing)。
维基百科给出了这个定义：

> 回归测试是重新运行功能和 非功能测试的操作，以确保以前开发和测试的软件在更改后仍然能正常运行。

随着编译器在每一步都在变化，我们必须确保每个新更改不会破坏先前步骤的功能（和错误检查）。所以每次我引入一个更改时，我会添加一个或多个测试来 a) 证明它有效，b) 在未来的更改中重新运行这个测试。只要所有测试都通过，我就确信新代码没有破坏旧代码。

### 功能测试

`runtests` 脚本查找带有 `out` 前缀的文件来进行功能测试。目前，我们有：

```
tests/out.input01.c  tests/out.input12.c   tests/out.input22.c
tests/out.input02.c  tests/out.input13.c   tests/out.input23.c
tests/out.input03.c  tests/out.input14.c   tests/out.input24.c
tests/out.input04.c  tests/out.input15.c   tests/out.input25.c
tests/out.input05.c  tests/out.input16.c   tests/out.input26.c
tests/out.input06.c  tests/out.input17.c   tests/out.input27.c
tests/out.input07.c  tests/out.input18a.c  tests/out.input28.c
tests/out.input08.c  tests/out.input18.c   tests/out.input29.c
tests/out.input09.c  tests/out.input19.c   tests/out.input30.c
tests/out.input10.c  tests/out.input20.c   tests/out.input53.c
tests/out.input11.c  tests/out.input21.c   tests/out.input54.c
```

这是对编译器功能的 33 个独立测试。现在，我确切地知道我们的编译器有点脆弱。这些测试都没有真正对编译器施加压力：它们都是简单的几行代码测试。稍后，我们将开始添加一些恶劣的压力测试，以帮助加强编译器并使其更具弹性。

### 非功能测试

`runtests` 脚本查找带有 `err` 前缀的文件来进行非功能测试。目前，我们有：

```
tests/err.input31.c  tests/err.input39.c  tests/err.input47.c
tests/err.input32.c  tests/err.input40.c  tests/err.input48.c
tests/err.input33.c  tests/err.input41.c  tests/err.input49.c
tests/err.input34.c  tests/err.input42.c  tests/err.input50.c
tests/err.input35.c  tests/err.input43.c  tests/err.input51.c
tests/err.input36.c  tests/err.input44.c  tests/err.input52.c
tests/err.input37.c  tests/err.input45.c
tests/err.input38.c  tests/err.input46.c
```

在这段旅程中，我通过查找编译器中的 `fatal()` 调用来创建这 22 个编译器错误检查测试。对于每一个，我尝试编写一个会触发它的小输入文件。你可以阅读匹配的源文件，看看是否能弄清楚每个文件触发了什么语法或语义错误。

## 其他形式的测试

这不是软件开 methodology 的课程，所以我不会过多地讲解测试。但我会给你一些链接，这些是我强烈建议你查看的内容：

  + [单元测试](https://en.wikipedia.org/wiki/Unit_testing)
  + [测试驱动开发](https://en.wikipedia.org/wiki/Test-driven_development)
  + [持续集成](https://en.wikipedia.org/wiki/Continuous_integration)
  + [版本控制](https://en.wikipedia.org/wiki/Version_control)

我没有对我们的编译器进行任何单元测试。这里的主要原因是代码在函数的 API 方面非常不稳定。我没有使用传统的瀑布式开发模型，所以我将花费太多时间来重写我的单元测试以匹配所有函数的最新 API。所以，在某种意义上，我是在危险地生活着：代码中会有一些我们尚未检测到的潜在 bug。

然而，可以肯定的是，会有*更多*的 bug，编译器看起来它接受了 C 语言，但当然这不是真的。编译器违反了[最小惊讶原则](https://en.wikipedia.org/wiki/Principle_of_least_astonishment)。我们需要花费一些时间来添加"普通"C 程序员期望看到的功能。

## 一个惊喜

最后，对于目前的编译器，我们有一个很好的功能上的惊喜。前一段时间，我故意省略了测试函数调用的参数数量和类型是否与函数原型匹配的代码（在 `expr.c` 中）：

```c
  // XXX Check type of each argument against the function's prototype
```

我省略了这个，因为我不想在一个步骤中添加太多新代码。

现在我们有了原型，我一直想最终添加对 `printf()` 的支持，这样我们就可以抛弃我们自己的 `printint()` 和 `printchar()` 函数。但我们还不能这样做，因为 `printf()` 是一个[可变参数函数](https://en.wikipedia.org/wiki/Variadic_function)：它可以接受可变数量的参数。而现在，我们的编译器只允许带有固定数量参数的函数声明。

*但是*（这就是惊喜），因为我们不检查函数调用中的参数数量，只要我们给出了一个存在的原型，我们就可以向 `printf()` 传递*任意数量*的参数。所以，目前，这段代码（`tests/input53.c`）是有效的：

```c
int printf(char *fmt);

int main()
{
  printf("Hello world, %d\n", 23);
  return(0);
}
```

这是一件好事！

有一个问题。使用给定的 `printf()` 原型，`cgcall()` 中的清理代码在函数返回时不会调整栈指针，因为原型中的参数少于六个。但是我们可以使用十个参数调用 `printf()`：我们会将其中的四个压入栈中，但 `printf()` 返回时 `cgcall()` 不会清理这四个参数。

## 结论与下一步

这一步中没有新的编译器代码，但我们现在正在测试编译器的错误检查能力，我们现在有 54 个回归测试来帮助确保我们在添加新功能时不会破坏编译器。而且，幸运的是，我们现在也可以使用 `printf()` 以及其他外部固定参数计数的函数。

在我们编译器编写旅程的下一部分，我想我会尝试：

  + 添加对外部预处理器的支持
  + 允许编译器编译命令行上命名的多个文件
  + 向编译器添加 `-o`、`-c` 和 `-S` 标志，使其感觉更像一个"正常"的 C 编译器[下一步](../28_Runtime_Flags/Readme_zh.md)