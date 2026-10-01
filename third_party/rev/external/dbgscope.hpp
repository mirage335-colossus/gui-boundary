#ifndef DBGSCOPE_H
#define DBGSCOPE_H

// Structured, per-scope-timed derive logging layered on top of the flat `dbg` primitive.
// Diagnostics are emitted and flushed immediately: a long OCC operation must identify
// the active stage/scope while it is running, not reveal it only after the derive ends.
// The WHOLE facility compiles out in release exactly like
// dbg.hpp -- no chrono, no allocation, no string building on the hot path.
//
// Reporting only, never behavior (same house rule as Operation::OpDiag): nothing here
// touches computation, ordering, or the prev/implied chain. Single-threaded derive is
// assumed; the routing state is thread_local as cheap insurance if a strategy ever forks.

#include <dbg.hpp>

#ifdef DEBUG

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace dbgscope {

    // Verbosity dial, in the spirit of Cam::App::occDiagLevel. Not yet gating output
    // (all levels emit today); reserved so inner sub-op chatter can be dialed down later.
    inline int level = 2;

    // One stage's live-log state. `totalMs` accumulates only direct (depth-1)
    // child scopes so nested sub-scopes are not double-counted.
    struct Block {
        int stage = 0;
        bool started = false;
        long long totalMs = 0;
    };

    // The whole derive's buffered output, flushed grouped at the very end.
    struct Log {
        std::vector<Block> blocks;

        // Lazily create-or-find a stage's block. Creation order is stage order because
        // the geometry pass writes stages ascending; the toolpath pass reuses them.
        Block& forStage(int stage) {
            for (Block& b : blocks) { if (b.stage == stage) { return b; } }
            blocks.push_back(Block{ stage, false, 0 });
            return blocks.back();
        }

        void start(Block& block) {
            if (block.started) { return; }
            block.started = true;
            dbg("[Stage %d] Calculating...", block.stage);
        }

        // Geometry and toolpath are separate passes, so only their combined stage total
        // remains deferred. All actionable diagnostics have already printed live.
        void flush() {
            bool first = true;
            for (size_t i = 0; i < blocks.size(); i++) {
                Block& b = blocks[i];
                if (!b.started) { continue; }
                if (!first) { dbg(""); }
                dbg("[Stage %d] Done in %lldms", b.stage, b.totalMs);
                first = false;
            }
            blocks.clear();
        }
    };

    // Routing state for the active derive (single-threaded; thread_local as insurance).
    inline thread_local Log* active   = nullptr;   // current derive's log, or null
    inline thread_local int  curStage = 0;         // 1-based stage index being processed
    inline thread_local int  depth    = 0;         // scope nesting depth (0 = at stage level)

    // Emit a diagnostic content line immediately. dbg() flushes stdout on every line.
    inline void note(const std::string& s) {
        if (active && curStage > 0) {
            Block& block = active->forStage(curStage);
            active->start(block);
            dbg("[Stage %d] %s", curStage, s.c_str());
        } else {
            dbg("%s", s.c_str());
        }
    }

    // printf-style variant, so existing `dbg("...", a, b)` sites re-home with a rename.
    inline void notef(const char* fmt, ...) {
        char buf[2048];
        va_list a; va_start(a, fmt);
        vsnprintf(buf, sizeof(buf), fmt, a);
        va_end(a);
        note(std::string(buf));
    }

    // Owns the active Log for one derive. Construction arms routing and starts a whole-
    // derive clock (used only for the [Derive] summary split); flush() is called
    // explicitly by the driver after both passes to print combined stage totals.
    struct Session {
        Log log;
        std::chrono::steady_clock::time_point t0;
        Session()  : t0(std::chrono::steady_clock::now()) { active = &log; }
        ~Session() { active = nullptr; }               // no auto-flush; driver flushes
        void flush() { log.flush(); }
        long long ms() const {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
        }
    };

    // Sets the current stage index for its lexical scope (one stage's slice within a
    // pass), restoring the prior value on exit so passes/reentry nest cleanly.
    struct StageMark {
        int prev;
        explicit StageMark(int stage) : prev(curStage) { curStage = stage; }
        ~StageMark() { curStage = prev; }
    };

    // A timed sub-scope (operation / strategy / sub-op). On destruct it files
    // "[Stage N] <label> Done in Xms" into the current block; a depth-1 scope also adds
    // its time to the stage total. Falls back to live dbg when no session is active.
    struct Scope {
        std::chrono::steady_clock::time_point t0;
        std::string label;
        int myDepth;
        int myStage;
        bool routed;
        explicit Scope(const std::string& lbl)
            : t0(std::chrono::steady_clock::now()), label(lbl) {
            depth++;
            myDepth = depth;
            myStage = curStage;
            routed  = (active && curStage > 0);
            if (routed) {
                Block& block = active->forStage(myStage);
                active->start(block);
                dbg("[Stage %d] %s Calculating...", myStage, label.c_str());
            }
            else {
                dbg("%s Calculating...", label.c_str());
            }
        }
        ~Scope() {
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            if (routed) {
                Block& b = active->forStage(myStage);
                dbg("[Stage %d] %s Done in %lldms", myStage, label.c_str(), ms);
                if (myDepth == 1) { b.totalMs += ms; }
            } else {
                dbg("%s Done in %lldms", label.c_str(), ms);
            }
            depth--;
        }
    };

} // namespace dbgscope

// Timed-scope macros: in DEBUG they declare a uniquely-named local whose destructor times
// the enclosing block; in release they vanish ENTIRELY -- the label expression (string
// building, displayName() calls) is never evaluated, so there is zero hot-path cost.
#define DBGSCOPE_CAT2(a, b) a##b
#define DBGSCOPE_CAT(a, b)  DBGSCOPE_CAT2(a, b)
#define DBG_STAGE(idx)   dbgscope::StageMark DBGSCOPE_CAT(_dbgStage_, __LINE__)(idx)
#define DBG_SCOPE(label) dbgscope::Scope     DBGSCOPE_CAT(_dbgScope_, __LINE__)(label)

#else // !DEBUG -- empty stubs mirroring dbg.hpp; the whole facility disappears.

#include <string>

namespace dbgscope {
    inline int level = 0;
    struct Session { void flush() {} long long ms() const { return 0; } };
    struct StageMark { explicit StageMark(int) {} };
    struct Scope { explicit Scope(const char*) {} };
    inline void note(const std::string&) {}
    inline void notef(const char*, ...) {}
}

#define DBG_STAGE(idx)   ((void)0)
#define DBG_SCOPE(label) ((void)0)

#endif // DEBUG

#endif // DBGSCOPE_H
