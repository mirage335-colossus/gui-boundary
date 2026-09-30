#include "gui/web.hpp"
#include "application.hpp"
#include <memory>

namespace {
struct Runtime {
    gui::WebAdapter adapter;
    std::unique_ptr<Example> app;
    std::unique_ptr<gui::WebSession> session;
    std::string response;
    explicit Runtime(std::string epoch):adapter([this](const gui::Event& event){if(app)app->handle(event);}){
        app=std::make_unique<Example>(adapter);
        session=std::make_unique<gui::WebSession>(adapter,std::move(epoch),[this]{return app->next_service();},
            [this](gui::ServiceResult result){return app->complete_service(std::move(result));});
    }
};
std::unique_ptr<Runtime> runtime;
std::uint64_t next_runtime=1;
}
extern "C" {
const char* gui_web_create(const char* epoch){runtime=std::make_unique<Runtime>(epoch&&*epoch?epoch:"wasm-session-"+std::to_string(next_runtime++));runtime->response=runtime->session->initial();return runtime->response.c_str();}
const char* gui_web_receive(const char* message){if(!runtime)gui_web_create(nullptr);runtime->response=runtime->session->receive(message?message:"");return runtime->response.c_str();}
}
