#!/usr/bin/env python3
"""Fail early when Android SDL Java glue and native SDL disagree.

The crash fixed by this check happened in JNI_OnLoad: native SDL 3.4.4 was
statically linked into libmain.so while the APK shipped SDL 3.4.16 Java glue.
RegisterNatives looked for onNativePadDown(II)Z but Java declared (III)Z, and
Android aborted before main() ran.

This script is intentionally dependency-free so it can run both at CMake
configure time and after APK assembly.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import zipfile

EXPECTED_TABLES = {
    "SDLActivity",
    "SDLInputConnection",
    "SDLAudioManager",
    "SDLControllerManager",
}

PRIMITIVES = {
    "void": "V",
    "boolean": "Z",
    "byte": "B",
    "char": "C",
    "short": "S",
    "int": "I",
    "long": "J",
    "float": "F",
    "double": "D",
}

JAVA_NAME = r"[A-Za-z_$][A-Za-z0-9_$]*"
JAVA_TYPE = r"[A-Za-z_$][A-Za-z0-9_$.]*(?:\s*\[\s*\])*"
NATIVE_DECL = re.compile(
    r"\bnative\s+(?P<ret>" + JAVA_TYPE + r")\s+(?P<name>" + JAVA_NAME + r")"
    r"\s*\((?P<args>[^)]*)\)\s*;",
    re.S,
)


class VerificationError(RuntimeError):
    pass


def strip_java_comments(text: str) -> str:
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)


def type_descriptor(java_type: str) -> str:
    java_type = re.sub(r"\s+", "", java_type)
    array_depth = java_type.count("[]")
    base = java_type.replace("[]", "")
    if base in PRIMITIVES:
        code = PRIMITIVES[base]
    else:
        if "." not in base:
            # The SDL glue uses these short names; keep the mapping explicit.
            base = {
                "String": "java.lang.String",
                "Object": "java.lang.Object",
                "Class": "java.lang.Class",
            }.get(base, base)
        code = "L" + base.replace(".", "/") + ";"
    return "[" * array_depth + code


def method_descriptor(return_type: str, args: str) -> str:
    arg_codes = []
    args = args.strip()
    if args:
        for arg in args.split(","):
            tokens = arg.strip().split()
            if len(tokens) < 2:
                raise VerificationError(f"cannot parse Java native argument: {arg!r}")
            # Ignore parameter modifiers such as final; the last token is the name.
            arg_type = " ".join(t for t in tokens[:-1] if t != "final")
            arg_codes.append(type_descriptor(arg_type))
    return "(" + "".join(arg_codes) + ")" + type_descriptor(return_type)


def java_native_methods(path: Path) -> dict[str, str]:
    text = strip_java_comments(path.read_text(encoding="utf-8"))
    methods: dict[str, str] = {}
    for match in NATIVE_DECL.finditer(text):
        name = match.group("name")
        descriptor = method_descriptor(match.group("ret"), match.group("args"))
        if name in methods and methods[name] != descriptor:
            raise VerificationError(f"{path}: overloaded native method {name} is not supported by this check")
        methods[name] = descriptor
    return methods


def parse_native_tables(c_text: str) -> dict[str, dict[str, str]]:
    tables: dict[str, dict[str, str]] = {}
    pattern = re.compile(r"JNINativeMethod\s+(\w+)_tab\s*\[\s*\]\s*=\s*\{(.*?)\n\s*\};", re.S)
    entry_pattern = re.compile(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,')
    for table in pattern.finditer(c_text):
        class_name = table.group(1)
        entries = dict(entry_pattern.findall(table.group(2)))
        if not entries:
            raise VerificationError(f"empty or unparsable JNI table for {class_name}")
        tables[class_name] = entries
    return tables


def read_sdl_version(sdl_source: Path) -> tuple[int, int, int]:
    header = sdl_source / "include" / "SDL3" / "SDL_version.h"
    text = header.read_text(encoding="utf-8")
    values = []
    for part in ("MAJOR", "MINOR", "MICRO"):
        match = re.search(rf"^#define\s+SDL_{part}_VERSION\s+(\d+)", text, re.M)
        if not match:
            raise VerificationError(f"{header}: missing SDL_{part}_VERSION")
        values.append(int(match.group(1)))
    return tuple(values)  # type: ignore[return-value]


def read_java_version(java_dir: Path) -> tuple[int, int, int]:
    text = strip_java_comments((java_dir / "SDLActivity.java").read_text(encoding="utf-8"))
    values = []
    for part in ("MAJOR", "MINOR", "MICRO"):
        match = re.search(rf"\bSDL_{part}_VERSION\s*=\s*(\d+)\s*;", text)
        if not match:
            raise VerificationError(f"SDLActivity.java: missing SDL_{part}_VERSION")
        values.append(int(match.group(1)))
    return tuple(values)  # type: ignore[return-value]


def check_app_libraries(app_java: Path) -> None:
    text = strip_java_comments(app_java.read_text(encoding="utf-8"))
    match = re.search(r"getLibraries\s*\([^)]*\)\s*\{.*?return\s+new\s+String\s*\[\s*\]\s*\{(.*?)\}\s*;", text, re.S)
    if not match:
        raise VerificationError(f"{app_java}: cannot find getLibraries()")
    libraries = re.findall(r'"([^"]+)"', match.group(1))
    if "SDL3" in libraries:
        raise VerificationError(
            f"{app_java}: do not load a separate libSDL3.so; SDL is statically linked into libmain.so"
        )
    if "main" not in libraries:
        raise VerificationError(f"{app_java}: getLibraries() must load main")


def verify_source(sdl_source: Path, java_dir: Path, app_java: Path | None) -> int:
    android_c = sdl_source / "src" / "core" / "android" / "SDL_android.c"
    tables = parse_native_tables(android_c.read_text(encoding="utf-8"))
    missing = EXPECTED_TABLES - tables.keys()
    if missing:
        raise VerificationError(f"{android_c}: missing JNI tables: {', '.join(sorted(missing))}")

    native_version = read_sdl_version(sdl_source)
    java_version = read_java_version(java_dir)
    if native_version != java_version:
        raise VerificationError(
            f"SDL version mismatch: native {'.'.join(map(str, native_version))}, "
            f"Java {'.'.join(map(str, java_version))}"
        )

    checked = 0
    for class_name in sorted(EXPECTED_TABLES):
        java_path = java_dir / f"{class_name}.java"
        java_methods = java_native_methods(java_path)
        native_methods = tables[class_name]
        for name, native_descriptor in sorted(native_methods.items()):
            java_descriptor = java_methods.get(name)
            if java_descriptor is None:
                raise VerificationError(f"{java_path}: missing native method {name}{native_descriptor}")
            if java_descriptor != native_descriptor:
                raise VerificationError(
                    f"JNI signature mismatch for {class_name}.{name}: "
                    f"native {native_descriptor}, Java {java_descriptor}"
                )
            checked += 1
        extra = java_methods.keys() - native_methods.keys()
        if extra:
            raise VerificationError(f"{java_path}: native methods not registered by SDL_android.c: {', '.join(sorted(extra))}")

    if app_java is not None:
        check_app_libraries(app_java)
    print(f"SDL JNI contract OK: version {'.'.join(map(str, native_version))}, {checked} native methods")
    return checked


def verify_apk(apk: Path) -> None:
    with zipfile.ZipFile(apk) as zf:
        names = set(zf.namelist())
    required = {"classes.dex", "lib/arm64-v8a/libmain.so", "lib/arm64-v8a/libpng16.so"}
    missing = required - names
    if missing:
        raise VerificationError(f"{apk}: missing APK entries: {', '.join(sorted(missing))}")
    redundant = sorted(n for n in names if n.endswith("/libSDL3.so"))
    if redundant:
        raise VerificationError(
            f"{apk}: redundant shared SDL packaged ({', '.join(redundant)}); "
            "Android uses SDL statically linked into libmain.so"
        )
    print(f"APK SDL packaging OK: {apk}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdl-source", type=Path)
    parser.add_argument("--java-dir", type=Path)
    parser.add_argument("--app-java", type=Path)
    parser.add_argument("--apk", type=Path)
    args = parser.parse_args(argv)

    try:
        did_work = False
        if args.sdl_source is not None:
            if args.java_dir is None:
                raise VerificationError("--sdl-source requires --java-dir")
            verify_source(args.sdl_source, args.java_dir, args.app_java)
            did_work = True
        elif args.app_java is not None:
            check_app_libraries(args.app_java)
            did_work = True
        if args.apk is not None:
            verify_apk(args.apk)
            did_work = True
        if not did_work:
            raise VerificationError("nothing to verify; pass --sdl-source/--java-dir and/or --apk")
    except (VerificationError, OSError, zipfile.BadZipFile) as exc:
        print(f"Android SDL verification failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
