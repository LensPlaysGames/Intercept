#ifndef FUNCOMPILER_DOM_H
#define FUNCOMPILER_DOM_H

#include <codegen/codegen_forward.h>
#include <stdbool.h>
#include <vector.h>

/// Structure used to store dominator information such as the
/// dominator tree and the list of dominators of each block.
///
/// The following definitions may be useful in understanding
/// the concept of dominance and dominator trees:
///
///   *dominates*:
/// A block B1 *dominates* another block B2, iff all paths from the
/// entry block to B2 go through B1. That is, when control flow
/// reaches B2, it must have come from B1. By definition, every
/// block dominates itself.
///
///   *strictly dominates*:
/// A block B1 *strictly dominates* another block B2, iff B1 dominates
/// B2 and B1 != B2.
///
///   *immediately dominates*:
/// A block B1 *immediately dominates* another block B2, iff B1 strictly
/// dominates B2 and there is no other block B3 such that B1 strictly
/// dominates B3 and B3 strictly dominates B2. To put it differently, the
/// immediate dominator of a block B2 is the closest block B1 that strictly
/// dominates it, such that there is no other block in between that is
/// strictly dominated by B1 and strictly dominates B2. Every block (except
/// the entry block) has exactly one immediate dominator.
///
///   *dominator tree*:
/// The *dominator tree* of a control flow graph is a tree containing a node
/// for each block in the CFG such that each node’s children are the blocks
/// that it immediately dominates.
///
/// By way of illustration, consider the following CFG:
///
///                   B0
///                  ╱  ╲
///                 B1  B3
///                ╱  ╲ ╱
///               B2  B4
///               |   |
///               B5  B6
///
/// In the graph above,
///    - B0 dominates all blocks since it is the root.
///    - B1 dominates B2 and B5, but *not* e.g. B4 since B4 can
///      also be reached from B3.
///    - B2 dominates B5.
///    - B4 dominates B6.
///
/// The dominator tree for this CFG is:
///
///                    B0
///                 ╱  |  ╲
///                B1  B3  B4
///                |       |
///                B2      B6
///                |
///                B5
/// Depth-first search tree.
typedef struct DominatorTree {
  /// Map from blocks to their immediate dominators.
  IRBlockMap doms;
} DominatorTree;

/// Build the dominator tree of a function.
DominatorTree dom_tree_build(IRFunction *f);

/// Free the memory used by the dominator tree.
void dom_tree_free(DominatorTree *info);

#endif // FUNCOMPILER_DOM_H
