module;

#include <string>
#include <vector>
#include <dbg.hpp>

export module Rev.Element.Svg;
 
import Rev.Core.Rect;
import Rev.Core.Color;
import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Graphics.Canvas;
import Rev.Primitive.Svg;

export namespace Rev::Element {

    using namespace Rev::Core;

    using namespace Rev::Appearance;

    struct Svg : public Element {

        Primitives::Svg* svg = nullptr;
        Resource resource;

        float rotation = 0.0f;
        float opacity = 1.0f;

        // Create
        Svg(Element* parent, Resource resource, StyleList styles = {}, std::string name = "Svg") : Element(parent, styles, name) {

            this->resource = resource;
            svg = new Primitives::Svg(shared->canvas);
        }

        // Destroy
        ~Svg() {
            delete svg;
        }

        void computePrimitives(Event& e) override {

            if (resolved.hidden) { return; }

            const Rect bounds = this->rect.rounded().translate({ 0.0, 0.0 });

            if (bounds.w <= 0.0f || bounds.h <= 0.0f) { return; }

            Primitives::Svg::Data& data = *svg->data;

            data.rect = bounds;
            data.color = resolved.style.text.color;
            data.rotation = rotation;
            data.opacity = opacity * resolved.opacity;

            svg->resource = resource;
            svg->compute();

            Element::computePrimitives(e);
        }

        // Draw own rect
        void draw(Event& e) override {

            svg->draw();

            Element::draw(e);
        }
    };
};
