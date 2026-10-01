module;

#include <bit>
#include <algorithm>

#include <sentinel.hpp>

export module Rev.Element.Resolved;

import Rev.Appearance;

export namespace Rev::Element {

    using namespace sentinel;
    using namespace Rev::Appearance;

    struct ResolvedDim {

        // -0.0f means "unset" or "unspecified"
        float val = -0.0f, min = -0.0f, max = -0.0f;
        bool growable = false;
        bool fit = false;

        // Limit dimension based on resolved min/max values
        void clamp() {
            if (max && val > max) { val = max; }
            if (min && val < min) { val = min; }
        }

        // Determine whether this dimension is capable of growing
        bool canGrow() {

            if (!growable) { return false; }    // If not grow, we can never grow
            if (!max) { return true; }          // If no max, we can always grow
            if (val < max) { return true; }     // If there's space left, we can grow
            
            growable = false;                   // Once false, always false
            return false;                       // Default is false
        }

        // Dimension is offered an amount to grow by, it may take less but not more
        float grow(float share) {

            float take = share;
            if (max) { take = std::min(share, max - val); }

            val += take;

            return take;
        }
    };

    struct ResolvedSize {

        ResolvedDim w, h;

        // Clamp all dims
        void clamp() {
            w.clamp(); h.clamp();
        }

        // Get maximum along axis
        float getMax(Axis axis) {
            if (axis == Axis::Horizontal) { return w.max; }
            if (axis == Axis::Vertical) { return h.max; }
            return -0.0f;
        }

        // Get minimum along axis
        float getMin(Axis axis) {
            if (axis == Axis::Horizontal) { return w.min; }
            if (axis == Axis::Vertical) { return h.min; }
            return -0.0f;
        }

        // Get number of growable dims
        int canGrow(Axis axis) {

            if (axis == Axis::Horizontal) { return w.canGrow(); }
            if (axis == Axis::Vertical) { return h.canGrow(); }

            return w.canGrow() + h.canGrow();
        }
    };

    struct ResolvedLrtb {

        ResolvedDim l, r, t, b;

        // Clamp all dims
        void clamp() {
            l.clamp(); r.clamp();
            t.clamp(); b.clamp();
        }

        // Get maximum along axis
        float getMax(Axis axis) {

            // Get maximum in horizontal direction
            if (axis == Axis::Horizontal) {
                if (!l.max || !r.max) { return -0.0f; }
                return (l.max + r.max);
            }

            // Get maximum in vertical direction
            if (axis == Axis::Vertical) {
                if (!t.max || !b.max) { return -0.0f; }
                return (t.max + b.max);
            }

            return -0.0f;
        }

        // Get minimum along axis
        float getMin(Axis axis) {

            // Get minimum in horizontal direction
            if (axis == Axis::Horizontal) {
                if (!l.min && !r.min) { return 0; }
                return (l.min + r.min);
            }

            // Get minimum in vertical direction
            if (axis == Axis::Vertical) {
                if (!t.min && !b.min) { return 0; }
                return (t.min + b.min);
            }

            return 0;
        }

        // Get number of growable dims
        int canGrow(Axis axis) {

            if (axis == Axis::Horizontal) { return l.canGrow() + r.canGrow(); }
            if (axis == Axis::Vertical) { return t.canGrow() + b.canGrow(); }

            return (
                l.canGrow() + r.canGrow() +
                t.canGrow() + b.canGrow()
            );
        }
    };

    struct SizeDetails {

        float width = -0.0f, height = -0.0f;

        float marginWidth = -0.0f, marginHeight = -0.0f;
        float paddingWidth = -0.0f, paddingHeight = -0.0f;

        float innerWidth = -0.0f, innerHeight = -0.0f;
        float outerWidth = -0.0f, outerHeight = -0.0f;
    };

    struct Resolved {
        
        static constexpr ResolvedSize defResolvedSize = ResolvedSize();
        static constexpr ResolvedLrtb defResolvedLrtb = ResolvedLrtb();
        static constexpr SizeDetails defSizeDetails = SizeDetails();

        bool hasHoverStyle = false;
        bool hasPressStyle = false;
        bool hasDragStyle = false;
        bool hasFocusStyle = false;
        bool hasDisabledStyle = false;

        Style style;

        ResolvedSize size;
        ResolvedLrtb mar, pad, pos;

        // Persistent scroll offset (content shifted up/left by these amounts).
        // Deliberately NOT cleared by reset(): scroll position must survive the
        // per-frame resolve wipe. Clamped against content/viewport in resolveRects.
        struct ScrollOffset { float x = 0.0f, y = 0.0f; };
        ScrollOffset scroll;

        SizeDetails min, max;
        float innerWidth;
        float innerHeight;

        float minContentWidth;
        float minContentHeight;

        int depth = 0;

        // Effective opacity, accumulated multiplicatively from ancestors (own
        // opacity * parent's effective opacity) in cascadeStyle -- parallel to
        // `depth`. A child can never be more opaque than its parent.
        float opacity = 1.0f;

        bool hidden = false;

        // Effective disabled state, cascaded from ancestors (own intent OR a
        // disabled ancestor) in cascadeStyle -- parallel to `hidden`.
        bool disabled = false;

        bool wrap = false;
        bool absolute = false;
        bool affectsParentSize = false;

        // Clamp all dims
        void clamp() {
            size.clamp();
            mar.clamp();
            pad.clamp();
        }

        void reset() {

            // Reset res, rect, layout
            size = defResolvedSize;
            mar = defResolvedLrtb;
            pad = defResolvedLrtb;
            pos = defResolvedLrtb;
            min = defSizeDetails;
            max = defSizeDetails;

            // Element does not wrap if its position is not absolute
            absolute = style.layout.position == Position::Absolute;
            wrap = !absolute;
            affectsParentSize = !absolute;
        }

        // Get outer dimension along axis
        float getOuter(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.val + mar.l.val + mar.r.val; }
            if (axis == Axis::Vertical) { return size.h.val + mar.t.val + mar.b.val; }
            return 0;
        }

        // Get max outer dimension along axis
        float getMaxOuter(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.max + mar.l.max + mar.r.max; }
            if (axis == Axis::Vertical) { return size.h.max + mar.t.max + mar.b.max; }
            return 0;
        }

        // Get max outer dimension along axis
        float getMinOuter(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.min + mar.l.min + mar.r.min; }
            if (axis == Axis::Vertical) { return size.h.min + mar.t.min + mar.b.min; }
            return 0;
        }

        // Get inner dimension along axis
        float getInner(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.val - pad.l.val - pad.r.val; }
            if (axis == Axis::Vertical) { return size.h.val - pad.t.val - pad.b.val; }
            return 0;
        }

        // Get max inner dimension along axis
        float getMaxInner(Axis axis) {
            if (axis == Axis::Horizontal) { return std::max(size.w.max - pad.l.min - pad.r.min, 0.0f); }
            if (axis == Axis::Vertical) { return std::max(size.h.max - pad.t.min - pad.b.min, 0.0f); }
            return 0;
        }

        // Get min inner dimension along axis
        float getMinInner(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.min - pad.l.max - pad.r.max; }
            if (axis == Axis::Vertical) { return size.h.min - pad.t.max - pad.b.max; }
            return 0;
        }

        // Get maximum along axis
        float getMax(Axis axis) {
            
            float maxSize = size.getMax(axis);
            float maxMar = mar.getMax(axis);

            if (!maxSize || !maxMar) { return 0; }

            return maxSize + maxMar;
        }

        // Get minimum along axis
        float getMin(Axis axis) {
            
            float minSize = size.getMin(axis);
            float minMar = mar.getMin(axis);

            if (!minSize && !minMar) { return 0; }

            return minSize + minMar;
        }

        float getMinSize(Axis axis, Dist::Type type) {

            // Choose axis, prefer minimum if matching type
            Dist& minDist = (axis == Axis::Horizontal) ? style.size.min.width : style.size.min.height;
            Dist& nomDist = (axis == Axis::Horizontal) ? style.size.width : style.size.height;
            Dist& dist = (minDist.type == type) ? minDist : nomDist;

            // Return only if type matches
            if (dist.type == type) { return dist.val; }
            else { return -0.0f; }
        }

        float getMaxSize(Axis axis, Dist::Type type) {

            // Choose axis, prefer minimum if matching type
            Dist& maxDist = (axis == Axis::Horizontal) ? style.size.max.width : style.size.max.height;
            Dist& nomDist = (axis == Axis::Horizontal) ? style.size.width : style.size.height;
            Dist& dist = (maxDist.type == type) ? maxDist : nomDist;

            // Return only if type matches
            if (dist.type == type) { return dist.val; }
            else { return -0.0f; }
        }

        float getMinPadding(Axis axis, Dist::Type type) {

            float min = -0.0f;

            // Chose axis, prefer minimum if matching type
            Dist& minA = (axis == Axis::Horizontal) ? style.padding.min.left : style.padding.min.top;
            Dist& nomA = (axis == Axis::Horizontal) ? style.padding.left : style.padding.top;
            Dist& a = (minA.type == type) ? minA : nomA;

            // Chose axis, prefer minimum if matching type
            Dist& minB = (axis == Axis::Horizontal) ? style.padding.min.right : style.padding.min.bottom;
            Dist& nomB = (axis == Axis::Horizontal) ? style.padding.right : style.padding.bottom;
            Dist& b = (minB.type == type) ? minB : nomB;

            // Dimensions contribute only if matching type
            if (a.type == type) { min += a.val; }
            if (b.type == type) { min += b.val; }

            return min;
        }

        float getMinMargin(Axis axis, Dist::Type type) {

            float min = -0.0f;

            // Chose axis, prefer minimum if matching type
            Dist& minA = (axis == Axis::Horizontal) ? style.margin.min.left : style.margin.min.top;
            Dist& nomA = (axis == Axis::Horizontal) ? style.margin.left : style.margin.top;
            Dist& a = (minA.type == type) ? minA : nomA;

            // Chose axis, prefer minimum if matching type
            Dist& minB = (axis == Axis::Horizontal) ? style.margin.min.right : style.margin.min.bottom;
            Dist& nomB = (axis == Axis::Horizontal) ? style.margin.right : style.margin.bottom;
            Dist& b = (minB.type == type) ? minB : nomB;

            // Dimensions contribute only if matching type
            if (a.type == type) { min += a.val; }
            if (b.type == type) { min += b.val; }

            return min;
        }

        // Get number of growable dims
        int canGrow(Axis axis) {
            return (
                size.canGrow(axis) +
                mar.canGrow(axis) +
                pad.canGrow(axis)
            );
        }

        float grow(float share, Axis axis) {

            float take = 0;
            
            // Grow horizontal dimensions
            if (axis == Axis::Horizontal) {
                if (size.w.canGrow()) { take += size.w.grow(share); }
                if (mar.l.canGrow()) { take += mar.l.grow(share); }
                if (mar.r.canGrow()) { take += mar.r.grow(share); }
            }

            // Grow vertical dimensions
            else if (axis == Axis::Vertical) {
                if (size.h.canGrow()) { take += size.h.grow(share); }
                if (mar.t.canGrow()) { take += mar.t.grow(share); }
                if (mar.b.canGrow()) { take += mar.b.grow(share); }
            }

            return take;
        }
    };
}