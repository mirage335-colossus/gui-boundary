#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace gui {
// Logical client coordinates; all rectangles are half open.
struct Point { double x=0, y=0; bool operator==(const Point&) const = default; };
struct Size { double width=0, height=0; bool operator==(const Size&) const = default; };
struct Rect {
    double x=0, y=0, width=0, height=0;
    bool operator==(const Rect&) const = default;
};
inline constexpr double coordinate_limit=1000000;
inline bool valid_rect(Rect r) {
    return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&
        std::abs(r.x)<=coordinate_limit&&std::abs(r.y)<=coordinate_limit&&
        r.width>=0&&r.height>=0&&r.width<=coordinate_limit&&r.height<=coordinate_limit&&
        std::abs(r.x+r.width)<=coordinate_limit&&std::abs(r.y+r.height)<=coordinate_limit;
}
inline bool has_area(Rect r) { return r.width>0&&r.height>0; }
inline bool contains(Rect r,Point p) {
    return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;
}
namespace geometry_detail {
// Subtraction and re-addition can round to opposite sides of an endpoint,
// especially when a negative origin crosses zero. Keep the reconstructed
// endpoint inside the requested edge, without adding tolerance to the contract.
inline double extent_to(double origin,double end) {
    const auto extent=std::max(0.0,end-origin);
    return extent>0&&origin+extent>end?std::nextafter(extent,0.0):extent;
}
}
inline Rect intersect(Rect a,Rect b) {
    const auto x=std::max(a.x,b.x),y=std::max(a.y,b.y);
    return {x,y,geometry_detail::extent_to(x,std::min(a.x+a.width,b.x+b.width)),
                geometry_detail::extent_to(y,std::min(a.y+a.height,b.y+b.height))};
}
struct DeviceRect { int x=0,y=0; unsigned width=0,height=0; };
// Snap endpoints independently. Shared edges therefore share device coordinates.
inline DeviceRect device_rect(Rect r,double scale) {
    if(!valid_rect(r)||!std::isfinite(scale)||scale<=0||scale>16)
        throw std::invalid_argument("Invalid rectangle or display scale");
    const auto x=static_cast<int>(std::round(r.x*scale));
    const auto y=static_cast<int>(std::round(r.y*scale));
    const auto right=static_cast<int>(std::round((r.x+r.width)*scale));
    const auto bottom=static_cast<int>(std::round((r.y+r.height)*scale));
    return {x,y,static_cast<unsigned>(right-x),static_cast<unsigned>(bottom-y)};
}
// Native pointer -> logical client point -> local backing pixel. Captions are
// separate widgets and must not be included in the bitmap rectangle.
inline std::optional<Point> pixel_at(Rect area,unsigned width,unsigned height,Point logical) {
    if(!valid_rect(area)||!has_area(area)||!contains(area,logical)||!width||!height)return {};
    return Point{std::min(double(width-1),std::floor((logical.x-area.x)*width/area.width)),
                 std::min(double(height-1),std::floor((logical.y-area.y)*height/area.height))};
}
enum class Axis { horizontal, vertical };
struct Allocation { double fixed=0,weight=1; };
// Fixed sizes consume available space in order. Remaining space is shared by
// nonnegative weights. This helper emits absolute rectangles for the contract.
inline std::vector<Rect> arrange(Rect area,Axis axis,std::span<const Allocation> items,double gap=0) {
    if(!valid_rect(area)||(axis!=Axis::horizontal&&axis!=Axis::vertical)||
       !std::isfinite(gap)||gap<0||gap>coordinate_limit)
        throw std::invalid_argument("Invalid layout");
    double fixed=0,largest_weight=0;
    for(auto item:items) {
        if(!std::isfinite(item.fixed)||!std::isfinite(item.weight)||item.fixed<0||item.weight<0||
           item.fixed>coordinate_limit||item.weight>coordinate_limit)
            throw std::invalid_argument("Invalid allocation");
        fixed=std::min(coordinate_limit,fixed+item.fixed);
        if(item.fixed==0)largest_weight=std::max(largest_weight,item.weight);
    }
    // Normalize before summing and multiplying: tiny positive weights must
    // still share space even when extent * weight would underflow.
    double weight=0;
    if(largest_weight>0)
        for(auto item:items)if(item.fixed==0)weight+=item.weight/largest_weight;
    double extent=axis==Axis::horizontal?area.width:area.height;
    double flexible=std::max(0.0,extent-fixed-gap*double(items.empty()?0:items.size()-1));
    double cursor=0;
    std::vector<Rect> result;result.reserve(items.size());
    for(auto item:items) {
        double length=std::min(std::max(0.0,extent-cursor),
            item.fixed>0?item.fixed:(weight>0?flexible*((item.weight/largest_weight)/weight):0));
        const auto origin=(axis==Axis::horizontal?area.x:area.y)+cursor;
        const auto end=axis==Axis::horizontal?area.x+area.width:area.y+area.height;
        // Translation can round an endpoint past its parent by one ULP even
        // when the local allocation fits. Constrain against the absolute edge.
        length=std::min(length,geometry_detail::extent_to(origin,end));
        result.push_back(axis==Axis::horizontal?Rect{origin,area.y,length,area.height}:
            Rect{area.x,origin,area.width,length});
        cursor=std::min(extent,cursor+length+gap);
    }
    return result;
}
}
