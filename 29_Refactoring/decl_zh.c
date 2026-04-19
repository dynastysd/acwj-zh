#include "defs.h"
#include "data.h"
#include "decl.h"

// 声明的解析
// Copyright (c) 2019 Warren Toomey, GPL3


// 解析当前token并返回
// 基本类型的枚举值。同时
// 扫描下一个token
int parse_type(void) {
  int type;
  switch (Token.token) {
  case T_VOID:
    type = P_VOID;
    break;
  case T_CHAR:
    type = P_CHAR;
    break;
  case T_INT:
    type = P_INT;
    break;
  case T_LONG:
    type = P_LONG;
    break;
  default:
    fatald("非法的类型,token", Token.token);
  }

  // 扫描一个或多个额外的'*'token
  // 并确定正确的指针类型
  while (1) {
    scan(&Token);
    if (Token.token != T_STAR)
      break;
    type = pointer_to(type);
  }

  // 离开时下一个token已被扫描
  return (type);
}

// variable_declaration: type identifier ';'
//        | type identifier '[' INTLIT ']' ';'
//        ;
//
// 解析标量变量或具有给定大小的数组的声明
// 标识符已被扫描,我们有类型
// class是变量的存储类
void var_declaration(int type, int class) {

  // Text现在有标识符的名称
  // 如果下一个token是'['
  if (Token.token == T_LBRACKET) {
    // 跳过 '['
    scan(&Token);

    // 检查我们是否有数组大小
    if (Token.token == T_INTLIT) {
      // 将其添加为已知的数组并在汇编中为其生成空间
      // 我们将数组视为指向其元素类型的指针
      if (class == C_LOCAL) {
	fatal("目前尚未实现局部数组的声明");
      } else {
	addglob(Text, pointer_to(type), S_ARRAY, class, Token.intvalue);
      }
    }
    // 确保有后续的 ']'
    scan(&Token);
    match(T_RBRACKET, "]");
  } else {
    // 将其添加为已知的标量
    // 并在汇编中为其生成空间
    if (class == C_LOCAL) {
      if (addlocl(Text, type, S_VARIABLE, class, 1) == -1)
	fatals("重复的局部变量声明", Text);
    } else {
      addglob(Text, type, S_VARIABLE, class, 1);
    }
  }
}

// param_declaration: <null>
//           | variable_declaration
//           | variable_declaration ',' param_declaration
//
// 解析函数名后括号中的参数
// 将它们作为符号添加到符号表并返回参数数量
// 如果id不是-1,则存在现有函数原型,
// 该函数具有此符号槽号
static int param_declaration(int id) {
  int type, param_id;
  int orig_paramcnt;
  int paramcnt = 0;

  // 将id加1,以便它要么是零(没有原型),
  // 要么是符号表中第一个现有参数的位置
  param_id = id + 1;

  // 获取任何现有原型参数计数
  if (param_id)
    orig_paramcnt = Symtable[id].nelems;

  // 循环直到最后的右括号
  while (Token.token != T_RPAREN) {
    // 获取类型和标识符
    // 并将其添加到符号表
    type = parse_type();
    ident();

    // 我们有一个现有的原型
    // 检查此类型是否与原型匹配
    if (param_id) {
      if (type != Symtable[id].type)
	fatald("参数类型与原型的参数不匹配", paramcnt + 1);
      param_id++;
    } else {
      // 将新参数添加到新原型
      var_declaration(type, C_PARAM);
    }
    paramcnt++;

    // 此时必须是','或')'
    switch (Token.token) {
    case T_COMMA:
      scan(&Token);
      break;
    case T_RPAREN:
      break;
    default:
      fatald("参数列表中遇到意外的token", Token.token);
    }
  }

  // 检查此列表中的参数数量是否与
  // 任何现有原型匹配
  if ((id != -1) && (paramcnt != orig_paramcnt))
    fatals("函数参数数量不匹配", Symtable[id].name);

  // 返回参数计数
  return (paramcnt);
}

//
// function_declaration: type identifier '(' parameter_list ')' ;
//      | type identifier '(' parameter_list ')' compound_statement   ;
//
// 解析函数声明
// 标识符已被扫描,我们有类型
struct ASTnode *function_declaration(int type) {
  struct ASTnode *tree, *finalstmt;
  int id;
  int nameslot, endlabel, paramcnt;

  // Text有标识符的名称。如果存在且是函数,
  // 则获取id。否则,将id设为-1
  if ((id = findsymbol(Text)) != -1)
    if (Symtable[id].stype != S_FUNCTION)
      id = -1;

  // 如果这是新的函数声明,获取
  // 结束标签的标签id,并将函数
  // 添加到符号表,
  if (id == -1) {
    endlabel = genlabel();
    nameslot = addglob(Text, type, S_FUNCTION, C_GLOBAL, endlabel);
  }
  // 扫描'('、任何参数和')'
  // 传入任何现有函数原型符号槽号
  lparen();
  paramcnt = param_declaration(id);
  rparen();

  // 如果这是新的函数声明,
  // 用参数数量更新函数符号条目
  if (id == -1)
    Symtable[nameslot].nelems = paramcnt;

  // 声明以分号结尾,只是原型
  if (Token.token == T_SEMI) {
    scan(&Token);
    return (NULL);
  }
  // 这不仅仅是原型
  // 将全局参数复制为局部参数
  if (id == -1)
    id = nameslot;
  copyfuncparams(id);

  // 将Functionid全局变量设置为函数的符号id
  Functionid = id;

  // 获取复合语句的AST树
  tree = compound_statement();

  // 如果函数类型不是P_VOID..
  if (type != P_VOID) {

    // 如果函数中没有语句则报错
    if (tree == NULL)
      fatal("非void类型函数中没有语句");

    // 检查复合语句中最后一个AST操作是否是return语句
    finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    if (finalstmt == NULL || finalstmt->op != A_RETURN)
      fatal("非void类型函数没有return");
  }
  // 返回A_FUNCTION节点,它有函数的id
  // 和复合语句子树
  return (mkastunary(A_FUNCTION, type, tree, id));
}


// 解析一个或多个全局声明,可以是
// 变量或函数
void global_declarations(void) {
  struct ASTnode *tree;
  int type;

  while (1) {

    // 我们必须读取类型和标识符之后
    // 才能看到用于函数声明的'('或用于
    // 变量声明的','或';'
    // Text由ident()调用填充
    type = parse_type();
    ident();
    if (Token.token == T_LPAREN) {

      // 解析函数声明
      tree = function_declaration(type);

      // 只是函数原型,没有代码
      if (tree == NULL)
	continue;

      // 真实的函数,为其生成汇编代码
      if (O_dumpAST) {
	dumpAST(tree, NOLABEL, 0);
	fprintf(stdout, "\n\n");
      }
      genAST(tree, NOLABEL, 0);

      // 现在释放与
      // 此函数关联的符号
      freeloclsyms();
    } else {

      // 解析全局变量声明
      // 并跳过结尾的分号
      var_declaration(type, C_GLOBAL);
      semi();
    }

    // 当我们到达EOF时停止
    if (Token.token == T_EOF)
      break;
  }
}