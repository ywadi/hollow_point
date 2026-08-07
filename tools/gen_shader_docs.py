#!/usr/bin/env python3
"""Generate the markdown reference for the *shader* contract (T0141.14).

The C++ side has `tools/gen_api_docs.py` over `engine/include/hp/`, and the CI
job that fails on a dirty tree afterwards. This is the same mechanism for the
surface a game's **shader** is written against, and it exists for the same
reason: the consumer is a coding agent, and a shader API it has to guess at is
one it will invent. Absence invites invention.

## What is public, and how that is decided

**The boundary is declared in the source, not inferred from a filename**, and
getting it wrong is the failure this tool is most able to cause: a reference
that documents `HpSurface.slang`'s internals would tell a game author to call
`HpTriplanarSample` or read `g_Material`, and D27 exists precisely to stop
DiligentFX's and the engine's internals becoming a public contract nobody can
then change.

There is no `include/` vs `src/` split to lean on -- both shader files sit in
`engine/shaders/` -- so the classification is a marker comment:

    // hp-shader-doc: public     the whole file is contract
    // hp-shader-doc: private    the whole file is implementation
    // hp-shader-doc: export     (in a private file) the next declaration is
                                 contract, and only that declaration

**Every `.slang` file under the shader directory must carry a file marker**, or
this refuses to run and names the file. That is deliberate, and it mirrors
`cmake/hp_embed_shaders.cmake` refusing a stray `.psh`: a rule that is checked
stays true, a rule that is remembered does not. The default direction is safe --
a new file is undocumented until somebody classifies it, never public by
accident.

**`export` exists because the contract genuinely spans two files.**
`HpMaterial.slang` is the vocabulary (what the engine hands you, what you fill
in) and is public in full. `interface IHpMaterial` is the *hooks* -- the thing a
game actually overrides (D28) -- and it cannot move into the contract file,
because its default implementations are the standard material and they call
DiligentFX's getters and the engine's own resources, which are declared where
they are. So the declaration is exported from the private file and nothing else
in it is. That is the honest description of the boundary rather than a tidier
one that would be false.

## Strict, with no baseline

`gen_api_docs.py` carries a ratchet baseline because it inherited 87 declarations
with 39 documented. This surface is small and completely documented today, so
missing documentation is an error from the first commit and there is no list of
exemptions to erode. If that ever becomes untenable, add the baseline
deliberately -- do not weaken the check to make a failure go away.

## Parsing

Hand-rolled, over a brace-depth scan. Slang has no libclang, and the input is
two files this repository owns. The mitigation for a hand-rolled parser is that
it **fails loudly rather than emitting nothing**: an unparsable declaration
inside a public region is an error, so the failure mode is a red build rather
than a silently shrinking reference.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

MARKER = re.compile(r"^\s*//\s*hp-shader-doc:\s*(public|private|export)\s*$")
DOC_LINE = re.compile(r"^\s*///\s?(.*)$")
COMMENT_LINE = re.compile(r"^\s*//\s?(.*)$")
SEPARATOR = re.compile(r"^\s*//\s*-{3,}\s*$")
PREPROC_COND = re.compile(r"^\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b")
# A line that is nothing but an attribute -- `[__AttributeUsage(...)]` above a
# user-defined attribute's declaration (T0160.1). Stepped over when looking for
# a declaration's prose, for the same reason `#if` is: the comment belongs to
# the declaration, and demanding it sit between the attribute and the struct
# would mean writing the source to suit the generator.
ATTRIBUTE_LINE = re.compile(r"^\s*\[[^\]]*\]\s*$")
TYPE_DECL = re.compile(r"^\s*(struct|interface)\s+([A-Za-z_]\w*)\s*(?::[^{]*)?\s*(\{)?\s*$")
# A free function's signature at column zero -- `HpLight HpGetLight(int index, ...)`.
# Contract since T0145: the lighting stage's primitives are free functions a game
# calls (`HpStandardLight`, `HpResolveLighting`), and they cannot live in the
# public contract file because they read `g_Frame`, which is declared after it.
# So they are exported from the private file exactly as `IHpMaterial` is.
FUNCTION_DECL = re.compile(r"^([A-Za-z_][\w:<>, ]*?)\s+([A-Za-z_]\w*)\s*\(")
DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\s*(.*?)\s*$")
FIELD = re.compile(r"^\s*([A-Za-z_][\w:<>, ]*?)\s+([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*;\s*$")


def strip_markers(lines: list[str]) -> list[str]:
    return [line for line in lines if not MARKER.match(line)]


HEADING = re.compile(r"^(#{1,6})(\s)")


def demote(prose: str) -> str:
    """Push reproduced markdown headings below the ones this page generates.

    The source comments are written as documents in their own right -- the
    contract file opens with `## The authoring shape` -- and reproducing them
    verbatim put a `##` heading in the middle of a page whose declarations are
    also `##`, so the contract's own prose read as a sibling of the structs it
    describes. Demoting by two keeps every reproduced heading strictly inside
    the section that quotes it, and never collides with a member's `###`.

    Fenced code is left alone: a `#` at the start of a line inside a fence is a
    preprocessor directive, and `#define` is exactly what this page documents.
    """
    out: list[str] = []
    fenced = False
    for line in prose.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
        elif not fenced:
            match = HEADING.match(line)
            if match:
                line = "#" * min(6, len(match.group(1)) + 2) + match.group(2) + line[match.end():]
        out.append(line)
    return "\n".join(out)


def clean_comment(lines: list[str]) -> str:
    """Prose out of a run of `///` or `//` lines, markers and rules removed."""
    out: list[str] = []
    for line in lines:
        if MARKER.match(line) or SEPARATOR.match(line):
            continue
        match = DOC_LINE.match(line) or COMMENT_LINE.match(line)
        out.append(match.group(1).rstrip() if match else "")
    while out and not out[0]:
        out.pop(0)
    while out and not out[-1]:
        out.pop()
    return "\n".join(out)


def doc_above(lines: list[str], index: int) -> str:
    """The comment run immediately above `lines[index]`.

    `///` is preferred and `//` is accepted, because this project's shader
    prose is written as ordinary block comments above a declaration as often as
    it is written as doc comments, and the prose is the point -- dropping it
    because of the slash count would throw away the explanation an agent needs.

    Blank lines and preprocessor conditionals are stepped over: `HP_UNSHADED`
    is documented above its `#ifndef` guard, not between the guard and the
    `#define`, and demanding otherwise would mean rewriting the source to suit
    the generator.
    """
    i = index - 1
    while i >= 0 and (
        not lines[i].strip() or PREPROC_COND.match(lines[i]) or ATTRIBUTE_LINE.match(lines[i])
    ):
        i -= 1
    end = i + 1
    while i >= 0 and (DOC_LINE.match(lines[i]) or COMMENT_LINE.match(lines[i])):
        i -= 1
    return clean_comment(lines[i + 1:end])


def file_preamble(lines: list[str]) -> str:
    """The leading comment block, which is the page's own explanation."""
    end = 0
    while end < len(lines) and (
        COMMENT_LINE.match(lines[end]) or DOC_LINE.match(lines[end]) or not lines[end].strip()
    ):
        end += 1
    return clean_comment(lines[:end])


def split_members(body: list[str], start_line: int, where: str) -> list[dict]:
    """Fields and methods of a struct or interface body, at depth 1 only.

    Nested bodies -- a method's implementation -- are skipped wholesale, which
    is what keeps a default implementation's internals out of the reference
    while its signature stays in.
    """
    members: list[dict] = []
    i = 0
    while i < len(body):
        line = body[i]
        stripped = line.strip()
        if (
            not stripped
            or DOC_LINE.match(line)
            or COMMENT_LINE.match(line)
            or stripped.startswith("#")
        ):
            i += 1
            continue

        # A signature may wrap across lines; accumulate until its parentheses
        # balance.
        chunk = [line]
        j = i
        while "(" in "".join(chunk) and "".join(chunk).count("(") > "".join(chunk).count(")"):
            j += 1
            if j >= len(body):
                raise SystemExit(
                    f"error: {where}:{start_line + i}: unterminated declaration -- "
                    f"gen_shader_docs.py cannot parse it, and refuses to silently omit it"
                )
            chunk.append(body[j])
        text = " ".join(part.strip() for part in chunk)

        if "(" in text:
            signature = text.split("{")[0].split(";")[0].strip()
            name = re.search(r"([A-Za-z_]\w*)\s*\(", signature)
            if not name:
                raise SystemExit(
                    f"error: {where}:{start_line + i}: cannot read a method name out of "
                    f"'{signature}'"
                )

            # **Whether a method has a default is looked for *after* the
            # signature, never inside it.** The first version tested `"{" in
            # text` and got every method wrong, because this project writes the
            # brace on its own line -- so the reference said "no default --
            # every material must implement this" about seven methods that all
            # have one, directly under prose beginning "Default:". That is the
            # confidently-wrong documentation `gen_api_docs.py`'s stale-param
            # check exists to prevent, arriving from a different direction.
            k = j
            rest = text[len(signature):].lstrip()
            while not rest:
                k += 1
                if k >= len(body):
                    raise SystemExit(
                        f"error: {where}:{start_line + i}: '{signature}' is followed by "
                        f"neither a body nor a ';'"
                    )
                candidate = body[k].strip()
                if not candidate or candidate.startswith("//") or candidate.startswith("#"):
                    continue
                rest = candidate
            defaulted = rest.startswith("{")
            if defaulted:
                depth = 0
                while k < len(body):
                    depth += body[k].count("{") - body[k].count("}")
                    if depth == 0:
                        break
                    k += 1
                if k >= len(body):
                    raise SystemExit(
                        f"error: {where}:{start_line + i}: unterminated body for '{signature}'"
                    )
            members.append({
                "kind": "method",
                "name": name.group(1),
                "signature": signature,
                "doc": doc_above(body, i),
                "line": start_line + i,
                # A body means a default a game inherits; no body means a
                # requirement it must supply. The distinction is the whole of
                # D28's "adding a method with a default is free".
                "defaulted": defaulted,
            })
            i = k + 1
            continue

        field = FIELD.match(line)
        if field:
            members.append({
                "kind": "field",
                "name": field.group(2),
                "signature": f"{field.group(1)} {field.group(2)}{field.group(3) or ''}",
                "doc": doc_above(body, i),
                "line": start_line + i,
                "defaulted": None,
            })
            i += 1
            continue

        i += 1
    return members


def collect(path: pathlib.Path, lines: list[str], public_file: bool) -> list[dict]:
    """Documented declarations in one file."""
    entities: list[dict] = []
    i = 0
    exported_next = False
    while i < len(lines):
        line = lines[i]
        marker = MARKER.match(line)
        if marker:
            if marker.group(1) == "export":
                if public_file:
                    raise SystemExit(
                        f"error: {path}:{i + 1}: 'hp-shader-doc: export' in a file already "
                        f"marked public. A decorative marker is one nobody trusts -- remove it"
                    )
                exported_next = True
            i += 1
            continue

        wanted = public_file or exported_next

        decl = TYPE_DECL.match(line)
        if decl:
            if not wanted:
                i += 1
                continue
            kind, name = decl.group(1), decl.group(2)
            start = i
            # Find the opening brace, then the matching close.
            j = i
            while j < len(lines) and "{" not in lines[j]:
                j += 1
            depth = 0
            body_start = j + 1
            k = j
            while k < len(lines):
                depth += lines[k].count("{") - lines[k].count("}")
                if depth == 0:
                    break
                k += 1
            if k >= len(lines):
                raise SystemExit(f"error: {path}:{start + 1}: unterminated {kind} {name}")
            entities.append({
                "kind": kind,
                "name": name,
                "doc": doc_above(lines, start),
                "line": start + 1,
                "members": split_members(lines[body_start:k], body_start + 1, str(path)),
            })
            exported_next = False
            i = k + 1
            continue

        define = DEFINE.match(line)
        if define and not define.group(1).startswith("_"):
            # A leading underscore is an include guard by this project's
            # convention (`_HP_MATERIAL_SLANG_`), and a guard is not API.
            if wanted:
                entities.append({
                    "kind": "macro",
                    "name": define.group(1),
                    "doc": doc_above(lines, i),
                    "line": i + 1,
                    "signature": f"#define {define.group(1)} {define.group(2)}".rstrip(),
                    "members": [],
                })
                exported_next = False
            i += 1
            continue

        # An exported free function (T0145). Only ever reached behind an
        # explicit `export` marker -- `wanted` is checked before the match is
        # even attempted, so a private helper in a private file is never
        # documented by accident, which is the failure mode this generator is
        # most able to cause.
        if wanted and exported_next:
            function = FUNCTION_DECL.match(line)
            if function:
                signature_lines = [line]
                k = i
                joined = line
                while joined.count("(") > joined.count(")"):
                    k += 1
                    if k >= len(lines):
                        raise SystemExit(
                            f"error: {path}:{i + 1}: unterminated signature for "
                            f"'{function.group(2)}'"
                        )
                    signature_lines.append(lines[k])
                    joined = " ".join(part.strip() for part in signature_lines)
                signature = joined.split("{")[0].split(";")[0].strip()
                entities.append({
                    "kind": "function",
                    "name": function.group(2),
                    "doc": doc_above(lines, i),
                    "line": i + 1,
                    "signature": signature,
                    "members": [],
                })
                exported_next = False
                # Step over the body so nothing inside it is parsed as a
                # declaration of its own.
                depth = 0
                opened = False
                while k < len(lines):
                    depth += lines[k].count("{") - lines[k].count("}")
                    if "{" in lines[k]:
                        opened = True
                    if opened and depth == 0:
                        break
                    k += 1
                i = k + 1
                continue

        # A top-level global declaration -- the sampler palette and the
        # deprecated texture slots (T0161). These are contract exactly as a
        # struct is: an author names `HpSamplerLinearWrap` in code, and a
        # reference that omits it documents a vocabulary nobody can use.
        # Struct members never reach this branch; a struct's whole body is
        # skipped by the TYPE_DECL arm above.
        field = FIELD.match(line)
        if field and wanted:
            entities.append({
                "kind": "global",
                "name": field.group(2),
                "doc": doc_above(lines, i),
                "line": i + 1,
                "signature": f"{field.group(1)} {field.group(2)}{field.group(3) or ''};",
                "members": [],
            })
            exported_next = False
            i += 1
            continue

        if exported_next and line.strip() and not line.strip().startswith("//"):
            raise SystemExit(
                f"error: {path}:{i + 1}: 'hp-shader-doc: export' is followed by something "
                f"this generator does not document ('{line.strip()[:60]}')"
            )
        i += 1
    return entities


def defects(source: str, entities: list[dict]) -> list[str]:
    """Undocumented public declarations, which are errors rather than gaps."""
    problems: list[str] = []
    for entity in entities:
        where = f"{source} {entity['name']}"
        if not entity["doc"]:
            problems.append(f"{where}: missing-doc")
        for member in entity.get("members", []):
            if not member["doc"]:
                problems.append(f"{where}::{member['name']}: missing-doc")
    return problems


RULES = """## Rules a game's shader may not violate

These come from **D27** and **D28** (`claude_documentation/documentation/02-decision-log.md`)
and they are constraints, not style. Each one has already cost somebody a
compile error or a silent breakage.

- **This is the only engine contract a shader may use.** A game shader must not
  `#include` a DiligentFX header, or call anything not listed on this page.
  Doing so makes DiligentFX's internals part of this engine's public contract,
  so an upstream rename silently breaks every shipped game. *We* include them;
  they include us.
- **A game writes a `struct HpMaterial : IHpMaterial`**, overriding only the
  methods it wants. `override` is **mandatory** -- omitting it is a compile
  error, which is what stops engine behaviour being replaced by accident.
- **A game cannot define a free `HpSurface` function.** D27 originally described
  that shape and it has not worked since T0142.15 put a game's module inside the
  engine's translation unit: a second body is
  `error[E30201]: function 'HpSurface' already has a body`, measured. The
  whole-output hook is `IHpMaterial.surface()`.
- **The module includes nothing.** The engine includes *it*, after this
  contract and after DiligentFX's getters, so an override may call them
  without naming a header.
- **Every method has a default, and the defaults are the standard material.**
  A material that overrides nothing renders exactly what the engine renders.
- **Adding to this contract is free; removing from it breaks shipped games.**
  Nothing is exposed here before the system behind it exists -- a field that
  returns a placeholder is worse than no field, because a shader written
  against it silently does nothing.

Authoring guide: `claude_documentation/documentation/09-gameplay-authoring.md`.
Material asset format: `claude_documentation/documentation/11-material-format.md`."""


def render_unit(unit: dict) -> str:
    out: list[str] = []
    out.append(f"# `{unit['title']}`")
    out.append("")
    out.append(f"*Generated from `{unit['source']}` — do not edit.*")
    out.append("")
    if unit["note"]:
        out.append(unit["note"])
        out.append("")
    if unit["preamble"]:
        out.append(demote(unit["preamble"]))
        out.append("")

    entities = unit["entities"]
    members = sum(len(e.get("members", [])) for e in entities)
    out.append(f"{len(entities)} declaration(s), {members} member(s), all documented.")
    out.append("")

    for entity in entities:
        out.append(f"## `{entity['name']}`")
        out.append("")
        if entity["kind"] in ("macro", "global"):
            out.append(f"```hlsl\n{entity['signature']}\n```")
        elif entity["kind"] == "function":
            # The signature, which for a free function *is* the declaration --
            # `function HpStandardLight` would tell a reader nothing about what
            # to pass it.
            out.append(f"```hlsl\n{entity['signature']};\n```")
        else:
            out.append(f"```hlsl\n{entity['kind']} {entity['name']}\n```")
        out.append("")
        if entity["doc"]:
            out.append(demote(entity["doc"]))
            out.append("")
        for member in entity.get("members", []):
            out.append(f"### `{entity['name']}::{member['name']}`")
            out.append("")
            suffix = ""
            if member["kind"] == "method":
                suffix = " { ... }" if member["defaulted"] else ";"
            else:
                suffix = ";"
            out.append(f"```hlsl\n{member['signature']}{suffix}\n```")
            out.append("")
            if member["kind"] == "method":
                out.append(
                    "*Has a default implementation — a material that does not override it "
                    "gets the standard material's behaviour.*"
                    if member["defaulted"]
                    else "*No default — every material must implement this.*"
                )
                out.append("")
            out.append(demote(member["doc"]))
            out.append("")
    return "\n".join(out).rstrip() + "\n"


def render_index(units: list[dict]) -> str:
    out: list[str] = []
    out.append("# HollowPoint shader contract")
    out.append("")
    out.append("*Generated by `zig build docs` from `engine/shaders/` — do not edit.*")
    out.append("")
    out.append(
        "This is the complete surface a game's material shader is written against. "
        "Shaders are authored in **Slang** (D28), compiled to SPIR-V, and a game's "
        "module arrives through the virtual filesystem as content (D13). Nothing "
        "outside this page is contract, however visible it is from inside a shader — "
        "see the rules below."
    )
    out.append("")
    out.append(RULES)
    out.append("")
    out.append("## Pages")
    out.append("")
    out.append("| Page | Source | Declarations |")
    out.append("|---|---|---|")
    for unit in units:
        out.append(
            f"| [`{unit['title']}`]({unit['page']}.md) | `{unit['source']}` | "
            f"{len(unit['entities'])} |"
        )
    out.append("")
    out.append("## Every symbol")
    out.append("")
    out.append("| Symbol | Kind | Page |")
    out.append("|---|---|---|")
    for unit in units:
        for entity in unit["entities"]:
            out.append(f"| `{entity['name']}` | {entity['kind']} | [`{unit['title']}`]({unit['page']}.md) |")
            for member in entity.get("members", []):
                out.append(
                    f"| `{entity['name']}::{member['name']}` | {member['kind']} | "
                    f"[`{unit['title']}`]({unit['page']}.md) |"
                )
    return "\n".join(out).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shaders", required=True, help="engine/shaders")
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--stamp", default="",
                        help="write a stamp file here after a successful run. For the build "
                             "graph, not for humans: zig's Run step caches a command only if "
                             "it believes it produces output, and the real output lands in "
                             "the source tree where zig cannot model it")
    args = parser.parse_args()

    shader_dir = pathlib.Path(args.shaders).resolve()
    out_dir = pathlib.Path(args.out).resolve()
    sources = sorted(shader_dir.glob("*.slang"))
    if not sources:
        print(f"error: no .slang files under {shader_dir}", file=sys.stderr)
        return 1

    units: list[dict] = []
    problems: list[str] = []
    for path in sources:
        lines = path.read_text().splitlines()
        markers = {m.group(1) for m in (MARKER.match(line) for line in lines) if m}
        classification = markers & {"public", "private"}
        if len(classification) != 1:
            print(
                f"error: {path} carries no 'hp-shader-doc: public' or "
                f"'hp-shader-doc: private' marker (found {sorted(markers) or 'none'}).\n"
                f"  Every shader source must say which side of the contract it is on, "
                f"because the alternative is a reference that documents the engine's\n"
                f"  implementation as though a game could rely on it (D27). A new file "
                f"is private until somebody decides otherwise, and deciding is the point.",
                file=sys.stderr,
            )
            return 1
        public_file = classification == {"public"}
        entities = collect(path, lines, public_file)
        rel = f"engine/shaders/{path.name}"
        problems += defects(path.name, entities)

        if public_file:
            units.append({
                "title": path.stem,
                "page": path.stem,
                "source": rel,
                "note": "",
                "preamble": file_preamble(strip_markers(lines)),
                "entities": entities,
            })
        elif entities:
            # An exported **type** out of an implementation file gets its own
            # page, and the page says so. Titling it after the file would be a
            # lie in both directions — the file is not contract, and the
            # declaration is.
            for entity in entities:
                if entity["kind"] == "function":
                    continue
                units.append({
                    "title": entity["name"],
                    "page": entity["name"],
                    "source": rel,
                    "note": (
                        f"**Exported from an implementation file.** `{rel}` is the engine's "
                        f"own shader and is *not* contract — nothing else in it may be relied "
                        f"on. `{entity['name']}` is marked `hp-shader-doc: export` because it "
                        f"is what a game implements, and it lives there because its default "
                        f"implementations *are* the standard material."
                    ),
                    "preamble": "",
                    "entities": [entity],
                })

            # Exported **functions** share one page, because they are one thing:
            # the engine helpers a shader may call. A page each would scatter
            # four lines of signature across four files and bury them in the
            # index — and they are read as a set, not one at a time.
            functions = [entity for entity in entities if entity["kind"] == "function"]
            if functions:
                units.append({
                    "title": "Engine functions",
                    "page": "engine-functions",
                    "source": rel,
                    "note": (
                        f"**Exported from an implementation file.** `{rel}` is the engine's "
                        f"own shader and is *not* contract — nothing else in it may be relied "
                        f"on. The functions below are marked `hp-shader-doc: export` because a "
                        f"game's module calls them: they are the engine's own defaults and "
                        f"primitives, and they live there because they read the frame's "
                        f"constant buffers, which are declared after the contract file is "
                        f"included."
                    ),
                    "preamble": "",
                    "entities": functions,
                })

    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        print(
            f"error: {len(problems)} undocumented declaration(s) in the shader contract.\n"
            "  This surface is small and fully documented; there is no baseline to add to.\n"
            "  Write the comment -- an agent that cannot read what a hook means invents one.",
            file=sys.stderr,
        )
        return 1

    if not units:
        print("error: no public shader declarations found -- the reference would be empty",
              file=sys.stderr)
        return 1

    units.sort(key=lambda u: u["title"])
    out_dir.mkdir(parents=True, exist_ok=True)
    for unit in units:
        (out_dir / f"{unit['page']}.md").write_text(render_unit(unit))
    (out_dir / "index.md").write_text(render_index(units))

    total = sum(len(u["entities"]) for u in units)
    members = sum(len(e.get("members", [])) for u in units for e in u["entities"])
    print(f"shader docs: {total} declaration(s), {members} member(s) across "
          f"{len(units)} page(s) -> {out_dir}")

    if args.stamp:
        stamp = pathlib.Path(args.stamp)
        stamp.parent.mkdir(parents=True, exist_ok=True)
        stamp.write_text(f"{total} declarations, {members} members\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
