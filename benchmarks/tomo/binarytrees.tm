# binary-trees — The Computer Language Benchmarks Game
#
# A Tomo port. No inline C at all: `say()` produces the exact output. This
# benchmark is an allocation/GC stress test — it builds and discards many
# short-lived binary trees — so the port leans on Tomo's heap pointers and GC
# rather than any manual memory management.
#
# Design notes:
#   - A tree node is one self-referential struct with two optional heap
#     pointers (`@Tree?`); a leaf is a node whose children are both `none`.
#     `bottom_up_tree(0)` allocates that leaf, matching the reference (which
#     allocates a node for every leaf too, so the allocation counts line up).
#   - `item_check` tests `left == none` to detect a leaf, exactly like the
#     reference's `tree->left == NULL`.
#   - All depths/counters are native `Int64`, not arbitrary-precision `Int`.
#
# Usage: binarytrees <max-depth>   (e.g. ./binarytrees 21)

struct Tree{left:@Tree?=none, right:@Tree?=none}

func bottom_up_tree(depth:Int64 -> @Tree)
    if depth > 0
        return @Tree{bottom_up_tree(depth-1), bottom_up_tree(depth-1)}
    return @Tree{}

func item_check(tree:@Tree -> Int64)
    if tree.left == none
        return 1
    return 1 + item_check(tree.left!) + item_check(tree.right!)

func main(n:Int64)
    min_depth := Int64(4)
    max_depth := if min_depth + 2 > n then min_depth + 2 else n
    stretch_depth := max_depth + 1

    stretch := bottom_up_tree(stretch_depth)
    say("stretch tree of depth $stretch_depth\t check: $(item_check(stretch))")

    long_lived := bottom_up_tree(max_depth)

    for depth in min_depth.to(max_depth, step=2)
        iterations := Int64(1) << (max_depth - depth + min_depth)
        check := Int64(0)
        for _ in Int64(1).to(iterations)
            check += item_check(bottom_up_tree(depth))
        say("$iterations\t trees of depth $depth\t check: $check")

    say("long lived tree of depth $max_depth\t check: $(item_check(long_lived))")
