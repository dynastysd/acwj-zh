// opt.c - AST树优化代码
// 版权所有 (c) 2019 Warren Toomey, GPL3

// 优化一个AST树，使用深度优先节点遍历
struct ASTnode *optimise(struct ASTnode *n);