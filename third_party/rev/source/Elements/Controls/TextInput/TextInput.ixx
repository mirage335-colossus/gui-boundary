module;

#include <string>
#include <algorithm>
#include <managed.hpp>

export module Rev.Element.TextInput;

import Rev.Core.Pos;
import Rev.Core.Resource;
import Rev.Core.Observable;
import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.ControlTheme;

export namespace Rev::Element {
    using namespace Rev::Core;
    using namespace Rev::Appearance;

    using namespace ControlTheme;

    // Decorative only — visible but never hit-tested or focused.
    struct PlaceholderText : public Text {
        PlaceholderText(Element* parent, std::string content, StyleList styles = {})
            : Text(parent, content, styles) {}

        bool contains(Pos& pos) override {
            return false;
        }
    };

    struct TextInput : public Element {
        struct Params {
            std::string label;
            std::string placeholder;
            size_t maxLength = 500;
            bool selectAllOnFocus = true;
            // Disabled / locked: the value is shown but cannot be edited or
            // focused, and the field renders in a muted, display-only style.
            bool disabled = false;

            static Params Default() {
                return {
                    .label = "Text Input",
                    .placeholder = "Enter text...",
                    .maxLength = 500,
                    .selectAllOnFocus = true
                };
            };
        };

        Params params;
        Observable<bool> value;
        bool disabled = false;

        Text* label = nullptr;
        Box* container = nullptr;
        Box* field = nullptr;
        PlaceholderText* placeholderText = nullptr;
        Text* text = nullptr;

        TextInput(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Element(parent, styles) {
            this->name = "TextInput";
            // Prepend (not add) so caller-supplied margins/padding win over the
            // Control defaults — matches Dropdown, keeping inputs and dropdowns
            // vertically aligned when they share a row (e.g. RowField top:0).
            this->styles.prepend(&Control);
            this->params = p;

            label = new Text(this, params.label, { &Label });
            container = new Box(this, { &Field, &FieldFocus, &FieldDisabled });
            field = new Box(container, { &FieldInner });
            placeholderText = new PlaceholderText(field, params.placeholder, { &Placeholder });
            text = new Text(field, "", { &FieldText, &FieldTextDisabled });

            text->editable = true;
            text->selectable = true;
            text->selectAllOnFocus = params.selectAllOnFocus;

            setDisabled(params.disabled);
        }

        // Lock/unlock the input.  A disabled input shows its value but cannot be
        // edited, focused, or selected, and renders muted (display-only) -- used,
        // e.g., for coordinates that are derived/locked (the work frame's Y/Z, which
        // are pinned to the rotary axis) while leaving editable siblings (the X) live.
        void setDisabled(bool d) {
            disabled = d;

            // Drive the disabled style state so the *Disabled styles apply, on
            // every element that carries one.
            container->setDisabled(d);
            field->setDisabled(d);
            text->setDisabled(d);

            // A locked field is inert: not editable, not focusable/selectable.
            text->editable   = !d;
            text->selectable = !d;

            // If we lock while focused, drop focus so no caret lingers.
            if (d && text->targetFlags.focus) {
                text->targetFlags.focus = false;
                text->dirty.style = true;
            }
        }

        void keyDown(Event& e) override {
            if (disabled) return;   // locked: display-only, ignore keys
            tell(&Element::keyDown, e);
            if (!e.propagate) return;
            if (text->targetFlags.focus) text->keyDown(e);
        }

        void textInput(Event& e) override {
            if (disabled) return;   // locked: display-only, ignore input
            tell(&Element::textInput, e);
            if (!e.propagate || !text->targetFlags.focus) {
                return;
            }

            if (!acceptProposedContent(previewAfterInput(text, e.keyboard.input))) {
                return;
            }

            text->textInput(e);
        }

        static std::string previewAfterInput(Text* text, const std::string& input) {
            std::string current = text->content.get();
            int left = std::min(text->selectAnchor, text->selectEnd);
            int right = std::max(text->selectAnchor, text->selectEnd);

            if (left != right) {
                current.erase((size_t)left, (size_t)(right - left));
                current.insert((size_t)left, input);
                return current;
            }

            int pos = std::clamp(text->cursor, 0, (int)current.size());
            current.insert((size_t)pos, input);
            return current;
        }

        virtual bool acceptProposedContent(const std::string& proposed) const {
            if (proposed.find_first_of("\r\n") != std::string::npos) {
                return false;
            }

            return !(params.maxLength > 0 && proposed.size() > params.maxLength);
        }

        void computeStyle(Event& e) override {
            // Keep the disabled style state in sync (idempotent; survives any
            // external reset).
            container->setDisabled(disabled);
            field->setDisabled(disabled);
            text->setDisabled(disabled);

            bool showFieldFocus = !disabled && text->targetFlags.focus;

            if (container->targetFlags.focus != showFieldFocus) {
                container->targetFlags.focus = showFieldFocus;
                container->dirty.style = true;
            }

            Visibility visibility = text->content.get().empty() ? Visibility::Visible : Visibility::Hidden;

            if (placeholderText->style->visibility != visibility) {
                placeholderText->style->visibility = visibility;
                placeholderText->dirty.style = true;
            }

            Element::computeStyle(e);
        }
    };
}
