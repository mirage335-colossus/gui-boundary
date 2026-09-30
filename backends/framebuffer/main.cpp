#include "host.hpp"
#include <iostream>

int main(int argc,char** argv) {
    try {
        bool self_test=false;std::string output;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--self-test")self_test=true;
            else if(arg=="--output"&&i+1<argc)output=argv[++i];
            else throw std::invalid_argument("Usage: gui_framebuffer [--self-test] [--output image.ppm]");
        }
        framebuffer_example::Session session;
        if(self_test)framebuffer_example::smoke(session);
        else if(output.empty())output="gui-boundary.ppm";
        const auto frame=session.adapter.frame();
        if(!output.empty())framebuffer_example::write_ppm(frame,output);
        std::cout<<(self_test?"Framebuffer self-test passed: ":"Rendered: ")<<frame.width<<'x'<<frame.height;
        if(!output.empty())std::cout<<" to "<<output;
        std::cout<<'\n';
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
