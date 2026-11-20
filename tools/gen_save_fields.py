#!/usr/bin/env python3
"""Generates src/game/savegame/tables/savefields.h: every field the tagged
savegame format stores, by name, for edict_t, gclient_t, level_locals_t and
game_locals_t.

It reads the struct definitions from the game headers, flattens nested
structs into dotted names ("monsterinfo.stand", "client... ") and sorts each
member into a kind:
  SF_RAW       plain data (numbers, vectors, arrays, enums), saved as bytes
  SF_STRING    char pointer, saved as text
  SF_EDICT     edict_t pointer, saved as entity number
  SF_CLIENT    gclient_t pointer, saved as client number
  SF_ITEM      gitem_t pointer, saved as item index
  SF_FUNCTION  function pointer, saved as the function's name
  SF_MMOVE     mmove_t pointer, saved as the move's name
Pointers of any other type stop the generator, so a new pointer field can
not be lost silently: give it a kind here or list it in SKIP.

Run it after changing a saved struct (the save check fails when the table is
out of date):
  python3 tools/gen_save_fields.py           write the table
  python3 tools/gen_save_fields.py --check   exit 1 if the table is out of date
"""

import argparse
import re
import sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent
HEADERS = [REPO_DIR / "src/common/header/shared.h", REPO_DIR / "src/game/header/game.h",
           REPO_DIR / "src/game/header/local.h"]
OUTPUT = REPO_DIR / "src/game/savegame/tables/savefields.h"

# saved structs: (C type, table name)
ROOTS = [("edict_t", "edict"), ("gclient_t", "client"), ("level_locals_t", "level"), ("game_locals_t", "game")]

# members not saved: the engine's own bookkeeping, rebuilt when entities are linked again
SKIP = {
    # client: set again for player slots on load; ai: bot state, set up again when a bot spawns
    "edict_t": {"area", "client", "ai"},
    # set by InitAllocations when the game is loaded
    "game_locals_t": {"clients", "maxclients", "maxentities"},
    # CTF multiplayer state (ghost slot, menu, grapple hook), not used in saved games
    "gclient_t": {"resp.ghost", "menu", "ctf_grapple"},
}

POINTER_KINDS = {
    "edict_t": "SF_EDICT", "struct edict_s": "SF_EDICT",
    "gclient_t": "SF_CLIENT", "struct gclient_s": "SF_CLIENT",
    "gitem_t": "SF_ITEM", "struct gitem_s": "SF_ITEM",
    "char": "SF_STRING",
    "mmove_t": "SF_MMOVE",
}


PROBLEMS = []  # unhandled pointer fields, reported together


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    return "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))


def parse_structs(text):
    """{type name: body} for typedef'd and tagged structs, plus typedef aliases and enum names."""
    structs, aliases, enums = {}, {}, set()
    for match in re.finditer(r"typedef\s+struct\s*(\w*)\s*\{([^{}]*)\}\s*(\w+)\s*;", text):
        tag, body, name = match.groups()
        structs[name] = body
        if tag:
            structs["struct " + tag] = body
    for match in re.finditer(r"(?<!typedef )struct\s+(\w+)\s*\{([^{}]*)\}\s*;", text):
        structs["struct " + match.group(1)] = match.group(2)
    for match in re.finditer(r"typedef\s+struct\s+(\w+)\s+(\w+)\s*;", text):
        aliases[match.group(2)] = "struct " + match.group(1)
    for match in re.finditer(r"typedef\s+enum\s*\w*\s*\{[^{}]*\}\s*(\w+)\s*;", text):
        enums.add(match.group(1))
    return structs, aliases, enums


def members(body):
    """[(type, name, pointer, is_array, is_function)] of a struct body."""
    result = []
    for declaration in body.split(";"):
        declaration = " ".join(declaration.replace("*", " * ").split())
        if not declaration:
            continue
        function = re.match(r"(.+?)\(\s*\*\s*(\w+)\s*\)\s*\(.*\)$", declaration)
        if function:
            result.append(("function", function.group(2), 0, False, True))
            continue
        base = re.match(r"((?:const\s+|unsigned\s+|signed\s+|struct\s+)*\w+)\s+(.*)$", declaration)
        if not base:
            raise SystemExit(f"cannot read member declaration: {declaration!r}")
        type_name = re.sub(r"\bconst\s+", "", base.group(1)).strip()
        for declarator in base.group(2).split(","):
            declarator = declarator.strip()
            pointer = declarator.count("*")
            name_match = re.match(r"[\s*]*(?:const\s+)?(\w+)\s*((?:\[[^\]]*\])*)$", declarator)
            if not name_match:
                raise SystemExit(f"cannot read declarator {declarator!r} in {declaration!r}")
            result.append((type_name, name_match.group(1), pointer, bool(name_match.group(2)), False))
    return result


def flatten(type_name, prefix, structs, aliases, enums, root, out):
    body = structs.get(aliases.get(type_name, type_name))
    if body is None:
        raise SystemExit(f"struct {type_name} not found")
    for member_type, name, pointer, is_array, is_function in members(body):
        path = f"{prefix}{name}"
        if path in SKIP.get(root, set()):
            continue
        if is_function:
            out.append((path, "SF_FUNCTION"))
            continue
        if pointer:
            kind = POINTER_KINDS.get(member_type)
            if kind is None or pointer > 1 or is_array:
                PROBLEMS.append(f"{root}.{path}: pointer to {member_type} has no save kind; "
                                f"add it to POINTER_KINDS or SKIP in {Path(__file__).name}")
                continue
            out.append((path, kind))
            continue
        resolved = aliases.get(member_type, member_type)
        if resolved in structs and not is_array:
            flatten(member_type, path + ".", structs, aliases, enums, root, out)
            continue
        if resolved in structs and is_array and has_pointers(resolved, structs, aliases):
            raise SystemExit(f"{root}.{path}: array of {member_type} holds pointers; not supported")
        out.append((path, "SF_RAW"))


def has_pointers(type_name, structs, aliases):
    for member_type, _name, pointer, _is_array, is_function in members(structs[aliases.get(type_name, type_name)]):
        resolved = aliases.get(member_type, member_type)
        if pointer or is_function or (resolved in structs and has_pointers(resolved, structs, aliases)):
            return True
    return False


def generate():
    text = strip_comments("\n".join(header.read_text() for header in HEADERS))
    structs, aliases, enums = parse_structs(text)
    lines = ["/*", " * optech2: fields of the tagged savegame format.",
             " * Generated by tools/gen_save_fields.py from the game headers; do not edit.",
             " * Run that script after changing a saved struct.", " */", ""]
    for type_name, table_name in ROOTS:
        fields = []
        flatten(type_name, "", structs, aliases, enums, type_name, fields)
        lines.append(f"static const savefield_t savefields_{table_name}[] = {{")
        for path, kind in fields:
            lines.append(f'\t{{"{path}", offsetof({type_name}, {path}), '
                         f"sizeof((({type_name} *)0)->{path}), {kind}}},")
        lines.append("};")
        lines.append("")
    if PROBLEMS:
        raise SystemExit("\n".join(PROBLEMS))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="only check that the table is up to date")
    arguments = parser.parse_args()
    table = generate()
    if arguments.check:
        current = OUTPUT.read_text() if OUTPUT.exists() else ""
        if current != table:
            print(f"{OUTPUT.relative_to(REPO_DIR)} is out of date: run python3 tools/gen_save_fields.py")
            return 1
        print("save field table is up to date")
        return 0
    OUTPUT.write_text(table)
    counts = re.findall(r"static const savefield_t (\w+)\[\] = \{(.*?)\};", table, re.DOTALL)
    for name, body in counts:
        print(f"{name}: {body.count('offsetof')} fields")
    return 0


if __name__ == "__main__":
    sys.exit(main())
