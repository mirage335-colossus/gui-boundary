#include "adapter.hpp"
#include "application.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

int main(int argc,char** argv) {
    try {
        std::string output;
        if(argc==3&&std::string(argv[1])=="--output")output=argv[2];
        else if(argc!=1)throw std::invalid_argument("Usage: gui_rev_demo [--output screenshot.ppm]");
        Example* application=nullptr;
        gui::rev::Adapter adapter([&](const gui::Event& event) {
            if(!application)return;
            application->handle(event);
            if(!adapter.closed()&&!adapter.service_active())if(auto request=application->next_service())
                adapter.service(std::move(*request),[&](gui::ServiceResult result){application->complete_service(std::move(result));});
        });
        Example example(adapter);application=&example;adapter.show();
        while(!adapter.closed()) {
            example.retry_presentation();adapter.sync();
            if(!adapter.service_active())if(auto request=example.next_service())
                adapter.service(std::move(*request),[&](gui::ServiceResult result){example.complete_service(std::move(result));});
            if(!adapter.pump())break;
            if(!output.empty()) {
                const auto pixels=adapter.capture();std::ofstream file(output,std::ios::binary);
                file<<"P6\n"<<pixels.width()<<' '<<pixels.height()<<"\n255\n";
                file.write(reinterpret_cast<const char*>(pixels.pixels().data()),static_cast<std::streamsize>(pixels.pixels().size()));
                if(!file)throw std::runtime_error("Could not write native screenshot");
                adapter.close();break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        if(!adapter.error().empty())throw std::runtime_error(adapter.error());
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
