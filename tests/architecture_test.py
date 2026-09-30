#!/usr/bin/env python3
"""Guard dependency direction and obvious feature-ID dispatch in generic code.

This is a source-level tripwire, not a proof of arbitrary program behavior. The
cross-renderer synthetic-application test supplies independent behavioral checks.
"""

from pathlib import Path
import re
import sys


def strip_comments(source):
    # Keep string literals intact: include paths and ID constants are evidence.
    token = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*.*?\*/', re.S)
    return token.sub(lambda match: match.group(1) or " " * len(match.group(0)), source)


def violations(source, public_header=False):
    source = strip_comments(source)
    findings = []
    dependencies = re.compile(r'(?:#\s*include\s*[<"]|(?:from|import)\s*["\'])([^>"\']+)')
    for match in dependencies.finditer(source):
        path = match.group(1).replace("\\", "/")
        if "examples/" in path or Path(path).name in ("application.hpp", "session.hpp"):
            findings.append("Generic renderer depends on application composition: " + path)
        if public_header and ("backends/" in path or path.startswith(("FL/", "SDL", "windows.h"))):
            findings.append("Public contract depends on a host implementation: " + path)
    if re.search(r'\bExample\b', source):
        findings.append("Generic renderer names the example application type")
    identity = r'(?:\b\w+\s*(?:\.|->)\s*)*\b(?:id|binding)\b'
    literal = r'(?:"[^"\n]+"|\'[^\'\n]+\')'
    patterns = [
        identity + r'\s*(?:===?|!==?)\s*' + literal,
        literal + r'\s*(?:===?|!==?)\s*' + identity,
        identity + r'\s*\.\s*(?:starts_with|ends_with|startsWith|endsWith|find|includes)\s*\(\s*' + literal,
        r'\b(?:strcmp|strncmp)\s*\([^\n;]*(?:id|binding)[^\n;]*,\s*' + literal,
        r'\bswitch\s*\([^)]*(?:\bid\b|\bbinding\b)[^)]*\)\s*\{[^}]*\bcase\s+' + literal,
        r'\bfind_widget\s*\([^;\n]*\{\s*' + literal,
        r'\bWidgetKey\s*[({]\s*' + literal,
    ]
    if any(re.search(pattern, source, re.S) for pattern in patterns):
        findings.append("Generic renderer branches on or requests a literal application identity")
    return findings


def negative_fixtures():
    rejected = [
        '#include "../../examples/application.hpp"',
        '#include "application.hpp"',
        'import "../examples/model.mjs";',
        'Example* model;',
        'if (widget.spec.key.id == "feature::x") do_work();',
        'if ("feature::x" == widget.spec.key.id) do_work();',
        'if (value.binding !== "feature::x") do_work();',
        'if (id.starts_with("feature::")) do_work();',
        'if (strcmp(widget.key.id.c_str(), "feature::x") == 0) do_work();',
        'switch (widget.key.id) { case "feature::x": run(); }',
        'find_widget(snapshot, {"feature::x", 1});',
        'auto target = WidgetKey{"feature::x", 1};',
    ]
    for source in rejected:
        assert violations(source), "Architecture guard missed its negative fixture: " + source
    assert violations('#include <FL/Fl.H>', public_header=True), "Public native dependency was accepted"
    accepted = [
        'if (widget.spec.key.id == input.target.id) dispatch();',
        'if (type == "activate") send(Activate{});',
        'if (widget.spec.kind == Kind::button) draw_button();',
        'if (widget.key.id.empty()) reject();',
        'find_widget(snapshot, target);',
        '// Example uses id == "example".\n#include "gui/contract.hpp"',
    ]
    for source in accepted:
        assert not violations(source), "Architecture guard rejected a generic fixture: " + source


def main(root):
    negative_fixtures()
    failures = []
    sources = sorted((root / "include" / "gui").rglob("*.hpp"))
    sources += sorted(path for path in (root / "backends").rglob("*")
                      if path.suffix in (".hpp", ".h", ".cpp", ".mjs", ".js", ".py")
                      and path.name not in ("host.hpp", "wasm.cpp")
                      and not re.fullmatch(r"main(?:_[a-z0-9]+)?\.cpp", path.name))
    for path in sources:
        for reason in violations(path.read_text(), public_header=path.is_relative_to(root / "include")):
            failures.append(str(path.relative_to(root)) + ": " + reason)
    if failures:
        raise AssertionError("\n".join(failures))
    print(f"Architecture dependency and identity guards passed for {len(sources)} files; negative fixtures passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1])
