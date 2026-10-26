#include "data/vdd.hpp"

#include <charconv>
#include <stdexcept>

#include "core/fs.hpp"

namespace tto {

const VddNode* VddNode::child(std::string_view k) const {
    const std::string want = to_lower_ascii(k);
    for (const auto& c : children)
        if (to_lower_ascii(c.key) == want) return &c;
    return nullptr;
}

const VddNode* VddNode::at(std::string_view path) const {
    const VddNode* n = this;
    while (n && !path.empty()) {
        const auto dash = path.find('-');
        n = n->child(path.substr(0, dash));
        path = dash == std::string_view::npos ? std::string_view{} : path.substr(dash + 1);
    }
    return n;
}

std::optional<double> VddNode::number(std::string_view path) const {
    const VddNode* n = at(path);
    if (!n) return std::nullopt;
    std::string v = n->value;
    for (auto& c : v)
        if (c == ',') c = '.';
    double d = 0;
    const char* b = v.data();
    while (*b == ' ') ++b;
    auto [p, ec] = std::from_chars(b, v.data() + v.size(), d);
    if (ec != std::errc{}) return std::nullopt;
    return d;
}

std::string VddNode::text(std::string_view path, std::string_view fallback) const {
    const VddNode* n = at(path);
    return n ? n->value : std::string(fallback);
}

VddNode parse_vdd(std::string_view text) {
    VddNode root;
    std::vector<VddNode*> stack{&root};
    int lineNo = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.empty()) continue;
        std::size_t depth = 0;
        while (depth < line.size() && line[depth] == '\t') ++depth;
        line.remove_prefix(depth);
        const auto eq = line.find('=');
        if (eq == std::string_view::npos)
            throw std::runtime_error("HG.vdd line " + std::to_string(lineNo) + ": missing '='");
        if (depth + 1 > stack.size())
            throw std::runtime_error("HG.vdd line " + std::to_string(lineNo) + ": unexpected indentation");
        stack.resize(depth + 1);
        VddNode n;
        n.key = cp1254_to_utf8(line.substr(0, eq));
        n.value = cp1254_to_utf8(line.substr(eq + 1));
        stack.back()->children.push_back(std::move(n));
        stack.push_back(&stack.back()->children.back());
    }
    return root;
}

}  // namespace tto
