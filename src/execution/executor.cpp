//
// Created by Haoran Gan on 11/16/25.
//

#include "execution/executor.hpp"

static void ExplainInto(const Executor &node, int depth, std::string &out) {
    out.append(static_cast<size_t>(depth) * 2, ' ');
    if (depth > 0) out += "-> ";
    out += node.Name();
    out += "\n";
    for (const Executor *child : node.Children()) {
        ExplainInto(*child, depth + 1, out);
    }
}

std::string Explain(const Executor &root) {
    std::string out;
    ExplainInto(root, 0, out);
    return out;
}
