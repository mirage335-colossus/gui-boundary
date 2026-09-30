#include "gui/web.hpp"
#include "application.hpp"
#include <cassert>
#include <iostream>

using gui::web_detail::Json;
using gui::web_detail::Parser;
using gui::web_detail::encode;
const Json& widget(const Json& response,std::string_view id){
    for(const auto& value:response.at("snapshot").at("widgets").array())if(value.at("key").at("id").str()==id)return value;
    throw std::runtime_error("Missing widget");
}
int main(){
    for(const auto* invalid:{"{\"a\":1,\"a\":2}","[1,]","01","1e999","\"\\ud800\"","\"\\u0000\"","true false"}){
        bool rejected=false;try{(void)Parser(invalid).parse();}catch(const std::exception&){rejected=true;}assert(rejected);
    }
    assert(Parser("\"\\ud83d\\ude03\"").parse().str()=="😃");
    assert(Parser(encode(Json{"Line\n\"quoted\"\\é"})).parse().str()=="Line\n\"quoted\"\\é");
    bool bounded=false;try{(void)Parser(std::string(1024*1024+1,' ')).parse();}catch(const std::length_error&){bounded=true;}assert(bounded);
    bounded=false;try{(void)Parser(std::string(30,'[')+"0"+std::string(30,']')).parse();}catch(const std::exception&){bounded=true;}assert(bounded);
    assert(gui::web_detail::integer(Json{"18446744073709551615"})==UINT64_MAX);
    gui::WebAdapter limited;gui::Snapshot oversized;oversized.title=std::string(4*1024*1024+1,'x');bounded=false;
    try{limited.present(std::move(oversized));}catch(const std::length_error&){bounded=true;}assert(bounded);

    Example* app=nullptr;gui::WebAdapter adapter([&](const gui::Event& event){if(app)app->handle(event);});
    Example example(adapter);app=&example;
    gui::WebSession session(adapter,"test-epoch",[&]{return example.next_service();},[&](gui::ServiceResult result){return example.complete_service(std::move(result));});
    auto initial=Parser(session.initial()).parse();assert(initial.at("ack").str()=="0");
    assert(initial.at("snapshot").at("pages").array().size()==2);
    assert(initial.at("snapshot").at("pages").array()[0].at("bounds").array().size()==4);
    assert(widget(initial,"bitmap").at("image").at("rgb").str().size()>100);
    auto seq=std::uint64_t{0};
    auto envelope=[&](Json operation,std::uint64_t sequence,std::string epoch="test-epoch"){
        return encode(Json::Object{{"epoch",std::move(epoch)},{"seq",std::to_string(sequence)},{"operation",std::move(operation)}});};
    auto send=[&](Json operation){return Parser(session.receive(envelope(std::move(operation),++seq))).parse();};
    const auto key=[](std::string id,std::uint64_t generation=1){return gui::web_detail::key(gui::WidgetKey{std::move(id),generation});};
    const Json edit=Json::Object{{"type","edit"},{"key",key("editor")},{"base","Example text"},{"value","Updated é"}};
    const auto stale=Parser(session.receive(envelope(edit,1,"old-epoch"))).parse();assert(stale.at("ack").str()=="0");
    assert(widget(stale,"editor").at("text").str()=="Example text");
    auto out=send(edit);assert(widget(out,"editor").at("text").str()=="Updated é");
    out=send(Json::Object{{"type","edit"},{"key",key("editor")},{"base","Example text"},{"value","Stale replacement"}});
    assert(widget(out,"editor").at("text").str()=="Updated é");
    out=send(Json::Object{{"type","checked"},{"key",key("toggle")},{"value",true}});
    assert(widget(out,"button").at("enabled").boolean());
    const Json add=Json::Object{{"type","activate"},{"key",key("button")}};
    out=send(add);const auto rows=widget(out,"list").at("records").array().size();assert(rows==1);
    const auto duplicate=Parser(session.receive(envelope(add,seq))).parse();assert(widget(duplicate,"list").at("records").array().size()==rows);
    const auto future=Parser(session.receive(envelope(add,seq+2))).parse();assert(future.at("ack").str()==std::to_string(seq));
    out=send(Json::Object{{"type","activate"},{"key",key("button",2)}});assert(!out.at("error").str().empty());
    assert(widget(out,"list").at("records").array().size()==rows);
    out=send(Json::Object{{"type","choose"},{"key",key("choice")},{"id","second"}});
    assert(widget(out,"choice").at("selected").str()=="second");
    out=send(Json::Object{{"type","submit"},{"key",key("editor")}});
    const auto service=out.at("service").at("id").str();
    out=send(Json::Object{{"type","service"},{"id",service},{"status","success"},{"value","Prompt result"},{"error",""}});
    assert(widget(out,"caption").at("text").str()=="Prompt result");
    out=send(Json::Object{{"type","service"},{"id",service},{"status","success"},{"value","Replay"},{"error",""}});
    assert(!out.at("error").str().empty());assert(widget(out,"caption").at("text").str()=="Prompt result");

    const auto measurements=out.at("snapshot").at("measurements").array();
    if(!measurements.empty()){
        const auto revision=gui::web_detail::integer(out.at("snapshot").at("revision"));
        out=send(Json::Object{{"type","measure"},{"values",Json::Array{Json::Object{{"id",measurements.front().at("id")},{"width",200.0},{"height",23.0}}}}});
        assert(gui::web_detail::integer(out.at("snapshot").at("revision"))>revision);
    }
    out=send(Json::Object{{"type","close"}});assert(out.at("snapshot").at("closed").boolean());

    // Exercise adapter vocabulary independently of the example's feature IDs.
    std::vector<gui::Event> inputs;gui::WebAdapter generic([&](const gui::Event& event){inputs.push_back(event);});
    gui::Snapshot fixture;fixture.revision=1;
    gui::Widget label;label.spec.key={"unrelated pointer label",7};label.spec.kind=gui::Kind::label;label.spec.pointer_input=true;
    label.state.bounds={10,10,100,30};label.state.text="Literal label";
    gui::Widget choice;choice.spec.key={"unrelated options",3};choice.spec.kind=gui::Kind::choice;choice.state.bounds={10,50,100,30};
    choice.state.options={{"a","Repeated","",true},{"b","Repeated","",true}};
    fixture.widgets={label,choice};generic.present(fixture);gui::WebSession bridge(generic,"test-epoch");(void)bridge.initial();
    std::uint64_t generic_seq=0;
    auto native=[&](Json op){return Parser(bridge.receive(envelope(std::move(op),++generic_seq))).parse();};
    native(Json::Object{{"type","pointer"},{"key",gui::web_detail::key(label.spec.key)},{"kind","click"},{"x",20},{"y",20},
        {"wheelX",0},{"wheelY",0},{"control",false},{"shift",false},{"alt",false}});
    assert(inputs.size()==1&&std::holds_alternative<gui::PointerInput>(std::get<gui::WidgetEvent>(inputs[0]).input));
    assert(generic.open_popup(choice.spec.key));
    auto popup=Parser(bridge.initial()).parse().at("snapshot").at("popup");assert(popup.at("options").array()[0].at("id").str()=="a");
    std::reverse(fixture.widgets[1].state.options.begin(),fixture.widgets[1].state.options.end());generic.present(fixture);
    native(Json::Object{{"type","popupChoice"},{"key",gui::web_detail::key(choice.spec.key)},{"id","a"}});
    assert(std::get<gui::ChooseOption>(std::get<gui::WidgetEvent>(inputs.back()).input).id=="a");
    generic.open_popup(choice.spec.key);generic.close();assert(Parser(bridge.initial()).parse().at("snapshot").at("closed").boolean());

    // A transient painter failure retains the last delivered browser frame and
    // its dirty debt, without repeating an acknowledged application operation.
    gui::WebAdapter recovering;gui::Snapshot pixels;pixels.revision=1;
    gui::Widget image;image.spec.key={"generic image",1};image.spec.kind=gui::Kind::bitmap;image.state.bounds={0,0,2,2};
    image.state.bitmap={"owned",1,gui::solid_bitmap(1,2,3)};pixels.widgets={image};recovering.present(pixels);
    gui::WebSession recovery(recovering,"test-epoch");(void)recovery.initial();
    auto fails=std::make_shared<bool>(true);pixels.revision=2;pixels.widgets[0].state.bitmap.revision=2;
    pixels.widgets[0].state.bitmap.source=gui::BitmapSource([fails](const gui::BitmapRequest& request,const gui::BitmapSink& sink){
        if(*fails)throw std::runtime_error("Transient paint failure");
        gui::solid_bitmap(4,5,6).paint(request,sink);
    });
    recovering.present(pixels);auto failed=Parser(recovery.receive(envelope(Json::Object{{"type","poll"}},1))).parse();
    assert(failed.at("ack").str()=="1"&&failed.at("snapshot").at("revision").str()=="1"&&!failed.at("error").str().empty());
    *fails=false;auto recovered=Parser(recovery.receive(envelope(Json::Object{{"type","poll"}},2))).parse();
    assert(recovered.at("snapshot").at("revision").str()=="2");
    gui::WebAdapter services;services.present({});bool offered=false,completed=false;
    gui::WebSession bounded_service(services,"test-epoch",[&]()->std::optional<gui::ServiceRequest>{
        if(offered)return {};
        offered=true;gui::ServiceRequest request;request.id=9;request.byte_limit=65537;return request;
    },[&](gui::ServiceResult result){completed=result.id==9&&result.status==gui::ServiceStatus::error;return true;});
    const auto bounded_reply=Parser(bounded_service.initial()).parse();assert(completed&&!bounded_reply.at("error").str().empty());
    std::cout<<"web protocol and application integration passed\n";
}
