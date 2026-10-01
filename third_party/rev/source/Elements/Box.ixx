module;

#include <string>
#include <vector>
#include <dbg.hpp>

export module Rev.Element.Box;
 
import Rev.Core.Rect;
import Rev.Core.Color;
import Rev.Core.RevisionFlag;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Graphics.Canvas;
import Rev.Primitive.Rectangle;

export namespace Rev::Element {

    using namespace Rev::Core;

    using namespace Rev::Primitives;
    using namespace Rev::Appearance;

    struct Box : public Element {

        Primitives::Rectangle* rectangle = nullptr;

        // Revision-flag test case: gate computePrimitives so a Box only rewrites
        // its mapped Rectangle data when its inputs actually changed. The two
        // inputs are resolved.style (tracked via styleRev) and rect (tracked by
        // comparing against prevRect, since layout can move us with no restyle).
        Core::RevisionObserver styleObs;
        Rect prevRect;

        // Create
        Box(Element* parent, StyleList styles = {}, std::string name = "Box") : Element(parent, styles, name) {
            rectangle = new Rectangle(shared->canvas);
        }

        // Destroy
        ~Box() {

            //dbg("[Box] destroying");
            delete rectangle;
        }

        void computePrimitives(Event& e) override {

            // Opacity is cascaded (not part of resolved.style), so reflect it every
            // pass rather than behind the style/rect gate below.
            rectangle->data->opacity = resolved.opacity;

            // Position and style are independent inputs, so they gate independently.
            // styleObs tracks resolved.style via styleRev; the rect compare catches
            // layout-driven moves that carry no style change.
            bool styleChanged = styleObs.changed(this->styleRev);
            bool rectChanged = !rect.compare(prevRect);

            if (!styleChanged && !rectChanged) {
                Element::computePrimitives(e);
                return;
            }

            prevRect = rect;

            Style& styleRef = resolved.style;
            Rectangle::Data& data = *rectangle->data;

            // Rect-only: depends purely on geometry
            //--------------------------------------------------

            if (rectChanged) {
                data.rect = this->rect.rounded().translate({ 0.0, 0.0 });
            }

            // Style-only: fill colour, corner radii, border colours
            //--------------------------------------------------
            // These read already-resolved style values, with no rect dependency.

            if (styleChanged) {

                data.color = styleRef.background.color;

                // Corner radii
                float tl, tr, bl, br;

                float mainRad = styleRef.border.radius.val;
                tl = tr = bl = br = mainRad;

                if (styleRef.border.tl.radius.val) { tl = styleRef.border.tl.radius.val; }
                if (styleRef.border.tr.radius.val) { tr = styleRef.border.tr.radius.val; }
                if (styleRef.border.bl.radius.val) { bl = styleRef.border.bl.radius.val; }
                if (styleRef.border.br.radius.val) { br = styleRef.border.br.radius.val; }

                data.corners = { tl, tr, bl, br };

                // Border colours
                Core::Color cl, cr, ct, cb;
                cl = cr = styleRef.border.color;
                ct = cb = styleRef.border.color;

                if (styleRef.border.left.color) { cl = styleRef.border.left.color; }
                if (styleRef.border.right.color) { cr = styleRef.border.right.color; }
                if (styleRef.border.top.color) { ct = styleRef.border.top.color; }
                if (styleRef.border.bottom.color) { cb = styleRef.border.bottom.color; }

                data.borderColor = { cl, cr, ct, cb };
            }

            // Mixed: border widths and shadow resolve style Dists against the
            // rect dimensions, so a change to *either* input requires a recompute.
            //--------------------------------------------------

            if (styleChanged || rectChanged) {

                // Border widths
                float wl, wr, wt, wb;

                wl = wr = styleRef.border.width.resolve(rect.w);
                wt = wb = styleRef.border.width.resolve(rect.h);

                if (styleRef.border.left.width) { wl = styleRef.border.left.width.resolve(rect.w); }
                if (styleRef.border.right.width) { wr = styleRef.border.right.width.resolve(rect.w); }
                if (styleRef.border.top.width) { wt = styleRef.border.top.width.resolve(rect.h); }
                if (styleRef.border.bottom.width) { wb = styleRef.border.bottom.width.resolve(rect.h); }

                data.borderWidth = { wl, wr, wt, wb };

                // Shadow
                data.shadow = {
                    .x = styleRef.shadow.x.resolve(rect.w),
                    .y = styleRef.shadow.y.resolve(rect.h),
                    .size = styleRef.shadow.size.resolve(0),
                    .blur = styleRef.shadow.blur.resolve(0),
                    .color = styleRef.shadow.color
                };
            }

            Element::computePrimitives(e);
        }

        // Draw own rect
        void stencil(Event& e) override {
            rectangle->stencil();
            Element::stencil(e);
        }

        // Draw own rect
        void draw(Event& e) override {

            Graphics::Canvas& canvas = *(shared->canvas);
            std::vector<Element*>& stencilStack = shared->stencilStack;

            rectangle->draw();

            // Draw stencil only after drawing self
            if (resolved.style.overflow == Overflow::Hide) {

                // Push element, set pre-draw stencil depth
                stencilStack.push_back(this);
                canvas.stencilPush(stencilStack.size() - 1);

                // Draw stencil, set post-draw stencil depth
                stencilStack.back()->stencil(e);
                canvas.stencilDepth(stencilStack.size());
            }

            Element::draw(e);
        }
    };
};
