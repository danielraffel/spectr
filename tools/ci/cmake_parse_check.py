#!/usr/bin/env python3
"""Refuse an unparseable CMakeLists.txt in milliseconds, not a gate cycle.

WHY THIS EXISTS. Twice in one day the same construct broke the same way, and
both times CI was what found it -- after downloading an SDK and starting a
configure, tens of minutes in:

    CMake Error at CMakeLists.txt:1276:
      Parse error.  Function missing ending ")".  End of file reached.

Both were conflict resolutions. This repository's CMakeLists conflicts cluster
on MULTI-LINE `set_tests_properties(...)` calls inside a `foreach`, because the
lanes that collide all append test registrations to the same block -- so the
conflict boundary lands INSIDE a call, between its opening line and its
closing `)`. Resolving such a hunk mechanically (keeping both sides, or
stripping the markers) splits the construct: the call never closes, the
`endforeach()` is swallowed, and the result is a file that still LOOKS like
CMake. Nothing in a diff review flags it, and a warm build directory does not
re-parse the file, so "it configures locally" can be true of a tree that
cannot configure at all.

WHAT IT CHECKS, and deliberately only this:

  1. Every command invocation closes. Reports the command and the line it was
     opened on, which is the information the CMake error withholds -- CMake
     reports where it gave up (end of file), not where the call began.
  2. Every block command is matched: if/endif, foreach/endforeach,
     while/endwhile, function/endfunction, macro/endmacro.

It is NOT a CMake parser and does not try to be. It does not evaluate
variables, validate command names, or check arguments -- cmake itself does all
of that, correctly, during the configure this check runs before. The one job
here is to make the class of failure that keeps costing gate cycles fail in
milliseconds instead.

Usage:
    python3 tools/ci/cmake_parse_check.py [paths...]      # default: repo CMakeLists.txt
    python3 tools/ci/cmake_parse_check.py --plant unclosed-call
    python3 tools/ci/cmake_parse_check.py --plant dropped-endforeach

Exit codes: 0 every file balances, 1 a finding, 2 no verdict (a named file is
missing, or the sweep had nothing to read -- never folded into green).
"""

import argparse
import os
import sys

# tools/ci/<this file> -> tools -> repo root.
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

OPENERS = {"if": "endif", "foreach": "endforeach", "while": "endwhile",
           "function": "endfunction", "macro": "endmacro"}
CLOSERS = {v: k for k, v in OPENERS.items()}
# Mid-block commands: they belong to an open block but neither open nor close.
MIDDLES = {"else", "elseif"}


def scan(text):
    """(findings, commands) for one CMake source.

    A hand-rolled scanner rather than a regex, because every one of comments,
    quoted arguments, bracket arguments and escapes can contain a parenthesis,
    and a regex that ignores them reports failures on healthy files -- which
    is worse than no check, since the first false positive is what gets a
    check disabled.
    """
    findings = []
    stack = []          # open command invocations: (name, line)
    blocks = []         # open block commands: (name, line)
    commands = 0
    i, line = 0, 1
    n = len(text)
    word_start = None

    while i < n:
        c = text[i]

        # Bracket comment / bracket argument: #[=*[ ... ]=*] and [=*[ ... ]=*]
        if c == "[" or (c == "#" and text.startswith("#[", i)):
            at = i + 1 if c == "#" else i
            j = at + 1
            eqs = 0
            while j < n and text[j] == "=":
                eqs += 1
                j += 1
            if j < n and text[j] == "[":
                close = "]" + "=" * eqs + "]"
                end = text.find(close, j + 1)
                if end < 0:
                    findings.append("unterminated bracket %s opened at line %d"
                                    % ("comment" if c == "#" else "argument", line))
                    return findings, commands
                line += text.count("\n", i, end + len(close))
                i = end + len(close)
                word_start = None
                continue

        # Line comment.
        if c == "#":
            end = text.find("\n", i)
            i = n if end < 0 else end
            word_start = None
            continue

        # Quoted argument. Spans lines; backslash escapes the next character.
        if c == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    break
                j += 1
            if j >= n:
                findings.append("unterminated quoted argument opened at line %d"
                                % line)
                return findings, commands
            line += text.count("\n", i, j)
            i = j + 1
            word_start = None
            continue

        if c == "\\":
            i += 2
            continue

        if c == "(":
            name = (text[word_start:i].strip().lower()
                    if word_start is not None else "")
            stack.append((name, line))
            if not stack[:-1]:      # a top-level invocation, not a nested paren
                commands += 1
            word_start = None
            i += 1
            continue

        if c == ")":
            if not stack:
                findings.append("line %d: a ')' closes nothing" % line)
                return findings, commands
            name, opened = stack.pop()
            if not stack:
                # The invocation is complete; adjudicate block nesting.
                if name in OPENERS:
                    blocks.append((name, opened))
                elif name in CLOSERS:
                    want = CLOSERS[name]
                    if not blocks:
                        findings.append("line %d: `%s()` closes a `%s()` that "
                                        "was never opened" % (opened, name, want))
                    elif blocks[-1][0] != want:
                        got, at = blocks.pop()
                        findings.append("line %d: `%s()` closes a `%s()` opened "
                                        "at line %d" % (opened, name, got, at))
                    else:
                        blocks.pop()
                elif name in MIDDLES and not blocks:
                    findings.append("line %d: `%s()` outside any block"
                                    % (opened, name))
            word_start = None
            i += 1
            continue

        if c == "\n":
            line += 1
            word_start = None
            i += 1
            continue

        if c.isspace():
            word_start = None
            i += 1
            continue

        if word_start is None:
            word_start = i
        i += 1

    # THE ONE THIS EXISTS FOR. CMake reports end-of-file; this reports the line
    # the call was opened on, which is what a person needs to fix it.
    for name, opened in stack:
        findings.append("line %d: `%s(` is never closed -- the parenthesis "
                        "opened here reaches end of file"
                        % (opened, name or "<anonymous>"))
    for name, opened in blocks:
        findings.append("line %d: `%s()` has no matching `end%s()`"
                        % (opened, name, name))
    return findings, commands


PLANTS = {
    # The exact shape both real failures took: a conflict boundary landing
    # between a multi-line call's opening line and its closing `)`, taking the
    # `endforeach()` with it.
    "unclosed-call": lambda s: s.replace(
        "        set_tests_properties(Spectr-materialized-unmute-pulse-control-${_pulse_plant}\n"
        "            PROPERTIES TIMEOUT 60)\n"
        "    endforeach()\n",
        "        set_tests_properties(Spectr-materialized-unmute-pulse-control-${_pulse_plant}\n",
        1),
    # A block left open on its own, with every call balanced.
    "dropped-endforeach": lambda s: s.replace(
        "            PROPERTIES TIMEOUT 60)\n    endforeach()\n",
        "            PROPERTIES TIMEOUT 60)\n", 1),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--plant", choices=sorted(PLANTS))
    args = ap.parse_args()

    paths = args.paths or [os.path.join(REPO, "CMakeLists.txt")]
    rc = 0
    checked = 0
    for path in paths:
        if not os.path.isfile(path):
            print("no verdict: %s is not a file" % path, file=sys.stderr)
            return 2
        text = open(path, encoding="utf-8").read()
        if args.plant:
            planted = PLANTS[args.plant](text)
            if planted == text:
                print("no verdict: --plant %s changed nothing in %s, so it "
                      "proves nothing" % (args.plant, path), file=sys.stderr)
                return 2
            text = planted
        findings, commands = scan(text)
        checked += 1
        # A sweep that parsed nothing is not a clean sweep.
        if commands == 0:
            print("no verdict: %s holds no command invocation, so this check "
                  "measured nothing" % path, file=sys.stderr)
            return 2
        if findings:
            rc = 1
            print("FAIL %s" % path, file=sys.stderr)
            for f in findings:
                print("  %s" % f, file=sys.stderr)
        else:
            print("ok   %s (%d command invocations balance)" % (path, commands))
    if not checked:
        print("no verdict: no file was checked", file=sys.stderr)
        return 2
    return rc


if __name__ == "__main__":
    sys.exit(main())
