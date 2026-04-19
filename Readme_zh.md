# 编译器编写之旅

**📖 中文（当前）** &nbsp;|&nbsp; [![English](https://img.shields.io/badge/English-英文-blue?logo=github)](README.md)

---

> 💡 **提示**：你正在阅读中文版。如需切换到英文原版，请点击上方的蓝色 English 按钮。

在这个 GitHub 仓库中，我正在记录自己编写一个 C 语言子集的自编译编译器的旅程。
我也把细节写了出来，这样如果你想跟随的话，就会有对我做了什么、为什么这么做、以及一些编译原理参考文献的解释。

但不会涉及太多理论，我希望这是一段实践之旅。

以下是迄今为止我所采取的步骤：

 + [第 0 章](00_Introduction/Readme_zh.md)：旅程引言
 + [第 1 章](01_Scanner/Readme_zh.md)：词法扫描引言
 + [第 2 章](02_Parser/Readme_zh.md)：解析引言
 + [第 3 章](03_Precedence/Readme_zh.md)：运算符优先级
 + [第 4 章](04_Assembly/Readme_zh.md)：一个真正的编译器
 + [第 5 章](05_Statements/Readme_zh.md)：语句
 + [第 6 章](06_Variables/Readme_zh.md)：变量
 + [第 7 章](07_Comparisons/Readme_zh.md)：比较运算符
 + [第 8 章](08_If_Statements/Readme_zh.md)：If 语句
 + [第 9 章](09_While_Loops/Readme_zh.md)：While 循环
 + [第 10 章](10_For_Loops/Readme_zh.md)：For 循环
 + [第 11 章](11_Functions_pt1/Readme_zh.md)：函数，第一部分
 + [第 12 章](12_Types_pt1/Readme_zh.md)：类型，第一部分
 + [第 13 章](13_Functions_pt2/Readme_zh.md)：函数，第二部分
 + [第 14 章](14_ARM_Platform/Readme_zh.md)：生成 ARM 汇编代码
 + [第 15 章](15_Pointers_pt1/Readme_zh.md)：指针，第一部分
 + [第 16 章](16_Global_Vars/Readme_zh.md)：正确声明全局变量
 + [第 17 章](17_Scaling_Offsets/Readme_zh.md)：更好的类型检查和指针偏移
 + [第 18 章](18_Lvalues_Revisited/Readme_zh.md)：左值和右值再探
 + [第 19 章](19_Arrays_pt1/Readme_zh.md)：数组，第一部分
 + [第 20 章](20_Char_Str_Literals/Readme_zh.md)：字符和字符串字面量
 + [第 21 章](21_More_Operators/Readme_zh.md)：更多的运算符
 + [第 22 章](22_Design_Locals/Readme_zh.md)：局部变量和函数调用的设计思路
 + [第 23 章](23_Local_Variables/Readme_zh.md)：局部变量
 + [第 24 章](24_Function_Params/Readme_zh.md)：函数参数
 + [第 25 章](25_Function_Arguments/Readme_zh.md)：函数调用和实参
 + [第 26 章](26_Prototypes/Readme_zh.md)：函数原型
 + [第 27 章](27_Testing_Errors/Readme_zh.md)：回归测试与惊喜
 + [第 28 章](28_Runtime_Flags/Readme_zh.md)：添加更多运行时标志
 + [第 29 章](29_Refactoring/Readme.md)：一些重构
 + [第 30 章](30_Design_Composites/Readme.md)：设计 Struct、Union 和 Enum
 + [第 31 章](31_Struct_Declarations/Readme.md)：实现 Struct，第一部分
 + [第 32 章](32_Struct_Access_pt1/Readme.md)：访问 Struct 中的成员
 + [第 33 章](33_Unions/Readme.md)：实现 Union 和成员访问
 + [第 34 章](34_Enums_and_Typedefs/Readme.md)：Enum 和 Typedef
 + [第 35 章](35_Preprocessor/Readme.md)：C 预处理器
 + [第 36 章](36_Break_Continue/Readme.md)：`break` 和 `continue`
 + [第 37 章](37_Switch/Readme.md)：Switch 语句
 + [第 38 章](38_Dangling_Else/Readme.md)：悬空 Else 问题
 + [第 39 章](39_Var_Initialisation_pt1/Readme.md)：变量初始化，第一部分
 + [第 40 章](40_Var_Initialisation_pt2/Readme.md)：全局变量初始化
 + [第 41 章](41_Local_Var_Init/Readme.md)：局部变量初始化
 + [第 42 章](42_Casting/Readme.md)：类型转换和 NULL
 + [第 43 章](43_More_Operators/Readme.md)：Bug 修复和更多运算符
 + [第 44 章](44_Fold_Optimisation/Readme.md)：常量折叠
 + [第 45 章](45_Globals_Again/Readme.md)：全局变量声明（再谈）
 + [第 46 章](46_Void_Functions/Readme.md)：Void 函数参数和扫描变更
 + [第 47 章](47_Sizeof/Readme.md)：sizeof 子集
 + [第 48 章](48_Static/Readme.md)：static 子集
 + [第 49 章](49_Ternary/Readme.md)：条件运算符
 + [第 50 章](50_Mop_up_pt1/Readme.md)：收尾工作，第一部分
 + [第 51 章](51_Arrays_pt2/Readme.md)：数组，第二部分
 + [第 52 章](52_Pointers_pt2/Readme.md)：指针，第二部分
 + [第 53 章](53_Mop_up_pt2/Readme.md)：收尾工作，第二部分
 + [第 54 章](54_Reg_Spills/Readme.md)：寄存器溢出
 + [第 55 章](55_Lazy_Evaluation/Readme.md)：惰性求值
 + [第 56 章](56_Local_Arrays/Readme.md)：局部数组
 + [第 57 章](57_Mop_up_pt3/Readme.md)：收尾工作，第三部分
 + [第 58 章](58_Ptr_Increments/Readme.md)：修复指针增减量
 + [第 59 章](59_WDIW_pt1/Readme.md)：为什么不能工作，第一部分
 + [第 60 章](60_TripleTest/Readme.md)：通过三重测试
 + [第 61 章](61_What_Next/Readme.md)：下一步是什么？
 + [第 62 章](62_Cleanup/Readme.md)：代码清理
 + [第 63 章](63_QBE/Readme.md)：使用 QBE 的新后端
 + [第 64 章](64_6809_Target/Readme.md)：6809 CPU 后端

我已经停止了 *acwj* 的工作，现在正在从头开始编写一种名为 [alic](https://github.com/DoctorWkt/alic) 的新语言。有兴趣可以看看！

## 版权声明

我借用了部分代码，以及很多想法，来自 Nils M Holm 编写的 [SubC](http://www.t3x.org/subc/) 编译器。他的代码是公共领域的。我认为我的代码与他的足够不同，我可以对我的代码应用不同的许可证。

除非另有说明：

 + 所有源代码和脚本版权归 Warren Toomey 所有，采用 GPL3 许可证。
 + 所有非源代码文档（如英文文档、图片文件）版权归 Warren Toomey 所有，采用 Creative Commons BY-NC-SA 4.0 许可证。
