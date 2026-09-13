#include "gui/layout.hpp"

#include <iostream>
#include <limits>
#include <map>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

bool near(double first, double second) { return std::abs(first - second) < 1e-8; }

template<class Exception, class Function>
void require_throws(Function action, const char* message) {
    try { action(); }
    catch (const Exception&) { return; }
    throw std::runtime_error(message);
}

gui::LayoutNode leaf(std::string id, std::optional<double> width = {}, std::optional<double> height = {}) {
    gui::LayoutNode result;
    result.id = std::move(id);
    result.width = width;
    result.height = height;
    return result;
}

gui::LayoutNode group(std::string id, gui::LayoutKind kind, std::vector<gui::LayoutNode> children) {
    auto result = leaf(std::move(id));
    result.kind = kind;
    result.children = std::move(children);
    return result;
}

void measured_columns_and_clipping() {
    auto root = group("root", gui::LayoutKind::column, {leaf("first"), leaf("second", 80)});
    root.padding = {10, 5, 10, 7};
    root.gap = 3;
    const auto original = root;
    std::map<std::string, double> widths;
    const auto box = gui::compose_layout(root, {20, 30, 200, 40}, [&](std::string_view id, double width) {
        widths.emplace(std::string(id), width);
        return gui::Size{width, id == "first" ? 20.0 : 30.0};
    });
    require(root == original, "Composition mutated declarations");
    require(box.bounds == gui::Rect{20, 30, 200, 65}, "Automatic content height lost padding or gap");
    require(box.content == gui::Rect{30, 35, 180, 53}, "Content insets changed");
    require(box.clip == gui::Rect{20, 30, 200, 40}, "Viewport did not clip automatic height");
    require(widths.at("first") == 180 && widths.at("second") == 80, "Leaf measurement received wrong width");
    require(box.children[0].bounds == gui::Rect{30, 35, 180, 20}, "First child origin changed");
    require(box.children[1].bounds == gui::Rect{30, 58, 80, 30}, "Column flow changed");
    require(box.children[1].clip == gui::Rect{30, 58, 80, 12}, "Inherited clipping lost viewport");
    const auto flat = gui::flatten_layout(box);
    require(flat.size() == 3 && flat[0].id == "root" && flat[1].id == "first" && flat[2].id == "second",
            "Flattened layout changed declaration order");
    require(flat[1].children.empty() && flat[1].bounds == box.children[0].bounds, "Flattened records retained a subtree");

    root.height = 20;
    const auto clipped = gui::compose_layout(root, {20, 30, 200, 100}, [](std::string_view, double width) {
        return gui::Size{width, 30};
    });
    require(clipped.bounds.height == 20 && clipped.content.height == 8, "Fixed container height ignored");
    require(clipped.children[0].bounds.height == 30 && clipped.children[0].clip.height == 8,
            "Clipping changed measured text height");
    require(!gui::has_area(clipped.children[1].clip), "Overflowing child remained visible");
}

void weighted_rows_and_nested_height() {
    auto nested = group("nested", gui::LayoutKind::row, {leaf("short"), leaf("tall")});
    nested.weight = 3;
    auto fixed = leaf("fixed", 40, 12);
    auto middle = leaf("middle");
    auto root = group("row", gui::LayoutKind::row, {fixed, middle, nested});
    root.gap = 5;
    root.padding = {5, 2, 5, 3};
    const auto box = gui::compose_layout(root, {10, 20, 260, 100}, [](std::string_view id, double width) {
        return gui::Size{width, id == "middle" ? 40.0 : id == "tall" ? 25.0 : 10.0};
    });
    require(box.bounds.height == 45, "Row height did not use tallest child");
    require(near(box.children[0].bounds.width, 40) && near(box.children[1].bounds.width, 50) &&
            near(box.children[2].bounds.width, 150), "Weighted remaining widths incorrect");
    require(box.children[0].bounds.height == 12, "Equal-height row overrode explicit child height");
    require(box.children[1].bounds.height == 40 && box.children[2].bounds.height == 40, "Row did not stretch automatic heights");
    require(box.children[2].children[0].bounds.height == 40 && box.children[2].children[1].bounds.height == 40,
            "Nested row did not inherit equal height");
    root.equal_height = false;
    const auto natural = gui::compose_layout(root, {0, 0, 260, 100}, [](std::string_view, double width) {
        return gui::Size{width, 10};
    });
    require(natural.children[1].bounds.height == 10, "Disabled row stretching still stretched");
}

void wrapped_measurement_and_recomposition() {
    auto root = group("text-row", gui::LayoutKind::row, {leaf("left"), leaf("right")});
    std::vector<std::string> calls;
    const auto measure = [&](std::string_view id, double width) {
        calls.emplace_back(id);
        return gui::Size{width, width == 0 ? 0 : std::ceil(200.0 / width) * 10.0};
    };
    const auto wide = gui::compose_layout(root, {0, 0, 200, 100}, measure);
    require(wide.bounds.height == 20 && calls == std::vector<std::string>({"left", "right"}),
            "Composition repeated or reordered leaf measurement");
    calls.clear();
    const auto narrow = gui::compose_layout(root, {0, 0, 100, 100}, measure);
    require(narrow.bounds.height == 40 && calls.size() == 2, "Changed width did not remeasure wrapped content");
    require(wide.children[0].bounds.width == 100 && wide.bounds.height == 20, "Recomposition mutated prior owned output");
    root.width = 0;
    const auto zero = gui::compose_layout(root, {0, 0, 200, 100}, measure);
    require(zero.bounds.width == 0 && zero.bounds.height == 0, "Explicit zero root width was ignored");
}

void empty_zero_and_constrained_areas() {
    auto root = group("row", gui::LayoutKind::row, {leaf("zero", 0, 0), leaf("fill"), leaf("none")});
    root.children[2].weight = 0;
    std::map<std::string, double> widths;
    const auto box = gui::compose_layout(root, {0, 0, 100, 30}, [&](std::string_view id, double width) {
        widths.emplace(std::string(id), width);
        return gui::Size{width, 10};
    });
    require(box.children[0].bounds.width == 0 && box.children[0].bounds.height == 0, "Explicit zero became automatic");
    require(box.children[1].bounds.width == 100 && box.children[2].bounds.width == 0, "Zero weight received remaining width");
    require(widths.at("zero") == 0 && widths.at("none") == 0, "Zero-width callback skipped");

    root.children = {leaf("first", 80), leaf("last", 80)};
    root.gap = 10;
    const auto narrow = gui::compose_layout(root, {0, 0, 100, 30}, [](std::string_view, double width) { return gui::Size{width, 10}; });
    require(narrow.children[0].bounds.width == 80 && narrow.children[1].bounds.width == 10, "Fixed-width overflow was not clipped in order");
    root.padding = {90, 80, 90, 80};
    root.height = 5;
    const auto padded = gui::compose_layout(root, {0, 0, 100, 30}, [](std::string_view, double width) { return gui::Size{width, 10}; });
    require(padded.content.width == 0 && padded.content.height == 0, "Excess padding produced negative content");
    for (const auto& item : gui::flatten_layout(padded))
        require(gui::valid_rect(item.bounds) && gui::valid_rect(item.content) && gui::valid_rect(item.clip), "Empty area became invalid");

    auto empty = group("empty", gui::LayoutKind::column, {});
    empty.padding = {1, 2, 3, 4};
    const auto vacant = gui::compose_layout(empty, {0, 0, 20, 0}, {});
    require(vacant.bounds.height == 6 && !gui::has_area(vacant.clip), "Empty container lost padding or clipping");
}

void shared_allocation_and_tiny_weights() {
    const auto tiny = std::numeric_limits<double>::denorm_min();
    const std::vector<gui::Allocation> allocations{{0, tiny}, {0, tiny}};
    for (const auto axis : {gui::Axis::horizontal, gui::Axis::vertical}) {
        const auto boxes = gui::arrange({2, 3, 0.25, 0.25}, axis, allocations);
        require(boxes.size() == 2, "Allocation lost children");
        if (axis == gui::Axis::horizontal)
            require(boxes[0] == gui::Rect{2, 3, 0.125, 0.25} &&
                    boxes[1] == gui::Rect{2.125, 3, 0.125, 0.25}, "Tiny horizontal weights lost their share");
        else
            require(boxes[0] == gui::Rect{2, 3, 0.25, 0.125} &&
                    boxes[1] == gui::Rect{2, 3.125, 0.25, 0.125}, "Tiny vertical weights lost their share");
    }
    require_throws<std::invalid_argument>([&] {
        gui::arrange({0, 0, 1, 1}, static_cast<gui::Axis>(99), allocations);
    }, "Unknown layout axis accepted");

    auto root = group("row", gui::LayoutKind::row, {leaf("zero", 0), leaf("first"), leaf("second")});
    root.children[1].weight = tiny;
    root.children[2].weight = tiny;
    const auto box = gui::compose_layout(root, {2, 3, 0.25, 1}, [](std::string_view, double width) {
        return gui::Size{width, 1};
    });
    require(box.children[0].bounds.width == 0 && box.children[1].bounds.width == 0.125 &&
            box.children[2].bounds.width == 0.125, "Composed rows changed explicit zero or tiny weight allocation");

    const std::vector<gui::Allocation> mixed{{20, 999}, {0, 1}, {0, 3}};
    root.children[0].width = 20;
    root.children[1].weight = 1;
    root.children[2].weight = 3;
    root.gap = 2;
    for (const auto width : {0.0, 10.0, 24.0, 100.0, gui::coordinate_limit}) {
        const auto direct = gui::arrange({0, 0, width, 1}, gui::Axis::horizontal, mixed, root.gap);
        const auto composed = gui::compose_layout(root, {0, 0, width, 1}, [](std::string_view, double w) {
            return gui::Size{w, 1};
        });
        for (std::size_t index = 0; index < direct.size(); ++index)
            require(direct[index] == composed.children[index].bounds, "Direct and composed row allocation disagree");
    }
}

void fractional_coordinate_boundary() {
    const std::vector<std::pair<double, std::vector<double>>> cases{
        {560999.21799959836, {58613.620543797406, 384391.65170005366, 103032.9594951168,
            127895.95622763167, 517122.6807777377, 165341.53941432148}},
        {225149.20453407016, {528249.91365672438, 862059.47869001643, 49638.744410509113,
            991440.02202518703, 783317.20685757918, 546824.20517934905, 454494.92109901598,
            570854.52540963935, 171122.89171113094, 689270.70271867106}}
    };
    for (const auto& [origin, weights] : cases) {
        std::vector<gui::Allocation> allocations;
        auto root = group("root", gui::LayoutKind::row, {});
        for (std::size_t index = 0; index < weights.size(); ++index) {
            allocations.push_back({0, weights[index]});
            root.children.push_back(leaf(std::to_string(index)));
            root.children.back().weight = weights[index];
        }
        const auto extent = gui::coordinate_limit - origin;
        for (const auto axis : {gui::Axis::horizontal, gui::Axis::vertical}) {
            const auto area = axis == gui::Axis::horizontal ? gui::Rect{origin, 0, extent, 1} :
                gui::Rect{0, origin, 1, extent};
            for (const auto rect : gui::arrange(area, axis, allocations)) {
                require(gui::valid_rect(rect), "Translated allocation rounded outside the coordinate limit");
                (void)gui::device_rect(rect, 1.25);
            }
        }
        const auto composed = gui::compose_layout(root, {origin, 0, extent, 1}, [](std::string_view, double w) {
            return gui::Size{w, 1};
        });
        for (const auto& box : gui::flatten_layout(composed))
            (void)gui::device_rect(box.bounds, 1.25);
    }

    constexpr double origin = 267392.17687819182, extent = 732607.82312180824;
    constexpr double leading_padding = 239259.70480972205;
    for (const auto trailing_padding : {0.0, 0.01, 200000.0, gui::coordinate_limit}) {
        auto padded = leaf("padded");
        padded.height = extent;
        padded.padding = {leading_padding, leading_padding, trailing_padding, trailing_padding};
        const auto composed = gui::compose_layout(padded, {origin, origin, extent, extent},
            [](std::string_view, double width) { return gui::Size{width, 1}; });
        require(composed.content.x + composed.content.width <= composed.bounds.x + composed.bounds.width &&
                composed.content.y + composed.content.height <= composed.bounds.y + composed.bounds.height,
                "Fractional padding rounded content outside its parent");
        for (const auto& box : gui::flatten_layout(composed))
            (void)gui::device_rect(box.content, 1.25);
    }

    const gui::Rect across_zero{-861040.13262529613, -441720.32158836682, 1000000, 1000000};
    auto padded = leaf("across-zero");
    padded.height = across_zero.height;
    padded.padding = {378282.07432594924, 320178.15744318085, 0, 0};
    const auto composed = gui::compose_layout(padded, across_zero,
        [](std::string_view, double width) { return gui::Size{width, 1}; });
    const auto inside = [&](gui::Rect rect) {
        require(gui::valid_rect(rect) && rect.x + rect.width <= across_zero.x + across_zero.width &&
                rect.y + rect.height <= across_zero.y + across_zero.height,
                "Coordinate cancellation rounded beyond the inherited endpoint");
    };
    inside(composed.content);
    // This independently constructed valid rectangle extends past the parent;
    // intersection must not round its clipped endpoint back outside that parent.
    inside(gui::intersect(across_zero, {-482758.05829934689, -121542.16414518596, 1000000, 1000000}));
    for (const auto axis : {gui::Axis::horizontal, gui::Axis::vertical}) {
        const auto lead = axis == gui::Axis::horizontal ? padded.padding.left : padded.padding.top;
        const std::vector<gui::Allocation> allocations{{lead, 0}, {0, 1}};
        for (const auto rect : gui::arrange(across_zero, axis, allocations)) inside(rect);
    }
}

void translated_parent_containment() {
    auto root = group("translated", gui::LayoutKind::row, {leaf("first"), leaf("middle"), leaf("last")});
    root.padding = {5.2445452627777609, 0, 6.9380990397386348, 0};
    root.gap = 5.1955640280940854;
    root.children[0].weight = 0.31991330809500174;
    root.children[1].weight = 0.40673815872072555;
    root.children[2].weight = 0.010430003742940151;
    const auto box = gui::compose_layout(root, {322.89392466042978, 0, 92.162373411747382, 10},
        [](std::string_view, double width) { return gui::Size{width, 1}; });
    for (const auto& child : box.children)
        require(child.bounds.x >= box.content.x &&
                child.bounds.x + child.bounds.width <= box.content.x + box.content.width,
                "Translated child bounds rounded outside their parent's content width");
    for (const auto& child : gui::flatten_layout(box))
        (void)gui::device_rect(child.bounds, 1.25);
}

void bounds_and_input_validation() {
    const auto metric = [](std::string_view, double width) { return gui::Size{width, gui::coordinate_limit}; };
    auto root = group("root", gui::LayoutKind::column, {leaf("one"), leaf("two"), leaf("three")});
    root.gap = gui::coordinate_limit;
    for (const auto origin : {gui::Point{0, 999990}, gui::Point{-100, -999990}}) {
        const auto box = gui::compose_layout(root, {origin.x, origin.y, 100, 5}, metric);
        for (const auto& item : gui::flatten_layout(box))
            require(gui::valid_rect(item.bounds) && gui::valid_rect(item.content) && gui::valid_rect(item.clip), "Large layout escaped coordinate bounds");
    }
    const auto original = root;
    require_throws<std::runtime_error>([&] {
        gui::compose_layout(root, {0, 0, 100, 100}, [](std::string_view, double) -> gui::Size { throw std::runtime_error("Measurement failed"); });
    }, "Callback failure did not propagate");
    require(root == original, "Failed measurement changed declarations");

    const auto invalid = [&](gui::LayoutNode value) {
        unsigned calls = 0;
        require_throws<std::invalid_argument>([&] { gui::compose_layout(value, {0, 0, 100, 100}, [&](std::string_view, double) {
            ++calls; return gui::Size{};
        }); }, "Invalid declaration accepted");
        require(calls == 0, "Invalid tree reached native measurement");
    };
    auto duplicate = root;
    duplicate.children.back().id = duplicate.children.front().id;
    invalid(duplicate);
    auto missing = root;
    missing.id.clear(); invalid(missing);
    auto wrong_leaf = root;
    wrong_leaf.kind = gui::LayoutKind::leaf; invalid(wrong_leaf);
    for (const auto value : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), gui::coordinate_limit + 1}) {
        auto changed = root; changed.width = value; invalid(changed);
        changed = root; changed.height = value; invalid(changed);
        changed = root; changed.padding.left = value; invalid(changed);
        changed = root; changed.gap = value; invalid(changed);
        changed = root; changed.weight = value; invalid(changed);
    }
    auto unknown = root; unknown.kind = static_cast<gui::LayoutKind>(99); invalid(unknown);
    require_throws<std::invalid_argument>([&] { gui::compose_layout(root, {0, 0, 100, 100}, {}); }, "Missing callback accepted");
    require_throws<std::invalid_argument>([&] { gui::compose_layout(root, {0, 0, -1, 100}, metric); }, "Invalid viewport accepted");
    for (const auto bad : {gui::Size{-1, 0}, gui::Size{0, -1}, gui::Size{0, std::numeric_limits<double>::infinity()}, gui::Size{gui::coordinate_limit + 1, 0}})
        require_throws<std::invalid_argument>([&] { gui::compose_layout(root, {0, 0, 100, 100}, [bad](std::string_view, double) { return bad; }); }, "Invalid measured size accepted");

    auto deep = leaf("last");
    for (std::size_t n = 0; n < gui::layout_depth_limit; ++n)
        deep = group("level" + std::to_string(n), gui::LayoutKind::column, {std::move(deep)});
    invalid(deep);
    auto wide = group("wide", gui::LayoutKind::column, {});
    wide.children.reserve(gui::layout_node_limit);
    for (std::size_t n = 0; n < gui::layout_node_limit; ++n) wide.children.push_back(leaf("item" + std::to_string(n)));
    invalid(wide);

    auto changed_box = gui::compose_layout(root, {0, 0, 100, 100}, metric);
    changed_box.children[0].bounds.width = -1;
    require_throws<std::invalid_argument>([&] { gui::flatten_layout(changed_box); }, "Flatten accepted invalid bounds");
    changed_box = gui::compose_layout(root, {0, 0, 100, 100}, metric);
    changed_box.children[0].id = changed_box.id;
    require_throws<std::invalid_argument>([&] { gui::flatten_layout(changed_box); }, "Flatten accepted duplicate identities");
}

} // namespace

int main() {
    try {
        measured_columns_and_clipping();
        weighted_rows_and_nested_height();
        wrapped_measurement_and_recomposition();
        empty_zero_and_constrained_areas();
        shared_allocation_and_tiny_weights();
        fractional_coordinate_boundary();
        translated_parent_containment();
        bounds_and_input_validation();
        std::cout << "Composed layout checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
