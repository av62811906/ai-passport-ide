#!/usr/bin/env python3
"""Host tests for the IDE's source model (no Tk, no LVGL, no display).

Covers the pure logic behind the explorer and the code editor: which files are
listed, which paths may be written, newline preservation, and the C tokenizer
that drives syntax highlighting. The rendering itself still needs a real
window; this keeps the decisions deterministic.
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ide_sources", ROOT / "tools" / "simulator" / "ide_sources.py"
)
assert SPEC and SPEC.loader
SOURCES = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SOURCES
SPEC.loader.exec_module(SOURCES)


class ListSourcesTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="ai-passport-sources-")
        self.addCleanup(temporary.cleanup)
        self.main = Path(temporary.name).resolve() / "main"
        self.main.mkdir()

    def write(self, name: str, content: str = "") -> Path:
        path = self.main / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def names(self) -> list[str]:
        return [path.name for path in SOURCES.list_sources(self.main)]

    def test_lists_only_sources_sorted_by_name(self) -> None:
        self.write("b.h")
        self.write("a.c")
        self.write("CMakeLists.txt")
        self.write("README.md")
        self.write("notes.txt")
        self.assertEqual(self.names(), ["a.c", "b.h", "CMakeLists.txt"])

    def test_does_not_recurse_into_subdirectories(self) -> None:
        self.write("nested/deep.c")
        self.write("top.c")
        self.assertEqual(self.names(), ["top.c"])

    def test_missing_or_empty_directory_yields_nothing(self) -> None:
        self.assertEqual(SOURCES.list_sources(self.main), [])
        self.assertEqual(SOURCES.list_sources(self.main / "does-not-exist"), [])


class IsWithinTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="ai-passport-within-")
        self.addCleanup(temporary.cleanup)
        base = Path(temporary.name).resolve()
        self.main = base / "main"
        self.main.mkdir()
        self.inside = self.main / "app.c"
        self.inside.write_text("int main(void) {}\n", encoding="utf-8")
        self.outside = base / "outside"
        self.outside.mkdir()
        self.secret = self.outside / "secret.c"
        self.secret.write_text("x\n", encoding="utf-8")

    def symlink(self, link: Path, target: Path) -> None:
        try:
            link.symlink_to(target)
        except (NotImplementedError, OSError) as error:
            if isinstance(error, NotImplementedError) or getattr(error, "winerror", None) == 1314:
                self.skipTest("symlinks are unavailable here")
            raise

    def test_direct_child_and_root_are_allowed(self) -> None:
        self.assertTrue(SOURCES.is_within(self.inside, self.main))
        self.assertTrue(SOURCES.is_within(self.main, self.main))

    def test_parent_escape_and_outside_paths_are_rejected(self) -> None:
        self.assertFalse(SOURCES.is_within(self.main / ".." / "outside" / "secret.c", self.main))
        self.assertFalse(SOURCES.is_within(self.secret, self.main))
        self.assertFalse(SOURCES.is_within(Path("/etc/hosts"), self.main))

    def test_symlink_pointing_outside_is_rejected(self) -> None:
        link = self.main / "link.c"
        self.symlink(link, self.secret)
        self.assertFalse(SOURCES.is_within(link, self.main))

    def test_unreadable_path_is_rejected(self) -> None:
        with mock.patch.object(Path, "resolve", side_effect=OSError("denied")):
            self.assertFalse(SOURCES.is_within(self.inside, self.main))


class NewlineStyleTest(unittest.TestCase):
    def test_detects_crlf_only_when_present(self) -> None:
        self.assertEqual(SOURCES.newline_style("a\r\nb\r\n"), "\r\n")
        self.assertEqual(SOURCES.newline_style("a\nb\n"), "\n")
        self.assertEqual(SOURCES.newline_style("no newline"), "\n")

    def test_normalize_collapses_crlf_and_cr(self) -> None:
        self.assertEqual(SOURCES.normalize_newlines("a\r\nb\rc\nd"), "a\nb\nc\nd")


class TokenizeCTest(unittest.TestCase):
    def kinds(self, text: str) -> list[str]:
        return [kind for _start, _end, kind in SOURCES.tokenize_c(text)]

    def span_text(self, text: str, kind: str) -> list[str]:
        return [text[start:end] for start, end, found in SOURCES.tokenize_c(text) if found == kind]

    def test_empty_text_has_no_spans(self) -> None:
        self.assertEqual(SOURCES.tokenize_c(""), [])

    def test_keywords_types_and_calls(self) -> None:
        text = "static void app_tuner_start(int flag) {"
        self.assertEqual(self.span_text(text, "kw"), ["static", "void", "int"])
        self.assertEqual(self.span_text(text, "fn"), ["app_tuner_start"])

    def test_line_comment_swallows_quotes_and_keywords(self) -> None:
        text = '// "not a string" int x\nint y;'
        self.assertEqual(self.span_text(text, "cmt"), ['// "not a string" int x'])
        self.assertEqual(self.span_text(text, "kw"), ["int"])
        self.assertEqual(self.span_text(text, "str"), [])

    def test_block_comment_spans_lines_and_swallows_content(self) -> None:
        text = "/* line one\n   int fake;\n*/\nint real;"
        self.assertEqual(self.span_text(text, "cmt"), ["/* line one\n   int fake;\n*/"])
        self.assertEqual(self.span_text(text, "kw"), ["int"])

    def test_unterminated_block_comment_runs_to_end_of_file(self) -> None:
        text = "int a;\n/* open\nint b;\n"
        self.assertEqual(self.span_text(text, "cmt"), ["/* open\nint b;\n"])
        self.assertEqual(self.span_text(text, "kw"), ["int"])

    def test_string_with_escape_is_one_span(self) -> None:
        text = r'const char *s = "a\"b\n";'
        self.assertEqual(self.span_text(text, "str"), [r'"a\"b\n"'])
        self.assertEqual(self.span_text(text, "kw"), ["const", "char"])

    def test_unterminated_string_stops_at_line_end(self) -> None:
        text = 'char *s = "abc\nint x;'
        self.assertEqual(self.span_text(text, "str"), ['"abc'])
        self.assertEqual(self.span_text(text, "kw"), ["char", "int"])

    def test_char_literal_does_not_become_a_string(self) -> None:
        text = "char c = '\\n';"
        self.assertEqual(self.span_text(text, "chr"), ["'\\n'"])
        self.assertEqual(self.span_text(text, "str"), [])

    def test_preprocessor_line_covers_indent_and_whole_line(self) -> None:
        text = "  #define TUNER_HOP 512\nint x;"
        self.assertEqual(self.span_text(text, "pp"), ["  #define TUNER_HOP 512"])
        self.assertEqual(self.span_text(text, "kw"), ["int"])

    def test_numbers_include_hex_float_and_suffix(self) -> None:
        text = "x = 0x1F + 3.14 + 42u;"
        self.assertEqual(self.span_text(text, "num"), ["0x1F", "3.14", "42u"])

    def test_division_is_not_a_comment(self) -> None:
        text = "a = b / c / d;"
        self.assertEqual(self.span_text(text, "cmt"), [])

    def test_spans_are_ordered_and_never_overlap(self) -> None:
        text = (
            '#include <stdio.h>\n'
            "/* block */\n"
            "static int add(int a, int b) {\n"
            '    const char *tag = "add";  // trailing\n'
            "    return a + b + 0x10;\n"
            "}\n"
        )
        spans = SOURCES.tokenize_c(text)
        self.assertTrue(spans)
        previous_end = -1
        for start, end, kind in spans:
            self.assertIn(kind, SOURCES.TOKEN_KINDS)
            self.assertLessEqual(previous_end, start)
            self.assertLess(start, end)
            previous_end = end

    def test_comment_and_string_contents_produce_no_inner_spans(self) -> None:
        spans = SOURCES.tokenize_c('// int x = "s";\n/* char c; */\n')
        self.assertEqual([kind for _s, _e, kind in spans], ["cmt", "cmt"])


if __name__ == "__main__":
    unittest.main()
