#include "host.hpp"
#include <SDL.h>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>

namespace {
void require(bool condition,const char* what) {if(!condition)throw std::runtime_error(std::string(what)+": "+SDL_GetError());}
struct Sdl {
    Sdl() {require(SDL_Init(SDL_INIT_VIDEO)==0,"SDL initialization failed");}
    ~Sdl() {SDL_Quit();}
};
using Window=std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>;
using Renderer=std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)>;
using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
std::optional<gui::Key> translate(SDL_Keycode key,SDL_Keymod mods) {
    if((mods&KMOD_CTRL)!=0) {
        if(key==SDLK_a)return gui::Key::select_all;
        if(key==SDLK_n)return gui::Key::next_page;
        if(key==SDLK_p)return gui::Key::previous_page;
        if(key==SDLK_q)return gui::Key::quit;
    }
    switch(key) {
    case SDLK_TAB:return gui::Key::tab;case SDLK_RETURN:case SDLK_KP_ENTER:return gui::Key::enter;
    case SDLK_SPACE:return gui::Key::space;case SDLK_ESCAPE:return gui::Key::escape;
    case SDLK_LEFT:return gui::Key::left;case SDLK_RIGHT:return gui::Key::right;
    case SDLK_UP:return gui::Key::up;case SDLK_DOWN:return gui::Key::down;
    case SDLK_HOME:return gui::Key::home;case SDLK_END:return gui::Key::end;
    case SDLK_BACKSPACE:return gui::Key::backspace;case SDLK_DELETE:return gui::Key::delete_key;
    case SDLK_PAGEUP:return gui::Key::page_up;case SDLK_PAGEDOWN:return gui::Key::page_down;
    case SDLK_APPLICATION:return gui::Key::menu;
    case SDLK_F1:return gui::Key::f1;case SDLK_F2:return gui::Key::f2;case SDLK_F3:return gui::Key::f3;
    case SDLK_F4:return gui::Key::f4;case SDLK_F5:return gui::Key::f5;case SDLK_F6:return gui::Key::f6;
    case SDLK_F7:return gui::Key::f7;case SDLK_F8:return gui::Key::f8;case SDLK_F9:return gui::Key::f9;
    case SDLK_F10:return gui::Key::f10;case SDLK_F11:return gui::Key::f11;case SDLK_F12:return gui::Key::f12;
    default:return {};
    }
}
void clipboard(gui::FramebufferAdapter& adapter,SDL_Keycode key) {
    if(key==SDLK_v) {
        std::unique_ptr<char,decltype(&SDL_free)> value(SDL_GetClipboardText(),SDL_free);
        if(value)adapter.text(value.get());
        return;
    }
    if(adapter.prompt()||adapter.popup())return;
    const auto focused=adapter.focused();if(!focused)return;
    const auto* widget=gui::find_widget(adapter.snapshot(),*focused);
    if(!widget||widget->spec.kind!=gui::Kind::text)return;
    const auto selection=adapter.text_selection(*focused);
    const auto first=std::min(selection.anchor,selection.caret),last=std::max(selection.anchor,selection.caret);
    if(first==last)return;
    const auto value=widget->state.text.substr(first,last-first);
    require(SDL_SetClipboardText(value.c_str())==0,"Clipboard write failed");
    if(key==SDLK_x)adapter.text("");
}
void event(framebuffer_example::Session& session,SDL_Window* window,SDL_Renderer* renderer,const SDL_Event& event) {
    auto& adapter=session.adapter;
    if(event.type==SDL_QUIT)adapter.key(gui::Key::quit);
    else if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_CLOSE)adapter.key(gui::Key::quit);
    else if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED) {
        int width=0,height=0,physical_width=0,physical_height=0;
        SDL_GetWindowSize(window,&width,&height);require(SDL_GetRendererOutputSize(renderer,&physical_width,&physical_height)==0,"Cannot query renderer size");
        (void)physical_height;
        const double scale=width>0?double(physical_width)/width:1;
        if(width>0&&height>0)adapter.resize({double(width),double(height)},scale);
    } else if(event.type==SDL_TEXTINPUT)adapter.text(event.text.text);
    else if(event.type==SDL_KEYDOWN) {
        const auto mods=static_cast<SDL_Keymod>(event.key.keysym.mod);
        const auto symbol=event.key.keysym.sym;
        if((mods&KMOD_CTRL)!=0&&(symbol==SDLK_c||symbol==SDLK_v||symbol==SDLK_x))clipboard(adapter,symbol);
        else if(const auto translated=translate(symbol,mods))adapter.key(*translated,(mods&KMOD_CTRL)!=0,(mods&KMOD_SHIFT)!=0,(mods&KMOD_ALT)!=0);
    } else if(event.type==SDL_MOUSEBUTTONDOWN) {
        const gui::Point point{double(event.button.x),double(event.button.y)};
        if(event.button.button==SDL_BUTTON_RIGHT)adapter.context_menu(point);
        else if(event.button.button==SDL_BUTTON_LEFT) {
            const auto mods=SDL_GetModState();
            adapter.pointer({event.button.clicks>1?gui::PointerKind::double_click:gui::PointerKind::click,point,0,0,
                (mods&KMOD_CTRL)!=0,(mods&KMOD_SHIFT)!=0,(mods&KMOD_ALT)!=0});
        }
    } else if(event.type==SDL_MOUSEMOTION) {
        adapter.pointer({gui::PointerKind::move,{double(event.motion.x),double(event.motion.y)}});
    } else if(event.type==SDL_MOUSEWHEEL) {
        int x=0,y=0;SDL_GetMouseState(&x,&y);
        const double direction=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1;
        adapter.pointer({gui::PointerKind::wheel,{double(x),double(y)},event.wheel.x*direction,event.wheel.y*direction});
    }
    session.tick();
}
void push_key(SDL_Keycode key,SDL_Keymod mods=KMOD_NONE) {
    SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=key;event.key.keysym.mod=mods;
    require(SDL_PushEvent(&event)==1,"Cannot inject SDL key");
}
void push_text(const char* text) {
    SDL_Event event{};event.type=SDL_TEXTINPUT;SDL_strlcpy(event.text.text,text,sizeof(event.text.text));
    require(SDL_PushEvent(&event)==1,"Cannot inject SDL text");
}
void push_click(gui::Rect area) {
    const auto point=framebuffer_example::center(area);SDL_Event event{};event.type=SDL_MOUSEBUTTONDOWN;
    event.button.button=SDL_BUTTON_LEFT;event.button.clicks=1;event.button.x=int(point.x);event.button.y=int(point.y);
    require(SDL_PushEvent(&event)==1,"Cannot inject SDL pointer");
}
void inject_smoke(framebuffer_example::Session& session) {
    const auto& view=session.adapter.snapshot();
    push_click(framebuffer_example::first(view,gui::Kind::text).state.bounds);
    push_key(SDLK_a,KMOD_CTRL);push_text("SDL input");push_key(SDLK_RETURN);
    push_text("SDL prompt");push_key(SDLK_RETURN);
    push_click(framebuffer_example::first(view,gui::Kind::toggle).state.bounds);
    push_click(framebuffer_example::first(view,gui::Kind::button).state.bounds);
}
}
int main(int argc,char** argv) {
    try {
        bool self_test=false;std::string output;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--self-test")self_test=true;
            else if(arg=="--output"&&i+1<argc)output=argv[++i];
            else throw std::invalid_argument("Usage: gui_sdl [--self-test] [--output image.ppm]");
        }
        Sdl sdl;framebuffer_example::Session session;
        const auto initial=session.adapter.frame();
        Window window(SDL_CreateWindow(session.adapter.snapshot().title.c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
            int(initial.width),int(initial.height),SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(self_test?SDL_WINDOW_HIDDEN:0)),SDL_DestroyWindow);
        require(bool(window),"Window creation failed");
        Renderer renderer(SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_SOFTWARE),SDL_DestroyRenderer);
        require(bool(renderer),"Renderer creation failed");
        Texture texture(nullptr,SDL_DestroyTexture);unsigned texture_width=0,texture_height=0;std::uint64_t displayed=0;
        SDL_StartTextInput();
        if(self_test)inject_smoke(session);
        bool verified=false,resized=false;
        while(!session.adapter.closed()) {
            SDL_Event input{};
            if(SDL_WaitEventTimeout(&input,16)) {
                event(session,window.get(),renderer.get(),input);
                while(!session.adapter.closed()&&SDL_PollEvent(&input))event(session,window.get(),renderer.get(),input);
            }
            session.tick();if(session.adapter.closed())break;
            const auto frame=session.adapter.frame(displayed);
            if(!frame.width||!frame.height)continue;
            if(!texture||texture_width!=frame.width||texture_height!=frame.height) {
                texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_RGB24,SDL_TEXTUREACCESS_STREAMING,int(frame.width),int(frame.height)));
                require(bool(texture),"Texture creation failed");texture_width=frame.width;texture_height=frame.height;displayed=0;
            }
            if(displayed==0)require(SDL_UpdateTexture(texture.get(),nullptr,frame.pixels->data(),int(frame.stride_bytes))==0,"Texture upload failed");
            else if(frame.damage.width&&frame.damage.height) {
                const SDL_Rect damage{int(frame.damage.x),int(frame.damage.y),int(frame.damage.width),int(frame.damage.height)};
                require(SDL_UpdateTexture(texture.get(),&damage,frame.pixels->data()+std::size_t(frame.damage.y)*frame.stride_bytes+std::size_t(frame.damage.x)*3,
                    int(frame.stride_bytes))==0,"Damage upload failed");
            }
            require(SDL_RenderClear(renderer.get())==0,"Renderer clear failed");
            require(SDL_RenderCopy(renderer.get(),texture.get(),nullptr,nullptr)==0,"Frame presentation failed");SDL_RenderPresent(renderer.get());displayed=frame.revision;
            if(self_test&&!verified) {
                if(framebuffer_example::first(session.adapter.snapshot(),gui::Kind::list).state.records.size()!=1||session.adapter.prompt()||
                    framebuffer_example::first(session.adapter.snapshot(),gui::Kind::text).state.text!="SDL input")
                    throw std::runtime_error("Injected SDL input failed to update the shared application");
                verified=true;SDL_SetWindowSize(window.get(),800,600);
                SDL_Event resize{};resize.type=SDL_WINDOWEVENT;resize.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
                require(SDL_PushEvent(&resize)==1,"Cannot inject SDL resize");
            } else if(self_test&&verified&&!resized) {
                if(frame.width!=800||frame.height!=600)throw std::runtime_error("SDL resize did not rebuild complete frame");
                if(!output.empty())framebuffer_example::write_ppm(frame,output);
                resized=true;SDL_Event quit{};quit.type=SDL_QUIT;require(SDL_PushEvent(&quit)==1,"Cannot inject SDL close");
            }
        }
        SDL_StopTextInput();
        if(self_test) {if(!verified||!resized)throw std::runtime_error("SDL self-test closed too early");std::cout<<"SDL window, input, prompt, resize, texture upload and close passed\n";}
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
