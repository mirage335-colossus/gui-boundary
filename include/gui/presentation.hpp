#pragma once
#include "contract.hpp"

namespace gui {
// Shared transient chrome stays in logical client coordinates, just like the
// application snapshot. Native and software hosts consume the same geometry.
inline double popup_row_height(Size client,std::size_t count,double natural=24) {
    return count?std::min(natural,std::max(1.0,client.height/double(count))):natural;
}
inline Rect popup_bounds(Size client,Rect anchor,std::size_t count,double row_height) {
    const double width=std::min(client.width,std::max(180.0,anchor.width));
    const double height=double(count)*row_height;
    return {std::clamp(anchor.x,0.0,std::max(0.0,client.width-width)),
        std::clamp(anchor.y+anchor.height,0.0,std::max(0.0,client.height-height)),width,height};
}
inline Rect prompt_bounds(Size client) {
    const double width=std::min(440.0,client.width),height=std::min(144.0,client.height);
    return {(client.width-width)/2,(client.height-height)/2,width,height};
}
inline Rect prompt_field_bounds(Size client) {
    const auto box=prompt_bounds(client);return intersect(box,{box.x+12,box.y+46,std::max(0.0,box.width-24),28});
}
inline Rect prompt_cancel_bounds(Size client) {
    const auto box=prompt_bounds(client);return intersect(box,{box.x+12,box.y+box.height-36,90,24});
}
inline Rect prompt_accept_bounds(Size client) {
    const auto box=prompt_bounds(client);return intersect(box,{box.x+box.width-102,box.y+box.height-36,90,24});
}
inline Color tone_color(const Palette& palette,Tone tone) {
    switch(tone) {case Tone::muted:return palette.muted;case Tone::accent:return palette.accent;case Tone::error:return palette.error;default:return palette.text;}
}
struct PageTab {std::string id,label;Rect bounds;bool enabled=true,selected=false;};
// Chrome is computed once in logical client coordinates. Hosts render these
// rectangles; they never choose page sizes or positions from application IDs.
inline std::vector<PageTab> page_tabs(const Snapshot& snapshot) {
    std::vector<PageTab> result;
    const auto count=std::count_if(snapshot.pages.begin(),snapshot.pages.end(),[](const Page& p){return p.visible;});
    if(count==0||!has_area(snapshot.page_bar))return result;
    const auto width=snapshot.page_bar.width/double(count);
    for(const auto& page:snapshot.pages)if(page.visible)
        result.push_back({page.id,page.label,{snapshot.page_bar.x+double(result.size())*width,snapshot.page_bar.y,width,snapshot.page_bar.height},
            page.enabled&&!snapshot.modal_root,snapshot.active_page==page.id});
    return result;
}
inline std::string visible_text(const Widget& widget) {
    switch(widget.spec.kind) {
    case Kind::label:return widget.state.text;
    case Kind::text:return widget.state.text.empty()?widget.state.placeholder:widget.state.text;
    case Kind::choice:
        if(!widget.state.display_text.empty())return widget.state.display_text;
        for(const auto& option:widget.state.options)if(widget.state.selected==option.id)return option.label;
        return widget.state.placeholder;
    default:return widget.state.label;
    }
}
inline std::vector<WidgetKey> paint_order(const Snapshot& snapshot) {
    std::vector<WidgetKey> result;result.reserve(snapshot.widgets.size());
    for(const auto& widget:snapshot.widgets)if(!snapshot.modal_root||!in_modal_scope(snapshot,widget.spec.key))result.push_back(widget.spec.key);
    if(snapshot.modal_root)for(const auto& widget:snapshot.widgets)if(in_modal_scope(snapshot,widget.spec.key))result.push_back(widget.spec.key);
    return result;
}
}
