#pragma once
#include "retained_adapter.hpp"
#include "presentation.hpp"
#include "runtime.hpp"
#include <charconv>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>

namespace gui::web_detail {
// Small, deliberately bounded JSON codec for the example wire protocol. It
// rejects duplicate object keys, invalid UTF-8, non-finite numbers and excess
// nesting. Integer identities travel as decimal strings (JavaScript is exact
// only up to 2^53). No executable content or native callable crosses the wire.
struct Json {
    using Array=std::vector<Json>;using Object=std::map<std::string,Json,std::less<>>;
    std::variant<std::nullptr_t,bool,double,std::string,Array,Object> value=nullptr;
    Json()=default;Json(std::nullptr_t){};Json(bool v):value(v){};
    Json(double v):value(v){};Json(int v):value(double(v)){};
    Json(const char* v):value(std::string(v)){};Json(std::string v):value(std::move(v)){};
    Json(Array v):value(std::move(v)){};Json(Object v):value(std::move(v)){};
    const Json& at(std::string_view key) const {return std::get<Object>(value).at(std::string(key));}
    const std::string& str() const {return std::get<std::string>(value);}
    double number() const {return std::get<double>(value);}
    bool boolean() const {return std::get<bool>(value);}
    const Array& array() const {return std::get<Array>(value);}
};
inline std::uint64_t integer(const Json& j) {
    const auto& text=j.str();std::uint64_t out=0;
    const auto [end,ec]=std::from_chars(text.data(),text.data()+text.size(),out);
    if(text.empty()||ec!=std::errc{}||end!=text.data()+text.size()||(text.size()>1&&text[0]=='0'))
        throw std::invalid_argument("Invalid decimal identity");
    return out;
}
class Parser {
public:
    explicit Parser(std::string_view input):input_(input) {
        if(input.size()>1024*1024)throw std::length_error("Message exceeds 1 MiB");
    }
    Json parse() {auto out=value(0);space();if(pos_!=input_.size())fail();return out;}
private:
    std::string_view input_;std::size_t pos_=0,nodes_=0;
    [[noreturn]] static void fail(){throw std::invalid_argument("Malformed JSON");}
    void space(){while(pos_<input_.size()&&(input_[pos_]==' '||input_[pos_]=='\n'||input_[pos_]=='\r'||input_[pos_]=='\t'))++pos_;}
    bool take(char c){space();if(pos_<input_.size()&&input_[pos_]==c){++pos_;return true;}return false;}
    unsigned hex4(){unsigned out=0;for(int i=0;i<4;++i){if(pos_==input_.size())fail();const char c=input_[pos_++];
        unsigned d;if(c>='0'&&c<='9')d=unsigned(c-'0');else if(c>='a'&&c<='f')d=unsigned(c-'a'+10);
        else if(c>='A'&&c<='F')d=unsigned(c-'A'+10);else fail();
        out=out*16+d;}return out;}
    static void utf8(std::string& s,unsigned c){
        if(c<0x80)s+=char(c);else if(c<0x800){s+=char(0xc0|(c>>6));s+=char(0x80|(c&63));}
        else if(c<0x10000){s+=char(0xe0|(c>>12));s+=char(0x80|((c>>6)&63));s+=char(0x80|(c&63));}
        else {s+=char(0xf0|(c>>18));s+=char(0x80|((c>>12)&63));s+=char(0x80|((c>>6)&63));s+=char(0x80|(c&63));}}
    std::string string(){if(!take('"'))fail();std::string out;
        while(pos_<input_.size()){char c=input_[pos_++];if(c=='"'){if(!valid_utf8(out))fail();return out;}
            if(static_cast<unsigned char>(c)<32)fail();
            if(c!='\\'){out+=c;continue;}
            if(pos_==input_.size())fail();
            c=input_[pos_++];switch(c){
            case '"':case '\\':case '/':out+=c;break;case 'b':out+='\b';break;case 'f':out+='\f';break;
            case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
            case 'u':{unsigned u=hex4();if(u>=0xd800&&u<=0xdbff){
                if(pos_+2>input_.size()||input_.substr(pos_,2)!="\\u")fail();
                pos_+=2;const auto low=hex4();
                if(low<0xdc00||low>0xdfff)fail();
                u=0x10000+((u-0xd800)<<10)+(low-0xdc00);
            }else if(u>=0xdc00&&u<=0xdfff)fail();utf8(out,u);break;}default:fail();}}
        fail();}
    Json value(unsigned depth){space();if(++nodes_>65536||depth>24||pos_==input_.size())fail();
        if(input_[pos_]=='"')return string();
        if(take('{')){Json::Object out;if(take('}'))return out;do{const auto key=string();if(!take(':'))fail();
            if(!out.emplace(key,value(depth+1)).second)fail();
            if(take('}'))return out;}while(take(','));fail();}
        if(take('[')){Json::Array out;if(take(']'))return out;do{out.push_back(value(depth+1));if(take(']'))return out;}while(take(','));fail();}
        for(const auto& [word,v]:std::vector<std::pair<std::string_view,Json>>{{"null",nullptr},{"true",true},{"false",false}})
            if(input_.substr(pos_,word.size())==word){pos_+=word.size();return v;}
        const auto begin=pos_;if(input_[pos_]=='-')++pos_;if(pos_==input_.size())fail();
        if(input_[pos_]=='0')++pos_;else {if(input_[pos_]<'1'||input_[pos_]>'9')fail();while(pos_<input_.size()&&input_[pos_]>='0'&&input_[pos_]<='9')++pos_;}
        if(pos_<input_.size()&&input_[pos_]=='.'){++pos_;const auto digits=pos_;while(pos_<input_.size()&&input_[pos_]>='0'&&input_[pos_]<='9')++pos_;if(pos_==digits)fail();}
        if(pos_<input_.size()&&(input_[pos_]=='e'||input_[pos_]=='E')){++pos_;if(pos_<input_.size()&&(input_[pos_]=='+'||input_[pos_]=='-'))++pos_;
            const auto digits=pos_;while(pos_<input_.size()&&input_[pos_]>='0'&&input_[pos_]<='9')++pos_;if(pos_==digits)fail();}
        double result=0;std::istringstream number{std::string(input_.substr(begin,pos_-begin))};
        number.imbue(std::locale::classic());number>>result;
        if(number.fail()||!number.eof()||!std::isfinite(result))fail();
        return result;
    }
};
inline std::string encode(const Json& json){
    return std::visit([](const auto& v)->std::string{using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,std::nullptr_t>)return "null";
        else if constexpr(std::is_same_v<T,bool>)return v?"true":"false";
        else if constexpr(std::is_same_v<T,double>){if(!std::isfinite(v))throw std::invalid_argument("Non-finite JSON number");
            std::ostringstream number;number.imbue(std::locale::classic());number<<std::setprecision(std::numeric_limits<double>::max_digits10)<<v;return number.str();}
        else if constexpr(std::is_same_v<T,std::string>){std::string out="\"";const char* hex="0123456789abcdef";
            for(unsigned char c:v){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';}
        else if constexpr(std::is_same_v<T,Json::Array>){std::string out="[";for(const auto& e:v){if(out.size()>1)out+=',';out+=encode(e);}return out+']';}
        else {std::string out="{";for(const auto& [k,e]:v){if(out.size()>1)out+=',';out+=encode(Json{k})+':'+encode(e);}return out+'}';}
    },json.value);
}
inline Json key(const WidgetKey& k){return Json::Object{{"id",k.id},{"generation",std::to_string(k.generation)}};}
inline WidgetKey key(const Json& j){return {j.at("id").str(),integer(j.at("generation"))};}
inline Json rect(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
inline Json font(Font f){return Json::Object{{"size",f.size},{"bold",f.bold},{"tone",int(f.tone)}};}
inline Json color(Color c){return Json::Array{int(c.red),int(c.green),int(c.blue)};}
inline std::string base64(const std::vector<std::uint8_t>& bytes){
    constexpr auto chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;out.reserve((bytes.size()+2)/3*4);
    for(std::size_t i=0;i<bytes.size();i+=3){const auto n=std::uint32_t(bytes[i])<<16|(i+1<bytes.size()?std::uint32_t(bytes[i+1])<<8:0)|(i+2<bytes.size()?bytes[i+2]:0);
        out+=chars[n>>18];out+=chars[(n>>12)&63];out+=i+1<bytes.size()?chars[(n>>6)&63]:'=';out+=i+2<bytes.size()?chars[n&63]:'=';}return out;
}
inline Json options(const std::vector<Option>& values){Json::Array out;for(const auto& o:values){
    out.emplace_back(Json::Object{{"id",o.id},{"label",o.label},{"value",o.value},{"enabled",o.enabled}});}return out;}
}

namespace gui {
// Browser measurements arrive asynchronously. The first layout uses a declared
// monospace estimate; exact DOM measurements then request a shared relayout.
// Cache keys include all layout/font/scale inputs; old request IDs are rejected.
class WebAdapter final:public RetainedAdapter {
public:
    explicit WebAdapter(EventSink sink={}):RetainedAdapter(std::move(sink)){}
    void present(Snapshot next) override {
        if(next.widgets.size()>4096)throw std::length_error("Browser profile permits at most 4096 widgets");
        std::size_t strings=next.title.size(),records=0,cells=0,option_count=0,pixels=0;
        if(next.pages.size()>4096||next.key_bindings.size()>4096)throw std::length_error("Browser navigation limit");
        for(const auto& page:next.pages)strings+=page.id.size()+page.label.size();
        if(strings>4*1024*1024)throw std::length_error("Browser presentation text limit");
        for(const auto& w:next.widgets){
            strings+=w.spec.key.id.size()+w.spec.page.size()+w.spec.parent.size()+w.spec.binding.size()+w.state.bitmap.source_id.size();
            for(const auto* text:{&w.state.label,&w.state.text,&w.state.help,&w.state.accessible_name,&w.state.placeholder,&w.state.display_text})strings+=text->size();
            for(const auto* options:{&w.state.options,&w.state.actions}){option_count+=options->size();for(const auto& o:*options)strings+=o.id.size()+o.label.size()+o.value.size();}
            records+=w.state.records.size();for(const auto& r:w.state.records){strings+=r.id.size()+r.accessible_text.size();cells+=r.cells.size();for(const auto& c:r.cells)strings+=c.text.size();}
            if(w.spec.kind==Kind::bitmap){const auto grid=device_rect(w.state.bounds,next.display_scale);
                const auto bytes=bitmap_detail::multiply(pixel_row_bytes(grid.width,PixelFormat::rgb24),grid.height);
                if(bytes>16*1024*1024||pixels>16*1024*1024-bytes)throw std::length_error("Browser bitmap storage limit");
                pixels+=bytes;}
            if(strings>4*1024*1024||records>65536||cells>65536||option_count>65536||pixels>16*1024*1024)throw std::length_error("Browser presentation exceeds profile storage limits");
        }
        RetainedAdapter::present(std::move(next));
        for(auto it=scroll_commands_.begin();it!=scroll_commands_.end();)
            if(!find_widget(snapshot(),{it->first.first,it->first.second}))it=scroll_commands_.erase(it);else ++it;
        if(popup_){const auto a=resolved_availability(*popup_);if(!a.visible||!a.enabled){popup_.reset();popup_options_.clear();}}
    }
    bool open_popup(const WidgetKey& target) override {
        if(!RetainedAdapter::open_popup(target))return false;
        const auto& w=*find_widget(snapshot(),target);popup_=target;
        popup_options_=w.spec.kind==Kind::bitmap?w.state.actions:w.state.options;++popup_revision_;return true;
    }
    void close_popup(const WidgetKey& target) override {RetainedAdapter::close_popup(target);if(popup_==target){popup_.reset();popup_options_.clear();}}
    void close() override {RetainedAdapter::close();popup_.reset();popup_options_.clear();measurements_.clear();scroll_commands_.clear();}
    void scroll(const WidgetKey& target,Point offset) override {
        RetainedAdapter::scroll(target,offset);scroll_commands_[{target.id,target.generation}]=++scroll_revision_;
    }
    void choose_popup(const WidgetKey& target,const std::string& id){
        policy().require_interaction();
        if(popup_!=target)return;
        const auto selected=std::find_if(popup_options_.begin(),popup_options_.end(),[&](const Option& o){return o.id==id;});
        const auto index=std::size_t(selected-popup_options_.begin());popup_.reset();popup_options_.clear();policy().choose_popup(target,index);
    }
    Size measure_text(const TextMeasureRequest& r) const override {
        if(closed())throw std::logic_error("Adapter is closed");
        validate_measure_request(r);const auto data=web_detail::Json::Object{{"text",r.text},{"font",web_detail::font(r.font)},
            {"width",r.available_width},{"scale",r.display_scale},{"wrap",r.wrap==TextWrap::word}};
        const auto cache_key=web_detail::encode(data);auto found=measurements_.find(cache_key);
        if(found!=measurements_.end())return found->second.size;
        std::size_t chars=0;for(unsigned char c:r.text)if((c&0xc0)!=0x80)++chars;
        const auto width=double(chars)*r.font.size*0.61;
        const bool wraps=r.wrap==TextWrap::word&&r.available_width>0;
        Size fallback{wraps?std::min(width,r.available_width):width,r.font.size*1.25*(wraps?std::max(1.0,std::ceil(width/r.available_width)):1)};
        if(measurements_.size()>=512)measurements_.clear();
        measurements_.emplace(cache_key,Measurement{std::to_string(next_measure_++),data,fallback,false});return fallback;
    }
    bool measured(const web_detail::Json& values){bool changed=false;if(values.array().size()>512)throw std::length_error("Too many measurements");
        policy().require_interaction();
        auto next=measurements_;
        for(const auto& item:values.array()){const auto& id=item.at("id").str();const Size size{item.at("width").number(),item.at("height").number()};
            if(!valid_rect({0,0,size.width,size.height}))throw std::invalid_argument("Invalid measurement");
            for(auto& [key,m]:next){(void)key;if(m.id==id&&!m.exact){changed|=std::abs(m.size.width-size.width)>.01||std::abs(m.size.height-size.height)>.01;m.size=size;m.exact=true;break;}}}
        measurements_=std::move(next);return changed;
    }
    web_detail::Json presentation(){using namespace web_detail;Json::Array widgets,pages,measurements,bindings;
        if(!closed())for(const auto& paint_key:paint_order(snapshot())){const auto& w=*find_widget(snapshot(),paint_key);const auto& s=w.spec;const auto& v=w.state;const auto a=resolved_availability(s.key);
            Json::Array records;for(const auto& r:v.records){Json::Array cells;for(const auto& c:r.cells){
                cells.emplace_back(Json::Object{{"text",c.text},{"bounds",rect(c.bounds)},{"font",font(c.font)},{"wrap",c.wrap==TextWrap::word}});}
                records.emplace_back(Json::Object{{"id",r.id},{"text",r.accessible_text},{"enabled",r.enabled},{"activatable",r.activatable},{"cells",std::move(cells)}});}
            Json::Object out{{"key",key(s.key)},{"kind",int(s.kind)},{"parent",s.parent},{"page",s.page},{"bounds",rect(a.bounds)},{"clip",rect(a.clip)},
                {"visible",a.visible},{"enabled",a.enabled},{"inModal",in_modal_scope(snapshot(),s.key)},{"label",v.label},{"text",v.text},{"help",v.help},{"accessibleName",v.accessible_name},
                {"placeholder",v.placeholder},{"displayText",v.display_text},{"font",font(v.font)},{"wrap",v.wrap==TextWrap::word},{"checked",v.checked},
                {"selected",v.selected?Json{*v.selected}:Json{}},{"options",options(v.options)},{"actions",options(v.actions)},{"records",std::move(records)},
                {"rowHeight",s.row_height},{"activateOnSelect",s.activate_on_select},{"followTail",s.follow_tail},{"pointerInput",s.pointer_input},
                {"multiline",s.text_policy.multiline},{"readOnly",s.text_policy.read_only},{"maxBytes",std::to_string(s.text_policy.max_bytes)},{"submit",int(s.text_policy.submit)},
                {"contentSize",Json::Array{v.content_size.width,v.content_size.height}}};
            if(s.kind==Kind::group||s.kind==Kind::list||s.kind==Kind::text){const auto offset=scroll_offset(s.key);out["scroll"]=Json::Array{offset.x,offset.y};
                const auto found=scroll_commands_.find({s.key.id,s.key.generation});out["scrollRevision"]=std::to_string(found==scroll_commands_.end()?0:found->second);}
            if(v.content_clip)out["contentClip"]=rect(*v.content_clip);
            if(s.kind==Kind::text){const auto selection=text_selection(s.key);out["selection"]=Json::Array{double(selection.anchor),double(selection.caret)};}
            if(s.kind==Kind::bitmap&&a.visible){policy().repaint(s.key);const auto& image=policy().image(s.key);
                // The browser receives only owned RGB bytes. RGB is requested in
                // the shared example; conversion keeps generic snapshots valid.
                BitmapImage rgb(image.width(),image.height());rgb.blit(0,0,image.block());
                out["image"]=Json::Object{{"width",double(rgb.width())},{"height",double(rgb.height())},{"rgb",base64(rgb.pixels())},
                    {"source",v.bitmap.source_id},{"revision",std::to_string(v.bitmap.revision)}};}
            widgets.emplace_back(std::move(out));}
        for(const auto& p:page_tabs(snapshot()))pages.emplace_back(Json::Object{{"id",p.id},{"label",p.label},{"bounds",rect(p.bounds)},{"enabled",p.enabled},{"selected",p.selected}});
        for(const auto& b:snapshot().key_bindings)bindings.emplace_back(Json::Object{{"key",int(b.key)},{"control",b.control},{"shift",b.shift},{"alt",b.alt}});
        for(const auto& [key,m]:measurements_){(void)key;if(!m.exact)measurements.emplace_back(Json::Object{{"id",m.id},{"request",m.request}});}
        Json popup;if(popup_){const auto a=resolved_availability(*popup_);popup=Json::Object{{"key",key(*popup_)},{"revision",std::to_string(popup_revision_)},{"bounds",rect(a.bounds)},{"options",options(popup_options_)}};}
        const auto& p=snapshot().palette;const Json palette=Json::Object{{"background",color(p.background)},{"surface",color(p.surface)},
            {"text",color(p.text)},{"muted",color(p.muted)},{"accent",color(p.accent)},{"error",color(p.error)},
            {"border",color(p.border)},{"selection",color(p.selection)},{"disabled",color(p.disabled)}};
        const auto focused_key=focused();return Json::Object{{"revision",std::to_string(snapshot().revision)},{"title",snapshot().title},{"palette",palette},
            {"width",snapshot().client_size.width},{"height",snapshot().client_size.height},{"scale",snapshot().display_scale},{"pages",std::move(pages)},
            {"activePage",snapshot().active_page?Json{*snapshot().active_page}:Json{}},{"widgets",std::move(widgets)},{"measurements",std::move(measurements)},{"keyBindings",std::move(bindings)},
            {"focus",focused_key?key(*focused_key):Json{}},{"popup",popup},{"closed",closed()}};
    }
private:
    struct Measurement {std::string id;web_detail::Json request;Size size;bool exact=false;};
    mutable std::map<std::string,Measurement> measurements_;mutable std::uint64_t next_measure_=1;
    std::optional<WidgetKey> popup_;std::vector<Option> popup_options_;std::uint64_t popup_revision_=0;
    std::map<std::pair<std::string,std::uint64_t>,std::uint64_t> scroll_commands_;std::uint64_t scroll_revision_=0;
};

// One logical browser session. Sequence validation is separate from widget
// generation validation. Consumed operations are acknowledged even if the
// application rejects them; an uncertain transport can retry the same sequence
// without re-running an action. A new process/session must have a new epoch.
class WebSession {
public:
    using NextService=std::function<std::optional<ServiceRequest>()>;
    using CompleteService=std::function<bool(ServiceResult)>;
    WebSession(WebAdapter& adapter,std::string epoch,NextService next={},CompleteService complete={})
        :adapter_(adapter),epoch_(std::move(epoch)),next_(std::move(next)),complete_(std::move(complete)){
        if(epoch_.empty()||!valid_utf8(epoch_))throw std::invalid_argument("Invalid session epoch");}
    std::string initial(){return response();}
    std::string receive(std::string_view message){
        try {const auto input=web_detail::Parser(message).parse();
            if(input.at("epoch").str()!=epoch_)return response("Stale session epoch");
            const auto seq=web_detail::integer(input.at("seq"));
            if(seq<=ack_)return response("Duplicate operation ignored");
            if(ack_==std::numeric_limits<std::uint64_t>::max()||seq!=ack_+1)return response("Out-of-order operation");
            // Consume before dispatch: failures after an application side effect
            // cannot cause the command to be re-executed by a network retry.
            ack_=seq;dispatch(input.at("operation"));return response();
        }catch(const std::exception& error){return response(error.what());}
    }
private:
    WebAdapter& adapter_;std::string epoch_;std::uint64_t ack_=0;NextService next_;CompleteService complete_;std::optional<ServiceRequest> service_;
    std::optional<web_detail::Json> last_presentation_;
    std::string response(std::string error={}){using namespace web_detail;
        if(adapter_.closed())service_.reset();
        if(!service_&&next_&&!adapter_.closed())service_=next_();
        if(service_&&(service_->title.size()>65536||service_->value.size()>65536||service_->byte_limit>65536)){
            const auto id=service_->id;service_.reset();error="Browser service text/byte limit exceeds 64 KiB";
            if(complete_)complete_(ServiceResult{id,ServiceStatus::error,{},error});
        }
        Json service;
        if(service_)service=Json::Object{{"id",std::to_string(service_->id)},{"kind",int(service_->kind)},{"title",service_->title},{"value",service_->value},{"byteLimit",std::to_string(service_->byte_limit)}};
        try{last_presentation_=adapter_.presentation();}catch(const std::exception& failure){
            if(!last_presentation_)throw;
            error=std::string("Presentation failed; previous frame retained: ")+failure.what();
        }
        return encode(Json::Object{{"epoch",epoch_},{"ack",std::to_string(ack_)},{"error",std::move(error)},
            {"snapshot",*last_presentation_},{"service",std::move(service)}});
    }
    void dispatch(const web_detail::Json& op){using namespace web_detail;const auto& type=op.at("type").str();
        if(type=="poll")return;
        if(adapter_.closed())return;
        if(type=="measure"){if(adapter_.measured(op.at("values")))adapter_.policy().send(ResizeEvent{adapter_.snapshot().client_size,adapter_.snapshot().display_scale});return;}
        if(type=="service"){if(!service_||integer(op.at("id"))!=service_->id)throw std::invalid_argument("Stale service reply");
            const auto status=op.at("status").str();ServiceResult result{service_->id,status=="success"?ServiceStatus::success:status=="cancelled"?ServiceStatus::cancelled:ServiceStatus::error,op.at("value").str(),op.at("error").str()};
            if(!complete_||!complete_(std::move(result)))throw std::invalid_argument("Service reply rejected");
            service_.reset();return;}
        if(type=="resize"){const Size size{op.at("width").number(),op.at("height").number()};
            if(size.width>4096||size.height>4096)throw std::length_error("Browser client size exceeds 4096");
            adapter_.policy().send(ResizeEvent{size,op.at("scale").number()});return;}
        if(type=="page"){adapter_.policy().send(PageEvent{op.at("id").str()});return;}
        if(type=="shortcut"){const auto code=op.at("key").number();
            if(code<0||code>int(ShortcutKey::f12)||std::floor(code)!=code)throw std::invalid_argument("Unknown shortcut");
            adapter_.policy().send(ShortcutEvent{static_cast<ShortcutKey>(int(code)),op.at("control").boolean(),op.at("shift").boolean(),op.at("alt").boolean()});return;}
        if(type=="close"){adapter_.policy().send(CloseEvent{});return;}
        const auto target=key(op.at("key"));const auto* widget=find_widget(adapter_.snapshot(),target);
        if(!widget)throw std::invalid_argument("Stale widget generation");
        if(type=="focus"){adapter_.focus(target);return;}
        if(type=="popupOpen"){adapter_.open_popup(target);return;}
        if(type=="popupClose"){adapter_.close_popup(target);return;}
        if(type=="popupChoice"){adapter_.choose_popup(target,op.at("id").str());return;}
        if(type=="listKey"){const auto& value=op.at("value").str();
            if(value!="up"&&value!="down"&&value!="space"&&value!="enter")throw std::invalid_argument("Unknown list key");
            adapter_.policy().list_key(target,value=="up"?ListKey::up:value=="down"?ListKey::down:value=="space"?ListKey::space:ListKey::enter);return;}
        if(type=="selection"){adapter_.text_selection(target,{std::size_t(integer(op.at("anchor"))),std::size_t(integer(op.at("caret")))});return;}
        if(type=="scroll"){adapter_.policy().scroll(target,{op.at("x").number(),op.at("y").number()});return;}
        Input event;
        if(type=="activate")event=Activate{};
        else if(type=="checked")event=SetChecked{op.at("value").boolean()};
        else if(type=="edit")event=EditText{op.at("value").str(),op.at("base").str()};
        else if(type=="choose")event=ChooseOption{op.at("id").str()};
        else if(type=="select")event=SelectRecord{op.at("id").str()};
        else if(type=="activateRecord")event=ActivateRecord{op.at("id").str()};
        else if(type=="submit")event=SubmitText{};
        else if(type=="action")event=InvokeAction{op.at("id").str()};
        else if(type=="pointer"){const auto& kind=op.at("kind").str();if(kind!="click"&&kind!="double"&&kind!="move"&&kind!="wheel")throw std::invalid_argument("Unknown pointer kind");
            event=PointerInput{kind=="click"?PointerKind::click:kind=="double"?PointerKind::double_click:kind=="move"?PointerKind::move:PointerKind::wheel,
                {op.at("x").number(),op.at("y").number()},op.at("wheelX").number(),op.at("wheelY").number(),op.at("control").boolean(),op.at("shift").boolean(),op.at("alt").boolean()};}
        else throw std::invalid_argument("Unknown browser operation");
        adapter_.policy().send(WidgetEvent{target,std::move(event)});
    }
};
}
