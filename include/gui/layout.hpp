#pragma once

#include "geometry.hpp"
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gui {

enum class LayoutKind { leaf, row, column };

struct LayoutPadding {
    double left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const LayoutPadding&) const = default;
};

struct LayoutNode {
    std::string id;
    LayoutKind kind = LayoutKind::leaf;
    // An absent width fills its assigned width. An absent height fits content.
    // Explicit zero is an actual zero extent, never a request for automatic size.
    std::optional<double> width, height;
    LayoutPadding padding;
    double gap = 0;
    // Only row children without fixed widths participate in weighted sharing.
    double weight = 1;
    // A row stretches children without fixed heights to its content height.
    bool equal_height = true;
    std::vector<LayoutNode> children;
    bool operator==(const LayoutNode&) const = default;
};

struct LayoutBox {
    std::string id;
    Rect bounds;  // Outer frame, including padding.
    Rect content; // Available interior, excluding padding.
    Rect clip;    // Outer frame intersected with every inherited content clip.
    std::vector<LayoutBox> children;
    bool operator==(const LayoutBox&) const = default;
};

// Measure a leaf's content at the supplied logical width. Height contributes to
// automatic layout; returned width is validated but does not resize its column.
// The callback must handle zero width and return finite, nonnegative extents.
using LayoutMeasure = std::function<Size(std::string_view, double)>;
inline constexpr std::size_t layout_depth_limit = 128;
inline constexpr std::size_t layout_node_limit = 100000;

namespace layout_detail {

inline bool extent(double value) {
    return std::isfinite(value) && value >= 0 && value <= coordinate_limit;
}

inline double add(double first, double second) {
    return first >= coordinate_limit - second ? coordinate_limit : first + second;
}

inline double inset_width(double width, double left, double right) {
    return std::max(0.0, width - std::min(width, left) - std::min(width, right));
}

inline Rect inset(Rect bounds, LayoutPadding padding) {
    const auto left = std::min(bounds.width, padding.left);
    const auto top = std::min(bounds.height, padding.top);
    return {bounds.x + left, bounds.y + top,
        std::max(0.0, bounds.width - left - std::min(bounds.width - left, padding.right)),
        std::max(0.0, bounds.height - top - std::min(bounds.height - top, padding.bottom))};
}

inline void identify(std::string_view id, std::size_t depth, std::size_t& count,
                     std::unordered_set<std::string>& ids) {
    if (depth > layout_depth_limit || ++count > layout_node_limit)
        throw std::invalid_argument("Layout exceeds its structural limit.");
    if (id.empty() || !ids.emplace(id).second)
        throw std::invalid_argument("Layout IDs must be nonempty and unique.");
}

inline void validate(const LayoutNode& node, std::size_t depth, std::size_t& count,
                     std::unordered_set<std::string>& ids, bool has_measure) {
    identify(node.id, depth, count, ids);
    if ((node.width && !extent(*node.width)) || (node.height && !extent(*node.height)) ||
        !extent(node.padding.left) || !extent(node.padding.top) || !extent(node.padding.right) ||
        !extent(node.padding.bottom) || !extent(node.gap) || !extent(node.weight))
        throw std::invalid_argument("Invalid layout extent, padding, gap, or weight.");
    switch (node.kind) {
        case LayoutKind::leaf:
            if (!node.children.empty()) throw std::invalid_argument("A layout leaf cannot have children.");
            if (!has_measure) throw std::invalid_argument("A layout leaf requires a measurement callback.");
            break;
        case LayoutKind::row:
        case LayoutKind::column: break;
        default: throw std::invalid_argument("Unknown layout kind.");
    }
    for (const auto& child : node.children) validate(child, depth + 1, count, ids, has_measure);
}

struct Measured {
    const LayoutNode* node = nullptr;
    double width = 0, height = 0;
    std::vector<Measured> children;
};

inline Measured measure(const LayoutNode& node, double available_width, const LayoutMeasure& callback) {
    Measured result{&node, std::min(available_width, node.width.value_or(available_width)), 0, {}};
    const auto inner_width = inset_width(result.width, node.padding.left, node.padding.right);
    double content_height = 0;
    if (node.kind == LayoutKind::leaf) {
        const auto size = callback(node.id, inner_width);
        if (!extent(size.width) || !extent(size.height))
            throw std::invalid_argument("Measurement returned invalid extents.");
        content_height = size.height;
    } else if (node.kind == LayoutKind::column) {
        result.children.reserve(node.children.size());
        for (const auto& child : node.children) {
            if (!result.children.empty()) content_height = add(content_height, node.gap);
            result.children.push_back(measure(child, inner_width, callback));
            content_height = add(content_height, result.children.back().height);
        }
    } else {
        double fixed = 0, gaps = 0, largest_weight = 0;
        for (std::size_t index = 0; index < node.children.size(); ++index) {
            const auto& child = node.children[index];
            if (index != 0) gaps = add(gaps, node.gap);
            if (child.width) fixed = add(fixed, *child.width);
            else largest_weight = std::max(largest_weight, child.weight);
        }
        // Normalize first so even a large set of large weights has a bounded sum.
        double total_weight = 0;
        if (largest_weight > 0) {
            for (const auto& child : node.children)
                if (!child.width) total_weight += child.weight / largest_weight;
        }
        const auto flexible = std::max(0.0, inner_width - fixed - gaps);
        double cursor = 0;
        result.children.reserve(node.children.size());
        for (const auto& child : node.children) {
            const auto desired = child.width.value_or(total_weight > 0
                ? flexible * ((child.weight / largest_weight) / total_weight) : 0);
            const auto width = std::min(std::max(0.0, inner_width - cursor), desired);
            result.children.push_back(measure(child, width, callback));
            content_height = std::max(content_height, result.children.back().height);
            cursor = std::min(inner_width, add(add(cursor, width), node.gap));
        }
    }
    result.height = node.height.value_or(add(add(node.padding.top, content_height), node.padding.bottom));
    return result;
}

inline LayoutBox place(const Measured& measured, double x, double y, Rect inherited_clip,
                       std::optional<double> forced_height = {}) {
    const auto& node = *measured.node;
    const auto height = std::min(forced_height.value_or(measured.height), coordinate_limit - std::max(0.0, y));
    LayoutBox result{node.id, {x, y, measured.width, height}, {}, {}, {}};
    result.content = inset(result.bounds, node.padding);
    result.clip = intersect(result.bounds, inherited_clip);
    const auto child_clip = intersect(result.content, result.clip);
    result.children.reserve(measured.children.size());
    double cursor = node.kind == LayoutKind::row ? result.content.x : result.content.y;
    for (const auto& child : measured.children) {
        if (node.kind == LayoutKind::row) {
            const auto fill_height = node.equal_height && !child.node->height
                ? std::optional<double>{result.content.height} : std::nullopt;
            result.children.push_back(place(child, cursor, result.content.y, child_clip, fill_height));
            cursor = std::min(result.content.x + result.content.width, cursor + child.width + node.gap);
        } else {
            result.children.push_back(place(child, result.content.x, cursor, child_clip));
            cursor = std::min(coordinate_limit, cursor + result.children.back().bounds.height + node.gap);
        }
    }
    return result;
}

inline void flatten(const LayoutBox& box, std::vector<LayoutBox>& result, std::size_t depth,
                    std::size_t& count, std::unordered_set<std::string>& ids) {
    identify(box.id, depth, count, ids);
    if (!valid_rect(box.bounds) || !valid_rect(box.content) || !valid_rect(box.clip))
        throw std::invalid_argument("Invalid composed layout rectangle.");
    result.push_back({box.id, box.bounds, box.content, box.clip, {}});
    for (const auto& child : box.children) flatten(child, result, depth + 1, count, ids);
}

} // namespace layout_detail

// Root width is constrained by viewport.width. Automatic height fits the full
// measured content, with coordinate-limit saturation; viewport.height clips it.
// Fixed container heights clip flowing children instead of changing their text
// measurement. Declarations and caller-owned output remain unchanged on failure.
inline LayoutBox compose_layout(const LayoutNode& root, Rect viewport, const LayoutMeasure& measure) {
    if (!valid_rect(viewport)) throw std::invalid_argument("Invalid layout viewport.");
    std::size_t count = 0;
    std::unordered_set<std::string> ids;
    layout_detail::validate(root, 1, count, ids, static_cast<bool>(measure));
    const auto measured = layout_detail::measure(root, viewport.width, measure);
    return layout_detail::place(measured, viewport.x, viewport.y, viewport);
}

// Return parent-before-child owned records. Each record's children vector is
// empty; its id/bounds/content/clip correspond to the hierarchy's same node.
inline std::vector<LayoutBox> flatten_layout(const LayoutBox& root) {
    std::vector<LayoutBox> result;
    std::size_t count = 0;
    std::unordered_set<std::string> ids;
    layout_detail::flatten(root, result, 1, count, ids);
    return result;
}

} // namespace gui
