#include "../examples/application.hpp"
#include "gui/memory_adapter.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class Function> void fails(Function function) {
    try {function();}catch(const std::exception&){return;}
    throw std::runtime_error("Expected presentation failure");
}
void authoritative_input() {
    // A queued facade can receive an event after the adapter accepted it.
    // The application must apply the common policy again to its current model.
    std::vector<gui::Event> queued;
    gui::MemoryAdapter memory([&](const gui::Event& event){queued.push_back(event);},64*1024*1024,
        [](const gui::TextMeasureRequest& request){return gui::Size{request.available_width,24};});
    gui::Adapter& boundary=memory;Example application(boundary);
    const auto initial=gui::find_widget(application.view(),{"editor",1})->state.text;
    check(memory.send(gui::WidgetEvent{{"editor",1},gui::EditText{"Delayed",initial}})==gui::Delivery::delivered,
          "Valid editor input was not queued");
    application.handle(gui::WidgetEvent{{"editor",1},gui::EditText{"Latest",initial}});
    const auto revision=application.view().revision;
    application.handle(queued.front());
    check(application.view().revision==revision&&
          gui::find_widget(application.view(),{"editor",1})->state.text=="Latest",
          "Delayed edit overwrote authoritative text");

    // These can arise from an obsolete or faulty event bridge. None may mutate
    // the model or poison a subsequent presentation with invalid state.
    const std::vector<gui::Event> rejected={
        gui::WidgetEvent{{"heading",1},gui::SetChecked{true}},
        gui::WidgetEvent{{"heading",1},gui::Activate{}},
        gui::WidgetEvent{{"button",1},gui::Activate{}}, // initially disabled
        gui::WidgetEvent{{"editor",2},gui::EditText{"Replacement","Latest"}},
        gui::WidgetEvent{{"editor",1},gui::EditText{std::string(257,'x'),"Latest"}},
        gui::WidgetEvent{{"editor",1},gui::EditText{"line\nbreak","Latest"}},
        gui::WidgetEvent{{"editor",1},gui::EditText{std::string(1,char(0xff)),"Latest"}},
        gui::WidgetEvent{{"choice",1},gui::ChooseOption{"removed"}},
        gui::WidgetEvent{{"list",1},gui::SelectRecord{"removed"}},
        gui::WidgetEvent{{"bitmap",1},gui::InvokeAction{"removed"}},
        gui::WidgetEvent{{"bitmap",1},gui::PointerInput{gui::PointerKind::click,{0,0}}},
        gui::PageEvent{"removed"},gui::ResizeEvent{{-1,100},1},gui::ResizeEvent{{100,100},0}
    };
    for(const auto& event:rejected) {
        application.handle(event);
        check(application.view().revision==revision,"Rejected facade event changed the model");
    }
    application.handle(gui::WidgetEvent{{"toggle",1},gui::SetChecked{true}});
    application.handle(gui::WidgetEvent{{"button",1},gui::Activate{}});
    check(gui::find_widget(application.view(),{"list",1})->state.records.size()==1,
          "Rejected input prevented later valid feature work");
    check(application.show_bitmap_actions(),"Shared application could not open bitmap actions");
    check(memory.choose_popup({"bitmap",1},0)==gui::Delivery::delivered,"Action chooser did not queue an action");
    application.handle(queued.back());
    check(gui::find_widget(application.view(),{"bitmap",1})->state.bitmap.revision==1,
          "Action chooser failed to reach the shared bitmap operation");
    application.handle(gui::PageEvent{"other"});
    const auto hidden_revision=application.view().revision;
    application.handle(gui::WidgetEvent{{"button",1},gui::Activate{}});
    check(application.view().revision==hidden_revision,"Hidden-page input reached the model");
    application.handle(gui::CloseEvent{});
    application.handle(gui::PageEvent{"main"});
    check(boundary.closed()&&application.view().active_page=="other","Late input reopened the application");
}
void publication_recovery() {
    bool fail_metrics=false;
    gui::MemoryAdapter memory({},100000,[&](const gui::TextMeasureRequest& request) {
        if(fail_metrics)throw std::runtime_error("Temporary metric failure");
        return gui::Size{request.available_width,24};
    });
    gui::Adapter& boundary=memory;Example application(boundary);
    fail_metrics=true;
    fails([&]{application.handle(gui::WidgetEvent{{"toggle",1},gui::SetChecked{true}});});
    check(application.presentation_pending()&&
          !gui::find_widget(memory.snapshot(),{"toggle",1})->state.checked,
          "Failed presentation did not preserve applied state and retry debt");
    fail_metrics=false;
    application.handle(gui::WidgetEvent{{"toggle",1},gui::SetChecked{true}});
    check(!application.presentation_pending()&&
          gui::find_widget(memory.snapshot(),{"toggle",1})->state.checked,
          "Retry of an unchanged authoritative value could not recover presentation");

    application.handle(gui::WidgetEvent{{"editor",1},gui::SubmitText{}});
    const auto request=application.next_service();check(bool(request),"Service request absent");
    fail_metrics=true;
    fails([&]{application.complete_service({request->id,gui::ServiceStatus::success,"Retained reply",{}});});
    check(!application.complete_service({request->id,gui::ServiceStatus::success,"Duplicate",{}}),
          "Failed publication allowed duplicate service completion");
    fail_metrics=false;application.retry_presentation();
    check(gui::find_widget(memory.snapshot(),{"caption",1})->state.text=="Retained reply",
          "Consumed service reply was lost after failed publication");
    fails([&]{application.set_caption(std::string(1,char(0xff)));});
    check(gui::find_widget(application.view(),{"caption",1})->state.text=="Retained reply"&&
          !application.presentation_pending(),"Invalid caption poisoned the model");

    // The initial RGB grid fits the budget; doubling scale does not. Recovery
    // must allow a smaller resize instead of endlessly retrying the large grid.
    fails([&]{application.handle(gui::ResizeEvent{{640,480},2});});
    check(application.presentation_pending(),"Bitmap budget failure lost retry debt");
    application.handle(gui::ResizeEvent{{640,480},1});
    check(!application.presentation_pending()&&memory.snapshot().display_scale==1,
          "Smaller resize could not replace failed presentation");
    fail_metrics=true;fails([&]{application.set_caption("Pending close");});
    application.handle(gui::CloseEvent{});application.retry_presentation();
    check(boundary.closed()&&!application.presentation_pending(),"Close waited for failing presentation");
}
void full_list_recovery() {
    gui::MemoryAdapter memory({},64*1024*1024,
        [](const gui::TextMeasureRequest& request){return gui::Size{request.available_width,24};});
    Example application(memory);
    // Seed a capacity fixture directly rather than performing tens of thousands
    // of presentations. The underlying Example is non-const; production callers
    // use its event facade and never mutate the borrowed view.
    auto& fixture=const_cast<gui::Snapshot&>(application.view());
    for(auto& widget:fixture.widgets) {
        if(widget.spec.kind==gui::Kind::toggle)widget.state.checked=true;
        if(widget.spec.kind==gui::Kind::list) {
            const auto count=std::size_t(gui::coordinate_limit/widget.spec.row_height);
            widget.state.records.reserve(count);
            for(std::size_t index=0;index<count;++index) {
                gui::Record record;record.id="fixture-"+std::to_string(index);
                widget.state.records.push_back(std::move(record));
            }
        }
    }
    application.set_caption("At capacity");
    const auto revision=application.view().revision;
    fails([&]{application.handle(gui::WidgetEvent{{"button",1},gui::Activate{}});});
    check(!application.presentation_pending()&&application.view().revision==revision,
          "List overflow left an invalid desired snapshot");
    application.handle(gui::WidgetEvent{{"menu",1},gui::ChooseOption{"clear"}});
    check(gui::find_widget(application.view(),{"list",1})->state.records.empty(),
          "List capacity error obstructed shared Clear action");
}
}
int main() {
    try {authoritative_input();publication_recovery();full_list_recovery();std::cout<<"Shared application checks passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
