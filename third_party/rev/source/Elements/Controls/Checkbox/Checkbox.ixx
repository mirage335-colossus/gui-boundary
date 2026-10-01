module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include <managed.hpp>

export module Rev.Element.Checkbox;

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

    struct Checkbox : public Element {

        Text* label = nullptr;

        Box* checkbox = nullptr;
            Svg* check = nullptr; 

        struct Params {

            std::string label;
            bool def;

            static Params Default() {
                return {
                    .label = "checkbox",
                    .def = false
                };
            };
        };

        Params params;
        Observable<bool> value;

        Checkbox(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Element(parent, styles) {

            this->name = "Checkbox";
            this->styles.prepend(&Control);
            
            this->params = p;
            this->value = params.def;

            label = new Text(this, params.label, { &Label });

            checkbox = new Box(this, { &CheckboxBox, &CheckboxDisabled, &CheckboxFocus });
                check = new Svg(checkbox, File("./check.svg"), { &CheckboxMark });

            checkbox->onClick([this](Event& e) {

                if (this->targetFlags.disabled) { return; }
                this->value = !value;

                this->refresh(e);
            });
        }

        void computeStyle(Event& e) {

            if (value.changed()) {
                
                if (value) {

                    checkbox->styles.add(&CheckboxChecked);
                    checkbox->styles.add(&CheckboxPress);
                    
                    check->opacity = 1.0f;
                }

                else {

                    checkbox->styles.remove(&CheckboxChecked);
                    checkbox->styles.remove(&CheckboxPress);

                    check->opacity = 0.0f;
                }

                value.changedFlag = false;
            }

            Element::computeStyle(e);
        }
    };
};
