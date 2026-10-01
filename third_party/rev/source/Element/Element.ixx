module;

#include <algorithm>

#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include <ranges>
#include <functional>

#include <sentinel.hpp>
#include <dbg.hpp>

export module Rev.Element;

import Rev.Graphics.Canvas;

import Rev.Core.Pos;
import Rev.Core.Rect;
import Rev.Core.DirtyFlag;
import Rev.Core.RevisionFlag;
import Rev.Core.Dispatcher;

import Rev.Appearance;
import Rev.Element.Event;
import Rev.Element.Resolved;

export namespace Rev::Element {

    using namespace sentinel;
    using namespace Rev::Core;
    using namespace Rev::Appearance;

    struct Element {

        enum class Type {
            Single,
            Group
        };

        Type type;
        std::vector<Element*> children;

        struct Shared {

            struct DirtyElements {
                std::vector<Element*> refresh;
                std::vector<Element*> restyle;
                std::vector<Element*> animate;
            };

            DirtyElements dirty;

            // Gate for the whole-tree flex pass: raised by anything that may
            // change geometry, consumed and cleared by Window::draw. While false,
            // the flex pipeline is skipped and last frame's geometry is reused.
            bool layoutDirty = true;

            Graphics::Canvas* canvas = nullptr;
            std::vector<Element*> stencilStack;

            void* state = nullptr;

            Event* event = nullptr;

            // The currently focused editable text element (if any). Lets non-text
            // elements (e.g. the 3D view) cede the keyboard to a focused field
            // even when both happen to be in the focus set at once.
            Element* focusedText = nullptr;

            // Top-level windows registered by the application (e.g. tool settings popups).
            std::vector<void*>* windowGroup = nullptr;
        };

        struct Dirty {
            Core::DirtyFlag style;
            bool structure = false;
            bool draw = false;
        };

        // Shared betweeen elements
        Shared* shared = nullptr;

        // Self and parent
        Element* parent = nullptr;
        Element* next = nullptr;
        Element* last = nullptr;

        std::string name = "Element";

        // Style
        StylePtr style;
        StyleList styles;
        
        // Computing
        Resolved resolved;
        Rect rect;

        // Tracking
        size_t draws = 0;

        Dirty dirty;

        // Monotonic revision of this element's style inputs. Bridged off the
        // existing dirty.style signal (see ctor) so consumers can gate work with
        // a RevisionObserver instead of consuming a shared dirty flag. This is
        // the incremental on-ramp from DirtyFlag to RevisionFlag.
        Core::RevisionFlag styleRev;

        Core::Dispatcher<Event>* dispatcher = nullptr;

        // Create
        Element(Element* parent = nullptr, StyleList styles = {}, std::string name = "") {

            dispatcher = new Core::Dispatcher<Event>();

            if (parent && parent != this) { parent->addChild(this); }

            dirty.style.subscribe(&(this->styles.dirty));
            dirty.style.subscribe(&(this->style.dirty));

            dirty.style.onDirty([this]() {

                // Bridge: every time style inputs go dirty, advance the revision
                // so observers (e.g. Box::computePrimitives) see "style changed".
                this->styleRev.inc();

                if (!this->shared) { return; }
                //if (this->parent == this) { return; }

                this->shared->dirty.restyle.push_back(this);
                this->refresh(*shared->event);
            });

            this->styles = std::move(styles);
            this->styles.wireStyleLinks();

            dirty.style = true;

            this->name = name;
        }
        
        // Destroy
        virtual ~Element() {

            if (shared) {

                auto removeSelf = [this](std::vector<Element*>& list) {
                    list.erase(
                        std::remove(
                            list.begin(),
                            list.end(),
                            this
                        ),
                        list.end()
                    );
                };

                removeSelf(shared->dirty.refresh);
                removeSelf(shared->dirty.restyle);
                removeSelf(shared->dirty.animate);
                removeSelf(shared->stencilStack);

                // Don't leave a dangling focused-text pointer if a focused input
                // is destroyed (e.g. its host rebuilds while it has focus).
                if (shared->focusedText == this) {
                    shared->focusedText = nullptr;
                }
            }

            // Before doing anything structural, remove self from parent
            if (parent) {
                parent->removeChild(this);
            }

            if (style.pStyle) {
                delete style.pStyle;
                style.pStyle = nullptr;
            }

            // Delete children from a copy because child destructors mutate children
            std::vector<Element*> childrenCopy = children;

            for (Element* child : childrenCopy) {
                if (child) { delete child; }
            }

            children.clear();

            delete dispatcher;
            dispatcher = nullptr;
        }

        // Cast as pointer to canvas
        explicit operator Graphics::Canvas*() {
            return shared ? shared->canvas : nullptr;
        }

        // Inheritance
        //--------------------------------------------------
        
        void addChild(Element* child) {

            child->parent = this;
            child->shared = shared;

            children.push_back(child);

            if (shared) { shared->layoutDirty = true; }

            child->refresh(*shared->event);
        }

        void removeChild(Element* child) {
            auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end()) { children.erase(it); }
            if (shared) { shared->layoutDirty = true; }
        }

        void moveChild(
            Element* child,
            Element* target,
            bool before
        ) {

            if (!child || !target || child == target) {
                return;
            }

            if (child->parent != this) {
                return;
            }

            auto childIt = std::find(
                children.begin(),
                children.end(),
                child
            );

            if (childIt == children.end()) {
                return;
            }

            children.erase(childIt);

            auto targetIt = std::find(
                children.begin(),
                children.end(),
                target
            );

            if (targetIt == children.end()) {
                children.push_back(child);
                return;
            }

            if (before) {
                children.insert(targetIt, child);
            }

            else {
                children.insert(targetIt + 1, child);
            }

            if (shared) { shared->layoutDirty = true; }

            if (shared && shared->event) {
                child->refresh(*shared->event);
            }
        }

        void cascadeStyle() {

            // Set hidden state
            resolved.hidden = false;
            if (parent->resolved.hidden) { resolved.hidden = true; }
            if (resolved.style.visibility == Visibility::Hidden) { resolved.hidden = true; }

            resolved.affectsParentSize = !resolved.hidden &&
                resolved.style.layout.position != Position::Absolute;

            // Cascade disabled state: effective = own intent OR a disabled
            // ancestor. reset() leaves resolved.disabled alone, so its prior
            // value survives here -- if the effective value flips, mark the
            // styles dirty so the applies.disabled styles re-resolve. This lets a
            // single setDisabled on a section disable its whole subtree.
            bool wasDisabled = resolved.disabled;
            resolved.disabled = targetFlags.disabled;
            if (parent->resolved.disabled) { resolved.disabled = true; }
            if (resolved.disabled != wasDisabled) { styles.dirty = true; }

            // Set depth
            resolved.depth = parent->resolved.depth + 1 - resolved.style.zIndex;

            // Accumulate opacity: own opacity scaled by the parent's effective
            // opacity, so it compounds down the tree just like depth.
            resolved.opacity = parent->resolved.opacity * resolved.style.opacity.val;

            // Hidden subtrees still inherit state. Layout/draw queues prune
            // them separately; descendants must not retain visible, enabled
            // or opaque state from before their ancestor was hidden.
            for (Element* child : children) {
                child->cascadeStyle();
            }
        }

        // Computing
        //--------------------------------------------------

        std::vector<Transition> transitions;

        // Set while live transitions animate a geometry value (not pure paint),
        // so the animation pass knows to keep the layout dirty. Cleared on drain.
        bool animatesLayout = false;

        void transition(float* val, float newVal, int ms) {

            if (*val == newVal) { return; }

            float old = *val;
            *val = newVal;

            bool hadTransitions = !transitions.empty();

            Transition::createNew(*val, old, transitions, shared->event->time, ms);

            if (!hadTransitions && !transitions.empty()) {
                shared->dirty.animate.push_back(this);
            }

            // Subject is an unclassifiable float; assume it may move geometry.
            if (!transitions.empty()) { animatesLayout = true; }
        }

        virtual void animate(Event& e) {

            // Transition styles
            //--------------------------------------------------

            // Remove expired transitions
            transitions.erase(
                std::remove_if(transitions.begin(), transitions.end(), [&](const Transition& transition) {
                    return e.time > transition.endTime;
                }),
                transitions.end()
            );

            // Once nothing is animating, this element no longer forces relayout.
            if (transitions.empty()) { animatesLayout = false; }

            // Do transitions
            for (Transition& transition : transitions) {

                float& val = *transition.subject;

                if (e.time < transition.startTime) { continue; }

                if (e.time < transition.endTime) {
                    float t = float((e.time - transition.startTime)) / float((transition.endTime - transition.startTime));
                    val = Transition::ease(transition.startVal, transition.endVal, t);
                }

                if (e.time > transition.endTime) {
                    val = transition.endVal;
                }
            }
        }

        // Comptue style
        virtual void resolveStyle(Event& e) {

            resolved.hasHoverStyle = false;
            resolved.hasPressStyle = false;
            resolved.hasDragStyle = false;
            resolved.hasFocusStyle = false;
            resolved.hasDisabledStyle = false;

            // Calculate whether we have certain styles
            for (Style* style : styles.styles) {
                if (style->applies.hover) { resolved.hasHoverStyle = true; }
                if (style->applies.press) { resolved.hasPressStyle = true; }
                if (style->applies.drag) { resolved.hasDragStyle = true; }
                if (style->applies.focus) { resolved.hasFocusStyle = true; }
                if (style->applies.disabled) { resolved.hasDisabledStyle = true; }
            }

            // Compile / apply styles
            //--------------------------------------------------

            Style old = resolved.style;

            resolved.style = Style();
            resolved.style.dirty = false;

            Applies flags = {
                .hover = targetFlags.hover,
                .press = targetFlags.press,
                .drag = targetFlags.drag,
                .focus = targetFlags.focus,
                .disabled = resolved.disabled
            };

            // Apply other styles, then own style
            resolved.style.apply(styles, flags);
            if (style.pStyle) { resolved.style.apply(*(style.pStyle)); }

            // Set all styles as not dirty (anymore)
            if (style.pStyle) { style.pStyle->dirty = false; }
            for (Style* style : styles.styles) { style->dirty = false; }
            styles.dirty = false;

            this->dirty.style = false;

            // Suspicion gate: only a style change that actually touches geometry
            // dirties the layout. A pure-paint restyle (e.g. a hover background)
            // re-resolves the style but skips the whole flex pass this frame.
            bool layoutChanged = resolved.style.layoutDiffers(old);
            if (shared && layoutChanged) { shared->layoutDirty = true; }

            // Create transitions if needed
            //--------------------------------------------------

            // If this is our first draw, we do not animate
            if (draws == 0) { return; }

            bool hadTransitions = !transitions.empty();
            resolved.style.animate(old, transitions, e.time);

            // Add to "please animate" list if we now have transitions
            if (!hadTransitions && !transitions.empty()) {
                shared->dirty.animate.push_back(this);
            }

            // Sticky until the transitions drain, so a geometry transition keeps
            // relayouting even if a later pure-paint restyle intervenes.
            if (!transitions.empty() && layoutChanged) {
                animatesLayout = true;
            }
        }

        // Alter children to reflect data
        virtual void computeChildren(Event& e) {}
        
        // Alter style to reflect data
        virtual void computeStyle(Event& e) {}

        // Alter primitives to reflect style/data
        virtual void computePrimitives(Event& e) {}

        // Draw stencil (this must be seperate from draw logic)
        virtual void stencil(Event& e) {}

        // Draw color
        virtual void draw(Event& e) {

            // Draw = no longer dirty
            this->dirty.draw = false;
            draws += 1;

            // If there are any incomplte transitions, we must continue drawing
            if (!transitions.empty()) {
                refresh(e);
            }
        }

        // Layout
        //--------------------------------------------------

        /* 
            ////////// WARNING ////////// WARNING ////////// WARNING //////////
        
            THIS CODE IS NOT ELEGANT. IT IS NOT PERFORMANT. IT IS FUCKED.
            IT IS VERY BAD. IT IS FULL OF CLUMSY BAND-AIDS AND REDUNDANT
            CALCULATIONS. IT WILL BE FIXED OVER TIME, BUT IT IS NECESSARY
            THAT IS SHOULD AT LEAST WORK, FOR THE TIME BEING.

            ////////// WARNING ////////// WARNING ////////// WARNING //////////
        */

        struct Row {

            Rect rect;
            ResolvedSize size;
            std::vector<Element*> members;

            // Get number of growable dimensions
            int canGrow(Axis axis) {
               
                int numCanGrow = 0;

                for (Element* member : members) {
                    if (!member->resolved.affectsParentSize) { continue; }
                    numCanGrow += member->resolved.canGrow(axis);
                }

                return numCanGrow;
            }
        };

        struct Layout {

            Rect rect;
            ResolvedSize size;
            std::vector<Row> rows;
            bool done = true;

            // Return number of rows that can grow along the axis
            int growableRows(Axis axis) {

                int count = 0;

                for (Row& row : rows) {
                    count += row.size.canGrow(axis);
                }

                return count;
            }
        };

        Layout layout;

        // Default is to simply reset the layout
        virtual void computeLayout() {
            layout = Layout();
            layout.done = false;
        }

        // Step zero is to reset all data which will be modified
        void resetResolved() {

            resolved.reset();

            rect = Rect();
        }

        void resolveMinima() {

            if (resolved.hidden) { return; }

            float& minWidth = resolved.min.width;
            float& minHeight = resolved.min.height;

            float& minPaddingWidth = resolved.min.paddingWidth;
            float& minPaddingHeight = resolved.min.paddingHeight;
            
            float& minMarginWidth = resolved.min.marginWidth;
            float& minMarginHeight = resolved.min.marginHeight;

            float& minInnerWidth = resolved.min.innerWidth;
            float& minInnerHeight = resolved.min.innerHeight;

            float& minOuterWidth = resolved.min.outerWidth;
            float& minOuterHeight = resolved.min.outerHeight;

            // Get from style
            //--------------------------------------------------

            minWidth = resolved.getMinSize(Axis::Horizontal, Dist::Type::Abs);
            minHeight = resolved.getMinSize(Axis::Vertical, Dist::Type::Abs);

            minPaddingWidth = resolved.getMinPadding(Axis::Horizontal, Dist::Type::Abs);
            minPaddingHeight = resolved.getMinPadding(Axis::Vertical, Dist::Type::Abs);

            minMarginWidth = resolved.getMinMargin(Axis::Horizontal, Dist::Type::Abs);
            minMarginHeight = resolved.getMinMargin(Axis::Vertical, Dist::Type::Abs);

            // Infer from self (or children if necessary)
            //--------------------------------------------------

            if (set(minWidth)) {
                minInnerWidth = minWidth - minPaddingWidth;
                minOuterWidth = minWidth + minMarginWidth;
            }

            // A relative (percentage) width is dictated by the parent top-down, so
            // its minimum is NOT knowable at this bottom-up stage — and its content
            // overflows rather than pushing the parent outward. So we skip the
            // content-inference entirely, leaving min unset (it contributes nothing
            // upward). Only genuinely content-sized (auto/grow) dimensions infer.
            else if (resolved.style.size.width.type != Dist::Type::Rel) {

                float maxOfMin = -0.0f;

                // Get max, ignoring if absolute
                for (Element* c : children) {
                    if (!c->resolved.affectsParentSize) { continue; }
                    maxOfMin = std::max(maxOfMin, c->resolved.min.outerWidth);
                }

                maxOfMin = std::max(maxOfMin, this->resolved.minContentWidth);

                minInnerWidth = maxOfMin;
                minOuterWidth = maxOfMin + minPaddingWidth + minMarginWidth;
            }

            if (set(minHeight)) {
                minInnerHeight = minHeight - minPaddingHeight;
                minOuterHeight = minHeight + minMarginHeight;
            }

            // Same gate for a relative (percentage) height (see width above).
            else if (resolved.style.size.height.type != Dist::Type::Rel) {

                float maxOfMin = -0.0f;

                // Get max, ignoring if absolute
                for (Element* c : children) {
                    if (!c->resolved.affectsParentSize) { continue; }
                    maxOfMin = std::max(maxOfMin, c->resolved.min.outerHeight);
                }

                maxOfMin = std::max(maxOfMin, this->resolved.minContentHeight);

                minInnerHeight = maxOfMin;
                minOuterHeight = maxOfMin + minPaddingHeight + minMarginHeight;
            }
        }

        // Top down: resolve maximum feasible dimensions
        void resolveMaxima() {

            float& maxWidth = resolved.max.width;
            float& maxHeight = resolved.max.height;
            
            float& maxInnerWidth = resolved.max.innerWidth;
            float& maxInnerHeight = resolved.max.innerHeight;

            float& minInnerWidth = resolved.min.innerWidth;
            float& minInnerHeight = resolved.min.innerHeight;

            float& minPaddingWidth = resolved.min.paddingWidth;
            float& minPaddingHeight = resolved.min.paddingHeight;

            float& minMarginWidth = resolved.min.marginWidth;
            float& minMarginHeight = resolved.min.marginHeight;

            if (resolved.hidden) { return; }
        
            // Get from style
            //--------------------------------------------------
            
            maxWidth = resolved.getMaxSize(Axis::Horizontal, Dist::Type::Abs);
            maxHeight = resolved.getMaxSize(Axis::Vertical, Dist::Type::Abs);

            // Infer from set values
            //--------------------------------------------------

            if (set(maxWidth)) { maxInnerWidth = maxWidth - minPaddingWidth; }
            else { maxInnerWidth = parent->resolved.max.innerWidth - minMarginWidth - minPaddingWidth; }

            if (set(maxHeight)) { maxInnerHeight = maxHeight - minPaddingHeight; }
            else { maxInnerHeight = parent->resolved.max.innerHeight - minMarginHeight - minPaddingHeight; }

            // Subtract subling outer heights if no set height
            /*if (!set(maxHeight) && resolved.style.layout.wrap == Wrap::False) {
                for (Element* s : parent->children) {
                    if (s == this) { continue; }
                    maxInnerHeight -= s->resolved.min.outerHeight;
                }
            }*/

            // Ensure minimum dominates (in certain circumstances)
            if (maxInnerWidth < minInnerWidth) { maxInnerWidth = minInnerWidth; }
            if (maxInnerHeight < minInnerHeight) { maxInnerHeight = minInnerHeight; }
        }

        void resolveLayout() {

            if (resolved.hidden) { return; }

            // Wrap children
            //--------------------------------------------------

            // If there are no children, we defer to our compute layout function
            if (children.empty()) {
                this->computeLayout();
            }

            // Otherwise, we compute the layout ourselves
            else {

                layout = Layout();
                Row row = Row();
                float runningRelSize = 0.0f;

                for (Element* child : children) {

                    Element& elem = *child;

                    // Ignore wrap entirely if element does not wrap
                    if (!elem.resolved.wrap || resolved.style.layout.wrap == Wrap::False) {
                        row.members.push_back(child);
                        continue;
                    }
                    
                    bool horizontal = (resolved.style.layout.direction != Axis::Vertical);
                    Axis axis = horizontal ? Axis::Horizontal : Axis::Vertical;

                    // Get minimum abs/rel sizes
                    //--------------------------------------------------

                    // Wrap against the container's ACTUAL inner size, not its max. The
                    // max is anchored to the window's max-resize size and never subtracts
                    // fixed siblings, so wrapping against it under-counts and never fires.
                    // innerWidth/Height survive reset(), so last frame's true size is here;
                    // fall back to max on the very first frame (before any dims pass).
                    float maxAbsInnerSize = horizontal ? resolved.max.innerWidth : resolved.max.innerHeight;
                    float actualInnerSize = horizontal ? resolved.innerWidth : resolved.innerHeight;
                    if (actualInnerSize > 0.0f) { maxAbsInnerSize = actualInnerSize; }

                    float& minAbsLayoutSize = horizontal ? elem.layout.size.w.min : elem.layout.size.h.min;

                    // These absolute minima were already calculated before
                    float& minAbsSize = horizontal ? elem.resolved.min.width : elem.resolved.min.height;
                    float& minAbsMargin = horizontal ? elem.resolved.min.marginWidth : elem.resolved.min.marginHeight;
                    float& minAbsPadding = horizontal ? elem.resolved.min.paddingWidth : elem.resolved.min.paddingHeight;

                    // We directly get the proportional values
                    float minRelSize = elem.resolved.getMinSize(axis, Dist::Type::Rel);
                    float minRelMargin = elem.resolved.getMinMargin(axis, Dist::Type::Rel);
                    float minRelPadding = elem.resolved.getMinPadding(axis, Dist::Type::Rel); 

                    // Calculate additional relative sizes
                    //--------------------------------------------------

                    float inverseAbsMaxInner = 1.0f / maxAbsInnerSize;

                    if (set(minAbsSize)) { minRelSize += minAbsSize * inverseAbsMaxInner; }
                    else { minRelSize += (minAbsLayoutSize + minAbsPadding) * inverseAbsMaxInner; }
                    if (set(minAbsMargin)) { minRelMargin += minAbsMargin * inverseAbsMaxInner; }

                    float minRelOuterSize = -0.0f;
                    float minRelLayoutSize = minAbsLayoutSize * inverseAbsMaxInner;
                    
                    minRelOuterSize = minRelLayoutSize + minRelPadding;
                    if (set(minRelSize)) { minRelOuterSize = minRelSize ; }
                    minRelOuterSize += minRelMargin;

                    // Should we add another row (wrap) or should we keep adding more?
                    // We must wrap if we have exceeded the maximum allowed inner space,
                    // but not if this would be the first member of the row
                    if (runningRelSize + minRelOuterSize > 1.0 && !row.members.empty()) {
                        layout.rows.push_back(row);
                        row = Row();
                        runningRelSize = 0;
                    }

                    // Add element to row, add min outer size to running relative space
                    row.members.push_back(child);
                    runningRelSize += minRelOuterSize;
                }

                // Add last row that didn't overflow
                layout.rows.push_back(row);
            }

            // Measure layout val/min
            //--------------------------------------------------

            if (resolved.style.layout.direction == Axis::Vertical) {
            
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }
    
                        row.size.w.min = std::max(row.size.w.min, member->resolved.min.outerWidth);
                        row.size.h.min += member->resolved.min.outerHeight;
                    }

                    layout.size.w.min += row.size.w.min;
                    layout.size.h.min = std::max(layout.size.h.min, row.size.h.min);
                }
            }

            else {

                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }
    
                        row.size.w.min += member->resolved.min.outerWidth;
                        row.size.h.min = std::max(row.size.h.min, member->resolved.min.outerHeight);
                    }
    
                    layout.size.w.min = std::max(layout.size.w.min, row.size.w.min);
                    layout.size.h.min += row.size.h.min;
                }
            }

            // Adjust own minimum outer size to accomodate layout
            //--------------------------------------------------

            // A relative (percentage) dimension is parent-dictated top-down; its
            // minimum is not knowable here and its content overflows rather than
            // pushing the parent. So we must NOT re-derive its outer minimum from
            // content — doing so would overwrite the (deliberately unset) value
            // resolveMinima left, propagating the relative element's content past
            // the gate and inflating its ancestors. Mirror of the resolveMinima gate.
            if (set(resolved.min.width)) { resolved.min.outerWidth = resolved.min.width + resolved.min.marginWidth; }
            else if (resolved.style.size.width.type != Dist::Type::Rel) { resolved.min.outerWidth = layout.size.w.min + resolved.min.paddingWidth + resolved.min.marginWidth; }

            if (set(resolved.min.height)) { resolved.min.outerHeight = resolved.min.height + resolved.min.marginHeight; }
            else if (resolved.style.size.height.type != Dist::Type::Rel) { resolved.min.outerHeight = layout.size.h.min + resolved.min.paddingHeight + resolved.min.marginHeight; }

            float layoutPlusPaddingWidth = layout.size.w.min + resolved.style.padding.left.val + resolved.style.padding.right.val;
            float layoutPlusPaddingHeight = layout.size.h.min + resolved.style.padding.top.val + resolved.style.padding.bottom.val;

            Resolved& res = resolved;

            //if (res.style.size.width.type == Dist::Type::Rel) { res.size.w.min = 0.0f; }
            //if (res.style.size.height.type == Dist::Type::Rel) { res.size.h.min = 0.0f; }

            if (!set(res.size.w.min) && res.size.w.min < layoutPlusPaddingWidth) { res.size.w.min = layoutPlusPaddingWidth; }
            if (!set(res.size.h.min) && res.size.h.min < layoutPlusPaddingHeight) { res.size.h.min = layoutPlusPaddingHeight; }

            bool test = true;

            // Promote growable dims
            //--------------------------------------------------

            bool promoteHorizontal = resolved.style.size.width.type != Dist::Type::Shrink;
            bool promoteVertical = resolved.style.size.height.type != Dist::Type::Shrink;

            for (Row& row : layout.rows) {

                for (Element* member : row.members) {

                    Element& elem = *member;
                    Size& size = elem.resolved.style.size;
                    LrtbStyle& margin = elem.resolved.style.margin;

                    // Set dimensions as growable if style size is of type grow
                    if (size.width.type == Dist::Type::Grow) { elem.resolved.size.w.growable = true; }
                    if (size.height.type == Dist::Type::Grow) { elem.resolved.size.h.growable = true; }

                    if (margin.left.type == Dist::Type::Grow) { elem.resolved.mar.l.growable = true; }
                    if (margin.right.type == Dist::Type::Grow) { elem.resolved.mar.r.growable = true; }
                    if (margin.top.type == Dist::Type::Grow) { elem.resolved.mar.t.growable = true; }
                    if (margin.bottom.type == Dist::Type::Grow) { elem.resolved.mar.b.growable = true; }

                    if (!elem.resolved.affectsParentSize) {
                        continue;
                    }

                    if (resolved.style.layout.direction == Axis::Vertical) {
                    
                        if (promoteVertical && elem.resolved.canGrow(Axis::Vertical)) {
                            resolved.size.h.growable = true;
                        }
    
                        if (promoteHorizontal && elem.resolved.canGrow(Axis::Horizontal)) {
                            resolved.size.w.growable = true;
                            row.size.w.growable = true;
                            layout.size.w.growable = true;
                        }
                    }

                    else {
                        if (promoteHorizontal && elem.resolved.canGrow(Axis::Horizontal)) {
                            resolved.size.w.growable = true;
                        }
    
                        if (promoteVertical && elem.resolved.canGrow(Axis::Vertical)) {
                            resolved.size.h.growable = true;
                            row.size.h.growable = true;
                            layout.size.h.growable = true;
                        }
                    }
                }
            }
        }

        // Top down: Resolve flex and grow dimensions
        void resolveDims() {

            resolved.innerWidth = 0;
            resolved.innerHeight = 0;

            if (resolved.hidden) { return; }

            Resolved& res = resolved;
            
            if (parent == this) {
                res.size.w.val = res.size.w.min = res.size.w.max = resolved.style.size.width.val;
                res.size.h.val = res.size.h.min = res.size.h.max = resolved.style.size.height.val;
            }

            resolved.innerWidth = res.size.w.val;
            resolved.innerHeight = res.size.h.val;

            // Resolve own padding (needed for fit)
            //--------------------------------------------------
            
            res.pad.l.val = res.pad.l.min = res.pad.l.max = resolved.style.padding.left.val;
            res.pad.r.val = res.pad.r.min = res.pad.r.max = resolved.style.padding.right.val;
            res.pad.t.val = res.pad.t.min = res.pad.t.max = resolved.style.padding.top.val;
            res.pad.b.val = res.pad.b.min = res.pad.b.max = resolved.style.padding.bottom.val;

            resolved.innerWidth -= res.pad.l.val + res.pad.r.val;
            resolved.innerHeight -= res.pad.t.val + res.pad.b.val;

            if (children.empty()) {
                return;
            }

            // Resolve val/max of children prior to grow
            //--------------------------------------------------

            for (Element* pChild : children) {

                Element& child = *pChild;

                if (child.resolved.hidden) { continue; }

                Size& cSize = child.resolved.style.size;

                Dist& cWidth = cSize.width;
                Dist& cHeight = cSize.height;

                LrtbStyle& cMargin = child.resolved.style.margin;
                LrtbStyle& cPadding = child.resolved.style.padding;
                LrtbStyle& cPosition = child.resolved.style.position;

                float& compareValW = child.resolved.style.layout.position == Position::Absolute ? resolved.size.w.val : resolved.innerWidth;
                float& compareValH = child.resolved.style.layout.position == Position::Absolute ? resolved.size.h.val : resolved.innerHeight;

                // Resolve nominal
                if (cSize.width) { child.resolved.size.w.val = child.resolved.size.w.min = child.resolved.size.w.max = cSize.width.resolve(compareValW); }
                if (cSize.height) { child.resolved.size.h.val = child.resolved.size.h.min = child.resolved.size.h.max = cSize.height.resolve(compareValH); }

                // Resolve min
                if (cSize.min.width) { child.resolved.size.w.min = cSize.min.width.resolve(compareValW); }
                if (cSize.min.height) { child.resolved.size.h.min = cSize.min.height.resolve(compareValH); }

                // Resolve max
                if (cSize.max.width) { child.resolved.size.w.max = cSize.max.width.resolve(compareValW); }
                if (cSize.max.height) { child.resolved.size.h.max = cSize.max.height.resolve(compareValH); }

                // Override max if needed
                if (cSize.width.type == Dist::Type::Grow && !set(child.resolved.size.w.max)) { child.resolved.size.w.max = 9999999.0f; }
                if (cSize.height.type == Dist::Type::Grow && !set(child.resolved.size.h.max)) { child.resolved.size.h.max = 9999999.0f; }

                // Resolve child margin
                if (cMargin.left) { child.resolved.mar.l.val = child.resolved.mar.l.min = child.resolved.mar.l.max = cMargin.left.resolve(compareValW); }
                if (cMargin.right) { child.resolved.mar.r.val = child.resolved.mar.r.min = child.resolved.mar.r.max = cMargin.right.resolve(compareValW); }
                if (cMargin.top) { child.resolved.mar.t.val = child.resolved.mar.t.min = child.resolved.mar.t.max = cMargin.top.resolve(compareValH); }
                if (cMargin.bottom) { child.resolved.mar.b.val = child.resolved.mar.b.min = child.resolved.mar.b.max = cMargin.bottom.resolve(compareValH); }

                if (cMargin.min.left) { child.resolved.mar.l.min = cMargin.min.left.resolve(compareValW); }
                if (cMargin.min.right) { child.resolved.mar.r.min = cMargin.min.right.resolve(compareValW); }
                if (cMargin.min.top) { child.resolved.mar.t.min = cMargin.min.top.resolve(compareValH); }
                if (cMargin.min.bottom) { child.resolved.mar.b.min = cMargin.min.bottom.resolve(compareValH); }

                if (cMargin.max.left) { child.resolved.mar.l.max = cMargin.max.left.resolve(compareValW); }
                if (cMargin.max.right) { child.resolved.mar.r.max = cMargin.max.right.resolve(compareValW); }
                if (cMargin.max.top) { child.resolved.mar.t.max = cMargin.max.top.resolve(compareValH); }
                if (cMargin.max.bottom) { child.resolved.mar.b.max = cMargin.max.bottom.resolve(compareValH); }

                if (cMargin.left.type == Dist::Type::Grow && !set(child.resolved.mar.l.max)) { child.resolved.mar.l.max = 9999999.0f; }
                if (cMargin.right.type == Dist::Type::Grow && !set(child.resolved.mar.r.max)) { child.resolved.mar.r.max = 9999999.0f; }
                if (cMargin.top.type == Dist::Type::Grow && !set(child.resolved.mar.t.max)) { child.resolved.mar.t.max = 9999999.0f; }
                if (cMargin.bottom.type == Dist::Type::Grow && !set(child.resolved.mar.b.max)) { child.resolved.mar.b.max = 9999999.0f; }

                // Resolve child padding
                child.resolved.pad.l.val = child.resolved.pad.l.min = child.resolved.pad.l.max = cPadding.left.val;
                child.resolved.pad.r.val = child.resolved.pad.r.min = child.resolved.pad.r.max = cPadding.right.val;
                child.resolved.pad.t.val = child.resolved.pad.t.min = child.resolved.pad.t.max = cPadding.top.val;
                child.resolved.pad.b.val = child.resolved.pad.b.min = child.resolved.pad.b.max = cPadding.bottom.val;

                // Resolve child position
                if (cPosition.left) {
                    child.resolved.pos.l.val = cPosition.left.resolve(compareValW);
                }
                if (cPosition.right) {
                    child.resolved.pos.r.val = cPosition.right.resolve(compareValW);
                }
                if (cPosition.top) { child.resolved.pos.t.val = cPosition.top.resolve(compareValH); }
                if (cPosition.bottom) {
                    child.resolved.pos.b.val = cPosition.bottom.resolve(compareValH);
                }

                float minWidthFromPadding = child.resolved.pad.l.min + child.resolved.pad.r.min;
                float minHeightFromPadding = child.resolved.pad.t.min + child.resolved.pad.b.min;

                if (!set(child.resolved.size.w.min)) { child.resolved.size.w.min = child.layout.size.w.min + minWidthFromPadding; }
                if (!set(child.resolved.size.h.min)) { child.resolved.size.h.min = child.layout.size.h.min + minHeightFromPadding; }

                // If no set val, get from min
                if (!set(child.resolved.size.w.val)) { child.resolved.size.w.val = child.resolved.size.w.min; }
                if (!set(child.resolved.size.h.val)) { child.resolved.size.h.val = child.resolved.size.h.min; }

                // A set maximum is a hard ceiling. The steps above can leave the
                // value (and the content-derived minimum) ABOVE the resolved max
                // — e.g. a Grow element whose min just floored to its content yet
                // also declares an explicit max. Clamp them back down so the max
                // is actually obeyed. An "unset" max is the large sentinel, so
                // this is a no-op unless a real, smaller max was specified.
                if (set(child.resolved.size.w.max)) {
                    if (child.resolved.size.w.min > child.resolved.size.w.max) { child.resolved.size.w.min = child.resolved.size.w.max; }
                    if (child.resolved.size.w.val > child.resolved.size.w.max) { child.resolved.size.w.val = child.resolved.size.w.max; }
                }
                if (set(child.resolved.size.h.max)) {
                    if (child.resolved.size.h.min > child.resolved.size.h.max) { child.resolved.size.h.min = child.resolved.size.h.max; }
                    if (child.resolved.size.h.val > child.resolved.size.h.max) { child.resolved.size.h.val = child.resolved.size.h.max; }
                }
            }

            // Measure layout max prior to grow
            //--------------------------------------------------

            if (resolved.style.layout.direction == Axis::Vertical) {

                // Measure val
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }

                        row.size.w.val = std::max(row.size.w.val, member->resolved.getOuter(Axis::Horizontal));
                        row.size.h.val += member->resolved.getOuter(Axis::Vertical);
                    }

                    layout.size.w.val += row.size.w.val;
                    layout.size.h.val = std::max(layout.size.h.val, row.size.h.val);
                }

                // Measure max
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }

                        row.size.w.max = std::max(row.size.w.max, member->resolved.getMaxOuter(Axis::Horizontal));
                        row.size.h.max += member->resolved.getMaxOuter(Axis::Vertical);
                    }

                    layout.size.w.max += row.size.w.max;
                    layout.size.h.max = std::max(layout.size.h.max, row.size.h.max);
                }
            }

            else {

                // Measure val
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }

                        row.size.w.val += member->resolved.getOuter(Axis::Horizontal);
                        row.size.h.val = std::max(row.size.h.val, member->resolved.getOuter(Axis::Vertical));
                    }

                    layout.size.w.val = std::max(layout.size.w.val, row.size.w.val);
                    layout.size.h.val += row.size.h.val;
                }

                // Measure max
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }

                        row.size.w.max += member->resolved.getMaxOuter(Axis::Horizontal);
                        row.size.h.max = std::max(row.size.h.max, member->resolved.getMaxOuter(Axis::Vertical));
                    }

                    layout.size.w.max = std::max(layout.size.w.max, row.size.w.max);
                    layout.size.h.max += row.size.h.max;
                }
            }

            // Clamp layout
            for (Row& row : layout.rows) {

                if (row.size.w.max < row.size.w.min) { row.size.w.max = row.size.w.min; }
                if (row.size.h.max < row.size.h.min) { row.size.h.max = row.size.h.min; }
            }

            if (layout.size.w.max < layout.size.w.min) { layout.size.w.max = layout.size.w.min; }
            if (layout.size.h.max < layout.size.h.min) { layout.size.h.max = layout.size.h.min; }

            // Grow layout and members
            //--------------------------------------------------

            if (resolved.style.layout.direction == Axis::Vertical) { this->growVerticalMode(); }
            else { this->growHorizontalMode(); }

            // Measure layout val after grow
            //--------------------------------------------------

            if (!layout.rows.empty()) {
   
                layout.size.w.val = -0.0f;
                layout.size.h.val = -0.0f;
            }

            if (resolved.style.layout.direction == Axis::Vertical) {

                // Measure val
                for (Row& row : layout.rows) {

                    row.size.w.val = -0.0f;
                    row.size.h.val = -0.0f;

                    for (Element* member : row.members) {
                        if (!member->resolved.affectsParentSize) { continue; }
                        row.size.w.val = std::max(row.size.w.val, member->resolved.getOuter(Axis::Horizontal));
                        row.size.h.val += member->resolved.getOuter(Axis::Vertical);
                    }

                    layout.size.w.val += row.size.w.val;
                    layout.size.h.val = std::max(layout.size.h.val, row.size.h.val);
                }
            }

            else {

                // Measure val
                for (Row& row : layout.rows) {

                    row.size.w.val = -0.0f;
                    row.size.h.val = -0.0f;

                    for (Element* member : row.members) {
                        if (!member->resolved.affectsParentSize) { continue; }
                        row.size.w.val += member->resolved.getOuter(Axis::Horizontal);
                        row.size.h.val = std::max(row.size.h.val, member->resolved.getOuter(Axis::Vertical));
                    }

                    layout.size.w.val = std::max(layout.size.w.val, row.size.w.val);
                    layout.size.h.val += row.size.h.val;
                }
            }
        }

        void growHorizontalMode() {

            // Grow growable dimensions (horizontal)
            //--------------------------------------------------

            layout.size.w.max = std::min(layout.size.w.max, resolved.getInner(Axis::Horizontal));

            for (Row& row : layout.rows) {

                row.size.w.max = std::min(row.size.w.max, layout.size.w.max);
                
                float availableWidth = row.size.w.max - row.size.w.val;

                // Loop until break conditions are met
                while (true) {

                    int numGrowable = row.canGrow(Axis::Horizontal);
                    float share = availableWidth / float(numGrowable);

                    // When there's no more space or no more growable elements
                    if (!numGrowable || availableWidth < 0.01) {
                        break;
                    }

                    for (Element* member : row.members) {
                        float take = member->resolved.grow(share, Axis::Horizontal);
                        row.size.w.val += take;
                        availableWidth -= take;
                    }
                }
            }

            // Grow each row (vertical)
            //--------------------------------------------------

            // Consider moving back to "min" strategy to handle fitting
            layout.size.h.max = std::min(layout.size.h.max, resolved.getInner(Axis::Vertical));
            float availableHeight = layout.size.h.max - layout.size.h.val;

            // Loop until break conditions are met
            while (true) {

                int numGrowableH = layout.growableRows(Axis::Vertical);
                float shareH = availableHeight / float(numGrowableH);

                // When there's no more space or no more growable elements
                if (!numGrowableH || availableHeight < 0.01) {
                    break;
                }

                for (Row& row : layout.rows) {
                    float take = row.size.h.grow(shareH);
                    layout.size.h.val += take;
                    availableHeight -= take;
                }
            }

            // Grow each row member (vertical)
            //--------------------------------------------------

            for (Row& row : layout.rows) {

                // The cross extent a member may fill is its row's measured extent
                // capped at OUR inner cross size. The measured row extent is
                // max(member outer) and so includes margins — a relative-plus-margin
                // sibling can push it past the space we actually have. Every other
                // grow step is already bounded by getInner; this one must be too.
                float crossTarget = std::min(row.size.h.val, resolved.getInner(Axis::Vertical));

                for (Element* member : row.members) {

                    Element& elem = *member;

                    float availableElemHeight = crossTarget - elem.resolved.getOuter(Axis::Vertical);

                    while (true) {

                        int numGrowable = elem.resolved.canGrow(Axis::Vertical);
                        float share = availableElemHeight / float(numGrowable);

                        if (!numGrowable || availableElemHeight < 0.01) {
                            break;
                        }

                        float take = elem.resolved.grow(share, Axis::Vertical);
                        availableElemHeight -= take;
                    }
                }
            }
        }

        void growVerticalMode() {

            // Grow growable dimensions (vertical)
            //--------------------------------------------------
            // Main axis (vertical): members within each row share its height.
            // Mirror of growHorizontalMode's first (horizontal) phase.

            layout.size.h.max = std::min(layout.size.h.max, resolved.getInner(Axis::Vertical));

            for (Row& row : layout.rows) {

                row.size.h.max = std::min(row.size.h.max, layout.size.h.max);

                float availableHeight = row.size.h.max - row.size.h.val;

                // Loop until break conditions are met
                while (true) {

                    int numGrowable = row.canGrow(Axis::Vertical);
                    float share = availableHeight / float(numGrowable);

                    // When there's no more space or no more growable elements
                    if (!numGrowable || availableHeight < 0.01) {
                        break;
                    }

                    for (Element* member : row.members) {
                        float take = member->resolved.grow(share, Axis::Vertical);
                        row.size.h.val += take;
                        availableHeight -= take;
                    }
                }
            }

            // Grow each row (horizontal)
            //--------------------------------------------------
            // Cross axis (horizontal): the rows share our inner width.

            // Consider moving back to "min" strategy to handle fitting
            layout.size.w.max = std::min(layout.size.w.max, resolved.getInner(Axis::Horizontal));
            float availableWidth = layout.size.w.max - layout.size.w.val;

            // Loop until break conditions are met
            while (true) {

                int numGrowableW = layout.growableRows(Axis::Horizontal);
                float shareW = availableWidth / float(numGrowableW);

                // When there's no more space or no more growable elements
                if (!numGrowableW || availableWidth < 0.01) {
                    break;
                }

                for (Row& row : layout.rows) {
                    float take = row.size.w.grow(shareW);
                    layout.size.w.val += take;
                    availableWidth -= take;
                }
            }

            // Grow each row member (horizontal)
            //--------------------------------------------------
            // Cross axis (horizontal): each member fills its row's width.

            for (Row& row : layout.rows) {

                // Cross extent capped at our inner (mirror of growHorizontalMode):
                // a relative-plus-margin sibling must not drag a growable member
                // past the space we actually have.
                float crossTarget = std::min(row.size.w.val, resolved.getInner(Axis::Horizontal));

                for (Element* member : row.members) {

                    Element& elem = *member;

                    float availableElemWidth = crossTarget - elem.resolved.getOuter(Axis::Horizontal);

                    while (true) {

                        int numGrowable = elem.resolved.canGrow(Axis::Horizontal);
                        float share = availableElemWidth / float(numGrowable);

                        if (!numGrowable || availableElemWidth < 0.01) {
                            break;
                        }

                        float take = elem.resolved.grow(share, Axis::Horizontal);
                        availableElemWidth -= take;
                    }
                }
            }
        }

        // Reusable center function
        float center(float parent, float child, Align align) {

            switch (align) {
                case (Align::Start): { return 0; break; }
                case (Align::End): { return parent - child; break; }
                case (Align::Center): { return (parent - child) / 2; break; }
                case (Align::SpaceAround):
                case (Align::SpaceBetween):
                case (Align::Unset): { return 0; break; }
            }

            return 0;
        }

        // Resolve final positions (top down)
        void resolveRects() {

            if (resolved.hidden) { return; }

            // If top level
            if (parent == this) {
                rect = {
                    0, 0,
                    resolved.size.w.val, resolved.size.h.val
                };
            }

            // Requires children
            if (children.empty()) {
                return;
            }

            // Resolve dimensions
            //--------------------------------------------------

            // Resolve layout dimensions
            layout.rect.w = layout.size.w.val;
            layout.rect.h = layout.size.h.val;

            // Resolve row dimensions
            for (Row& row : layout.rows) {
                
                //row.size.clamp();
                row.rect.w = row.size.w.val;
                row.rect.h = row.size.h.val;

                // Resolve member dimensions
                for (Element* member : row.members) {
                    //member->resolved.size.clamp();
                    member->rect.w = member->resolved.size.w.val;
                    member->rect.h = member->resolved.size.h.val;
                }
            }

            // Resolve positions
            //--------------------------------------------------

            float layoutOffsetX = resolved.pad.l.val;
            float layoutOffsetY = resolved.pad.t.val;

            Style& rStyle = resolved.style;

            layoutOffsetX += center(resolved.getInner(Axis::Horizontal), layout.rect.w, rStyle.layout.horizontal);
            layoutOffsetY += center(resolved.getInner(Axis::Vertical), layout.rect.h, rStyle.layout.vertical);

            // Apply scroll offset
            //--------------------------------------------------
            // Every child is positioned relative to layout.rect, so shifting the
            // single layout origin scrolls the whole subtree coherently. This is
            // orthogonal to overflow/clipping: scrolled content still spills out
            // (and stays hittable) unless the element also sets overflow: Hide.

            Scroll scrollMode = rStyle.scroll;
            bool scrollX = (scrollMode == Scroll::Horizontal || scrollMode == Scroll::Both);
            bool scrollY = (scrollMode == Scroll::Vertical || scrollMode == Scroll::Both);

            if (scrollX) {
                float maxScroll = std::max(0.0f, layout.rect.w - resolved.getInner(Axis::Horizontal));
                if (resolved.scroll.x < 0.0f) { resolved.scroll.x = 0.0f; }
                if (resolved.scroll.x > maxScroll) { resolved.scroll.x = maxScroll; }
                layoutOffsetX -= resolved.scroll.x;
            }

            if (scrollY) {
                float maxScroll = std::max(0.0f, layout.rect.h - resolved.getInner(Axis::Vertical));
                if (resolved.scroll.y < 0.0f) { resolved.scroll.y = 0.0f; }
                if (resolved.scroll.y > maxScroll) { resolved.scroll.y = maxScroll; }
                layoutOffsetY -= resolved.scroll.y;
            }

            // Resolve layout position
            layout.rect.x = rect.x + layoutOffsetX;
            layout.rect.y = rect.y + layoutOffsetY;

            auto placeAbsoluteMember = [&](Element* member) {

                const float outerW =
                    member->rect.w +
                    member->resolved.mar.l.val +
                    member->resolved.mar.r.val;

                const float outerH =
                    member->rect.h +
                    member->resolved.mar.t.val +
                    member->resolved.mar.b.val;

                member->rect.x = rect.x + member->resolved.mar.l.val;
                member->rect.y = rect.y + member->resolved.mar.t.val;

                if (set(member->resolved.pos.l.val)) {
                    member->rect.x += member->resolved.pos.l.val;
                }
                else if (set(member->resolved.pos.r.val)) {
                    member->rect.x =
                        rect.x +
                        resolved.size.w.val -
                        outerW -
                        member->resolved.pos.r.val;
                }

                if (set(member->resolved.pos.t.val)) {
                    member->rect.y += member->resolved.pos.t.val;
                }
                else if (set(member->resolved.pos.b.val)) {
                    member->rect.y =
                        rect.y +
                        resolved.size.h.val -
                        outerH -
                        member->resolved.pos.b.val;
                }
            };

            // Cross-axis member alignment is opt-in. When enabled, each member is
            // aligned within its row on the axis perpendicular to `direction`,
            // using that axis's Align value.
            bool crossAlign = (rStyle.layout.crossAlign != CrossAlign::Unset);

            if (resolved.style.layout.direction == Axis::Vertical) {

                float runningX = 0;

                // Position rows
                for (Row& row : layout.rows) {

                    float rowOffsetY = center(layout.rect.h, row.rect.h, rStyle.layout.vertical);

                    row.rect.x = layout.rect.x + runningX;
                    row.rect.y = layout.rect.y + rowOffsetY;

                    float runningY = 0;

                    Element* first = row.members.front();
                    Element* last = row.members.back();

                    for (Element* member : row.members) {

                        if (member->resolved.hidden) { continue; }

                        if (member->resolved.style.layout.position == Position::Absolute) {
                            placeAbsoluteMember(member);
                            continue;
                        }

                        // Cross axis here is horizontal: align the member's outer
                        // width within the row's width.
                        float crossOffsetX = 0;

                        if (crossAlign) {
                            float memberOuterW =
                                member->rect.w +
                                member->resolved.mar.l.val +
                                member->resolved.mar.r.val;

                            crossOffsetX = center(row.rect.w, memberOuterW, rStyle.layout.horizontal);
                        }

                        member->rect.x = row.rect.x + crossOffsetX + member->resolved.mar.l.val;
                        member->rect.y = row.rect.y + runningY + member->resolved.mar.t.val;

                        member->rect.x += member->resolved.pos.l.val;
                        member->rect.y += member->resolved.pos.t.val;
   
                        runningY += member->rect.h + member->resolved.mar.t.val + member->resolved.mar.b.val;
                    }

                    runningX += row.rect.w;
                }
            }

            else {

                float runningY = 0;

                // Position rows
                for (Row& row : layout.rows) {

                    float rowOffsetX = center(layout.rect.w, row.rect.w, rStyle.layout.horizontal);
                    
                    row.rect.x = layout.rect.x + rowOffsetX;
                    row.rect.y = layout.rect.y + runningY;

                    float runningX = 0;

                    Element* first = row.members.front();
                    Element* last = row.members.back();

                    for (Element* member : row.members) {

                        if (member->resolved.hidden) { continue; }

                        if (member->resolved.style.layout.position == Position::Absolute) {
                            placeAbsoluteMember(member);
                            continue;
                        }

                        // Cross axis here is vertical: align the member's outer
                        // height within the row's height.
                        float crossOffsetY = 0;

                        if (crossAlign) {
                            float memberOuterH =
                                member->rect.h +
                                member->resolved.mar.t.val +
                                member->resolved.mar.b.val;

                            crossOffsetY = center(row.rect.h, memberOuterH, rStyle.layout.vertical);
                        }

                        member->rect.x = row.rect.x + runningX + member->resolved.mar.l.val;
                        member->rect.y = row.rect.y + crossOffsetY + member->resolved.mar.t.val;

                        member->rect.x += member->resolved.pos.l.val;
                        member->rect.y += member->resolved.pos.t.val;
                        
                        runningX += member->rect.w + member->resolved.mar.l.val + member->resolved.mar.r.val;
                    }

                    runningY += row.rect.h;
                }
            }
        }

        // Animation queue
        //--------------------------------------------------

        void requestAnimate() {

            if (!shared) { return; }

            auto& list = shared->dirty.animate;

            if (std::find(list.begin(), list.end(), this) == list.end()) {
                list.push_back(this);
            }
        }

        void stopAnimateRequest() {

            if (!shared) { return; }

            auto& list = shared->dirty.animate;

            list.erase(
                std::remove(list.begin(), list.end(), this),
                list.end()
            );
        }

        // Event callbacks (Rev::Core::Dispatcher)
        //--------------------------------------------------------------------------------

        using ListenerFunc = void (Element::*)(Event&);

        void listen(ListenerFunc func, const std::function<void(Event&)>& listener) {
            if (dispatcher) { dispatcher->listen(func, listener); }
        }

        void tell(ListenerFunc tellingFunc, Event& e) {
            if (dispatcher) { dispatcher->tell(tellingFunc, e); }
        }

        void onRefresh(const std::function<void(Event&)>& listener) { listen(&Element::refresh, listener); }
        void onClick(const std::function<void(Event&)>& listener) { listen(&Element::click, listener); }
        void onMouseDown(const std::function<void(Event&)>& listener) { listen(&Element::mouseDown, listener); }
        void onMouseUp(const std::function<void(Event&)>& listener) { listen(&Element::mouseUp, listener); }
        void onMouseMove(const std::function<void(Event&)>& listener) { listen(&Element::mouseMove, listener); }
        void onDrag(const std::function<void(Event&)>& listener) { listen(&Element::mouseDrag, listener); }
        void onGainFocus(const std::function<void(Event&)>& listener) { listen(&Element::gainFocus, listener); }
        void onLoseFocus(const std::function<void(Event&)>& listener) { listen(&Element::loseFocus, listener); }
        void onMouseEnter(const std::function<void(Event&)>& listener) { listen(&Element::mouseEnter, listener); }
        void onMouseLeave(const std::function<void(Event&)>& listener) { listen(&Element::mouseLeave, listener); }
        void onMouseWheel(const std::function<void(Event&)>& listener) { listen(&Element::mouseWheel, listener); }
        void onKeyDown(const std::function<void(Event&)>& listener) { listen(&Element::keyDown, listener); }
        void onKeyUp(const std::function<void(Event&)>& listener) { listen(&Element::keyUp, listener); }
        void onTextInput(const std::function<void(Event&)>& listener) { listen(&Element::textInput, listener); }

        // Event propagation
        //--------------------------------------------------

        struct TargetFlags {
            bool hit = false;
            bool click = false;
            bool hover = false;
            bool press = false;
            bool focus = false;
            bool drag = false;
            bool disabled = false;
        };

        TargetFlags targetFlags;
        bool tabStop = false;

        // Set this element's disabled INTENT (like hover/focus, a style state).
        // The effective disabled value is cascaded to descendants in
        // cascadeStyle, so disabling a section disables its whole subtree. Marks
        // dirty to kick a recompute; cascadeStyle then re-resolves what changed.
        // Does not itself gate input -- each control decides what "disabled"
        // MEANS behaviourally (Button swallows clicks, TextInput ignores keys).
        void setDisabled(bool d) {
            if (targetFlags.disabled == d) { return; }
            targetFlags.disabled = d;
            styles.dirty = true;
        }

        // When true and this element contains the cursor, elements drawn behind it
        // (earlier in draw order) do not receive hit for this frame.
        bool interceptHits = false;

        // Default behavior is to ask our rect if it contains a position
        virtual bool contains(Pos& pos) {
            return this->rect.contains(pos);
        }

        // When the element refreshes
        virtual void refresh(Event& e) {

            if (!this->dirty.draw) {

                this->dirty.draw = true;
                e.causedRefresh = true;
                shared->dirty.refresh.push_back(this);
            }
    
            // Propagate upwards
            if (parent && !parent->dirty.draw) {
                parent->refresh(e);
            }
        }

        // When the element gains focus
        virtual void gainFocus(Event& e) {

            if (!targetFlags.focus) {
                targetFlags.focus = true;
                if (resolved.hasFocusStyle) {
                    styles.dirty = true;
                }
            }

            dispatcher->tell(&Element::gainFocus, e);
            if (!e.propagate) { return; }

            // Propagate to children
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isFocusTarget = child.targetFlags.focus;

                if (containsEvent && !isFocusTarget) { child.gainFocus(e); }
                if (!e.propagate) { return; }
            }
        }

        // When the element loses focus
        virtual void loseFocus(Event& e) {

            if (targetFlags.focus) {
                targetFlags.focus = false;
                if (resolved.hasFocusStyle) { styles.dirty = true; }
            }

            dispatcher->tell(&Element::loseFocus, e);
            if (!e.propagate) { return; }

            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isFocusTarget = child.targetFlags.focus;

                if (!containsEvent && isFocusTarget) { child.loseFocus(e); }
                if (!e.propagate) { return; }
            }
        }

        virtual void click(Event& e) {

            dispatcher->tell(&Element::click, e);
            if (!e.propagate) { return; }


        }

        // When a mouse button is pressed
        virtual void mouseDown(Event& e) {

            // Mouse down event means we are a drag target
            if (!targetFlags.drag) {
                targetFlags.drag = true;
                if (resolved.hasDragStyle) { styles.dirty = true; }
            }

            if (!targetFlags.press) {
                targetFlags.press = true;
                if (resolved.hasPressStyle) {
                    styles.dirty = true;
                }
            }

            dispatcher->tell(&Element::mouseDown, e);
            if (!e.propagate) { return; }

            // Propagate
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool isFocusTarget = child.targetFlags.focus;
                bool containsEvent = child.targetFlags.hit;

                if (containsEvent) { child.mouseDown(e); }
                if (containsEvent && !isFocusTarget) { child.gainFocus(e); }
                if (!containsEvent && isFocusTarget) { child.loseFocus(e); }
            
                if (!e.propagate) { return; }
            }
        }

        // When a mouse button is released
        virtual void mouseUp(Event& e) {

            // Mouseup means dragging must end
            if (targetFlags.drag) {
                targetFlags.drag = false;
                if (resolved.hasDragStyle) { styles.dirty = true; }
            }

            if (targetFlags.press) {
                targetFlags.press = false;
                if (resolved.hasPressStyle) { styles.dirty = true; }
            }

            // Stop if listener does not pass "continue" flag
            dispatcher->tell(&Element::mouseUp, e);
            if (!e.propagate) { return; }

            // Process children in reverse
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool isDragTarget = child.targetFlags.drag;
                bool isPressTarget = child.targetFlags.press;
                bool containsEvent = child.targetFlags.hit;

                if (containsEvent || isDragTarget) { child.mouseUp(e); }
                if (containsEvent && isPressTarget) { child.click(e); }

                if (!e.propagate) { return; }
            }
        }

        // When the mouse moves on/over the element
        virtual void mouseMove(Event& e) {

            // If there is a cursor we need to set
            if (resolved.style.cursor != Cursor::Unset) {
                e.mouse.cursor = resolved.style.cursor;
            }

            // Stop if listener does not pass "continue" flag
            dispatcher->tell(&Element::mouseMove, e);
            if (!e.propagate) {
                return;
            }
            
            // Process children in reverse
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool isHoverTarget = child.targetFlags.hover;
                bool containsEvent = child.targetFlags.hit;

                if (containsEvent && !isHoverTarget) { child.mouseEnter(e); }
                if (!containsEvent && isHoverTarget) { child.mouseLeave(e); }
                if (containsEvent) { child.mouseMove(e); }

                if (!e.propagate) {
                    return;
                }
            }
        }

        // When the mouse enters the element
        virtual void mouseEnter(Event& e) {

            if (!targetFlags.hover) {
                targetFlags.hover = true;
                if (resolved.hasHoverStyle) {
                    styles.dirty = true;
                }
            }

            dispatcher->tell(&Element::mouseEnter, e);
            if (!e.propagate) { return; }

            // Propagate to children
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isHoverTarget = child.targetFlags.hover;

                if (containsEvent && !isHoverTarget) { child.mouseEnter(e); }
                if (!e.propagate) { return; }
            }
        }

        // When a mouse leaves the element
        virtual void mouseLeave(Event& e) {

            if (targetFlags.hover) {
                targetFlags.hover = false;
                if (resolved.hasHoverStyle) { styles.dirty = true; }
            }

            dispatcher->tell(&Element::mouseLeave, e);
            if (!e.propagate) { return; }

            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isHoverTarget = child.targetFlags.hover;

                if (!containsEvent && isHoverTarget) { child.mouseLeave(e); }
                if (!e.propagate) { return; }
            }
        }

        // When the mouse drags in/on the element
        virtual void mouseDrag(Event& e) {

            // Stop if listener does not pass "continue" flag
            dispatcher->tell(&Element::mouseDrag, e);
            if (!e.propagate) { return; }

            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                // We propagate to all children which are also drag targets
                if (child.targetFlags.drag) { child.mouseDrag(e); }
                if (!e.propagate) { return; }
            }
        }

        // Pixels of scroll per unit of native wheel delta (one notch = 120).
        static constexpr float scrollSpeed = 0.5f;

        // When the mouse scrolls
        virtual void mouseWheel(Event& e) {

            dispatcher->tell(&Element::mouseWheel, e);
            if (!e.propagate) { return; }

            // Offer the scroll to children first so the innermost scrollable
            // element under the cursor consumes it; a parent only takes over
            // once the child leaves e.propagate set (i.e. it can't move further).
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                // Check if the e position is within the child's rectangle
                if (child.targetFlags.hit) { child.mouseWheel(e); }
                if (!e.propagate) { return; }
            }

            // Consume the scroll if we scroll along the wheel's axis. The new
            // offset is clamped against content/viewport in resolveRects.
            Scroll mode = resolved.style.scroll;

            bool canX = (mode == Scroll::Horizontal || mode == Scroll::Both) && e.mouse.wheel.x != 0;
            bool canY = (mode == Scroll::Vertical || mode == Scroll::Both) && e.mouse.wheel.y != 0;

            if (!canX && !canY) { return; }

            // Wheel up / left (positive delta) reveals earlier content, i.e.
            // a smaller offset.
            if (canX) { resolved.scroll.x -= e.mouse.wheel.x * scrollSpeed; }
            if (canY) { resolved.scroll.y -= e.mouse.wheel.y * scrollSpeed; }

            // Scrolling shifts the child origin, so rects must be re-resolved.
            if (shared) { shared->layoutDirty = true; }

            refresh(e);
            e.propagate = false;
        }

        // When a key is pressed
        virtual void keyDown(Event& e) {

            dispatcher->tell(&Element::keyDown, e);
            if (!e.propagate) { return; }

            // Propogate in reverse order
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                if (child.targetFlags.focus) { child.keyDown(e); }
                if (!e.propagate) { return; }
            }
        }

        // When a key is released
        virtual void keyUp(Event& e) {

            dispatcher->tell(&Element::keyUp, e);
            if (!e.propagate) { return; }

            // Propagate in reverse order
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                if (child.targetFlags.focus) { child.keyUp(e); }
                if (!e.propagate) { return; }
            }
        }

        // When text is recieved
        virtual void textInput(Event& e) {

            dispatcher->tell(&Element::textInput, e);
            if (!e.propagate) { return; }

            // Propagate in reverse order
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                if (child.targetFlags.focus) { child.textInput(e); }
                if (!e.propagate) { return; }
            }
        }
    };
}
