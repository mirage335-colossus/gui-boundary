#include "gui/bitmap.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using namespace gui;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
template<class Error, class Function>
void rejects(Function function, std::string_view message) {
    try { function(); }
    catch (const Error&) { return; }
    throw std::runtime_error(std::string(message));
}

void storage_validation() {
    check(pixel_row_bytes(0, PixelFormat::mono1) == 0, "zero row bytes");
    check(pixel_row_bytes(1, PixelFormat::mono1) == 1, "one packed pixel");
    check(pixel_row_bytes(8, PixelFormat::mono1) == 1, "eight packed pixels");
    check(pixel_row_bytes(9, PixelFormat::mono1) == 2, "packed row rounding");
    check(pixel_row_bytes(9, PixelFormat::gray8) == 9, "gray row bytes");
    check(pixel_row_bytes(9, PixelFormat::rgb24) == 27, "RGB row bytes");
    rejects<std::invalid_argument>([] { pixel_row_bytes(0, static_cast<PixelFormat>(99)); },
                                   "unknown format accepted");
    const std::array<std::uint8_t, 5> bytes{10, 20, 99, 30, 40};
    const PixelBlock padded{2, 2, 3, PixelFormat::gray8, bytes};
    check(validate_pixel_block(padded) == 5, "last row should need no padding");
    rejects<std::invalid_argument>([&] {
        auto block = padded; block.bytes = block.bytes.first(4); validate_pixel_block(block);
    }, "short storage accepted");
    rejects<std::invalid_argument>([&] {
        auto block = padded; block.stride_bytes = 1; validate_pixel_block(block);
    }, "short stride accepted");
    rejects<std::length_error>([&] {
        auto block = padded; block.height = 3;
        block.stride_bytes = std::numeric_limits<std::size_t>::max(); validate_pixel_block(block);
    }, "row multiplication overflow accepted");
    rejects<std::length_error>([&] {
        auto block = padded;
        block.stride_bytes = std::numeric_limits<std::size_t>::max(); validate_pixel_block(block);
    }, "last row addition overflow accepted");
    rejects<std::length_error>([] {
        BitmapImage huge(std::numeric_limits<unsigned>::max(),
                         std::numeric_limits<unsigned>::max(), PixelFormat::rgb24);
    }, "owning allocation overflow accepted");
    check(validate_pixel_block({0, 5, 0, PixelFormat::gray8, {}}) == 0, "empty width needs storage");
    check(validate_pixel_block({5, 0, 0, PixelFormat::gray8, {}}) == 0, "empty height needs storage");
    BitmapImage image(4, 3, PixelFormat::gray8);
    image.blit(1, 1, padded);
    check(image.pixels() == std::vector<std::uint8_t>({0,0,0,0, 0,10,20,0, 0,30,40,0}),
          "padded transfer changed surrounding pixels");
    const auto before = image.pixels();
    rejects<std::out_of_range>([&] { image.blit(3, 1, padded); }, "out of bounds blit accepted");
    rejects<std::out_of_range>([&] { image.clear({3, 1, 2, 1}); }, "out of bounds clear accepted");
    rejects<std::out_of_range>([&] {
        image.blit(std::numeric_limits<unsigned>::max(), 0, padded);
    }, "placement overflow accepted");
    check(image.pixels() == before, "rejected operation changed storage");
    image.blit(4, 3, {0, 0, 0, PixelFormat::gray8, {}});
    image.clear({4, 3, 0, 0});
}

void conversion_and_overlap() {
    const std::array<std::uint8_t, 1> binary{0x40};
    const std::array<std::uint8_t, 2> neutral{0, 255};
    const std::array<std::uint8_t, 6> rgb_neutral{0,0,0,255,255,255};
    const std::array<PixelBlock, 3> inputs{{
        {2,1,1,PixelFormat::mono1,binary}, {2,1,2,PixelFormat::gray8,neutral},
        {2,1,6,PixelFormat::rgb24,rgb_neutral}
    }};
    for (const auto input : inputs)
        for (const auto target : {PixelFormat::mono1, PixelFormat::gray8, PixelFormat::rgb24}) {
            BitmapImage converted(2, 1, target); converted.blit(0, 0, input);
            const auto expected = target == PixelFormat::mono1 ? std::vector<std::uint8_t>{0x40} :
                target == PixelFormat::gray8 ? std::vector<std::uint8_t>{0,255} :
                std::vector<std::uint8_t>{0,0,0,255,255,255};
            check(converted.pixels() == expected, "format conversion changed black or white");
        }
    std::array<std::uint8_t, 5> gray{0, 127, 128, 255, 200};
    BitmapImage mono(13, 1, PixelFormat::mono1);
    mono.blit(5, 0, {4, 1, 5, PixelFormat::gray8, gray});
    check(mono.pixels() == std::vector<std::uint8_t>({1, 128}), "packed threshold or bit placement");
    mono.clear({7, 0, 1, 1});
    check(mono.pixels() == std::vector<std::uint8_t>({0, 128}), "packed clear changed adjacent bits");
    const std::array<std::uint8_t, 5> packed{0x80, 0xff, 9, 0x01, 0x80};
    BitmapImage expanded(9, 2, PixelFormat::rgb24);
    expanded.blit(0, 0, {9, 2, 3, PixelFormat::mono1, packed});
    check(expanded.pixels()[0] == 255 && expanded.pixels()[3] == 0 &&
          expanded.pixels()[24] == 255 && expanded.pixels()[27 + 21] == 255,
          "packed transfer lost row padding or bit order");
    check(expanded.pixels()[1] == 255 && expanded.pixels()[2] == 255,
          "gray expansion has unequal RGB channels");
    const std::array<std::uint8_t, 9> primary{255,0,0, 0,255,0, 0,0,255};
    BitmapImage luminance(3, 1, PixelFormat::gray8);
    luminance.blit(0, 0, {3, 1, 9, PixelFormat::rgb24, primary});
    check(luminance.pixels() == std::vector<std::uint8_t>({77,149,29}), "RGB conversion changed");
    BitmapImage owned(5, 1, PixelFormat::gray8);
    owned.blit(0, 0, {5, 1, 5, PixelFormat::gray8, gray});
    gray.fill(0);
    check(owned.pixels()[4] == 200, "source storage retained");
    auto aliased = owned.block(); aliased.width = 4;
    owned.blit(1, 0, aliased);
    check(owned.pixels() == std::vector<std::uint8_t>({0,0,127,128,255}), "horizontal overlap corrupted input");
    BitmapImage vertical(2, 3, PixelFormat::gray8);
    const std::array<std::uint8_t, 6> six{1,2,3,4,5,6};
    vertical.blit(0, 0, {2, 3, 2, PixelFormat::gray8, six});
    aliased = vertical.block(); aliased.height = 2;
    vertical.blit(0, 1, aliased);
    check(vertical.pixels() == std::vector<std::uint8_t>({1,2,1,2,3,4}), "vertical overlap corrupted input");
    BitmapImage bits(8, 1, PixelFormat::mono1);
    const std::array<std::uint8_t, 1> initial{0xb0};
    bits.blit(0, 0, {8,1,1,PixelFormat::mono1,initial});
    aliased = bits.block(); aliased.width = 4;
    bits.blit(2, 0, aliased);
    check(bits.pixels() == std::vector<std::uint8_t>{0xac}, "unaligned packed overlap corrupted input");
}

BitmapImage collect(const BitmapSource& source, BitmapRequest request) {
    BitmapImage result(request.width, request.height, request.format);
    source.paint(request, [&](unsigned x, unsigned y, PixelBlock block) { result.blit(x, y, block); });
    return result;
}

void source_contract() {
    BitmapImage original(23, 17);
    std::vector<std::uint8_t> bytes(23 * 17 * 3);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>((i * 17) % 256);
    original.blit(0, 0, {23, 17, 23 * 3, PixelFormat::rgb24, bytes});
    auto source = image_bitmap(original);
    original.clear({0, 0, 23, 17});
    auto retained = source;
    source = {};
    check(collect(retained, full_bitmap_request(23, 17)).pixels() == bytes, "source snapshot was not retained");
    for (auto format : {PixelFormat::mono1, PixelFormat::gray8, PixelFormat::rgb24}) {
        const auto whole = collect(retained, full_bitmap_request(23, 17, format));
        BitmapImage tiled(23, 17, format);
        for (int y = 16; y >= 0; y -= 4) {
            const auto top = static_cast<unsigned>(std::max(0, y - 3));
            for (unsigned x = 0; x < 23; x += 5) {
                const BitmapRequest request{23, 17, {x, top, std::min(5u, 23 - x),
                                                   static_cast<unsigned>(y) - top + 1}, format};
                retained.paint(request, [&](unsigned px, unsigned py, PixelBlock block) {
                    check(block.height == 1, "snapshot helper used more than row storage");
                    tiled.blit(px, py, block);
                });
            }
        }
        check(tiled.pixels() == whole.pixels(), "disjoint repaint differs from complete repaint");
    }
    rejects<std::invalid_argument>([&] { collect(retained, full_bitmap_request(24, 17)); },
                                   "image helper implicitly resized content");
    rejects<std::invalid_argument>([&] { retained.paint(full_bitmap_request(23, 17), {}); },
                                   "missing sink accepted");
    rejects<std::out_of_range>([&] {
        retained.paint({23, 17, {22, 0, 2, 1}, PixelFormat::gray8}, {});
    }, "invalid damage accepted");
    rejects<std::out_of_range>([&] {
        retained.paint({23, 17, {std::numeric_limits<unsigned>::max(), 0, 2, 1}, PixelFormat::gray8}, {});
    }, "damage overflow accepted");
    unsigned calls = 0;
    const BitmapSource count([&](const BitmapRequest&, const BitmapSink&) { ++calls; });
    count.paint({4, 3, {0, 0, 0, 3}, PixelFormat::gray8}, {});
    count.paint(full_bitmap_request(0, 3), {});
    check(calls == 0, "empty damage called producer");
    const auto gray = collect(solid_bitmap(128, 128, 128), full_bitmap_request(9, 2, PixelFormat::mono1));
    check(gray.pixels() == std::vector<std::uint8_t>({255,128,255,128}), "solid source packed padding");
}

void source_release_during_paint() {
    auto content = std::make_shared<const std::uint8_t>(42);
    const std::weak_ptr<const std::uint8_t> lifetime = content;
    BitmapSource source([content](const BitmapRequest&, const BitmapSink& sink) {
        sink(0, 0, {1, 1, 1, PixelFormat::gray8, {content.get(), 1}});
        // A producer can emit multiple blocks after the receiver releases its
        // original handle; all immutable captures must still be usable.
        sink(1, 0, {1, 1, 1, PixelFormat::gray8, {content.get(), 1}});
    });
    content.reset();
    unsigned calls = 0;
    source.paint(full_bitmap_request(2, 1, PixelFormat::gray8), [&](unsigned, unsigned, PixelBlock block) {
        source = {};
        check(!lifetime.expired(), "releasing a source destroyed an executing producer");
        check(block.bytes[0] == 42, "released source lost its owned pixels");
        ++calls;
    });
    check(calls == 2 && lifetime.expired(), "paint did not release its temporary producer retention");
    source.paint(full_bitmap_request(2, 1, PixelFormat::gray8), {});
}

void surface_lifecycle() {
    BitmapSurface surface;
    check(!surface.repaint() && !surface.dirty(), "default surface should be clean");
    check(surface.set_source("image-a", 1, solid_bitmap(30, 30, 30)), "initial source ignored");
    check(surface.resize(8, 6, PixelFormat::gray8) && surface.dirty(), "resize omitted invalidation");
    check(surface.repaint() && !surface.dirty(), "initial repaint did not commit");
    check(surface.image().pixels() == std::vector<std::uint8_t>(48, 30), "initial source missing");
    check(!surface.set_source("image-a", 1, solid_bitmap(90, 90, 90)), "equal identity was replaced");
    check(!surface.repaint(), "unchanged surface repainted");
    surface.invalidate({8, 6, 0, 0});
    check(!surface.dirty(), "empty invalidation dirtied surface");
    std::vector<BitmapRequest> requests;
    auto source = BitmapSource([&](const BitmapRequest& request, const BitmapSink& sink) {
        requests.push_back(request);
        solid_bitmap(60, 60, 60).paint(request, sink);
    });
    surface.set_source("image-a", 2, source);
    surface.repaint(); requests.clear();
    surface.invalidate({1, 2, 2, 1}); surface.invalidate({4, 1, 2, 3});
    surface.repaint();
    check(requests.size() == 1 && requests[0].damage == PixelRect{1,1,5,3}, "damage did not coalesce");
    check(surface.resize(9, 6, PixelFormat::gray8), "new grid ignored");
    surface.repaint();
    check(requests.back().width == 9 && requests.back().damage == PixelRect{0,0,9,6}, "resize reused stale grid");
    check(surface.resize(9, 6, PixelFormat::mono1), "new format ignored"); surface.repaint();
    check(requests.back().format == PixelFormat::mono1, "format change was not requested");
    check(!surface.resize(9, 6, PixelFormat::mono1), "identical grid invalidated");
    surface.resize(0, 6); const auto count = requests.size();
    check(!surface.repaint() && requests.size() == count, "zero width requested synthetic pixels");
}

void replacement_and_failures() {
    BitmapSurface surface;
    surface.resize(4, 2, PixelFormat::gray8);
    surface.set_source("base", 1, solid_bitmap(10, 10, 10)); surface.repaint();
    const auto before = surface.image().pixels();
    surface.set_source("failure", 1, BitmapSource([](const BitmapRequest& request, const BitmapSink& sink) {
        solid_bitmap(200, 200, 200).paint(request, sink);
        throw std::runtime_error("injected producer failure");
    }));
    for (int attempt = 0; attempt < 2; ++attempt) {
        rejects<std::runtime_error>([&] { surface.repaint(); }, "producer failure was lost");
        check(surface.image().pixels() == before && surface.dirty(), "failed repaint committed pixels or discarded damage");
    }
    surface.set_source("sparse", 1, BitmapSource([](const BitmapRequest& request, const BitmapSink& sink) {
        const std::array<std::uint8_t, 1> first{70}, second{90};
        sink(request.damage.x, request.damage.y, {1, 1, 1, request.format, first});
        sink(request.damage.x, request.damage.y, {1, 1, 1, request.format, second});
    }));
    surface.repaint();
    check(surface.image().pixels() == std::vector<std::uint8_t>({90,0,0,0,0,0,0,0}),
          "sparse replacement or last-write behavior changed");
    surface.invalidate({2, 1, 2, 1}); surface.repaint();
    check(surface.image().pixels() == std::vector<std::uint8_t>({90,0,0,0,0,0,90,0}),
          "partial repaint changed pixels outside damage");
    const auto stable = surface.image().pixels();
    surface.set_source("bad-output", 1, BitmapSource([](const BitmapRequest& request, const BitmapSink& sink) {
        const std::array<std::uint8_t, 1> value{90};
        sink(request.width, 0, {1, 1, 1, request.format, value});
    }));
    rejects<std::out_of_range>([&] { surface.repaint(); }, "out of bounds producer output accepted");
    check(surface.image().pixels() == stable && surface.dirty(), "invalid producer output damaged surface");
    surface.set_source("wrong-format", 1, solid_bitmap(10, 10, 10)); surface.repaint();
    surface.set_source("wrong-format", 2, BitmapSource([](const BitmapRequest&, const BitmapSink& sink) {
        const std::array<std::uint8_t, 3> value{1,2,3};
        sink(0, 0, {1, 1, 3, PixelFormat::rgb24, value});
    }));
    rejects<std::invalid_argument>([&] { surface.repaint(); }, "unrequested output format accepted");
    surface.set_source("reentrant", 1, BitmapSource([&](const BitmapRequest&, const BitmapSink&) {
        surface.invalidate({0,0,1,1});
    }));
    rejects<std::logic_error>([&] { surface.repaint(); }, "mutation during paint accepted");
    surface.set_source("blank", 1, {}); surface.repaint();
    check(surface.image().pixels() == std::vector<std::uint8_t>(8, 0), "empty source retained old pixels");
    surface.set_source("outside-damage", 1, BitmapSource([](const BitmapRequest& request, const BitmapSink& sink) {
        if (request.damage.width == request.width) return;
        const std::array<std::uint8_t, 1> value{1};
        sink(0, 0, {1, 1, 1, request.format, value});
    }));
    surface.repaint(); surface.invalidate({1,1,1,1});
    rejects<std::out_of_range>([&] { surface.repaint(); }, "producer wrote outside partial damage");
}

void copy_move_lifetime() {
    BitmapImage first(2, 1, PixelFormat::gray8);
    const std::array<std::uint8_t, 2> values{12, 34};
    first.blit(0, 0, {2,1,2,PixelFormat::gray8,values});
    BitmapImage second(std::move(first));
    check(second.pixels() == std::vector<std::uint8_t>({12,34}) && first.width() == 0 &&
          first.height() == 0 && first.pixels().empty(), "image move left invalid source storage");
    first.clear({}); first.blit(0,0,{});
    rejects<std::out_of_range>([&] { first.clear({0,0,1,1}); }, "moved image allowed stale geometry");
    first = BitmapImage(1,1); first = std::move(second);
    check(first.width() == 2 && second.width() == 0 && second.pixels().empty(), "image move assignment did not reset source");
    second = first; second.clear({0,0,2,1});
    check(first.pixels() == std::vector<std::uint8_t>({12,34}), "image copy shared mutable storage");

    BitmapSurface source;
    source.resize(3,2,PixelFormat::gray8); source.set_source("fill",1,solid_bitmap(40,40,40));
    BitmapSurface moved(std::move(source));
    check(source.image().width() == 0 && !source.dirty() && !source.repaint(), "surface move left stale geometry or damage");
    check(moved.repaint() && moved.image().pixels() == std::vector<std::uint8_t>(6,40), "surface move lost pending content");
    source.resize(1,1,PixelFormat::gray8);
    check(source.repaint() && source.image().pixels() == std::vector<std::uint8_t>{0}, "moved surface was not reusable");
    source = std::move(moved);
    check(moved.image().width() == 0 && !moved.dirty() && source.image().width() == 3,
          "surface move assignment did not reset source");
    BitmapSurface copied(source); copied.set_source("blank",1,{}); copied.repaint();
    check(source.image().pixels() == std::vector<std::uint8_t>(6,40), "surface copy shared mutable storage");
    moved = source; check(moved.image().pixels() == source.image().pixels(), "surface copy assignment lost storage");

    const auto before = source.image().pixels();
    const auto other_before = moved.image().pixels();
    const std::vector<std::function<void()>> operations{
        [&] { BitmapSurface copy(source); }, [&] { BitmapSurface copy(std::move(source)); },
        [&] { source = moved; }, [&] { source = std::move(moved); },
        [&] { moved = source; }, [&] { moved = std::move(source); }
    };
    std::uint64_t revision = 0;
    for (const auto& operation : operations) {
        source.set_source("guard",++revision,BitmapSource([&](const BitmapRequest&,const BitmapSink&) { operation(); }));
        rejects<std::logic_error>([&] { source.repaint(); }, "copy or move bypassed the active paint guard");
        check(source.image().pixels() == before && source.dirty() && moved.image().pixels() == other_before,
              "rejected surface transfer changed either object");
    }
    source.set_source("recovered",1,solid_bitmap(80,80,80));
    check(source.repaint() && source.image().pixels()[0] == 80, "surface transfer guard blocked recovery");
}
} // namespace

int main() {
    try {
        storage_validation(); conversion_and_overlap(); source_contract(); source_release_during_paint();
        surface_lifecycle(); replacement_and_failures(); copy_move_lifetime();
        std::cout << "Bitmap contract checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
