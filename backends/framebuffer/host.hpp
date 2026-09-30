#pragma once
#include "../../examples/application.hpp"
#include "gui/framebuffer.hpp"
#include <fstream>
#include <stdexcept>

namespace framebuffer_example {
// Composition root: the renderer and the application share no feature IDs.
struct Session {
    gui::FramebufferAdapter adapter;
    Example application;
    Session():adapter([this](const gui::Event& event){application.handle(event);start_service();}),application(adapter) {}
    void start_service() {
        if(adapter.closed())return;
        if(auto request=application.next_service())
            adapter.service(std::move(*request),[this](gui::ServiceResult result){application.complete_service(std::move(result));});
    }
    void tick() {
        if(adapter.closed())return;
        application.retry_presentation();
        start_service();
    }
};
inline void write_ppm(const gui::Frame& frame,const std::string& path) {
    std::ofstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open framebuffer output");
    file<<"P6\n"<<frame.width<<' '<<frame.height<<"\n255\n";
    if(frame.pixels)file.write(reinterpret_cast<const char*>(frame.pixels->data()),static_cast<std::streamsize>(frame.pixels->size()));
    if(!file)throw std::runtime_error("Cannot write framebuffer output");
}
inline const gui::Widget& first(const gui::Snapshot& snapshot,gui::Kind kind) {
    for(const auto& widget:snapshot.widgets)if(widget.spec.kind==kind)return widget;
    throw std::runtime_error("Example lacks a required control kind");
}
inline gui::Point center(gui::Rect box) {return {box.x+box.width/2,box.y+box.height/2};}
inline void smoke(Session& session) {
    auto& adapter=session.adapter;
    const auto editor=first(adapter.snapshot(),gui::Kind::text).spec.key;
    adapter.focus(editor);adapter.key(gui::Key::select_all);adapter.text("Frame input");adapter.key(gui::Key::enter);session.tick();
    if(!adapter.prompt())throw std::runtime_error("Editor did not open prompt");
    adapter.text("Frame service");adapter.key(gui::Key::enter);
    const auto toggle=first(adapter.snapshot(),gui::Kind::toggle).spec.key;
    adapter.pointer({gui::PointerKind::click,center(adapter.resolved_availability(toggle).bounds)});
    const auto button=first(adapter.snapshot(),gui::Kind::button).spec.key;
    adapter.pointer({gui::PointerKind::click,center(adapter.resolved_availability(button).bounds)});
    if(first(adapter.snapshot(),gui::Kind::list).state.records.size()!=1)
        throw std::runtime_error("Framebuffer input did not update the shared application");
    adapter.resize({800,600});session.tick();
    const auto frame=adapter.frame();
    if(frame.width!=800||frame.height!=600||!frame.pixels)throw std::runtime_error("Framebuffer resize failed");
}
}
