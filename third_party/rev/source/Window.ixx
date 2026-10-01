module;

#include <vector>
#include <queue>
#include <algorithm>
#include <string>
#include <codecvt>
#include <locale>
#include <dbg.hpp>

#include "WinEvent.hpp"

export module Rev.Window;

import Rev.Core.Pos;

import Rev.Element;
import Rev.Element.Box;
import Rev.Appearance;
import Rev.Element.Event;

import Rev.NativeWindow;
import Rev.Graphics.Canvas;

import Rev.Core.Resource;
import Rev.Core.Svg;

export namespace Rev {

    using namespace Rev::Element;
    using namespace Rev::Core;
    using namespace Rev::Appearance;

    struct Window : public Rev::Element::Element {

        struct Details {

            struct Extent {
                int width = 0;
                int height = 0;
            };

            struct Size {
                int width = 640;
                int height = 480;
                Extent min = { 0, 0 };
                Extent max = { 1000, 1000 };
            };

            std::string name = "Hello World";
            Size size = {};

            // Optional window icon, supplied as an SVG resource (e.g.
            // File("./Logo.svg")). Rasterized to the OS icon sizes and applied in
            // unifiedConstructor. Empty (data == nullptr) leaves the default.
            Core::Resource icon = {};

            int x = 0, y = 0;

            float scale = 1.0f;

            bool decorated = true;
            bool resizable = true;
            bool borderless = false;
            bool fullscreen = false;
            bool embedded = false;

            // No OS title bar, but a native resizable frame (WS_THICKFRAME): the
            // frame is suppressed via WM_NCCALCSIZE and resize is driven by
            // WM_NCHITTEST, giving native resize/snap with a custom 1px look.
            bool nativeFrameless = false;

            bool closeButton = true;
            bool minimizeButton = true;
            bool maximizeButton = true;

            std::string display = "";

            NativeWindow::Size nativeSize() const {
                return {
                    size.width, size.height,
                    size.min.width, size.min.height,
                    size.max.width, size.max.height
                };
            }

            NativeWindow::Details nativeDetails() const {
                return {
                    .name = name,
                    .size = nativeSize(),
                    .decorated = decorated,
                    .resizable = resizable,
                    .borderless = borderless,
                    .fullscreen = fullscreen,
                    .closeButton = closeButton,
                    .minimizeButton = minimizeButton,
                    .maximizeButton = maximizeButton,
                    .nativeFrameless = nativeFrameless
                };
            }
        };

        // Persistent event
        Event event;

        // Native windowing
        NativeWindow* window = nullptr;
        Details details;

        bool shouldClose = false;

        static NativeWindow::Relationship relationshipFor(
            Window* owner,
            const Details& details
        ) {
            if (!owner) {
                return NativeWindow::Relationship::Independent;
            }

            return details.embedded
                ? NativeWindow::Relationship::EmbeddedChild
                : NativeWindow::Relationship::OwnedTopLevel;
        }

        static void* nativeHandleFor(Window* owner) {
            return (
                owner && owner->window
                ? owner->window->handle
                : nullptr
            );
        }

        static std::vector<void*>* windowGroupFor(Window* owner) {
            return (
                owner && owner->shared
                ? owner->shared->windowGroup
                : nullptr
            );
        }

        void joinWindowGroup(std::vector<void*>* group) {

            if (!group) { return; }

            group->push_back(static_cast<void*>(this));

            if (shared) {
                shared->windowGroup = group;
            }
        }

        // A child of another Rev window is an owned top-level window by default.
        // Set Details::embedded=true when an actual WS_CHILD HWND is desired.
        Window(Window* owner, Details details) : Element(details.embedded ? owner : nullptr) {
            
            this->parent = details.embedded && owner
                ? owner
                : this;
            this->details = details;

            window = new NativeWindow(
                nativeHandleFor(owner),
                details.nativeDetails(),
                [this](WinEvent& event) { this->onEvent(event); },
                relationshipFor(owner, details)
            );

            this->unifiedConstructor();

            if (!details.embedded) {
                joinWindowGroup(windowGroupFor(owner));
            }
        }

        // With native window owner/parent. Uses Details::embedded to distinguish
        // an owned top-level window from a WS_CHILD window.
        Window(void* ownerOrParent, Details details) : Element() {

            this->details = details;

            window = new NativeWindow(
                ownerOrParent,
                details.nativeDetails(),
                [this](WinEvent& event) { this->onEvent(event); },
                details.embedded
                    ? NativeWindow::Relationship::EmbeddedChild
                    : NativeWindow::Relationship::OwnedTopLevel
            );

            this->parent = this;

            this->unifiedConstructor();
        }

        // Top-level window (same path as any other independent Rev window).
        // Register in application->windows, build UI as children of this.
        Window(std::vector<void*>& group, Details details) : Element() {

            this->parent = this;
            this->details = details;

            // Create native window
            window = new NativeWindow(
                nullptr,
                details.nativeDetails(),
                [this](WinEvent& event) { this->onEvent(event); },
                NativeWindow::Relationship::Independent
            );

            this->unifiedConstructor();

            joinWindowGroup(&group);
        }

        // Registered child of another top-level window. Owned top-level by
        // default; set Details::embedded=true for an actual WS_CHILD window.
        Window(std::vector<void*>& group, Window* owner, Details details) : Element() {

            this->parent = details.embedded && owner
                ? owner
                : this;
            this->details = details;

            window = new NativeWindow(
                nativeHandleFor(owner),
                details.nativeDetails(),
                [this](WinEvent& event) { this->onEvent(event); },
                relationshipFor(owner, details)
            );

            this->unifiedConstructor();

            if (!details.embedded) {
                joinWindowGroup(&group);
            }
        }

        void unifiedConstructor() {

            shared = new Shared();
            shared->event = &event;

            // Canvas
            //--------------------------------------------------

            shared->canvas = new Graphics::Canvas(window);
            event.canvas = shared->canvas;

            // Children
            //--------------------------------------------------

            // Window movement and resizing are now handled natively by the OS
            // (see NativeWindow WM_NCCALCSIZE / WM_NCHITTEST for the frameless
            // mode). The old manual screen-space drag bar has been removed.

            // Follow guidelines supplied by details
            //--------------------------------------------------

            if (!details.display.empty()) {

                std::vector<NativeWindow::Display> displays =
                    NativeWindow::getDisplays();

                for (NativeWindow::Display& display : displays) {

                    if (display.friendlyName.find(details.display)
                        == std::wstring::npos)
                    {
                        continue;
                    }

                    setRect(
                        display.x,
                        display.y,
                        display.w,
                        display.h
                    );

                    break;
                }
            }

            if (window) {

                details.scale = window->scale;
                details.size.width = (int)(window->size.w / window->scale);
                details.size.height = (int)(window->size.h / window->scale);

                if (shared && shared->canvas) {
                    shared->canvas->flags.resize = true;
                }
            }

            applyIcon();
        }

        // Rasterize the optional Details::icon SVG at the OS icon sizes and hand
        // the raw pixels to the native window. Done at this layer (not in
        // NativeWindow) because SVG rasterization lives above the native layer.
        void applyIcon() {

            if (!window) { return; }
            if (!details.icon.data || details.icon.size == 0) { return; }

            // Title-bar (small) and alt-tab/taskbar (big) icons, each rasterized
            // at its native size for crispness rather than scaling one bitmap.
            Core::Svg::Bitmap iconBig   = Core::Svg::rasterize(details.icon, 32, 32);
            Core::Svg::Bitmap iconSmall = Core::Svg::rasterize(details.icon, 16, 16);

            if (iconBig.data) {
                window->setIcon(iconBig.data, (int)iconBig.width, (int)iconBig.height, true);
                delete[] iconBig.data;
            }

            if (iconSmall.data) {
                window->setIcon(iconSmall.data, (int)iconSmall.width, (int)iconSmall.height, false);
                delete[] iconSmall.data;
            }
        }

        // Destroy
        ~Window() {

            if (window) {
                window->tryMakeContextCurrent();
            }

            if (shared && shared->windowGroup) {

                std::vector<void*>& group = *shared->windowGroup;
                void* handle = static_cast<void*>(this);

                if (std::find(group.begin(), group.end(), handle) != group.end()) {

                    group.erase(
                        std::remove(group.begin(), group.end(), handle),
                        group.end()
                    );
                }
            }

            // Destroy the element subtree while shared is still valid.
            // ~Window runs before ~Element; without this, ~Element would
            // touch shared->dirty after shared is deleted below.
            std::vector<Element*> childrenCopy = children;

            for (Element* child : childrenCopy) {
                if (child) { delete child; }
            }

            children.clear();
            parent = nullptr;

            if (shared) {
                delete shared->canvas;
                shared->canvas = nullptr;

                delete shared;
                shared = nullptr;
            }

            delete window;
            window = nullptr;
        }

        // Layout
        //--------------------------------------------------

        // Pre-constructed queues of all children
        std::vector<Element*> topDown;
        std::vector<Element*> bottomUp;

        // Calculate top-down call order
        void calcTopDownQueue(Element* element) {

            topDown.push_back(element);

            if (element != this && element->resolved.hidden) {
                return;
            }
        
            for (Element* child : element->children) {
                calcTopDownQueue(child);
            }
        }

        // Calculate bottom-up call order
        void calcBottomUpQueue(Element* element) {

            if (element == this || !element->resolved.hidden) {

                for (Element* child : element->children) {
                    calcBottomUpQueue(child);
                }
            }

            bottomUp.push_back(element);
        }

        // Calculate top-down / bottom-up call orders
        void calculateQueues() {

            topDown.clear(); bottomUp.clear();
            this->calcTopDownQueue(this);
            this->calcBottomUpQueue(this);
        }

        // Top-level only
        void calcFlexLayouts() {

            // Rebuild visibility before resetting: a formerly hidden subtree
            // was absent from the previous queue and still has old dimensions.
            this->cascadeStyle();
            this->calculateQueues();
            for (Element* element : topDown) { element->resetResolved(); }
            // resetResolved restores default parent-size eligibility, so apply
            // inherited hidden/disabled state again before measuring children.
            this->cascadeStyle();

            // Resolve minima, then maxima, then layout
            for (Element* element : bottomUp) { element->resolveMinima(); }
            for (Element* element : topDown) { element->resolveMaxima(); }

            for (Element* element : bottomUp) { element->resolveLayout(); }
            for (Element* element : topDown) { element->resolveDims(); }

            for (Element* element: topDown) { element->resolveRects(); }
        }

        // Calculate draw order
        //--------------------------------------------------

        std::vector<Element*> drawList;
        struct DeferredLayer {
            Element* element;
            size_t sequence;
        };
        struct DeferredLayerOrder {
            bool operator()(const DeferredLayer& a, const DeferredLayer& b) const {
                if (a.element->resolved.depth != b.element->resolved.depth)
                    return a.element->resolved.depth < b.element->resolved.depth;
                return a.sequence > b.sequence;
            }
        };
        // Lower effective layers paint first even when a higher layer (such as
        // hover help) was constructed before a dynamic overlay. Equal layers
        // retain their previous queue insertion order.
        std::priority_queue<DeferredLayer, std::vector<DeferredLayer>, DeferredLayerOrder> deferred;
        size_t deferred_sequence = 0;

        void recurseDrawList(Element* current) {

            for (Element* elem : current->children) {

                if (elem->resolved.hidden) {
                    continue;
                }

                if (elem->resolved.depth > current->resolved.depth) {
                    drawList.push_back(elem);
                    recurseDrawList(elem);
                    continue;
                }

                deferred.push({elem, deferred_sequence++});
            }

            while (!deferred.empty()) {

                Element* elem = deferred.top().element;

                if (elem->resolved.hidden) {
                    deferred.pop();
                    continue;
                }

                if (elem->resolved.depth > current->resolved.depth) {
                    deferred.pop();
                    drawList.push_back(elem);
                    recurseDrawList(elem);
                    continue;
                }

                break;
            }
        }

        void calcDrawList() {

            drawList.clear();
            deferred = {};
            deferred_sequence = 0;

            recurseDrawList(this);

            // Layers lifted above the root remain deferred after unwinding.
            // Emit them in effective layer order, still traversing each
            // parent's ordinary children before moving to the next layer.
            while (!deferred.empty()) {
                Element* elem = deferred.top().element;
                deferred.pop();

                if (elem->resolved.hidden) {
                    continue;
                }

                drawList.push_back(elem);
                recurseDrawList(elem);
            }
        }

        // Draw
        //--------------------------------------------------

        void computeStyle(Event& e) override {
            this->style->size = {
                .width = Px(details.size.width),
                .height = Px(details.size.height)
            };
            Element::computeStyle(e);
        }

        void refresh(Event& e) override {
            this->dirty.draw = true;
            requestNextFrame();
        }

        void requestNextFrame() {
            if (window) { window->requestFrame(); }
        }

        void computeStyleTopDown(Event& e, Element* element) {

            element->computeStyle(e);

            if (element != this && element->resolved.hidden) {
                return;
            }

            for (Element* child : element->children) {
                this->computeStyleTopDown(e, child);
            }
        }

        void computeChildrenTopDown(Event& e, Element* parent) {

            for (Element* child : parent->children) {

                if (child->resolved.hidden) {
                    continue;
                }
                
                child->computeChildren(e);

                this->computeChildrenTopDown(e, child);
            }
        }

        void draw(Event& e) override {

            //dbg("Drawing");

            Graphics::Canvas& canvas = *shared->canvas;
            canvas.beginFrame();

            // Reset before
            event.resetBeforeDispatch();
            shared->dirty.refresh.clear();
            shared->stencilStack.clear();

            this->computeChildrenTopDown(e, this);

            this->calculateQueues();

            // Compute styles before resolving
            //--------------------------------------------------

            this->computeStyleTopDown(e, this);

            this->dirty.draw = false;

            // Cascade inherited state (hidden / disabled / depth / opacity) BEFORE
            // resolving styles, so applies.disabled variants match this frame's
            // effective state. Cascading only inside the layout pass is not enough:
            // a disabled toggle is pure paint (raises no layoutDirty), so its
            // resolved.disabled would never recompute and the *Disabled styles
            // would never (un)apply. A flip here marks styles dirty, which pushes
            // the element into the restyle list resolved just below.
            this->cascadeStyle();

            // Resolve own style, then any that are explicitly marked
            this->resolveStyle(e);
            for (Element* element : shared->dirty.restyle) { element->resolveStyle(e); }
            shared->dirty.restyle.clear();

            // Animate transitions
            //--------------------------------------------------

            std::vector<Element*>& animate = shared->dirty.animate;

            for (Element* element : animate) { element->animate(e); }

            // Remove elements that no longer have any transitions
            animate.erase(
                std::remove_if(animate.begin(), animate.end(),
                    [](Element* element) { return element->transitions.empty(); }
                ),
                animate.end()
            );

            if (!animate.empty()) {
                requestNextFrame();

                // Relayout only while a layout-affecting transition is live.
                for (Element* element : animate) {
                    if (element->animatesLayout) {
                        shared->layoutDirty = true;
                        break;
                    }
                }
            }

            // Visibility and layout
            //--------------------------------------------------
            // Skip the entire flex pipeline when nothing this frame could have
            // changed geometry; last frame's resolved rects/sizes are reused.

            if (shared->layoutDirty) {
                this->calcFlexLayouts();
                shared->layoutDirty = false;
            }

            for (Element* element : topDown) {

                if (element->resolved.hidden) { continue; }

                element->computePrimitives(e);
            }

            //raphics::Canvas& canvas = *shared->canvas;
            std::vector<Element*>& stencilStack = shared->stencilStack;

            canvas.stencilReset(0);

            // Draw all elements
            //--------------------------------------------------

            this->calcDrawList();

            for (Element* element : drawList) {

                // Ignore self and hidden elements
                if (element == this) { continue; }

                size_t size = stencilStack.size();
                Element* back = size ? stencilStack.back() : nullptr;

                const auto descends_from = [](Element* child, Element* ancestor) {
                    for (Element* parent = child->parent; parent; parent = parent->parent) {
                        if (parent == ancestor) return true;
                        if (parent == parent->parent) break;
                    }
                    return false;
                };
                // A numeric layer depth is not a tree depth: unrelated
                // branches may have different z-indices. Exit every clip
                // outside this branch, or escaped by a lifted descendant.
                while (back && back != this &&
                       (!descends_from(element, back) || element->resolved.depth <= back->resolved.depth)) {
                    stencilStack.pop_back();
                    size = stencilStack.size();
                    back = size ? stencilStack.back() : nullptr;
                    canvas.stencilSet(size);

                    // Restore the enclosing clip after each level is removed.
                    if (back) { back->stencil(e); }
                    else { canvas.stencilReset(0); }
                }

                // Do not draw hidden objects
                if (element->resolved.hidden) { continue; }

                element->draw(e);
            }

            // Post-draw
            //--------------------------------------------------

            shared->canvas->endFrame();
            if (this->dirty.draw) {
                refresh(e);
            }
        }

        // Controlling window
        //--------------------------------------------------

        // Show and focus this top-level window.
        void show() {
            if (window) { window->show(); }
        }

        void popUp() {
            show();
        }

        // Hide (iconify) window
        void minimize() {

        }

        void maximize() {
            
        }

        void setSize(int width, int height) {
            this->window->setSize(width, height);
        }

        void setPos(int x, int y) {

            details.x = x;
            details.y = y;

            window->setPos(x, y);
        }

        void setTitle(const std::string& title) {

            details.name = title;

            if (window) {
                window->setTitle(title);
            }
        }

        void setRect(int x, int y, int w, int h) {

            details.x = x;
            details.y = y;

            details.size.width = w;
            details.size.height = h;

            window->setRect(x, y, w, h);
        }

        // Convert an absolute screen coordinate (physical px, as supplied by the
        // native layer) into window-local logical coordinates.
        //
        // The origin we subtract is the *client* area's top-left in screen space,
        // queried live from the OS via getClientPos() — NOT the cached details.x/y.
        // details.x/y is written from WM_MOVE (client origin) but also from setPos
        // (window origin, incl. frame), so it has two meanings and goes stale; a
        // hit-test built on it is off by the window's screen position, which only
        // happens to cancel out near (0,0). The live client origin is authoritative
        // everywhere on the desktop and across monitors.
        Pos screenToLocal(float screenX, float screenY) const {

            float s = (window && window->scale != 0.0f) ? window->scale : 1.0f;

            int ox = 0, oy = 0;
            if (window) { window->getClientPos(ox, oy); }

            return {
                (screenX - float(ox)) / s,
                (screenY - float(oy)) / s
            };
        }

        // Responding to window events
        //--------------------------------------------------

        static void markHitChain(Element* element) {

            for (Element* chain = element; chain; chain = chain->parent) {

                chain->targetFlags.hit = true;

                if (chain->parent == chain) { break; }
            }
        }

        // Mark hit targets. Uses draw order when available so interceptHits can
        // block pointer hits on elements painted underneath.
        void setTargets(Event& e) {

            for (Element* element : topDown) {
                element->targetFlags.hit = false;
            }

            if (!topDown.empty()) {
                calcDrawList();
            }

            if (drawList.empty()) {

                for (Element* element : bottomUp) {

                    Element& elem = *element;

                    if (elem.resolved.hidden) { continue; }
                    if (elem.contains(e.mouse.pos)) { markHitChain(element); }
                }

                return;
            }

            int blockBefore = -1;

            for (size_t i = 0; i < drawList.size(); i++) {

                Element& elem = *drawList[i];

                if (elem.resolved.hidden) { continue; }
                if (!elem.contains(e.mouse.pos)) { continue; }

                if (elem.interceptHits && (int)i > blockBefore) {
                    blockBefore = (int)i;
                }
            }

            for (size_t i = 0; i < drawList.size(); i++) {

                if (blockBefore >= 0 && (int)i < blockBefore) { continue; }

                Element& elem = *drawList[i];

                if (elem.resolved.hidden) { continue; }
                if (!elem.contains(e.mouse.pos)) { continue; }

                markHitChain(drawList[i]);
            }
        }

        virtual void onEvent(WinEvent& event) {

            if (event.subject) {
                window = (NativeWindow*)event.subject;
            }

            switch (event.type) {

                case (WinEvent::Create): { this->onOpen(); break; }
                case (WinEvent::Destroy): { this->shouldClose = true; break; }
                case (WinEvent::Close): {
                    this->onClose(event.rejected);

                    if (!event.rejected) {
                        this->shouldClose = true;
                    }

                    break;
                }

                case (WinEvent::Focus): { this->onFocus(); break; }
                case (WinEvent::Defocus): { this->onDefocus(); break; }

                case (WinEvent::Move): { this->onMove(event.c, event.d); break; }
                case (WinEvent::Resize): { this->onResize(event.c, event.d); break; }
                case (WinEvent::Maximize): { this->onMaximize(); break; }
                case (WinEvent::Minimize): { this->onMinimize(); break; }
                case (WinEvent::Restore): { this->onRestore(); break; }
                case (WinEvent::Scale): { this->onScale(window->scale); break; }

                case (WinEvent::Paint): { this->draw(this->event); break; }

                case (WinEvent::MouseMove): { this->onCursorPos(event.c, event.d); break; }
                case (WinEvent::MouseButton): { this->onMouseButton(event.a, event.b, event.c, event.d); break; }
                case (WinEvent::MouseWheel): { this->onMouseWheel(event.c, event.d); break; }
                case (WinEvent::CaptureLost): { this->onCaptureLost(); break; }

                case (WinEvent::Keyboard): { this->onKeyboard(event.a, event.b); break; }
                case (WinEvent::Character): { this->onCharacter(event.a); break; }
                case (WinEvent::Null):
                case (WinEvent::Clear): { break; }
            }
        }

        // Overridable callbacks
        //--------------------------------------------------

        virtual void onOpen() {
            //dbg("[Window] Open");
        }

        virtual void onClose(bool& rejectClose) {
            rejectClose = false;
        }

        virtual void onMaximize() {
            //dbg("[Window] Maximize");
        }

        virtual void onMinimize() {
            //dbg("[Window] Minimize");
        }

        virtual void onRestore() {
            //dbg("[Window] Restore");
        }
        
        // When the window gains focus
        virtual void onFocus() {
            //dbg("[Window] Focus");
        }

        // When the window loses focus
        virtual void onDefocus() {
            //dbg("[Window] Defocus");
        }

        // When the window changes position
        virtual void onMove(int x, int y) {

            //dbg("[Window] Move: %i, %i", x, y);

            this->details.x = x;
            this->details.y = y;
        }

        // When the window is resized
        virtual void onResize(int width, int height) {

            //dbg("[Window] Resize: %i, %i", width, height);

            details.size.width = width / window->scale;
            details.size.height = height / window->scale;

            if (!shared) { return; }
            if (!shared->canvas) { return; }

            shared->canvas->flags.resize = true;

            // The root box just changed size; the whole tree must re-resolve.
            shared->layoutDirty = true;

            this->refresh(event);
        }

        virtual void onScale(float scale) {

            //dbg("[Window] scale");

            details.scale = scale;
            // Native resize notifications can precede a DPI change (Win32's
            // suggested rectangle is applied before Scale is dispatched).
            // Recompute and invalidate at the new scale even when the physical
            // client size itself did not change.
            onResize(window->size.w, window->size.h);
        }

        // Mouse/keyboard callbacks (window only)
        //--------------------------------------------------

        // When a mouse button is clicked or released
        void onMouseButton(int button, int action, int screenX, int screenY) {

            //dbg("[Window] mouseButton");

            // Native supplies absolute screen coordinates; store them and derive
            // the window-local logical position used for hit-testing/dispatch.
            event.mouse.screenPos = { float(screenX), float(screenY) };
            event.mouse.pos = screenToLocal(float(screenX), float(screenY));

            if (action == NativeWindow::ButtonAction::Press) { event.mouse.down = event.mouse.pos; }
            if (action == NativeWindow::ButtonAction::Release) { event.mouse.up = event.mouse.pos; }

            event.resetBeforeDispatch();
            event.id += 1;

            this->setTargets(event);

            switch (button) {
                case (NativeWindow::MouseButton::Left): { event.mouse.lb.set(action, event.mouse.pos); break; }
                case (NativeWindow::MouseButton::Right): { event.mouse.rb.set(action, event.mouse.pos); break; }
            }

            switch (action) {
                case (NativeWindow::ButtonAction::Press): {
                    this->mouseDown(event);
                    break;
                }
                case (NativeWindow::ButtonAction::Release): { this->mouseUp(event); break; }
            }

            this->refresh(event);

            if (event.causedRefresh) {
                this->refresh(event);
            }
        }

        // When the mouse moves. Native supplies absolute screen coordinates.
        void onCursorPos(float screenX, float screenY) {

            //dbg("CursorPos");

            event.mouse.screenPos = { screenX, screenY };
            event.mouse.pos = screenToLocal(screenX, screenY);
            event.mouse.diff = event.mouse.pos - event.mouse.down;
            event.resetBeforeDispatch();

            this->setTargets(event);

            // Dispatch mouse move
            this->mouseMove(event);

            window->setCursor(event.mouse.cursor);

            if (targetFlags.drag) {
                this->mouseDrag(event);
            }

            if (event.causedRefresh) {
                this->refresh(event);
            }
        }

        // When mouse wheel or trackpad scrolls
        void onMouseWheel(float dx, float dy) {

            //dbg("[Window] mouseWheel: %f, %f", dx, dy);

            event.mouse.wheel = { dx, dy };
            event.resetBeforeDispatch();

            this->setTargets(event);

            this->mouseWheel(event);

            if (event.causedRefresh) {
                this->refresh(event);
            }
        }

        // When mouse capture is lost mid-drag (see NativeWindow WM_CAPTURECHANGED).
        // Synthesize a release so press/drag target flags don't get stuck on.
        void onCaptureLost() {

            event.mouse.lb.set(NativeWindow::ButtonAction::Release, event.mouse.pos);
            event.mouse.rb.set(NativeWindow::ButtonAction::Release, event.mouse.pos);

            event.resetBeforeDispatch();
            this->setTargets(event);

            this->mouseUp(event);

            this->refresh(event);
        }

        // When a key is depressed or released
        void onKeyboard(int key, int action) {

            //dbg("[Window] Key %s", window->keyToString(key));

            NativeWindow::Key winKey = static_cast<NativeWindow::Key>(key);

            // Persistent button-state representation of Key
            switch (winKey) {

                case (NativeWindow::Key::Ctrl): { event.keyboard.ctrl.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Shift): { event.keyboard.shift.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Alt): { event.keyboard.alt.set(action, event.mouse.pos); break; }
                
                case (NativeWindow::Key::Left): { event.keyboard.arrows.left.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Right): { event.keyboard.arrows.right.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Up): { event.keyboard.arrows.up.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Down): { event.keyboard.arrows.down.set(action, event.mouse.pos); break; }

                case (NativeWindow::Key::Backspace): { event.keyboard.backspace.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Delete): { event.keyboard.del.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Escape): { event.keyboard.escape.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Tab): { event.keyboard.tab.set(action, event.mouse.pos); break; }
                case (NativeWindow::Key::Space): { event.keyboard.space.set(action, event.mouse.pos); break; }

                case (NativeWindow::Key::Enter):
                case (NativeWindow::Key::NumpadEnter): { event.keyboard.enter.set(action, event.mouse.pos); break; }
                default: { break; }
            }

            // Clear textual represenation first
            event.keyboard.key = "";

            // Textual representation of Key.
            switch (winKey) {

                case (NativeWindow::Key::Ctrl): { event.keyboard.key = "ctrl"; break; }
                case (NativeWindow::Key::Shift): { event.keyboard.key = "shift"; break; }
                case (NativeWindow::Key::Alt): { event.keyboard.key = "alt"; break; }

                case (NativeWindow::Key::Left): { event.keyboard.key = "left"; break; }
                case (NativeWindow::Key::Right): { event.keyboard.key = "right"; break; }
                case (NativeWindow::Key::Up): { event.keyboard.key = "up"; break; }
                case (NativeWindow::Key::Down): { event.keyboard.key = "down"; break; }
                case (NativeWindow::Key::Home): { event.keyboard.key = "home"; break; }
                case (NativeWindow::Key::End): { event.keyboard.key = "end"; break; }

                case (NativeWindow::Key::Backspace): { event.keyboard.key = "backspace"; break; }
                case (NativeWindow::Key::Delete): { event.keyboard.key = "delete"; break; }
                case (NativeWindow::Key::Escape): { event.keyboard.key = "escape"; break; }
                case (NativeWindow::Key::Tab): { event.keyboard.key = "tab"; break; }

                case (NativeWindow::Key::Enter):
                case (NativeWindow::Key::NumpadEnter): { event.keyboard.key = "enter"; break; }

                case (NativeWindow::Key::Space): { event.keyboard.key = " "; break; }

                case (NativeWindow::Key::A): { event.keyboard.key = "a"; break; }
                case (NativeWindow::Key::B): { event.keyboard.key = "b"; break; }
                case (NativeWindow::Key::C): { event.keyboard.key = "c"; break; }
                case (NativeWindow::Key::D): { event.keyboard.key = "d"; break; }
                case (NativeWindow::Key::E): { event.keyboard.key = "e"; break; }
                case (NativeWindow::Key::F): { event.keyboard.key = "f"; break; }
                case (NativeWindow::Key::G): { event.keyboard.key = "g"; break; }
                case (NativeWindow::Key::H): { event.keyboard.key = "h"; break; }
                case (NativeWindow::Key::I): { event.keyboard.key = "i"; break; }
                case (NativeWindow::Key::J): { event.keyboard.key = "j"; break; }
                case (NativeWindow::Key::K): { event.keyboard.key = "k"; break; }
                case (NativeWindow::Key::L): { event.keyboard.key = "l"; break; }
                case (NativeWindow::Key::M): { event.keyboard.key = "m"; break; }
                case (NativeWindow::Key::N): { event.keyboard.key = "n"; break; }
                case (NativeWindow::Key::O): { event.keyboard.key = "o"; break; }
                case (NativeWindow::Key::P): { event.keyboard.key = "p"; break; }
                case (NativeWindow::Key::Q): { event.keyboard.key = "q"; break; }
                case (NativeWindow::Key::R): { event.keyboard.key = "r"; break; }
                case (NativeWindow::Key::S): { event.keyboard.key = "s"; break; }
                case (NativeWindow::Key::T): { event.keyboard.key = "t"; break; }
                case (NativeWindow::Key::U): { event.keyboard.key = "u"; break; }
                case (NativeWindow::Key::V): { event.keyboard.key = "v"; break; }
                case (NativeWindow::Key::W): { event.keyboard.key = "w"; break; }
                case (NativeWindow::Key::X): { event.keyboard.key = "x"; break; }
                case (NativeWindow::Key::Y): { event.keyboard.key = "y"; break; }
                case (NativeWindow::Key::Z): { event.keyboard.key = "z"; break; }
                default: { break; }
            }

            // Build the canonical chord string once, now that modifiers + key are
            // set, so handlers can match it with  event.keyboard.combo == "ctrl+s".
            event.keyboard.buildCombo();

            event.resetBeforeDispatch();
            this->setTargets(event);

            if (action) { this->keyDown(event); }
            else { this->keyUp(event); }

            if (event.causedRefresh) {
                this->refresh(event);
            }
        }

        void onCharacter(char32_t character) {

            // Convert UTF-32 → UTF-8
            std::u32string s32(1, character);
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
            std::string utf8 = conv.to_bytes(s32);
        
            dbg("[Window] Character: %s (U+%04X)", utf8.c_str(), (unsigned)character);
            event.keyboard.input = utf8;

            event.resetBeforeDispatch();
            this->setTargets(event);

            this->textInput(event);

            if (event.causedRefresh) {
                this->refresh(event);
            }
        }
    };
}
