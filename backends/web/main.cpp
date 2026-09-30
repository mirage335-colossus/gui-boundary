#include "gui/web.hpp"
#include "application.hpp"
#include <iostream>

int main(int argc,char** argv){
    try {
        Example* application=nullptr;
        gui::WebAdapter adapter([&](const gui::Event& event){if(application)application->handle(event);});
        Example example(adapter);application=&example;
        gui::WebSession session(adapter,argc>1?argv[1]:"stdio-session",[&]{return example.next_service();},
            [&](gui::ServiceResult result){return example.complete_service(std::move(result));});
        std::cout<<session.initial()<<'\n'<<std::flush;
        std::string line;line.reserve(4096);char c;bool oversized=false;
        while(std::cin.get(c)){
            if(c=='\n'){
                std::cout<<session.receive(oversized?std::string_view{}:std::string_view{line})<<'\n'<<std::flush;
                line.clear();oversized=false;
            }else if(line.size()<1024*1024)line+=c;else oversized=true;
        }
    }catch(const std::exception& error){std::cerr<<"Web bridge: "<<error.what()<<'\n';return 1;}
}
