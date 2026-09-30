#include "adapter.hpp"
#include "application.hpp"
#include <iostream>

namespace {
// Timers continue during FLTK's native menu loop. The shared model/services
// therefore make progress while the adapter defers unsafe widget replacement.
struct Pump {
    gui::fltk::Adapter& adapter;Example& app;std::exception_ptr failure;
    static void tick(void* data) {
        auto& self=*static_cast<Pump*>(data);
        try {
            if(self.adapter.closed())return;
            self.app.retry_presentation();self.adapter.sync();
            if(!self.adapter.service_active())if(auto request=self.app.next_service())
                self.adapter.service(std::move(*request),[&self](gui::ServiceResult result){self.app.complete_service(std::move(result));});
            Fl::repeat_timeout(.02,tick,data);
        } catch(...) {self.failure=std::current_exception();self.adapter.close();}
    }
    ~Pump() {Fl::remove_timeout(tick,this);}
};
}
int main() {
    try {
        Example* application=nullptr;
        gui::fltk::Adapter adapter([&](const gui::Event& event){
            if(!application)return;
            application->handle(event);
            if(!adapter.closed()&&!adapter.service_active())if(auto request=application->next_service())
                adapter.service(std::move(*request),[&](gui::ServiceResult result){application->complete_service(std::move(result));});
        });
        Example example(adapter);application=&example;adapter.show();
        Pump pump{adapter,example,{}};Fl::add_timeout(0,Pump::tick,&pump);
        while(!adapter.closed())Fl::wait(.02);
        if(pump.failure)std::rethrow_exception(pump.failure);
        if(!adapter.error().empty())throw std::runtime_error(adapter.error());
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
