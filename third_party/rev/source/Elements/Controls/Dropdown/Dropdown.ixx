module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#include <managed.hpp>

export module Rev.Element.Dropdown;

import Rev.Core.Pos;
import Rev.Core.Resource;

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

    struct Dropdown : public Element {

        Text* label = nullptr;

        Box* dropdown = nullptr;
            Box* fieldRow = nullptr;
            Text* dropdownText = nullptr;
            Svg* dropdownArrow = nullptr;

        Box* optionsContainer = nullptr;
        std::vector<Text*> options;
        bool open = false;
        int menuHighlight = -1;

        struct Item {
            std::string name;
            std::string value;
            bool disabled = false;
        };

        struct Params {

            std::string label = "Dropdown";
            std::vector<Item> options;
            std::string placeholder;
            std::string value;
            bool openUpward = false;  // open the list above the field instead of below

            static Params Default() {
                return {
                    .label = "Dropdown",
                    .options = {
                        { "Select...", "" },
                        { "Option 1", "1" },
                        { "Option 2", "2" },
                        { "Disabled", "3", true }
                    },
                    .placeholder = "Select...",
                    .value = ""
                };
            };
        };

        Params params;

        std::function<void(Event&)> onChange;

        Dropdown(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Element(parent, styles) {

            name = "Dropdown";
            params = p;

            this->styles.prepend(&Control);

            label = new Text(this, p.label, { &Label });

            dropdown = new Box(this, { &Field, &DropdownSurface, &FieldFocus });

                fieldRow = new Box(dropdown, { &FieldInner });
                    dropdownText = new Text(fieldRow, "", { &FieldText });
                    dropdownArrow = new Svg(
                        fieldRow,
                        File("Rev/source/Elements/Controls/Dropdown/chevron-right.svg"),
                        { &DropdownArrow }
                    );

                optionsContainer = new Box(dropdown, {
                    p.openUpward ? &OptionsContainerUpward : &OptionsContainer
                });
                optionsContainer->name = "OptionsContainer";

            dropdown->onLoseFocus([this](Event& e) {
                closeMenu(&e);
            });

            dropdown->onClick([this](Event& e) {
                if (optionsContainer->targetFlags.hit) { return; }
                if (open) { closeMenu(&e); }
                else { openMenu(); }
            });
        }

        void click(Event& e) override {
            Element::click(e);
            e.propagate = false;
        }

        void mouseDown(Event& e) override {
            Element::mouseDown(e);
            e.propagate = false;
        }

        void mouseUp(Event& e) override {
            Element::mouseUp(e);
            e.propagate = false;
        }

        void mouseMove(Event& e) override {
            Element::mouseMove(e);
            e.propagate = false;
        }

        void mouseDrag(Event& e) override {
            Element::mouseDrag(e);
            e.propagate = false;
        }

        void mouseEnter(Event& e) override {
            Element::mouseEnter(e);
            e.propagate = false;
        }

        void mouseLeave(Event& e) override {
            Element::mouseLeave(e);
            e.propagate = false;
        }

        void keyDown(Event& e) override {

            tell(&Element::keyDown, e);

            if (!e.propagate) {
                return;
            }

            handleMenuKeyDown(e);
        }

        bool isSelectable(const Item& item) const {
            return !item.disabled;
        }

        int indexOfValue(const std::string& val) const {

            for (size_t i = 0; i < params.options.size(); i++) {

                if (params.options[i].value == val) {
                    return (int)i;
                }
            }

            return -1;
        }

        int firstSelectableIndex() const {

            for (size_t i = 0; i < params.options.size(); i++) {

                if (isSelectable(params.options[i])) {
                    return (int)i;
                }
            }

            return -1;
        }

        int nextSelectableIndex(int from, int direction) const {

            if (params.options.empty()) {
                return -1;
            }

            int count = (int)params.options.size();
            int index = from;

            for (int step = 0; step < count; step++) {

                index = (index + direction + count) % count;

                if (isSelectable(params.options[index])) {
                    return index;
                }
            }

            return -1;
        }

        void setMenuHighlight(int index, Event& e) {

            menuHighlight = index;
            refresh(e);
        }

        void confirmHighlighted(Event& e) {

            if (
                menuHighlight >= 0 &&
                menuHighlight < (int)params.options.size()
            ) {
                Item& item = params.options[menuHighlight];

                if (isSelectable(item)) {
                    select(item, &e);
                    return;
                }
            }

            closeMenu(&e);
            e.propagate = false;
        }

        void handleMenuKeyDown(Event& e) {

            bool up = e.keyboard.arrows.up;
            bool down = e.keyboard.arrows.down;
            bool enter = e.keyboard.enter;
            bool escape = e.keyboard.escape;

            if (!open) {

                if (enter) {
                    openMenu();
                    e.propagate = false;
                    return;
                }

                if (up || down) {
                    openMenu();
                    setMenuHighlight(
                        nextSelectableIndex(-1, up ? -1 : +1),
                        e
                    );
                    e.propagate = false;
                    return;
                }

                return;
            }

            if (escape) {
                closeMenu(&e);
                e.propagate = false;
                return;
            }

            if (up) {
                setMenuHighlight(
                    nextSelectableIndex(menuHighlight, -1),
                    e
                );
                e.propagate = false;
                return;
            }

            if (down) {
                setMenuHighlight(
                    nextSelectableIndex(menuHighlight, +1),
                    e
                );
                e.propagate = false;
                return;
            }

            if (enter) {
                confirmHighlighted(e);
                return;
            }
        }

        Item getItemWithVal(const std::string& val) const {

            for (const Item& item : params.options) {

                if (item.value == val) {
                    return item;
                }
            }

            return { params.placeholder, "" };
        }

        Item getItemWithName(const std::string& name) const {

            for (const Item& item : params.options) {

                if (item.name == name) {
                    return item;
                }
            }

            return { params.placeholder, "" };
        }

        void select(Item item, Event* event = nullptr) {

            params.value = item.value;
            dropdownText->content = item.name;

            closeMenu(event);

            if (onChange && event) {
                onChange(*event);
            }
        }

        void openMenu() {

            dropdownArrow->transition(
                &dropdownArrow->rotation,
                3.14159f / 2.0f,
                200
            );

            optionsContainer->style->visibility = Visibility::Visible;
            optionsContainer->dirty.style = true;       // mirror closeMenu — without
                                                        // this the next style resolve
                                                        // never picks up the change
                                                        // and the menu stays hidden.
            optionsContainer->interceptHits = true;
            interceptHits = true;
            open = true;
            menuHighlight = -1;

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        void closeMenu(Event* e = nullptr) {

            dropdownArrow->transition(
                &dropdownArrow->rotation,
                3.14159f / 2.0f + 3.14159f,
                200
            );

            if (optionsContainer->style->visibility != Visibility::Hidden) {
                optionsContainer->style->visibility = Visibility::Hidden;
                optionsContainer->dirty.style = true;
            }

            open = false;
            menuHighlight = -1;
            optionsContainer->interceptHits = false;
            interceptHits = false;

            for (Text* option : options) {

                if (option->targetFlags.hover) {
                    option->targetFlags.hover = false;
                    option->dirty.style = true;
                }
            }

            if (e) {
                refresh(*e);
            }

            else if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        void computeChildren(Event& e) override {

            dropdownText->content = getItemWithVal(params.value).name;

            size_t oldSize = options.size();
            size_t newSize = params.options.size();

            for (size_t i = newSize; i < oldSize; i++) {
                delete options[i];
            }

            options.resize(newSize);

            for (size_t i = oldSize; i < newSize; i++) {

                Item& item = params.options[i];
                options[i] = new Text(
                    optionsContainer,
                    item.name,
                    { &Option, &OptionHover, &OptionDisabled }
                );

                // Capture the row INDEX, not the Item: option elements are reused when
                // the options list is rebuilt (only content/disabled are refreshed
                // below, NOT this handler), so a by-value Item capture goes stale and the
                // row fires the wrong entry (e.g. a newly-prepended "None" row running
                // the old tool's handler).  Resolving params.options[i] at click time
                // always acts on whatever the row currently shows.
                options[i]->onClick([this, i](Event& ev) {
                    if (i >= params.options.size()) { return; }
                    const Item& cur = params.options[i];
                    if (cur.disabled) { return; }
                    select(cur, &ev);
                });
            }

            for (size_t i = 0; i < newSize; i++) {

                Item& item = params.options[i];
                options[i]->content = item.name;

                options[i]->setDisabled(item.disabled);
            }

            syncOptionStyles();
        }

        void syncOptionStyles() {

            const int selectedIndex = indexOfValue(params.value);

            for (size_t i = 0; i < options.size(); i++) {

                Text* option = options[i];
                const bool showSelected =
                    !params.value.empty() &&
                    selectedIndex >= 0 &&
                    (int)i == selectedIndex;

                const bool showMenuHighlight =
                    open &&
                    menuHighlight >= 0 &&
                    (int)i == menuHighlight;

                if (showSelected) {
                    option->styles.add(&OptionSelected);
                }
                else {
                    option->styles.remove(&OptionSelected);
                }

                if (showMenuHighlight) {
                    option->styles.add(&OptionMenuHighlight);
                }
                else {
                    option->styles.remove(&OptionMenuHighlight);
                }

                option->dirty.style = true;
            }
        }

        void computeStyle(Event& e) override {

            syncOptionStyles();
            Element::computeStyle(e);
        }
    };
}
