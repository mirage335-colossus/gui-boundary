module;

#include <cmath>
#include <cstdio>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <rev_utf8.hpp>

#include <managed.hpp>
#include <dbg.hpp>

export module Rev.Element.Text;

import Rev.Appearance;
import Rev.Element.Event;
import Rev.Element.Box;

import Rev.Core.Pos;
import Rev.Core.Resource;
import Rev.Core.Observable;
import Rev.Core.Font;

import Rev.Primitive.Text;
import Rev.Primitive.Lines;

export namespace Rev::Element {

    using namespace Rev::Core;
    using namespace Rev::Appearance;

    namespace TextStyles {
        
        Style TextDefaults = {
            .text = {
                .font = File("Rev/resources/Fonts/Arial/Arial.ttf"),
                .color = rgba(0, 0, 0, 1),
                .size = 12_px,
            }
        };
    };

    using namespace TextStyles;

    struct Text : public Box {

        Primitives::Text* text = nullptr;
        Primitives::Lines* line = nullptr;

        Observable<std::string> content;
        Observable<bool> editable;
        Observable<bool> selectable;

        // On first focus, select all (runs after mouseDown on the same click and overrides it).
        bool selectAllOnFocus = false;

        std::string strContent;
        float fontSize = 12.0f;
        Font* font = nullptr;

        float width=0, height=0;
        float allocatedTextWidth=0;
        float minWidth, minHeight;
        float maxWidth, maxHeight;

        // Create
        Text(Element* parent, std::string content = "Hello World", StyleList styles = {}) : Box(parent, styles, "Text") {

            text = new Primitives::Text(shared->canvas);
            line = new Primitives::Lines(shared->canvas);

            this->styles.prepend(&TextStyles::TextDefaults);
            this->content = content;
        }

        // Destroy
        ~Text() {

            delete text;
            delete line;
        }

        // Managing content
        //--------------------------------------------------

        // Set content as a value
        void addContent(float val, int digits = 4) {

            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", 10, val);

            size_t count = 0;
            size_t point = 0;

            for (char& c : buffer) {
                
                if (c != '.') { count += 1; }
                else if (count ==  digits) { c = '\0'; break; }

                if (count > digits) { c = '\0'; break; }
            }

            content += buffer;
        }

        // Set content as a string
        void addContent(std::string value) {
            this->content = this->content.get() + std::move(value);
        }

        void setContent(std::string value) {
            this->content = std::move(value);
        }

        void setContent(float val, int digits = 4) {

            content = "";
            addContent(val, digits);
        }

        void insertAt(int pos, const std::string& toInsert) {
    
            // Read the current string out of the observable
            std::string str = content;
        
            // Clamp position (safety)
            pos = static_cast<int>(RevUtf8::boundary(str,static_cast<size_t>(std::max(0,pos))));
            str.insert(pos, toInsert);
        
            content = str;

            setCursorPos(cursor + (int)toInsert.size());
            selectEnd = selectAnchor = cursor;
        }

        void replaceAt(int posStart, int posEnd, const std::string& toInsert) {
            resetVerticalCursor();
    
            // Canonical ordering
            int left = std::min(posStart, posEnd);
            int right = std::max(posStart, posEnd);
        
            std::string str = content;
            int len = (int)str.size();
        
            // Clamp boundaries
            left = static_cast<int>(RevUtf8::boundary(str,static_cast<size_t>(std::max(0,left))));
            right = static_cast<int>(RevUtf8::boundary(str,static_cast<size_t>(std::max(0,right))));
        
            // Erase the range
            str.erase(left, right - left);
        
            // Insert new text
            str.insert(left, toInsert);
        
            // Update observable string
            content = str;
        
            // Update cursor position: at end of inserted text
            cursor = left + (int)toInsert.size();
        
            // Clear selection
            selectAnchor = cursor;
            selectEnd = cursor;
        }

        void deleteAt(int pos,int n) {
            resetVerticalCursor();
            auto str=content.get();
            size_t start=RevUtf8::boundary(str,static_cast<size_t>(std::max(0,pos))),end=start;
            if(n<0) for(int i=0;i>n&&start>0;--i) start=RevUtf8::previous(str,start);
            else for(int i=0;i<n&&end<str.size();++i) end=RevUtf8::next(str,end).end;
            if(end==start) return;
            str.erase(start,end-start); content=str;
            cursor=static_cast<int>(start); selectAnchor=selectEnd=cursor;
        }

        // Managing input
        //--------------------------------------------------

        int cursor = 0;
        int selectAnchor = 0, selectEnd = 0;
        float preferredCursorX=std::numeric_limits<float>::quiet_NaN();
        void resetVerticalCursor() { preferredCursorX=std::numeric_limits<float>::quiet_NaN(); }
        size_t cursorLineIndex() const {
            size_t found=0;
            for(size_t i=0;i<text->lines.size();++i) {
                const auto& candidate=text->lines[i];
                if(candidate.start<=static_cast<size_t>(std::max(0,cursor))&&
                    static_cast<size_t>(std::max(0,cursor))<=candidate.end+1) found=i;
            }
            return found;
        }
        float cursorOffsetOnLine(const Primitives::Text::Line& current) {
            float x=0;
            for(const auto rune:RevUtf8::runes(current.content)) {
                if(current.start+rune.begin>=static_cast<size_t>(std::max(0,cursor))) break;
                x+=font->getGlyph(rune.value).advance;
            }
            return x;
        }

        void setCursorPos(int newCursor) {
            resetVerticalCursor();
            cursor = static_cast<int>(RevUtf8::boundary(content.get(),static_cast<size_t>(std::max(0,newCursor))));
        }

        void selectAll() {
            resetVerticalCursor();

            int len = (int)content.get().size();

            selectAnchor = 0;
            selectEnd = len;

            if (editable) {
                cursor = len;
            }
        }

        int getCursorPosOnLine(Primitives::Text::Line& line, float x) {

            if (!font) {
                return line.start;
            }

            float left = line.rect.x;

            if (x <= left) {
                return line.start;
            }

            int idx = line.start;

            for (const auto rune : RevUtf8::runes(line.content)) {
                const auto c=rune.value;

                float right = left + font->getGlyph(c).advance;

                if (x <= right) {

                    float distLeft = x - left;
                    float distRight = right - x;

                    return distLeft < distRight ? idx : idx + static_cast<int>(rune.end-rune.begin);
                }

                left = right;
                idx += static_cast<int>(rune.end-rune.begin);
            }

            return line.end + 1;
        }

        Primitives::Text::Line* nearestLineByY(float y) {

            if (text->lines.empty()) {
                return nullptr;
            }

            Primitives::Text::Line* nearest = &text->lines.front();
            float best = std::abs(
                y - (nearest->rect.y + nearest->rect.h * 0.5f)
            );

            for (Primitives::Text::Line& line : text->lines) {

                float mid = line.rect.y + line.rect.h * 0.5f;
                float dist = std::abs(y - mid);

                if (dist < best) {
                    best = dist;
                    nearest = &line;
                }
            }

            return nearest;
        }

        int getCursorPos(Core::Pos pos) {

            int len = (int)strContent.size();

            if (text->lines.empty()) {
                return std::clamp(0, 0, len);
            }

            Primitives::Text::Line& first = text->lines.front();
            Primitives::Text::Line& last = text->lines.back();

            float top = first.rect.y;
            float bottom = last.rect.y + last.rect.h;

            if (pos.y < top) {
                return std::clamp(getCursorPosOnLine(first, pos.x), 0, len);
            }

            if (pos.y >= bottom) {
                return std::clamp(getCursorPosOnLine(last, pos.x), 0, len);
            }

            for (Primitives::Text::Line& line : text->lines) {

                if (line.rect.y > pos.y) { continue; }
                if (line.rect.y + line.rect.h < pos.y) { continue; }

                return std::clamp(getCursorPosOnLine(line, pos.x), 0, len);
            }

            Primitives::Text::Line* nearest = nearestLineByY(pos.y);

            if (!nearest) {
                return 0;
            }

            return std::clamp(getCursorPosOnLine(*nearest, pos.x), 0, len);
        }

        static bool isWordChar(unsigned char c) {
            return c>=128 || std::isalnum(c) != 0 || c == '_';
        }

        void selectWordAt(Pos pos) {

            std::string str = content;
            int len = (int)str.size();
            int idx = getCursorPos(pos);

            idx = std::clamp(idx, 0, len);

            int start = idx;
            int end = idx;

            auto expandFrom = [&](int at) {

                int s = at;
                int e = at;

                if (at < len && isWordChar((unsigned char)str[at])) {
                    while (s > 0 && isWordChar((unsigned char)str[s - 1])) { s--; }
                    while (e < len && isWordChar((unsigned char)str[e])) { e++; }
                }

                return std::pair<int, int>{ s, e };
            };

            if (idx < len && isWordChar((unsigned char)str[idx])) {
                auto range = expandFrom(idx);
                start = range.first;
                end = range.second;
            }

            else if (idx > 0 && isWordChar((unsigned char)str[idx - 1])) {
                auto range = expandFrom(idx - 1);
                start = range.first;
                end = range.second;
            }

            selectAnchor = start;
            selectEnd = end;

            if (editable) {
                cursor = end;
            }
        }

        void gainFocus(Event& e) override {

            bool wasFocused = targetFlags.focus;

            Box::gainFocus(e);

            // Register as the focused editable so view-level key handlers defer.
            if (editable && shared) {
                shared->focusedText = this;
            }

            if (selectAllOnFocus && !wasFocused) {
                selectAll();
                refresh(e);
            }
        }

        void loseFocus(Event& e) override {

            Box::loseFocus(e);

            if (shared && shared->focusedText == this) {
                shared->focusedText = nullptr;
            }
        }

        void mouseDown(Event& e) override {
            resetVerticalCursor();

            // If none apply, skip
            if (!(editable || selectable)) { return Box::mouseDown(e); }

            if (e.mouse.lb.isDoubleClick()) {
                selectWordAt(e.mouse.pos);
                this->refresh(e);
                Box::mouseDown(e);
                return;
            }

            // If we can select (or edit), modify select region
            if (selectable || editable) {
                if (e.keyboard.shift) { selectEnd = this->getCursorPos(e.mouse.pos); }
                else { selectEnd = selectAnchor = this->getCursorPos(e.mouse.pos); }
            }

            // If we can edit, move the cursor also
            if (editable) {
                cursor = selectEnd;
            }

            this->refresh(e);
            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {
            resetVerticalCursor();

            // If none apply, skip
            if (!(editable || selectable)) { return Box::mouseDrag(e); }

            // If we can select (or edit), modify select region
            if (selectable || editable) {
                selectEnd = this->getCursorPos(e.mouse.pos);
            }

            // If we can edit, move the cursor also
            if (editable) {
                cursor = selectEnd;
            }

            this->refresh(e);
            Box::mouseDrag(e);
        }

        void keyDown(Event& e) override {

            // Ctrl+A selects all. Lives here (not in TextInput) because Text owns
            // the selection model; works for any selectable or editable text.
            if (e.keyboard.ctrl && e.keyboard.key == "a" && (editable || selectable)) {
                selectAll();
                e.propagate = false;
                this->refresh(e);
                return Box::keyDown(e);
            }

            // All keyboard interactions require edit ability
            if (!editable) { return Box::keyDown(e); }

            // Avoid ugly long names
            bool left = e.keyboard.arrows.left; bool right = e.keyboard.arrows.right;
            bool up = e.keyboard.arrows.up; bool down = e.keyboard.arrows.down;
            const bool home=e.keyboard.key=="home",end=e.keyboard.key=="end";
            if(!up&&!down) resetVerticalCursor();

            // If any arrow key, move the cursor
            if (left || right || up || down || home || end) {

                if (left) { cursor=static_cast<int>(RevUtf8::previous(content.get(),static_cast<size_t>(std::max(0,cursor)))); }
                else if (right) { cursor=static_cast<int>(RevUtf8::next(content.get(),static_cast<size_t>(std::max(0,cursor))).end); }

                if((up||down||home||end)&&!text->lines.empty()) {
                    auto index=cursorLineIndex();
                    auto& current=text->lines[index];
                    if(home) cursor=e.keyboard.ctrl?0:static_cast<int>(current.start);
                    else if(end) cursor=e.keyboard.ctrl?static_cast<int>(content.get().size()):static_cast<int>(current.end+1);
                    else {
                        if(!std::isfinite(preferredCursorX)) preferredCursorX=cursorOffsetOnLine(current);
                        if(up&&index>0) --index;
                        if(down&&index+1<text->lines.size()) ++index;
                        auto& target=text->lines[index];
                        cursor=getCursorPosOnLine(target,target.rect.x+preferredCursorX);
                    }
                }
                if (e.keyboard.shift) { selectEnd = cursor; }
                else { selectAnchor = selectEnd = cursor; }
            }

            // Delete back or forward
            if (selectEnd == selectAnchor) {
                if (e.keyboard.backspace) { this->deleteAt(cursor, -1); }
                if (e.keyboard.del) { this->deleteAt(cursor, +1); }    
            }

            // Replace in region
            else {
                if (e.keyboard.backspace) { this->replaceAt(selectAnchor, selectEnd, ""); }
                if (e.keyboard.del) { this->replaceAt(selectAnchor, selectEnd, ""); }
            }

            this->refresh(e);
            Box::keyDown(e);
        }

        void textInput(Event& e) override {

            // Interactions require editable
            if (!editable) { return Box::textInput(e); }

            // Ignore backspace (handled in keyDown)
            if (e.keyboard.input == "\b") { return Box::textInput(e); }

            // If just cursor, insert. If range, replace
            if (selectEnd == selectAnchor) { this->insertAt(cursor, e.keyboard.input); }
            else { this->replaceAt(selectAnchor, selectEnd, e.keyboard.input); }

            this->refresh(e);
            Box::textInput(e);
        }

        // Compute style/primitive/etc
        //--------------------------------------------------

        void computeStyle(Event& e) override {

            // Recompute style only if either of these changed
            if (content.changed() || editable.changed() || selectable.changed()) {
                this->dirty.style = true;
            }
            
            Box::computeStyle(e);
        }

        // Resolve style (requires measuring text)
        //--------------------------------------------------

        void resolveStyle(Event& e) override {

            Box::resolveStyle(e);

            strContent = content;
            fontSize = resolved.style.text.size.val;
            if (!fontSize) { fontSize = 12.0f; }

            Core::Resource fontResource = resolved.style.text.font;
            if (!fontResource.data) { fontResource = File("Rev/resources/Fonts/Arial/Arial.ttf"); }

            font = text->fontAtlas->get(fontResource, fontSize, shared->canvas->details.scale);

            text->fontResource = fontResource;
            text->font         = font;
            text->fontSize     = fontSize;
            text->content = strContent;

            // Content-driven minima live outside resolved.style, so a text edit
            // slips past the style-diff gate; raise the flag when they move.
            float prevMinContentWidth = resolved.minContentWidth;
            float prevMinContentHeight = resolved.minContentHeight;

            this->measureText();

            float minPaddingWidth = resolved.getMinPadding(Axis::Horizontal, Dist::Type::Abs);
            float minPaddingHeight = resolved.getMinPadding(Axis::Vertical, Dist::Type::Abs);

            resolved.minContentWidth = minWidth + minPaddingWidth;
            resolved.minContentHeight = minHeight + minPaddingHeight;

            if (shared && (
                resolved.minContentWidth != prevMinContentWidth ||
                resolved.minContentHeight != prevMinContentHeight
            )) {
                shared->layoutDirty = true;
            }
            
            // If we can edit or select, use text cursor
            if (editable || selectable) {
                resolved.style.cursor = Cursor::Caret;
            }
        }

        // Computing text layout
        //--------------------------------------------------

        void measureText() {

            struct Tracked {
                float current = 0;
                float max = 0;
                float min = 99999999.0f;
            };

            Font& fontRef = *font;
            Tracked letter, word, line;

            // Track theoretical min/max letter, word, and line
            //--------------------------------------------------
            
            // Iterate through each character in the content
            for (const auto rune : RevUtf8::runes(strContent)) {
                const auto c=rune.value;

                // Track current
                letter.current = fontRef.getGlyph(c).advance;
                word.current += letter.current;
                line.current += letter.current;

                // Always track max char
                letter.min = std::min(letter.min, letter.current);
                letter.max = std::max(letter.max, letter.current);

                bool wordBreak = (c == ' ');
                bool lineBreak = (c == '\n' || c == '\r');

                // End of word (lines also count)
                if (wordBreak || lineBreak) {
                    word.min = std::min(word.min, word.current);
                    word.max = std::max(word.max, word.current);
                    word.current = 0;
                }

                // End of line
                if (lineBreak) {
                    line.min = std::min(line.min, line.current);
                    line.max = std::max(line.max, line.current);
                    line.current = 0;
                }
            }

            // Min/max any that weren't caught in the loop
            //--------------------------------------------------

            letter.min = std::min(letter.min, letter.current);
            letter.max = std::max(letter.max, letter.current);

            word.min = std::min(word.min, word.current);
            word.max = std::max(word.max, word.current);

            line.min = std::min(line.min, line.current);
            line.max = std::max(line.max, line.current);

            // We set our actual min/max depending on the wrap mode
            switch (resolved.style.text.wrap) {

                case (Wrap::BreakChar): {
                    minWidth = letter.max;
                    maxWidth = line.max;
                    break;
                }

                case (Wrap::BreakWord): {
                    minWidth = word.max;
                    maxWidth = line.max;
                    break;
                }

                default: {
                    minWidth = line.min;
                    maxWidth = line.max;
                    break;
                }
            }

            // Add line height as min width
            minHeight = fontRef.lineHeight;
        }

        void layoutText() {
            if(!font) return;
            text->lines.clear();
            Primitives::Text::Line current{"",0,size_t(-1),{0,0,0,font->lineHeight}};
            size_t chunkStart=0,chunkEnd=0;
            float chunkWidth=0;
            const bool wrap=resolved.style.text.wrap!=Wrap::False;
            auto emit=[&] {
                current.end=current.start+current.content.size()-1;
                text->lines.push_back(current);
                current={"",chunkStart,chunkStart-1,{0,0,0,font->lineHeight}};
            };
            auto append=[&] {
                if(chunkEnd==chunkStart) return;
                if(wrap&&!current.content.empty()&&current.rect.w+chunkWidth>maxWidth) emit();
                current.content.append(strContent,chunkStart,chunkEnd-chunkStart);
                current.rect.w+=chunkWidth;
                chunkStart=chunkEnd; chunkWidth=0;
            };
            for(const auto rune:RevUtf8::runes(strContent)) {
                if(rune.value=='\n'||rune.value=='\r') {
                    append(); emit();
                    chunkStart=chunkEnd=rune.end;
                    current.start=rune.end; current.end=rune.end-1;
                    continue;
                }
                chunkEnd=rune.end; chunkWidth+=font->getGlyph(rune.value).advance;
                if(resolved.style.text.wrap==Wrap::BreakChar ||
                    (resolved.style.text.wrap==Wrap::BreakWord&&rune.value==' ') ||
                    (resolved.style.text.wrap==Wrap::BreakLine&&rune.value=='.')) append();
            }
            append(); emit();
            width=0; height=0;
            for(const auto& line:text->lines) { height+=line.rect.h; width=std::max(width,line.rect.w); }
        }

        // Here we compute the layout ourselves
        void computeLayout() override {

            layout = Layout();

            this->maxWidth = resolved.max.innerWidth;
            // Flex allocation happens top-down after intrinsic text layout.
            // Reuse the previous allocated width, then reconcile the new width
            // in computePrimitives once this pass has resolved actual bounds.
            if(resolved.style.text.wrap!=Wrap::False&&allocatedTextWidth>0)
                this->maxWidth=std::min(this->maxWidth,allocatedTextWidth);
            this->layoutText();

            layout.size.w = { .val = width, .min = width };
            layout.size.h = { .val = height, .min = height };
        }

        void computePrimitives(Event& e) override {
            const auto allocatedWidth=resolved.getInner(Axis::Horizontal);
            if(std::isfinite(allocatedWidth)&&allocatedWidth>0) allocatedTextWidth=allocatedWidth;
            if(resolved.style.text.wrap!=Wrap::False&&std::isfinite(allocatedWidth)&&allocatedWidth>0&&std::abs(maxWidth-allocatedWidth)>.5f) {
                const auto oldHeight=height,oldWidth=width;
                maxWidth=allocatedWidth; layoutText();
                if(oldHeight!=height||oldWidth!=width) {
                    shared->layoutDirty=true;
                    refresh(e);
                }
            }

            // Set font size
            text->fontSize = resolved.style.text.size.val;
            if (!text->fontSize) { text->fontSize = 12.0f; }

            // Set font color
            text->data->color = {
                resolved.style.text.color.r, resolved.style.text.color.g,
                resolved.style.text.color.b, resolved.style.text.color.a
            };

            // Cascaded opacity
            text->data->opacity = resolved.opacity;

            text->xPos = rect.x + resolved.pad.l.val;
            text->yPos = rect.y + resolved.pad.t.val;

            float runningY = 0;

            for (Primitives::Text::Line& line : text->lines) {

                line.rect.x = std::round(rect.x + resolved.pad.l.val);
                line.rect.y = std::round(runningY + rect.y + resolved.pad.t.val);

                runningY += line.rect.h;
            }

            text->compute();

            // Skip cursor/region calculations if not editable
            if (!editable) { return Box::computePrimitives(e); }

            // Place cursor at cursor pos
            //--------------------------------------------------

            line->lines.clear();

            for (Primitives::Text::Line& line : text->lines) {
                if(&line!=&text->lines[cursorLineIndex()]) continue;

                if (line.end + 1 < cursor) { continue; }
                if (line.start > cursor) { continue; }
            
                int idx = line.start;
                float cursor_x = line.rect.x;

                // Get x position at line56
                for (const auto rune : RevUtf8::runes(line.content)) {
                const auto c=rune.value;

                    if (idx == cursor) {
                        break;
                    }

                    cursor_x += font->getGlyph(c).advance;
                    idx += static_cast<int>(rune.end-rune.begin);
                }
                
                // Place line at cursor position
                this->line->lines.push_back({
                    .points = { { cursor_x, line.rect.y }, { cursor_x, line.rect.y + line.rect.h } }, 
                    .color = { resolved.style.text.color.r, resolved.style.text.color.g, resolved.style.text.color.b, 1 }, .strokeWidth = 1.0f, .smoothing = 0.0f
                });

                break;
            }

            // Highlight selected region(s)
            //--------------------------------------------------

            if (selectEnd != selectAnchor) {

                int leftMost = std::min(selectAnchor, selectEnd);
                int rightMost = std::max(selectAnchor, selectEnd);

                // Add select lines
                for (Primitives::Text::Line& line : text->lines) {

                    // Skip if this line would not contain what we're looking for
                    if (line.end + 1 < leftMost) { continue; }
                    if (line.start + 1 > rightMost) { continue; }

                    int idx = line.start;
                    float left_x = line.rect.x;
                    float right_x = line.rect.x;

                    for (const auto rune : RevUtf8::runes(line.content)) {
                const auto c=rune.value;

                        if (idx < leftMost) { left_x += font->getGlyph(c).advance; }
                        if (idx < rightMost) { right_x += font->getGlyph(c).advance; }

                        idx += static_cast<int>(rune.end-rune.begin);
                    }

                    if (right_x - left_x < 0.1) { right_x = left_x + 5.0f; }

                    float line_y = line.rect.y + 0.5f * line.rect.h;
                    float line_width = line.rect.h;

                    // Place line at cursor position
                    this->line->lines.push_back({
                        .points = { { left_x, line_y }, { right_x, line_y } }, 
                        .color = { 0, 0, 1, 0.2 }, .strokeWidth = line_width, .smoothing = 0.0f
                    });
                }
            }

            line->data->opacity = resolved.opacity;
            line->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            // Always draw text
            text->draw();

            // Draw line only if editable
            if (targetFlags.focus && editable) { line->draw(); }
        }
    };
};
