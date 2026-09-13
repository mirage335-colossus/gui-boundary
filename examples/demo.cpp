#include "application.hpp"
#include "gui/memory_adapter.hpp"
#include <iostream>

// Native construction, input probes, CPU paint, and the event loop belong here.
// These explicit fixture metrics are for this display-free demonstration only.
// A native adapter supplies real glyph measurement through the same interface.
int main() {
    try {
        Example* application=nullptr;
        gui::MemoryAdapter adapter([&](const gui::Event& event){application->handle(event);},64*1024*1024,
            [](const gui::TextMeasureRequest& request){return gui::Size{request.available_width,24};});
        Example example(adapter);application=&example;
        adapter.send(gui::WidgetEvent{{"toggle",1},gui::SetChecked{true}});
        adapter.open_popup({"choice",1});adapter.choose_popup({"choice",1},1);
        example.select_editor();adapter.replace({"editor",1},"Updated text");
        adapter.send(gui::WidgetEvent{{"button",1},gui::Activate{}});
        if(!example.show_bitmap_actions())throw std::runtime_error("Bitmap actions unavailable");
        adapter.choose_popup({"bitmap",1},0);
        adapter.enter({"editor",1});adapter.repaint({"bitmap",1});
        if(const auto request=example.next_service()) {
            // A native root executes the host request and returns its actual result.
            gui::ServiceResult reply;reply.id=request->id;reply.value="Entered text";
            example.complete_service(std::move(reply));
        }
        gui::UiQueue queue;
        if(!queue.post([&]{example.set_caption("Updated on the UI thread");}))
            throw std::runtime_error("UI update rejected");
        const auto drained=queue.drain(8);
        if(!drained.errors.empty())std::rethrow_exception(drained.errors.front());
        // Call on each native tick, even with no fresh input or changed values.
        // A native callback catches a failure and schedules another attempt or close.
        example.retry_presentation();
        const auto& view=example.view();
        std::cout<<"Selected option: "<<*gui::find_widget(view,{"choice",1})->state.selected
                 <<"\nText: "<<gui::find_widget(view,{"editor",1})->state.text
                 <<"\nRows: "<<gui::find_widget(view,{"list",1})->state.records.size()
                 <<"\nBitmap: "<<adapter.image({"bitmap",1}).width()<<'x'
                 <<adapter.image({"bitmap",1}).height()<<'\n';
        queue.shutdown();adapter.send(gui::CloseEvent{});
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
