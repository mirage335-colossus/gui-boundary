#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>   // <-- needed for CATransaction
#import <objc/runtime.h>
#import "NativeWindow.mac.h"


@interface RevView : NSView
    @property (nonatomic, assign) void* userData;
    @property (nonatomic, assign) RevMacEventAcceptor acceptor;
@end

@implementation RevView

    // Ensure we can get key events
    - (BOOL)acceptsFirstResponder { return YES; }

    - (instancetype)initWithFrame:(NSRect)frame {
        if ((self = [super initWithFrame:frame])) {
            self.wantsLayer = YES; // Always layer-backed
            //self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
            self.layer.delegate = (id<CALayerDelegate>)self; // We’ll receive drawLayer:inContext:
        }
        return self;
    }

    // Core paint callback (layer-backed)
    - (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
        if (self.acceptor) {
            WinEvent ev { WinEvent::Paint, 0, 0, 0, 0 };
            self.acceptor(self.userData, ev);
        }
    }

    // Optional: keep a no-op drawRect for completeness, but not required
    - (void)drawRect:(NSRect)dirtyRect {
        // On modern macOS, this usually won't be called
    }

    #pragma mark - Resize

    // When view (essentially window) is resized
    - (void)setFrameSize:(NSSize)newSize {

        [super setFrameSize:newSize];

        if (self.acceptor) {
            WinEvent ev { WinEvent::Resize, 0, 0, (int)newSize.width, (int)newSize.height };
            self.acceptor(self.userData, ev);
        }
    }

    // When view (essentially window) is moved
    - (void)setFrameOrigin:(NSPoint)newOrigin {
        
        [super setFrameOrigin:newOrigin];

        if (self.acceptor && self.window) {

            // Convert to screen space
            NSRect screenRect = [self convertRect:[self bounds] toView:nil];
            NSRect globalRect = [self.window convertRectToScreen:screenRect];

            WinEvent ev { WinEvent::Move, 0, 0,
                        (int)globalRect.origin.x,
                        (int)globalRect.origin.y };

            self.acceptor(self.userData, ev);
        }
    }

    #pragma mark - Window lifecycle hooks

    // Add observer functions when view moves to window
    - (void)viewDidMoveToWindow {

        [super viewDidMoveToWindow];

        if (self.window) {

            NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];

            if (self.acceptor) {
                WinEvent ev { WinEvent::Create, 0, 0, 0, 0 };
                self.acceptor(self.userData, ev);
            }

            [nc addObserver:self selector:@selector(windowDidMiniaturize:)
                    name:NSWindowDidMiniaturizeNotification object:self.window];

            [nc addObserver:self selector:@selector(windowDidDeminiaturize:)
                    name:NSWindowDidDeminiaturizeNotification object:self.window];

            [nc addObserver:self selector:@selector(windowDidBecomeKey:)
                    name:NSWindowDidBecomeKeyNotification object:self.window];

            [nc addObserver:self selector:@selector(windowDidResignKey:)
                    name:NSWindowDidResignKeyNotification object:self.window];

            [nc addObserver:self selector:@selector(windowDidEnterFullScreen:)
                    name:NSWindowDidEnterFullScreenNotification object:self.window];
                    
            [nc addObserver:self selector:@selector(windowDidExitFullScreen:)
                    name:NSWindowDidExitFullScreenNotification object:self.window];
        }
    }

    - (void)viewWillMoveToWindow:(NSWindow *)newWindow {
        if (self.window) { [[NSNotificationCenter defaultCenter] removeObserver:self name:nil object:self.window]; }
        [super viewWillMoveToWindow:newWindow];
    }

    #pragma mark - Window notifications

    - (void)windowDidBecomeKey:(NSNotification*)n {
        WinEvent ev { WinEvent::Focus, 0, 0, 0, 0 };
        if (self.acceptor) self.acceptor(self.userData, ev);
    }

    - (void)windowDidResignKey:(NSNotification*)n {
        WinEvent ev { WinEvent::Defocus, 0, 0, 0, 0 };
        if (self.acceptor) self.acceptor(self.userData, ev);
    }

    // When window is minimized
    - (void)windowDidMiniaturize:(NSNotification*)n {
        WinEvent ev { WinEvent::Minimize, 0, 0, 0, 0 };
        if (self.acceptor) self.acceptor(self.userData, ev);
    }

    // When window is maximized
    - (void)windowDidEnterFullScreen:(NSNotification*)n {
        WinEvent ev { WinEvent::Maximize, 0, 0, 0, 0 };
        if (self.acceptor) self.acceptor(self.userData, ev);
    }

    // When window goes from mimnimized -> regular
    - (void)windowDidDeminiaturize:(NSNotification*)n {
        WinEvent ev { WinEvent::Restore, 0, 0, 0, 0 };
        if (self.acceptor) self.acceptor(self.userData, ev);
    }

    // When window goes from maximized -> regular
    - (void)windowDidExitFullScreen:(NSNotification*)n {
        WinEvent ev { WinEvent::Restore, 0, 0, 0, 0 };
        if (self.acceptor) self.acceptor(self.userData, ev);
    }

    #pragma mark - Keyboard input (interceptable)

    // When keyboard key is depressed
    - (void)keyDown:(NSEvent *)event {
        
        WinEvent ev { WinEvent::Keyboard, (uint64_t)[event keyCode], 1, 0, 0 };
        
        if (self.acceptor) self.acceptor(self.userData, ev);
        if (!ev.rejected) [super keyDown:event];
    }

    // When keyboard key is lifted
    - (void)keyUp:(NSEvent *)event {

        WinEvent ev { WinEvent::Keyboard, (uint64_t)[event keyCode], 0, 0, 0 };

        if (self.acceptor) self.acceptor(self.userData, ev);
        if (!ev.rejected) [super keyUp:event];
    }

    // When user pastes text or types character
    - (void)insertText:(id)string {

        NSString* s = (NSString*)string;

        for (NSUInteger i = 0; i < [s length]; ++i) {

            unichar ch = [s characterAtIndex:i];
            WinEvent ev { WinEvent::Character, (uint64_t)ch, 0, 0, 0 };

            if (self.acceptor) self.acceptor(self.userData, ev);
            if (ev.rejected) return; // stop immediately
        }

        [super insertText:string];
    }

    #pragma mark - Mouse input (interceptable)

    // When a mouse button is depressed
    - (void)mouseDown:(NSEvent *)event {

        NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
        WinEvent ev { WinEvent::MouseButton, 0, 1, (int)p.x, (int)p.y };

        if (self.acceptor) self.acceptor(self.userData, ev);
        if (!ev.rejected) [super mouseDown:event];
    }

    // When a mouse button is lifted
    - (void)mouseUp:(NSEvent *)event {

        NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
        WinEvent ev { WinEvent::MouseButton, 0, 0, (int)p.x, (int)p.y };

        if (self.acceptor) self.acceptor(self.userData, ev);
        if (!ev.rejected) [super mouseUp:event];
    }

    // When mouse moves
    - (void)sendMouseMove:(NSEvent *)e {
        NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
        WinEvent ev { WinEvent::MouseMove, 0, 0, (int)p.x, (int)p.y };
        if (self.acceptor) self.acceptor(self.userData, ev);
    }

    // All send to mouseMoved
    - (void)mouseMoved:(NSEvent *)e        { [self sendMouseMove:e]; }
    - (void)mouseDragged:(NSEvent *)e      { [self sendMouseMove:e]; }
    - (void)rightMouseDragged:(NSEvent *)e { [self sendMouseMove:e]; }
    - (void)otherMouseDragged:(NSEvent *)e { [self sendMouseMove:e]; }

    // Mouse wheel or trackpad scroll event
    - (void)scrollWheel:(NSEvent *)event {

        if (self.acceptor) {

            CGFloat dx = [event scrollingDeltaX];
            CGFloat dy = [event scrollingDeltaY];

            WinEvent ev { WinEvent::MouseWheel, 0, 0, (int)dx, (int)dy };

            self.acceptor(self.userData, ev);
        }
    }

@end

// Window creation / management
//--------------------------------------------------

RevMacWindowHandle rev_mac_window_create(
    int width, int height,
    int minWidth, int minHeight,
    int maxWidth, int maxHeight,
    bool borderless,
    void* userData,
    RevMacEventAcceptor acceptor,
    void* parent
) {
    @autoreleasepool {

        NSRect frame = NSMakeRect(0, 0, width, height);

        RevView* view = [[RevView alloc] initWithFrame:frame];
        view.userData = userData;
        view.acceptor = acceptor;

        if (parent) {
            
            // Parent is an NSView* provided by host (e.g. VST)
            NSView* parentView = (__bridge NSView*)parent;
            [parentView addSubview:view];

            // return the view handle
            return (__bridge_retained void*)view;  
        }
        
        else {

            NSWindowStyleMask styleMask = NSWindowStyleMaskTitled |
                                          NSWindowStyleMaskClosable |
                                          NSWindowStyleMaskResizable;

            if (borderless) {
                styleMask = NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable;
            }

            // No parent → create our own window and install RevView
            NSWindow* window = [[NSWindow alloc]
                initWithContentRect:frame
                          styleMask:styleMask
                            backing:NSBackingStoreBuffered
                              defer:NO];

            if (minWidth > 0 && minHeight > 0) {
                [window setContentMinSize:NSMakeSize(minWidth, minHeight)];
            }

            if (maxWidth > 0 && maxHeight > 0) {
                [window setContentMaxSize:NSMakeSize(maxWidth, maxHeight)];
            }

            [window setContentView:view];
            [window makeKeyAndOrderFront:nil];
            [window setAcceptsMouseMovedEvents:YES];

            // Keep view alive via association (window owns it now)
            objc_setAssociatedObject(window, "rev_view", view, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

            return (__bridge_retained void*)window; // return window handle
        }
    }
}


void rev_mac_window_destroy(RevMacWindowHandle handle) {

    if (!handle) { return; }

    NSObject* obj = (__bridge NSObject*)handle;

    // Destroy
    if ([obj isKindOfClass:[NSWindow class]]) {
        NSWindow* window = (NSWindow*)obj;
        [window close];
    }
    
    // safely detach from parent
    else if ([obj isKindOfClass:[NSView class]]) {
        NSView* view = (NSView*)obj;
        [view removeFromSuperview];
    }
}

void rev_mac_window_set_size(RevMacWindowHandle handle, int w, int h) {

    if (!handle) return;

    NSObject* obj = (__bridge NSObject*)handle;
    if ([obj isKindOfClass:[NSWindow class]]) {
        NSWindow* window = (NSWindow*)obj;
        NSRect frame = [window frame];
        frame.size = NSMakeSize(w,h);
        [window setFrame:frame display:YES];
    }
    
    // Resize view
    else if ([obj isKindOfClass:[NSView class]]) {
        NSView* view = (NSView*)obj;
        NSRect frame = [view frame];
        frame.size = NSMakeSize(w,h);
        [view setFrame:frame];
    }
}


// Utility
//--------------------------------------------------

void rev_mac_window_request_frame(RevMacWindowHandle handle) {
    if (!handle) return;

    NSObject *obj = (__bridge NSObject *)handle;
    RevView *view = nil;

    if ([obj isKindOfClass:[NSWindow class]]) {
        view = (RevView *)[(NSWindow*)obj contentView];
    } else if ([obj isKindOfClass:[NSView class]]) {
        view = (RevView *)obj;
    }
    if (!view) return;

    // Mark view dirty for next compositor pass
    [view setNeedsDisplay:YES];

    // Schedule a draw callback once AppKit becomes idle again
    RevView *weakView = view;

    dispatch_async(dispatch_get_main_queue(), ^{
        RevView *strongView = weakView;
        if (!strongView || !strongView.acceptor) return;

        // Only fire if we’re still alive and visible
        WinEvent ev { WinEvent::Paint, 0, 0, 0, 0 };
        strongView.acceptor(strongView.userData, ev);
    });
}

void rev_mac_wait_event() {
    @autoreleasepool {
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantFuture]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event) {
            [NSApp sendEvent:event];
        }
    }
}