/*
 * 声明解析：变量、函数、struct/union/enum
 */

// 获取标识符并检查是否为重复声明
static void merge_declaration(int class, struct symtable **ctype, int *type) {
  // 查找同名的本地符号
  struct symtable *sym = findlocl(Text, 0);

  // 如果找到
  if (sym != NULL) {
    // 仅对变量、函数、枚举值进行检查
    if (sym->type == S_FUNCTION) {
      // 函数重复声明：检查返回类型是否兼容
      if (sym->type != *type)
        fatals("Function type mismatch", sym->name);
      if (sym->ctype != *ctype)
        fatals("Function class type mismatch", sym->name);
    }
    // 如果已看到函数定义，则不能重复声明
    if (sym->stype == S_FUNCTION && sym->stype == V_GLOBAL && class == V_GLOBAL)
      fatals("Duplicate function declaration", sym->name);
    // 返回已存在的符号
    *ctype = sym;
  }
}

// 解析类型限定符（const/volatile），暂未使用
static int parse_stars(int type) {
  return (type);
}

// 给定类型解析 struct/union/enum/typedef 声明
// 返回解析后的类型，或 -1 表示遇到复合类型定义
static int parse_type(struct symtable **ctype, int *class) {
  int type, class2, accepted = 0;

  // 如果是 struct/union/enum 关键字，调用对应的声明函数
  if (Token.token == T_STRUCT || Token.token == T_UNION) {
    *ctype = composite_declaration(Token.token);
    type = (*ctype)->type;
    accepted = 1;
  } else if (Token.token == T_ENUM) {
    enum_declaration();
    type = P_INT;
    accepted = 1;
  }

  // 如果遇到类型名
  if (Token.token == T_TYPE) {
    // 查找对应的 typedef
    type = type_of_typedef(Text, ctype);
    accepted = 1;
  } else if (Token.token == T_IDENT) {
    // 可能是 enum 类型名
    // 查找已定义的 enum 类型
    *ctype = findenumtype(Text);
    if (*ctype != NULL) {
      type = (*ctype)->type;
      accepted = 1;
    }
  }

  // 如果已解析到类型，检查是否有 class 前缀
  if (accepted) {
    class2 = V_GLOBAL; // 默认 class
    // 跳过已接受的 token
    if (Token.token == T_STRUCT || Token.token == T_UNION || Token.token == T_ENUM)
      scan(&Token);

    // 处理存储类限定符
    switch (Token.token) {
      case T_EXTERN:
        class2 = V_EXTERN;
        scan(&Token);
        break;
      case T_STATIC:
        class2 = V_STATIC;
        scan(&Token);
        break;
    }
    *class = class2;
  }
  return (type);
}

// 解析标识符列表，用于函数参数或 struct/union 成员
static int identifier_list(struct symtable **ctypelist, int class,
                           int et1, int et2) {
  int type;
  struct symtable *ctype = NULL;

  // 解析初始类型
  type = parse_type(&ctype, &class);
  if (type == -1) // 复合类型定义，后面跟着标识符
  {
    // 返回复合类型的类型
    *ctypelist = ctype;
    return (ctype->type);
  }

  // 循环解析标识符
  while (1) {
    // 解析指针
    type = parse_stars(type);

    // 必须是标识符
    ident();

    // 确保参数之前未声明
    if (findlocl(Text, 0) != NULL)
      fatals("Duplicate parameter", Text);

    // 添加到符号表
    addlocl(Text, type, ctype, S_VARIABLE, class, 0, 0);
    scan(&Token);

    // 遇到结束符，停止解析
    if (Token.token == et1 || Token.token == et2)
      break;

    // 需要逗号分隔
    comma();
  }
  // 返回最后一个类型
  return (type);
}

// 解析函数参数声明列表
static int param_declaration_list(struct symtable *funcsym,
                                  struct symtable *newfuncsym) {
  int type, nparams = 0;

  // 函数有可变参数？
  if (Token.token == T_ELLIPSIS) {
    funcsym->has_ellipsis = 1;
    scan(&Token);
    return (nparams);
  }

  // 解析首个参数（可能为空）
  if (Token.token != T_RPAREN) {
    type = identifier_list(&newfuncsym->ctype, V_PARAM, T_COMMA, T_RPAREN);
    nparams++;
  }

  // 循环解析剩余参数
  while (Token.token == T_COMMA) {
    scan(&Token);
    // 可变参数
    if (Token.token == T_ELLIPSIS) {
      funcsym->has_ellipsis = 1;
      scan(&Token);
      break;
    }
    // 解析下一个参数
    type = identifier_list(&newfuncsym->ctype, V_PARAM, T_COMMA, T_RPAREN);
    nparams++;
  }
  return (nparams);
}

// 解析数组成员声明
static struct symtable *array_declaration(char *name, int type,
                                          struct symtable *ctype, int class) {
  int nelems = -1; // 未指定元素个数
  struct symtable *sym;

  // 跳过 '['
  scan(&Token);

  // 如果有元素个数，解析整型字面量
  if (Token.token == T_INTLIT)
    nelems = Token.intvalue;

  // 跳过元素个数和 ']'
  scan(&Token);
  rbracket();

  // 如果是 extern/static，不分配空间
  if (class == V_EXTERN || class == V_STATIC)
    sym = addglob(name, type, ctype, S_ARRAY, class, nelems, 0);
  else
    // 添加到全局符号表
    sym = addglob(name, type, ctype, S_ARRAY, class, nelems, 0);

  return (sym);
}

// 解析标量（简单变量）声明
static struct symtable *scalar_declaration(char *name, int type,
                                           struct symtable *ctype, int class,
                                           struct ASTnode **tree) {
  struct symtable *sym = NULL;
  struct ASTnode *left, *right, *temp;
  int lclass;

  // 计算变量的存储类
  if (class == V_GLOBAL)
    lclass = V_GLOBAL;
  else
    lclass = V_LOCAL;

  // 添加到符号表
  sym = addglob(name, type, ctype, S_VARIABLE, lclass, 0, 0);

  // 如果有初始化表达式
  if (Token.token == T_ASSIGN) {
    // 记录行号
    int savelinenum = linenum;

    scan(&Token);

    // 全局变量初始化需要延迟处理
    if (class == V_GLOBAL) {
      // 标记符号有待初始化的表达式
      sym->st_initialized = 1;
      // 创建赋值节点
      left = mkastunary(A_ADDRGLOBAL, type, ctype, NULL, sym, 0);
      right = parse_expression();
      *tree = mkastnode(A_ASSIGN, type, ctype, left, NULL, right, NULL, 0);
      (*tree)->linenum = savelinenum;
    } else {
      // 局部变量：解析初始值表达式
      // 左值为变量的地址，右值为表达式的值
      left = mkastleaf(A_LVIDENT, type, ctype, NULL, sym, 0);
      right = parse_expression();
      // 构建赋值语句
      temp = mkastnode(A_ASSIGN, type, ctype, left, NULL, right, NULL, 0);
      // 用 A_GLUE 连接到已有的树
      if (*tree == NULL)
        *tree = temp;
      else
        *tree = mkastnode(A_GLUE, P_NONE, NULL, *tree, NULL, temp, NULL, 0);
    }
  }
  return (sym);
}

// 解析函数定义
static struct symtable *function_declaration(char *funcname, int type,
                                              struct symtable *ctype,
                                              int class) {
  struct symtable *newfuncsym, *oldfuncsym;
  struct ASTnode *tree, *finalstmt;
  int endlabel = label++;
  int paramcnt;

  // 假设函数只返回标量类型，所以下面传 NULL
  // 添加到全局符号表
  newfuncsym = addglob(funcname, type, NULL, S_FUNCTION, class, 0, 0);
  newfuncsym->has_ellipsis = 0; // 暂时假设没有可变参数

  // 将全局 Functionid 置 NULL，避免将当前函数参数与前一个函数匹配
  Functionid = NULL;

  // 解析 '('、参数列表和 ')'
  // 传入已有的函数原型指针
  lparen();
  paramcnt = param_declaration_list(oldfuncsym, newfuncsym);
  rparen();

  // 如果是新的函数声明，更新符号表中的参数个数
  // 同时将参数列表复制到函数的节点中
  if (newfuncsym) {
    newfuncsym->nelems = paramcnt;
    oldfuncsym = newfuncsym;
  }

  // 如果声明以分号结束，只是原型
  if (Token.token == T_SEMI) {
    return (oldfuncsym);
  }

  // 这不仅是原型
  // 设置 Functionid 为函数的符号指针
  Functionid = oldfuncsym;

  // 获取复合语句的 AST 树，并标记尚未解析循环或 switch
  Looplevel = 0;
  Switchlevel = 0;
  lbrace();
  tree = compound_statement(0);
  rbrace();

  // 如果函数类型不是 P_VOID
  if (type != P_VOID) {
    // 如果函数没有语句，报错
    if (tree == NULL)
      fatal("No statements in function with non-void type");

    // 检查复合语句的最后一个 AST 操作是否是 return 语句
    // 注意！因为已经释放了树，无法再进行此检查
#if 0
    finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    if (finalstmt == NULL || finalstmt->op != A_RETURN)
      fatal("No return for function with non-void type");
#endif
  }

  // 构建 A_FUNCTION 节点，包含函数符号指针和复合语句子树
  tree = mkastunary(A_FUNCTION, type, ctype, tree, oldfuncsym, endlabel);
  tree->linenum = linenum;

  // 对 AST 树进行优化
  // 原来是 tree = optimise(tree);

  // 序列化树
  serialiseAST(tree);
  freetree(tree, 0);

  // 刷新内存中的符号表
  // 我们已不在函数中
  flushSymtable();
  Functionid = NULL;

  return (oldfuncsym);
}

// 解析复合类型声明：struct 或 union
// 查找已存在的 struct/union 声明，或构建新的符号表项并返回指针
static struct symtable *composite_declaration(int type) {
  struct symtable *ctype = NULL;
  struct symtable *m;
  struct ASTnode *unused;
  int offset;
  int t;

  // 跳过 struct/union 关键字
  scan(&Token);

  // 查看是否有后跟的 struct/union 名
  if (Token.token == T_IDENT) {
    // 查找匹配的类型
    if (type == P_STRUCT)
      ctype = findstruct(Text);
    else
      ctype = findunion(Text);
    scan(&Token);
  }
  // 如果下一个 token 不是 LBRACE，这是已存在 struct/union 类型的使用
  // 返回类型指针
  if (Token.token != T_LBRACE) {
    if (ctype == NULL)
      fatals("unknown struct/union type", Text);
    return (ctype);
  }
  // 确保此 struct/union 类型之前未定义
  if (ctype)
    fatals("previously defined struct/union", Text);

  // 构建复合类型并跳过左花括号
  if (type == P_STRUCT)
    ctype = addtype(Text, P_STRUCT, NULL, S_STRUCT, V_GLOBAL, 0, 0);
  else
    ctype = addtype(Text, P_UNION, NULL, S_UNION, V_GLOBAL, 0, 0);
  scan(&Token);

  // 扫描成员列表
  while (1) {
    // 获取下一个成员。m 作为虚拟参数
    t = declaration_list(&m, V_MEMBER, T_SEMI, T_RBRACE, &unused);
    if (t == -1)
      fatal("Bad type in member list");
    if (Token.token == T_SEMI)
      scan(&Token);
    if (Token.token == T_RBRACE)
      break;
  }

  // 找到右花括号
  rbrace();

  // 设置初始成员的偏移量，并找到其后的第一个空闲字节
  m = ctype->member;
  m->st_posn = 0;
  offset = typesize(m->type, m->ctype);

  // 设置复合类型中每个连续成员的偏移量
  // Union 很简单。对于 struct，需要对齐成员并找到下一个空闲字节
  for (m = m->next; m != NULL; m = m->next) {
    // 设置此成员的偏移量
    if (type == P_STRUCT)
      m->st_posn = genalign(m->type, offset, 1);
    else
      m->st_posn = 0;

    // 获取此成员之后的下一个空闲字节偏移量
    offset += typesize(m->type, m->ctype);
  }

  // 设置复合类型的总大小
  ctype->size = offset;
  return (ctype);
}

// 解析 enum 声明
static void enum_declaration(void) {
  struct symtable *etype = NULL;
  char *name = NULL;
  int intval = 0;

  // 跳过 enum 关键字
  scan(&Token);

  // 如果有后跟的 enum 类型名，获取指向已存在 enum 类型节点的指针
  if (Token.token == T_IDENT) {
    etype = findenumtype(Text);
    name = strdup(Text); // 因为很快会被覆盖
    scan(&Token);
  }
  // 如果下一个 token 不是 LBRACE，检查是否有 enum 类型名，然后返回
  if (Token.token != T_LBRACE) {
    if (etype == NULL)
      fatals("undeclared enum type:", name);
    return;
  }
  // 确实是 LBRACE，跳过它
  scan(&Token);

  // 如果有 enum 类型名，确保之前未声明
  if (etype != NULL)
    fatals("enum type redeclared:", etype->name);

  // 如果有类型名，为此标识符构建 enum 类型节点
  if (name != NULL) {
    etype = addtype(name, P_INT, NULL, S_ENUMTYPE, V_GLOBAL, 0, 0);
    free(name);
  }

  // 循环获取所有枚举值
  while (1) {
    // 确保是标识符
    // 复制它以防有整型字面量跟随
    ident();
    name = strdup(Text);

    // 确保此枚举值之前未声明
    etype = findenumval(name);
    if (etype != NULL)
      fatals("enum value redeclared:", name);

    // 如果下一个 token 是 '='，跳过它并获取后续的整型字面量
    if (Token.token == T_ASSIGN) {
      scan(&Token);
      if ((Token.token != T_INTLIT) && (Token.token != T_CHARLIT))
        fatal("Expected int literal after '='");
      intval = Token.intvalue;
      scan(&Token);
    }
    // 为此标识符构建枚举值节点
    // 为下一个枚举标识符递增值
    etype = addglob(name, P_INT, NULL, S_ENUMVAL, V_GLOBAL, 0, intval++);

    free(name);

    // 遇到右花括号则退出，否则获取逗号
    if (Token.token == T_RBRACE)
      break;
    comma();
  }
  scan(&Token); // 跳过右花括号
}

// 解析 typedef 声明，返回类型和 ctype
static int typedef_declaration(struct symtable **ctype) {
  int type, class = 0;

  // 跳过 typedef 关键字
  scan(&Token);

  // 获取关键字后的实际类型
  type = parse_type(ctype, &class);
  if (class != 0)
    fatal("Can't have static/extern in a typedef declaration");

  // 获取后续的 '*' token
  type = parse_stars(type);

  // 查看 typedef 标识符是否已存在
  if (findtypedef(Text) != NULL)
    fatals("redefinition of typedef", Text);

  // 不存在，添加到类型列表
  addtype(Text, type, *ctype, S_TYPEDEF, class, 0, 0);
  scan(&Token);
  return (type);
}

// 给定 typedef 名，返回它代表的类型
static int type_of_typedef(char *name, struct symtable **ctype) {
  struct symtable *t;

  // 在列表中查找 typedef
  t = findtypedef(name);
  if (t == NULL)
    fatals("unknown type", name);
  scan(&Token);
  *ctype = t->ctype;
  return (t->type);
}

// 解析变量或函数的声明
// 类型和后续的 '*' 已扫描，标识符在 Token 变量中
// class 参数是符号的存储类
// 返回符号在符号表中的指针
static struct symtable *symbol_declaration(int type, struct symtable *ctype,
                                          int class, struct ASTnode **tree) {
  struct symtable *sym = NULL;
  char *varname = strdup(Text);

  // 确保是标识符
  // 上面已复制，所以可以扫描更多 token，例如局部变量的赋值表达式
  ident();

  // 处理函数声明
  if (Token.token == T_LPAREN) {
    sym = function_declaration(varname, type, ctype, class);
    free(varname);
    return (sym);
  }

  // 查看此数组或标量变量是否已声明
  switch (class) {
    case V_EXTERN:
    case V_STATIC:
    case V_GLOBAL:
    case V_LOCAL:
    case V_PARAM:
      if (findlocl(varname, 0) != NULL)
        fatals("Duplicate local variable declaration", varname);
      break;
    case V_MEMBER:
      if (findmember(varname) != NULL)
        fatals("Duplicate struct/union member declaration", varname);
  }

  // 添加数组或标量变量到符号表
  if (Token.token == T_LBRACKET) {
    sym = array_declaration(varname, type, ctype, class);
    *tree = NULL; // 局部数组不初始化
  } else
    sym = scalar_declaration(varname, type, ctype, class, tree);
  free(varname);
  return (sym);
}

// 解析有初始类型的符号列表
// 返回符号的类型。et1 和 et2 是结束 token
int declaration_list(struct symtable **ctype, int class, int et1, int et2,
                      struct ASTnode **gluetree) {
  int inittype, type;
  struct symtable *sym;
  struct ASTnode *tree = NULL;
  *gluetree = NULL;

  // 获取初始类型。如果是 -1，则是复合类型定义，返回此类型
  if ((inittype = parse_type(ctype, &class)) == -1)
    return (inittype);

  // 现在解析符号列表
  while (1) {
    // 查看此符号是否是指针
    type = parse_stars(inittype);

    // 解析此符号
    sym = symbol_declaration(type, *ctype, class, &tree);

    // 解析了函数，没有列表，所以离开
    if (sym->stype == S_FUNCTION) {
      if (class != V_GLOBAL && class != V_STATIC)
        fatal("Function definition not at global level");
      return (type);
    }
    // 将局部声明的 AST 树粘合在一起，构建待执行的赋值序列
    if (*gluetree == NULL)
      *gluetree = tree;
    else
      *gluetree =
          mkastnode(A_GLUE, P_NONE, NULL, *gluetree, NULL, tree, NULL, 0);

    // 到达列表末尾，离开
    if (Token.token == et1 || Token.token == et2)
      return (type);

    // 否则需要逗号作为分隔符
    comma();
  }

  return (0); // 保持 -Wall 开心
}

// 解析一个或多个全局声明：变量、函数或 struct
void global_declarations(void) {
  struct symtable *ctype = NULL;
  struct ASTnode *unused;

  // 循环解析声明列表直到文件末尾
  while (Token.token != T_EOF) {
    declaration_list(&ctype, V_GLOBAL, T_SEMI, T_EOF, &unused);

    // 跳过任何分隔的分号
    if (Token.token == T_SEMI)
      scan(&Token);
  }
}