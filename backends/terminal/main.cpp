#include "gui/terminal.hpp"
#include "../../examples/application.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
using Clock=std::chrono::steady_clock;
constexpr std::string_view enter_screen="\x1b[?1049h\x1b[?25l\x1b[?2004h\x1b[2J";
constexpr std::string_view leave_screen="\x1b[?2004l\x1b[?25h\x1b[?1049l";

#ifdef _WIN32
// The Windows console supplies native key and resize events. Rendering uses
// virtual-terminal output, enabled only for this host's lifetime.
class TerminalHost {
public:
    TerminalHost() {
        input_=GetStdHandle(STD_INPUT_HANDLE);output_=GetStdHandle(STD_OUTPUT_HANDLE);
        if(!GetConsoleMode(input_,&input_mode_)||!GetConsoleMode(output_,&output_mode_))
            throw std::runtime_error("Run this example in a Windows console.");
        if(!SetConsoleMode(output_,output_mode_|ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            throw std::runtime_error("This console does not support virtual-terminal output.");
        if(!SetConsoleMode(input_,ENABLE_WINDOW_INPUT|ENABLE_EXTENDED_FLAGS)) {
            SetConsoleMode(output_,output_mode_);
            throw std::runtime_error("Could not configure console input.");
        }
        write(enter_screen);
    }
    ~TerminalHost() {
        write(leave_screen);SetConsoleMode(input_,input_mode_);SetConsoleMode(output_,output_mode_);
    }
    std::pair<unsigned,unsigned> size() const {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if(!GetConsoleScreenBufferInfo(output_,&info))return {80,24};
        return {unsigned(info.srWindow.Right-info.srWindow.Left+1),unsigned(info.srWindow.Bottom-info.srWindow.Top+1)};
    }
    bool stopped() const {return false;}
    void poll(gui::TerminalAdapter& adapter) {
        if(WaitForSingleObject(input_,30)!=WAIT_OBJECT_0)return;
        INPUT_RECORD records[64];DWORD count=0;
        if(!ReadConsoleInputW(input_,records,64,&count))throw std::runtime_error("Console input failed.");
        for(DWORD i=0;i<count&&!adapter.closed();++i) {
            if(records[i].EventType!=KEY_EVENT||!records[i].Event.KeyEvent.bKeyDown)continue;
            const auto event=records[i].Event.KeyEvent;
            const bool shift=(event.dwControlKeyState&SHIFT_PRESSED)!=0;
            const bool control=(event.dwControlKeyState&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
            for(WORD repeat=0;repeat<event.wRepeatCount&&!adapter.closed();++repeat) {
                switch(event.wVirtualKeyCode) {
                    case VK_TAB:adapter.key(gui::Key::tab,control,shift);break;
                    case VK_RETURN:adapter.key(gui::Key::enter,control,shift);break;
                    case VK_ESCAPE:adapter.key(gui::Key::escape);break;
                    case VK_BACK:adapter.key(gui::Key::backspace,control,shift);break;
                    case VK_DELETE:adapter.key(gui::Key::delete_key,control,shift);break;
                    case VK_LEFT:adapter.key(gui::Key::left,control,shift);break;
                    case VK_RIGHT:adapter.key(gui::Key::right,control,shift);break;
                    case VK_UP:adapter.key(gui::Key::up,control,shift);break;
                    case VK_DOWN:adapter.key(gui::Key::down,control,shift);break;
                    case VK_HOME:adapter.key(gui::Key::home,control,shift);break;
                    case VK_END:adapter.key(gui::Key::end,control,shift);break;
                    case VK_PRIOR:adapter.key(gui::Key::page_up,control,shift);break;
                    case VK_NEXT:adapter.key(gui::Key::page_down,control,shift);break;
                    case VK_F1:case VK_F2:case VK_F3:case VK_F4:case VK_F5:case VK_F6:
                    case VK_F7:case VK_F8:case VK_F9:case VK_F10:case VK_F11:case VK_F12:
                        adapter.key(static_cast<gui::Key>(int(gui::Key::f1)+event.wVirtualKeyCode-VK_F1),control,shift);break;
                    default:input_character(adapter,event.uChar.UnicodeChar);break;
                }
            }
        }
    }
    void draw(const std::vector<std::string>& lines) {
        std::string frame;
        for(std::size_t row=0;row<lines.size();++row)frame+="\x1b["+std::to_string(row+1)+";1H"+lines[row];
        if(frame!=last_) {write(frame);last_=std::move(frame);}
    }
private:
    HANDLE input_=INVALID_HANDLE_VALUE,output_=INVALID_HANDLE_VALUE;
    DWORD input_mode_=0,output_mode_=0;
    wchar_t high_surrogate_=0;
    std::string last_;
    void write(std::string_view text) noexcept {
        DWORD written=0;WriteFile(output_,text.data(),DWORD(text.size()),&written,nullptr);
    }
    void input_character(gui::TerminalAdapter& adapter,wchar_t character) {
        if(!character)return;
        if(character>=0xd800&&character<=0xdbff) {high_surrogate_=character;return;}
        wchar_t chars[2]={character,0};int length=1;
        if(character>=0xdc00&&character<=0xdfff&&high_surrogate_) {
            chars[0]=high_surrogate_;chars[1]=character;length=2;
        }
        high_surrogate_=0;
        char bytes[8];
        const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,chars,length,bytes,8,nullptr,nullptr);
        if(size>0)adapter.input(std::string_view(bytes,std::size_t(size)));
    }
};
#else
volatile std::sig_atomic_t stop_requested=0;
extern "C" void stop_signal(int) {stop_requested=1;}

// Only this object owns terminal modes. Destruction restores them on normal
// close, input/output failure, application exceptions, SIGINT, SIGTERM or SIGHUP.
class TerminalHost {
public:
    TerminalHost() {
        if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)||tcgetattr(STDIN_FILENO,&original_)<0)
            throw std::runtime_error("Run this example in an interactive terminal.");
        input_flags_=fcntl(STDIN_FILENO,F_GETFL);output_flags_=fcntl(STDOUT_FILENO,F_GETFL);
        if(input_flags_<0||output_flags_<0)throw std::runtime_error("Could not read terminal flags.");
        struct sigaction action{};action.sa_handler=stop_signal;sigemptyset(&action.sa_mask);
        if(sigaction(SIGINT,&action,&old_int_)<0)throw std::runtime_error("Could not install terminal signal handler.");
        int_signal_=true;
        if(sigaction(SIGTERM,&action,&old_term_)<0) {restore();throw std::runtime_error("Could not install terminal signal handler.");}
        term_signal_=true;
        if(sigaction(SIGHUP,&action,&old_hup_)<0) {restore();throw std::runtime_error("Could not install terminal signal handler.");}
        hup_signal_=true;
        auto raw=original_;
        raw.c_iflag&=~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
        raw.c_oflag&=~OPOST;raw.c_cflag|=CS8;raw.c_lflag&=~(ECHO|ICANON|IEXTEN|ISIG);
        raw.c_cc[VMIN]=0;raw.c_cc[VTIME]=0;
        if(tcsetattr(STDIN_FILENO,TCSANOW,&raw)<0) {restore();throw std::runtime_error("Could not configure terminal input.");}
        configured_=true;
        if(fcntl(STDIN_FILENO,F_SETFL,input_flags_|O_NONBLOCK)<0||
           fcntl(STDOUT_FILENO,F_SETFL,output_flags_|O_NONBLOCK)<0) {
            restore();throw std::runtime_error("Could not configure nonblocking terminal I/O.");
        }
        pending_=enter_screen;
    }
    ~TerminalHost() {
        // Drop queued frames on shutdown. Cleanup is short and bounded even if
        // the terminal reader disappeared or has stopped accepting output.
        std::string_view cleanup=leave_screen;
        const auto deadline=Clock::now()+std::chrono::milliseconds(150);
        while(!cleanup.empty()&&Clock::now()<deadline) {
            const auto written=::write(STDOUT_FILENO,cleanup.data(),cleanup.size());
            if(written>0)cleanup.remove_prefix(std::size_t(written));
            else if(written<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)) {
                pollfd output{STDOUT_FILENO,POLLOUT,0};::poll(&output,1,10);
            } else break;
        }
        restore();
    }
    std::pair<unsigned,unsigned> size() const {
        winsize size{};
        if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&size)<0||!size.ws_col||!size.ws_row)return {80,24};
        return {size.ws_col,size.ws_row};
    }
    bool stopped() const {return stop_requested!=0;}
    void poll(gui::TerminalAdapter& adapter) {
        pollfd descriptors[2]={{STDIN_FILENO,POLLIN,0},{STDOUT_FILENO,short(pending_.empty()?0:POLLOUT),0}};
        const auto ready=::poll(descriptors,2,30);
        if(ready<0&&errno!=EINTR)throw std::runtime_error("Terminal polling failed.");
        if(descriptors[0].revents&(POLLHUP|POLLERR|POLLNVAL)) {adapter.key(gui::Key::quit);return;}
        if(descriptors[1].revents&(POLLHUP|POLLERR|POLLNVAL))throw std::runtime_error("Terminal output disconnected.");
        if(descriptors[0].revents&POLLIN) {
            char bytes[4096];const auto count=::read(STDIN_FILENO,bytes,sizeof(bytes));
            if(count>0) {adapter.input(std::string_view(bytes,std::size_t(count)));last_input_=Clock::now();}
            else if(count==0)adapter.key(gui::Key::quit);
            else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)throw std::runtime_error("Terminal input failed.");
        } else if(Clock::now()-last_input_>std::chrono::milliseconds(60))adapter.flush_escape();
        if(descriptors[1].revents&POLLOUT)flush();
    }
    void draw(const std::vector<std::string>& lines) {
        std::string frame;
        for(std::size_t row=0;row<lines.size();++row)frame+="\x1b["+std::to_string(row+1)+";1H"+lines[row];
        if(frame==last_)return;
        last_=frame;
        // A partly written frame remains intact. At most one newer complete
        // frame is retained, so a slow terminal cannot grow an unbounded queue.
        if(pending_.empty()) {pending_=std::move(frame);offset_=0;}
        else replacement_=std::move(frame);
    }
private:
    termios original_{};
    int input_flags_=-1,output_flags_=-1;
    bool configured_=false,int_signal_=false,term_signal_=false,hup_signal_=false;
    struct sigaction old_int_{},old_term_{},old_hup_{};
    std::string pending_,replacement_,last_;
    std::size_t offset_=0;
    Clock::time_point last_input_=Clock::now();
    void restore() noexcept {
        if(configured_)tcsetattr(STDIN_FILENO,TCSANOW,&original_);
        if(input_flags_>=0)fcntl(STDIN_FILENO,F_SETFL,input_flags_);
        if(output_flags_>=0)fcntl(STDOUT_FILENO,F_SETFL,output_flags_);
        if(int_signal_)sigaction(SIGINT,&old_int_,nullptr);
        if(term_signal_)sigaction(SIGTERM,&old_term_,nullptr);
        if(hup_signal_)sigaction(SIGHUP,&old_hup_,nullptr);
    }
    void flush() {
        if(pending_.empty())return;
        const auto count=::write(STDOUT_FILENO,pending_.data()+offset_,pending_.size()-offset_);
        if(count<0) {
            if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return;
            throw std::runtime_error("Terminal output failed.");
        }
        offset_+=std::size_t(count);
        if(offset_==pending_.size()) {pending_=std::move(replacement_);replacement_.clear();offset_=0;}
    }
};
#endif
}

int main(int argc,char** argv) {
    if(argc==2&&std::string_view(argv[1])=="--help") {
        std::cout<<"Standalone terminal UI example\n"
                 <<"Tab/Shift-Tab: focus; Enter/Space: activate; arrows: edit/select;\n"
                 <<"Ctrl-A: select text; Ctrl-U: clear; Ctrl-O: options;\n"
                 <<"Ctrl-N and Ctrl-P: pages; PgUp/PgDn: lists; Ctrl-Q: quit.\n"
                 <<"Enter submits; Ctrl-J inserts newline; Ctrl-S sends Ctrl-Enter. Esc cancels a prompt or popup.\n";
        return 0;
    }
    if(argc!=1) {std::cerr<<"Usage: "<<argv[0]<<" [--help]\n";return 2;}
    try {
        TerminalHost host;
        Example* application=nullptr;
        std::function<void()> service_pump;
        gui::TerminalAdapter adapter([&](const gui::Event& event) {
            if(application)application->handle(event);
            // A single native read may contain submission plus more typing.
            // Establish modality before the next decoded input is dispatched.
            if(service_pump)service_pump();
        });
        Example example(adapter);application=&example;
        service_pump=[&] {
            if(!adapter.closed()&&!adapter.prompt())if(auto request=example.next_service())
                adapter.service(std::move(*request),[&](gui::ServiceResult result) {example.complete_service(std::move(result));});
        };
        adapter.focus_next();
        std::pair<unsigned,unsigned> dimensions{};
        while(!adapter.closed()) {
            if(host.stopped()) {adapter.key(gui::Key::quit);break;}
            const auto current=host.size();
            if(current!=dimensions) {adapter.terminal_size(current.first,current.second);dimensions=current;}
            example.retry_presentation();
            service_pump();
            host.draw(adapter.ansi_rows());
            host.poll(adapter);
        }
    } catch(const std::exception& error) {std::cerr<<"Terminal example: "<<error.what()<<'\n';return 1;}
}
