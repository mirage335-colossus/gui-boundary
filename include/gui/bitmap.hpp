#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui {

// All coordinates use a top-left origin, x to the right and y downward.
// Rectangles are half-open. A width or height of zero denotes an empty area.
enum class PixelFormat { mono1, gray8, rgb24 };
struct PixelRect {
    unsigned x = 0, y = 0, width = 0, height = 0;
    bool operator==(const PixelRect&) const = default;
};
// Rows run from top to bottom; stride is positive and may include padding.
// Mono1 stores the leftmost pixel in the high bit; 1 is white, 0 is black.
// Gray8 is one byte per pixel. RGB24 stores opaque R, G, B bytes in that order.
// bytes is borrowed for the synchronous call only. The last row needs no padding.
struct PixelBlock {
    unsigned width = 0, height = 0;
    std::size_t stride_bytes = 0;
    PixelFormat format = PixelFormat::gray8;
    std::span<const std::uint8_t> bytes;
};
struct BitmapRequest {
    unsigned width = 0, height = 0;
    PixelRect damage;
    PixelFormat format = PixelFormat::rgb24;
};
inline BitmapRequest full_bitmap_request(unsigned width, unsigned height,
                                         PixelFormat format = PixelFormat::rgb24) {
    return {width, height, {0, 0, width, height}, format};
}

namespace bitmap_detail {
inline std::size_t multiply(std::size_t a, std::size_t b) {
    if (b && a > std::numeric_limits<std::size_t>::max() / b)
        throw std::length_error("bitmap size overflow");
    return a * b;
}
inline std::size_t add(std::size_t a, std::size_t b) {
    if (a > std::numeric_limits<std::size_t>::max() - b)
        throw std::length_error("bitmap size overflow");
    return a + b;
}
inline bool empty(PixelRect area) { return area.width == 0 || area.height == 0; }
inline void contained(PixelRect inner, PixelRect outer) {
    if (inner.x < outer.x || inner.y < outer.y ||
        inner.x - outer.x > outer.width || inner.y - outer.y > outer.height ||
        inner.width > outer.width - (inner.x - outer.x) ||
        inner.height > outer.height - (inner.y - outer.y))
        throw std::out_of_range("bitmap rectangle is outside its bounds");
}
using Rgb = std::array<std::uint8_t, 3>;
inline Rgb read(const std::uint8_t* row, unsigned x, PixelFormat format) {
    if (format == PixelFormat::rgb24) {
        const auto i = std::size_t{x} * 3;
        return {row[i], row[i + 1], row[i + 2]};
    }
    const auto value = format == PixelFormat::gray8 ? row[x] :
        static_cast<std::uint8_t>((row[x / 8] & (0x80u >> (x % 8))) ? 255 : 0);
    return {value, value, value};
}
inline void write(std::uint8_t* row, unsigned x, PixelFormat format, Rgb rgb) {
    if (format == PixelFormat::rgb24) {
        const auto i = std::size_t{x} * 3;
        std::copy(rgb.begin(), rgb.end(), row + i);
        return;
    }
    // Fixed integer conversion; neutral grays remain unchanged.
    const auto gray = static_cast<std::uint8_t>(
        (77u * rgb[0] + 150u * rgb[1] + 29u * rgb[2] + 128u) / 256u);
    if (format == PixelFormat::gray8) row[x] = gray;
    else {
        const auto bit = static_cast<std::uint8_t>(0x80u >> (x % 8));
        if (gray >= 128) row[x / 8] |= bit;
        else row[x / 8] &= static_cast<std::uint8_t>(~bit);
    }
}
} // namespace bitmap_detail

inline std::size_t pixel_row_bytes(unsigned width, PixelFormat format) {
    switch (format) {
    case PixelFormat::mono1: return width / 8u + (width % 8u != 0);
    case PixelFormat::gray8: return width;
    case PixelFormat::rgb24: return bitmap_detail::multiply(width, 3);
    }
    throw std::invalid_argument("unknown bitmap format");
}
// Returns the minimum readable byte count, including inter-row padding.
// Empty blocks require zero storage; their stride is ignored.
inline std::size_t validate_pixel_block(PixelBlock block) {
    const auto row = pixel_row_bytes(block.width, block.format);
    if (!block.width || !block.height) return 0;
    if (block.stride_bytes < row) throw std::invalid_argument("bitmap stride is too short");
    const auto needed = bitmap_detail::add(
        bitmap_detail::multiply(block.height - 1u, block.stride_bytes), row);
    if (block.bytes.size() < needed) throw std::invalid_argument("bitmap storage is too short");
    return needed;
}
inline void validate_bitmap_request(const BitmapRequest& request) {
    (void)pixel_row_bytes(request.width, request.format);
    bitmap_detail::contained(request.damage, {0, 0, request.width, request.height});
}

using BitmapSink = std::function<void(unsigned x, unsigned y, PixelBlock)>;

// Copyable retained handle. Producers must capture immutable owned data and
// produce the same pixels for the same full grid and requested format. Damage
// limits output, never changes the coordinate system. Sink calls may overlap;
// later calls replace earlier pixels. Every block must match the requested format.
// Neither producer nor receiver may retain the sink or borrowed pixel spans.
// Calls are synchronous and serialized by the caller. A default source is blank.
class BitmapSource {
public:
    using Paint = std::function<void(const BitmapRequest&, const BitmapSink&)>;
    BitmapSource() = default;
    explicit BitmapSource(Paint paint) {
        if (paint) paint_ = std::make_shared<const Paint>(std::move(paint));
    }
    void paint(const BitmapRequest& request, const BitmapSink& sink) const {
        validate_bitmap_request(request);
        if (bitmap_detail::empty(request.damage) || !paint_) return;
        if (!sink) throw std::invalid_argument("bitmap sink is empty");
        (*paint_)(request, [&](unsigned x, unsigned y, PixelBlock block) {
            validate_pixel_block(block);
            bitmap_detail::contained({x, y, block.width, block.height}, request.damage);
            if (block.format != request.format)
                throw std::invalid_argument("bitmap producer returned an unrequested format");
            if (block.width && block.height) sink(x, y, block);
        });
    }
private:
    std::shared_ptr<const Paint> paint_;
};

// Owning tightly packed storage. New pixels are black. blit validates before
// writing, copies immediately, permits padded input, and supports all format
// conversions. Overlapping input from this image is safe: the input rectangle
// is copied before destination writes begin. Blits never scale or clip.
class BitmapImage {
public:
    BitmapImage() = default;
    BitmapImage(const BitmapImage&) = default;
    BitmapImage& operator=(const BitmapImage& other) {
        if (this != &other) {
            BitmapImage copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
    BitmapImage(BitmapImage&& other) noexcept
        : width_(std::exchange(other.width_, 0)), height_(std::exchange(other.height_, 0)),
          format_(std::exchange(other.format_, PixelFormat::rgb24)),
          stride_(std::exchange(other.stride_, 0)), pixels_(std::move(other.pixels_)) {
        other.pixels_.clear();
    }
    BitmapImage& operator=(BitmapImage&& other) noexcept {
        if (this != &other) {
            width_ = std::exchange(other.width_, 0); height_ = std::exchange(other.height_, 0);
            format_ = std::exchange(other.format_, PixelFormat::rgb24);
            stride_ = std::exchange(other.stride_, 0); pixels_ = std::move(other.pixels_);
            other.pixels_.clear();
        }
        return *this;
    }
    BitmapImage(unsigned width, unsigned height, PixelFormat format = PixelFormat::rgb24)
        : width_(width), height_(height), format_(format),
          stride_(pixel_row_bytes(width, format)),
          pixels_(bitmap_detail::multiply(stride_, height), 0) {}
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }
    PixelFormat format() const { return format_; }
    const std::vector<std::uint8_t>& pixels() const { return pixels_; }
    PixelBlock block() const { return {width_, height_, stride_, format_, pixels_}; }
    void blit(unsigned x, unsigned y, PixelBlock input) {
        validate_pixel_block(input);
        bitmap_detail::contained({x, y, input.width, input.height}, {0, 0, width_, height_});
        if (!input.width || !input.height) return;
        const auto row_bytes = pixel_row_bytes(input.width, input.format);
        std::vector<std::uint8_t> copy(bitmap_detail::multiply(row_bytes, input.height));
        for (unsigned row = 0; row < input.height; ++row)
            std::copy_n(input.bytes.data() + input.stride_bytes * row, row_bytes,
                        copy.data() + row_bytes * row);
        for (unsigned row = 0; row < input.height; ++row)
            for (unsigned col = 0; col < input.width; ++col)
                bitmap_detail::write(pixels_.data() + stride_ * (y + row), x + col, format_,
                    bitmap_detail::read(copy.data() + row_bytes * row, col, input.format));
    }
    void clear(PixelRect area) {
        bitmap_detail::contained(area, {0, 0, width_, height_});
        if (bitmap_detail::empty(area)) return;
        for (unsigned row = 0; row < area.height; ++row)
            for (unsigned col = 0; col < area.width; ++col)
                bitmap_detail::write(pixels_.data() + stride_ * (area.y + row),
                                     area.x + col, format_, {0, 0, 0});
    }
private:
    unsigned width_ = 0, height_ = 0;
    PixelFormat format_ = PixelFormat::rgb24;
    std::size_t stride_ = 0;
    std::vector<std::uint8_t> pixels_;
};

// A source that fills every requested pixel, with bounded one-row scratch space.
inline BitmapSource solid_bitmap(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return BitmapSource([rgb = bitmap_detail::Rgb{red, green, blue}]
        (const BitmapRequest& request, const BitmapSink& sink) {
        const auto area = request.damage;
        std::vector<std::uint8_t> row(pixel_row_bytes(area.width, request.format), 0);
        for (unsigned x = 0; x < area.width; ++x)
            bitmap_detail::write(row.data(), x, request.format, rgb);
        for (unsigned y = 0; y < area.height; ++y)
            sink(area.x, area.y + y, {area.width, 1, row.size(), request.format, row});
    });
}

// Owns a snapshot by value. Grid dimensions must match; resizing is an explicit
// producer concern. Reads and converts only requested pixels, one row at a time.
inline BitmapSource image_bitmap(BitmapImage image) {
    return BitmapSource([image = std::make_shared<const BitmapImage>(std::move(image))]
        (const BitmapRequest& request, const BitmapSink& sink) {
        if (request.width != image->width() || request.height != image->height())
            throw std::invalid_argument("bitmap image and requested grid differ");
        const auto input = image->block();
        const auto area = request.damage;
        std::vector<std::uint8_t> row(pixel_row_bytes(area.width, request.format), 0);
        for (unsigned y = 0; y < area.height; ++y) {
            std::fill(row.begin(), row.end(), 0);
            for (unsigned x = 0; x < area.width; ++x)
                bitmap_detail::write(row.data(), x, request.format,
                    bitmap_detail::read(input.bytes.data() + input.stride_bytes * (area.y + y),
                                        area.x + x, input.format));
            sink(area.x, area.y + y, {area.width, 1, row.size(), request.format, row});
        }
    });
}

// A CPU reference surface. UI adapters present image() at 1:1 backing-pixel
// scale after a successful repaint. Invalidation may merge rectangles into a
// bounding box. Repaint clears damaged pixels to black, then applies the source.
// It commits only on success; on exceptions, previous pixels and damage survive.
// resize immediately replaces storage with a black grid and invalidates it.
// IDs are opaque; equal (ID, revision) means the exact same immutable content.
// Change either token when content changes. Geometry/format changes repaint even
// when these tokens stay equal. All access is serialized on one owning thread.
class BitmapSurface {
public:
    BitmapSurface() = default;
    BitmapSurface(const BitmapSurface& other) {
        other.ensure_idle();
        image_ = other.image_; source_ = other.source_;
        identity_ = other.identity_; damage_ = other.damage_;
    }
    BitmapSurface& operator=(const BitmapSurface& other) {
        ensure_idle(); other.ensure_idle();
        if (this != &other) {
            BitmapSurface copy(other);
            swap_state(copy);
        }
        return *this;
    }
    // Successful moves leave the source empty, clean, and reusable. Both sides
    // must be idle; even self-assignment is rejected during producer execution.
    BitmapSurface(BitmapSurface&& other) {
        other.ensure_idle();
        swap_state(other);
    }
    BitmapSurface& operator=(BitmapSurface&& other) {
        ensure_idle(); other.ensure_idle();
        if (this != &other) {
            BitmapSurface moved(std::move(other));
            swap_state(moved);
        }
        return *this;
    }
    bool set_source(std::string id, std::uint64_t revision, BitmapSource source) {
        ensure_idle();
        if (identity_ && identity_->first == id && identity_->second == revision) return false;
        identity_ = std::pair{std::move(id), revision};
        source_ = std::move(source);
        invalidate({0, 0, image_.width(), image_.height()});
        return true;
    }
    bool resize(unsigned width, unsigned height, PixelFormat format = PixelFormat::rgb24) {
        ensure_idle();
        if (image_.width() == width && image_.height() == height && image_.format() == format) return false;
        BitmapImage next(width, height, format);
        image_ = std::move(next);
        damage_.reset();
        invalidate({0, 0, width, height});
        return true;
    }
    void invalidate(PixelRect area) {
        ensure_idle();
        bitmap_detail::contained(area, {0, 0, image_.width(), image_.height()});
        if (bitmap_detail::empty(area)) return;
        if (!damage_) { damage_ = area; return; }
        const auto left = std::min(area.x, damage_->x), top = std::min(area.y, damage_->y);
        const auto right = std::max(area.x + area.width, damage_->x + damage_->width);
        const auto bottom = std::max(area.y + area.height, damage_->y + damage_->height);
        damage_ = PixelRect{left, top, right - left, bottom - top};
    }
    bool repaint() {
        ensure_idle();
        if (!damage_) return false;
        struct Guard {
            bool& flag;
            explicit Guard(bool& value) : flag(value) { flag = true; }
            ~Guard() { flag = false; }
        } guard(painting_);
        BitmapImage next = image_;
        next.clear(*damage_);
        source_.paint({image_.width(), image_.height(), *damage_, image_.format()},
            [&](unsigned x, unsigned y, PixelBlock block) { next.blit(x, y, block); });
        image_ = std::move(next);
        damage_.reset();
        return true;
    }
    const BitmapImage& image() const { return image_; }
    bool dirty() const { return damage_.has_value(); }
private:
    void swap_state(BitmapSurface& other) noexcept {
        using std::swap;
        swap(image_, other.image_); swap(source_, other.source_);
        swap(identity_, other.identity_); swap(damage_, other.damage_);
    }
    void ensure_idle() const {
        if (painting_) throw std::logic_error("bitmap surface mutation during repaint");
    }
    BitmapImage image_;
    BitmapSource source_;
    std::optional<std::pair<std::string, std::uint64_t>> identity_;
    std::optional<PixelRect> damage_;
    bool painting_ = false;
};

} // namespace gui
