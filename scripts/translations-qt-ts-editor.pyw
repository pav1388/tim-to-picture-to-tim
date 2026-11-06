#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Qt Linguist TS helper editor.

The editor changes only <translation> elements (or removes an explicitly
requested <message>).  It does not re-serialize the XML document, so unrelated
formatting, comments, whitespace and declarations are preserved.

Requires Python 3.8+ and Tkinter.
"""

import codecs
import hashlib
import html
import os
import re
import stat
import sys
import tempfile
import tkinter as tk
from tkinter import ttk, filedialog, messagebox


# ---------------------------------------------------------------------------
# TS parsing / serialization.  Character offsets are collected from the
# original text and later applied from right to left.  This is deliberate:
# ElementTree would normalize the whole document when writing it back.
# ---------------------------------------------------------------------------

CTX_RE = re.compile(r"<context\b[^>]*>(.*?)</context\s*>", re.S)
MSG_RE = re.compile(r"<message\b[^>]*>.*?</message\s*>", re.S)
NAME_RE = re.compile(r"<name\b[^>]*>(.*?)</name\s*>", re.S)
SRC_RE = re.compile(r"<source\b[^>]*>(.*?)</source\s*>", re.S)
TR_RE = re.compile(
    r"<translation\b(?P<attrs>[^>]*?)(?P<self>/\s*>|>(?P<body>.*?)</translation\s*>)",
    re.S,
)
FORM_RE = re.compile(
    r"<(?P<tag>numerusform|lengthvariant)\b[^>]*>(?P<body>.*?)</(?P=tag)\s*>",
    re.S,
)
BYTE_RE = re.compile(
    r'<byte\b[^>]*\bvalue\s*=\s*["\']([^"\']+)["\'][^>]*/\s*>', re.I
)
TYPE_ATTR_RE = re.compile(r'(?<![\w:-])type\s*=\s*(["\'])(.*?)\1', re.S)
XML_DECL_ENCODING_RE = re.compile(
    br"<\?xml\s+[^?]*?encoding\s*=\s*[\"']([A-Za-z0-9._-]+)[\"']",
    re.I,
)


class TsFormatError(ValueError):
    pass


def mask_xml_comments(text):
    """Mask comments, CDATA and the DOCTYPE subset without changing offsets.

    This prevents literal tag-looking text inside comments/CDATA/entities from
    being mistaken for real <context>, <message> or <translation> elements.
    """
    chars = list(text)
    lower = text.lower()
    pos = 0

    def blank(start, end):
        for index in range(start, end):
            if chars[index] not in "\r\n":
                chars[index] = " "

    while pos < len(text):
        candidates = []
        for token in ("<!--", "<![CDATA["):
            found = text.find(token, pos)
            if found >= 0:
                candidates.append((found, token))
        doctype_pos = lower.find("<!doctype", pos)
        if doctype_pos >= 0:
            candidates.append((doctype_pos, "<!doctype"))
        if not candidates:
            break

        start, token = min(candidates, key=lambda item: item[0])
        if token == "<!--":
            close = text.find("-->", start + 4)
            if close < 0:
                raise TsFormatError("Незакрытый XML-комментарий.")
            end = close + 3
        elif token == "<![CDATA[":
            close = text.find("]]>", start + 9)
            if close < 0:
                raise TsFormatError("Незакрытая секция CDATA.")
            end = close + 3
        else:
            # DOCTYPE may contain an internal subset with nested declarations
            # and '>' characters inside quotes. Track quotes and [ ... ].
            index = start + len(token)
            bracket_depth = 0
            quote = None
            while index < len(text):
                ch = text[index]
                if quote:
                    if ch == quote:
                        quote = None
                elif ch in ("\"", "'"):
                    quote = ch
                elif ch == "[":
                    bracket_depth += 1
                elif ch == "]" and bracket_depth:
                    bracket_depth -= 1
                elif ch == ">" and bracket_depth == 0:
                    break
                index += 1
            if index >= len(text):
                raise TsFormatError("Незакрытое объявление DOCTYPE.")
            end = index + 1
        blank(start, end)
        pos = end
    return "".join(chars)


def decode_ts_fragment(raw):
    """Decode XML text/Qt byte markers while retaining literal CDATA content."""
    cdata_values = []

    def cdata_hold(match):
        token = "__QT_TS_CDATA_{}_TOKEN__".format(len(cdata_values))
        cdata_values.append(match.group(1))
        return token

    # CDATA is text, not markup: a literal string such as '<b>bold</b>' must
    # not be stripped as XML elements. Hold it until normal XML unescaping ends.
    value = re.sub(r"<!\[CDATA\[(.*?)\]\]>", cdata_hold, raw, flags=re.S)
    value = re.sub(r"<!--.*?-->", "", value, flags=re.S)

    def byte_repl(match):
        raw_value = match.group(1).strip()
        try:
            if raw_value.lower().startswith("x"):
                number = int(raw_value[1:], 16)
            else:
                number = int(raw_value, 0)
            if 0 <= number <= 0x10FFFF:
                return chr(number)
        except (ValueError, OverflowError):
            pass
        return match.group(0)

    value = BYTE_RE.sub(byte_repl, value)
    value = re.sub(r"<[^>]+>", "", value)
    value = html.unescape(value)
    for index, content in enumerate(cdata_values):
        value = value.replace("__QT_TS_CDATA_{}_TOKEN__".format(index), content)
    return value


def encode_ts_text(value):
    """Escape editor text as valid XML text, using Qt <byte/> for XML controls."""
    out = []
    for ch in value:
        code = ord(ch)
        if ch == "&":
            out.append("&amp;")
        elif ch == "<":
            out.append("&lt;")
        elif ch == ">":
            out.append("&gt;")
        elif ch == '"':
            out.append("&quot;")
        elif ch == "'":
            out.append("&apos;")
        elif (code < 0x20 and ch not in "\t\n\r") or code in (0xFFFE, 0xFFFF) or 0xD800 <= code <= 0xDFFF:
            if code <= 0xFF:
                out.append('<byte value="x{:x}"/>'.format(code))
            else:
                raise ValueError("Символ U+{:04X} нельзя записать в XML 1.0.".format(code))
        else:
            out.append(ch)
    return "".join(out)


def get_type(attrs):
    match = TYPE_ATTR_RE.search(attrs or "")
    return match.group(2) if match else ""


def set_type_attr(attrs, value):
    """Set/remove translation's type attribute while retaining other attrs."""
    attrs = attrs or ""
    match = TYPE_ATTR_RE.search(attrs)
    if value:
        replacement = 'type="{}"'.format(value.replace("&", "&amp;").replace('"', "&quot;"))
        if match:
            return attrs[:match.start()] + replacement + attrs[match.end():]
        return attrs.rstrip() + " " + replacement if attrs.strip() else " " + replacement
    if match:
        # Remove preceding whitespace with the attribute, if present.
        start = match.start()
        while start > 0 and attrs[start - 1].isspace():
            start -= 1
        return attrs[:start] + attrs[match.end():]
    return attrs


def _form_info(raw):
    raw = raw or ""
    searchable = mask_xml_comments(raw)
    matches = list(FORM_RE.finditer(searchable))
    if matches:
        tag = matches[0].group("tag")
        # Only treat a homogeneous form collection as editable forms.  If
        # malformed/mixed markup is encountered, keep it as one raw value.
        if all(m.group("tag") == tag for m in matches):
            return tag, [
                {
                    "start": m.start("body"),
                    "end": m.end("body"),
                    "raw": raw[m.start("body"):m.end("body")],
                    "text": decode_ts_fragment(raw[m.start("body"):m.end("body")]),
                }
                for m in matches
            ]
    return "", [{"start": 0, "end": len(raw), "raw": raw, "text": decode_ts_fragment(raw)}]


def parse_ts(text):
    masked = mask_xml_comments(text)
    result = []
    for context_match in CTX_RE.finditer(masked):
        context_body = context_match.group(1)
        context_base = context_match.start(1)
        name_match = NAME_RE.search(context_body)
        context_name = (
            decode_ts_fragment(text[context_base + name_match.start(1):context_base + name_match.end(1)])
            if name_match else ""
        )

        for message_match in MSG_RE.finditer(context_body):
            body_mask = message_match.group(0)
            body = text[context_base + message_match.start():context_base + message_match.end()]
            base = context_base + message_match.start()

            source_match = SRC_RE.search(body_mask)
            translation_match = TR_RE.search(body_mask)
            if not translation_match:
                # Messages without <translation> are left byte-for-byte alone.
                continue

            source = (
                decode_ts_fragment(body[source_match.start(1):source_match.end(1)])
                if source_match else ""
            )
            self_closing = translation_match.group("body") is None
            if self_closing:
                tr_raw = ""
            else:
                tr_raw = body[translation_match.start("body"):translation_match.end("body")]
            attrs = body[translation_match.start("attrs"):translation_match.end("attrs")]
            form_tag, forms_meta = _form_info(tr_raw)
            forms = [f["text"] for f in forms_meta]

            msg = {
                "ctx": context_name,
                "src": source,
                "forms": forms,
                "form_tag": form_tag,
                "form_meta": forms_meta,
                "tr_raw": tr_raw,
                "attrs": attrs,
                "type": get_type(attrs),
                "selfclose": self_closing,
                "ms": base,
                "me": base + len(body),
                "s": base + translation_match.start(),
                "e": base + translation_match.end(),
                "deleted": False,
                "modified": False,
            }
            msg["original"] = {
                "forms": list(forms),
                "tr_raw": tr_raw,
                "attrs": attrs,
                "type": msg["type"],
                "selfclose": self_closing,
            }
            result.append(msg)
    return result


def message_status(message):
    kind = message.get("type", "")
    if kind in ("obsolete", "vanished", "unfinished"):
        return kind
    return "finished" if any(v.strip() for v in message.get("forms", [])) else "unfinished"


def make_translation(message):
    attrs = message.get("attrs", "")
    if message.get("type"):
        attrs = set_type_attr(attrs, message["type"])
    else:
        attrs = set_type_attr(attrs, "")
    attrs = attrs or ""
    raw = message.get("tr_raw", "")
    if message.get("selfclose") and not raw:
        return "<translation{}/>".format(attrs)
    return "<translation{}>{}</translation>".format(attrs, raw)


def set_message_forms(message, values):
    """Commit editor values to a message model; preserve plural/variant tags."""
    values = list(values)
    if len(values) != len(message["forms"]):
        raise ValueError("Количество полей перевода изменилось неожиданным образом.")

    original = message["original"]
    if values == original["forms"]:
        message["forms"] = list(original["forms"])
        message["tr_raw"] = original["tr_raw"]
        message["attrs"] = original["attrs"]
        message["type"] = original["type"]
        message["selfclose"] = original["selfclose"]
        message["form_tag"], message["form_meta"] = _form_info(message["tr_raw"])
    else:
        raw = message["tr_raw"]
        if message["form_tag"]:
            for meta, value in reversed(list(zip(message["form_meta"], values))):
                encoded = encode_ts_text(value)
                raw = raw[:meta["start"]] + encoded + raw[meta["end"]:]
        else:
            raw = encode_ts_text(values[0])
        message["tr_raw"] = raw
        message["form_tag"], message["form_meta"] = _form_info(raw)
        message["forms"] = [f["text"] for f in message["form_meta"]]
        if any(v.strip() for v in values):
            message["type"] = ""
            message["attrs"] = set_type_attr(message["attrs"], "")
        else:
            message["type"] = "unfinished"
            message["attrs"] = set_type_attr(message["attrs"], "unfinished")
        message["selfclose"] = not bool(raw)

    message["modified"] = (
        message["tr_raw"] != original["tr_raw"]
        or message["attrs"] != original["attrs"]
        or message["type"] != original["type"]
        or message["selfclose"] != original["selfclose"]
    )


def mark_message_finished(message):
    if not any(v.strip() for v in message.get("forms", [])):
        raise ValueError("Нельзя пометить пустой перевод как finished.")
    message["type"] = ""
    message["attrs"] = set_type_attr(message.get("attrs", ""), "")
    message["modified"] = (
        message["tr_raw"] != message["original"]["tr_raw"]
        or message["attrs"] != message["original"]["attrs"]
        or message["type"] != message["original"]["type"]
        or message["selfclose"] != message["original"]["selfclose"]
    )


def compose_document(original_text, messages):
    """Apply translation replacements/deletions to the original text."""
    operations = []
    for message in messages:
        if message["deleted"]:
            operations.append((message["ms"], message["me"], "delete"))
        elif message["modified"]:
            operations.append((message["s"], message["e"], make_translation(message)))

    operations.sort(key=lambda op: op[0], reverse=True)
    last_start = len(original_text) + 1
    text = original_text
    for start, end, replacement in operations:
        if start < 0 or end < start or end > len(original_text):
            raise TsFormatError("Внутренняя ошибка диапазона XML при сохранении.")
        if end > last_start:
            raise TsFormatError("Пересекающиеся XML-диапазоны; сохранение отменено.")
        text = text[:start] + ("" if replacement == "delete" else replacement) + text[end:]
        last_start = start
    return text


def decode_file(path):
    with open(path, "rb") as stream:
        data = stream.read()
    if data.startswith(codecs.BOM_UTF8):
        encoding = "utf-8-sig"
    # UTF-32 LE starts with the UTF-16 LE BOM bytes, so test it first.
    elif data.startswith(codecs.BOM_UTF32_LE) or data.startswith(codecs.BOM_UTF32_BE):
        encoding = "utf-32"
    elif data.startswith(codecs.BOM_UTF16_LE) or data.startswith(codecs.BOM_UTF16_BE):
        encoding = "utf-16"
    else:
        match = XML_DECL_ENCODING_RE.search(data[:512])
        encoding = match.group(1).decode("ascii") if match else "utf-8"
    try:
        codecs.lookup(encoding)
        return data.decode(encoding), encoding, hashlib.sha256(data).hexdigest()
    except (LookupError, UnicodeDecodeError) as exc:
        raise TsFormatError("Не удалось прочитать файл как {}: {}".format(encoding, exc))


def atomic_write(path, text, encoding):
    """Write via a temporary file and replace the destination atomically."""
    directory = os.path.dirname(os.path.abspath(path)) or os.getcwd()
    basename = os.path.basename(path)
    data = text.encode(encoding)
    old_mode = None
    if os.path.exists(path):
        old_mode = stat.S_IMODE(os.stat(path).st_mode)

    fd, temp_path = tempfile.mkstemp(prefix="." + basename + ".", suffix=".tmp", dir=directory)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if old_mode is not None:
            os.chmod(temp_path, old_mode)
        os.replace(temp_path, path)
    except Exception:
        try:
            os.unlink(temp_path)
        except OSError:
            pass
        raise
    return hashlib.sha256(data).hexdigest()


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------

LIGHT = {
    "bg": "#f0f0f0", "fg": "#1a1a1a", "field": "#ffffff", "field_fg": "#1a1a1a",
    "select": "#4a90d9", "select_fg": "#ffffff", "border": "#c0c0c0",
    "status_bg": "#e0e0e0", "status_fg": "#1a1a1a", "muted": "#666666",
}
DARK = {
    "bg": "#2b2b2b", "fg": "#e0e0e0", "field": "#1e1e1e", "field_fg": "#e0e0e0",
    "select": "#3a6ea5", "select_fg": "#ffffff", "border": "#555555",
    "status_bg": "#1e1e1e", "status_fg": "#b0b0b0", "muted": "#888888",
}


class App(tk.Tk):
    def __init__(self, path=None):
        super().__init__()
        self.title("Qt TS Editor")
        self.geometry("1240x780")
        self.minsize(820, 560)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        self.path = None
        self.text = ""              # exact document baseline for current session
        self.encoding = "utf-8"
        self.disk_hash = None
        self.msgs = []
        self.cur_index = None
        self.cur = None
        self.editor_widgets = []
        self.editor_notebook = None
        self.editor_pages = []
        self._loading_editor = False
        self._loading_tree = False
        self._last_filter = "all"
        self.dark = False
        self.find_last = ""
        self.find_pos = -1

        self.style = ttk.Style(self)
        try:
            self.style.theme_use("clam")
        except tk.TclError:
            pass

        self._build_ui()
        self._apply_theme()
        self._bind_shortcuts()
        self._update_dirty_ui()

        if path:
            self.open_file(path, ask_first=False)

    # ------------------------------ UI setup ------------------------------

    def _build_ui(self):
        self.status_var = tk.StringVar(value="Готово")
        self.status = ttk.Label(self, textvariable=self.status_var, relief=tk.SUNKEN,
                                anchor="w", style="Status.TLabel")
        self.status.pack(side=tk.BOTTOM, fill=tk.X)

        toolbar = ttk.Frame(self)
        toolbar.pack(side=tk.TOP, fill=tk.X, padx=8, pady=(8, 4))
        row1 = ttk.Frame(toolbar)
        row1.pack(fill=tk.X)
        ttk.Button(row1, text="Открыть…", command=self.ask_open).pack(side=tk.LEFT)
        ttk.Button(row1, text="Сохранить", command=self.save).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(row1, text="Сохранить как…", command=self.save_as).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Separator(row1, orient="vertical").pack(side=tk.LEFT, fill="y", padx=9)
        ttk.Label(row1, text="Фильтр:").pack(side=tk.LEFT)
        self.filter_var = tk.StringVar(value="all")
        self.filter_box = ttk.Combobox(
            row1, textvariable=self.filter_var, state="readonly", width=13,
            values=("all", "unfinished", "finished", "obsolete", "vanished")
        )
        self.filter_box.pack(side=tk.LEFT, padx=(5, 0))
        self.filter_box.bind("<<ComboboxSelected>>", self.on_filter_change)
        self.theme_button = ttk.Button(row1, text="🌙 Тёмная", command=self.toggle_theme)
        self.theme_button.pack(side=tk.RIGHT)

        row2 = ttk.Frame(toolbar)
        row2.pack(fill=tk.X, pady=(6, 0))
        ttk.Button(row2, text="Статистика", command=self.show_stats).pack(side=tk.LEFT)
        ttk.Separator(row2, orient="vertical").pack(side=tk.LEFT, fill="y", padx=9)
        ttk.Label(row2, text="Поиск:").pack(side=tk.LEFT)
        self.search_var = tk.StringVar()
        self.search_entry = ttk.Entry(row2, textvariable=self.search_var, width=32)
        self.search_entry.pack(side=tk.LEFT, padx=(5, 0))
        self.search_entry.bind("<Return>", lambda _e: self.find_next())
        ttk.Button(row2, text="Найти", command=self.find_next).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Label(row2, text="Ctrl+S — сохранить   Ctrl+Enter — применить", style="Muted.TLabel").pack(side=tk.RIGHT)

        main = ttk.Panedwindow(self, orient="horizontal")
        main.pack(fill="both", expand=True, padx=8, pady=(4, 8))

        left = ttk.Frame(main)
        main.add(left, weight=1)
        ttk.Label(left, text="Сообщения .ts").pack(anchor="w", pady=(0, 4))
        list_frame = ttk.Frame(left)
        list_frame.pack(fill="both", expand=True)
        list_frame.rowconfigure(0, weight=1)
        list_frame.columnconfigure(0, weight=1)
        self.tree = ttk.Treeview(list_frame, columns=("status", "context", "source"),
                                 show="headings", selectmode="browse")
        for key, width, heading in (
            ("status", 90, "Статус"), ("context", 150, "Контекст"), ("source", 360, "Оригинал")
        ):
            self.tree.heading(key, text=heading)
            self.tree.column(key, width=width, stretch=(key == "source"), anchor="w")
        self.tree.grid(row=0, column=0, sticky="nsew")
        tree_v = ttk.Scrollbar(list_frame, orient="vertical", command=self.tree.yview)
        tree_v.grid(row=0, column=1, sticky="ns")
        tree_h = ttk.Scrollbar(list_frame, orient="horizontal", command=self.tree.xview)
        tree_h.grid(row=1, column=0, sticky="ew")
        self.tree.configure(yscrollcommand=tree_v.set, xscrollcommand=tree_h.set)
        self.tree.bind("<<TreeviewSelect>>", self.on_pick)

        right = ttk.Frame(main)
        main.add(right, weight=2)
        self.info_context = ttk.Label(right, text="Контекст: —")
        self.info_context.pack(anchor="w", pady=(0, 2))
        self.info_status = ttk.Label(right, text="Статус: —")
        self.info_status.pack(anchor="w", pady=(0, 5))

        self.paned = ttk.Panedwindow(right, orient="vertical")
        self.paned.pack(fill="both", expand=True)

        upper = ttk.Frame(self.paned)
        self.paned.add(upper, weight=1)
        source_box = ttk.LabelFrame(upper, text="Оригинал")
        source_box.pack(fill="both", expand=True)
        source_header = ttk.Frame(source_box)
        source_header.pack(fill="x", padx=6, pady=(4, 0))
        ttk.Label(source_header, text="Исходная строка", style="Muted.TLabel").pack(side=tk.LEFT)
        ttk.Button(source_header, text="Копировать", command=self.copy_source).pack(side=tk.RIGHT)
        source_body = ttk.Frame(source_box)
        source_body.pack(fill="both", expand=True, padx=6, pady=5)
        source_body.rowconfigure(0, weight=1)
        source_body.columnconfigure(0, weight=1)
        self.source_text = tk.Text(source_body, height=5, wrap="word", state="disabled", undo=False)
        self.source_text.grid(row=0, column=0, sticky="nsew")
        source_scroll = ttk.Scrollbar(source_body, orient="vertical", command=self.source_text.yview)
        source_scroll.grid(row=0, column=1, sticky="ns")
        self.source_text.configure(yscrollcommand=source_scroll.set)
        self.source_text.bind("<Button-3>", lambda e: self._text_menu(self.source_text, e, False))
        self.source_text.bind("<Button-2>", lambda e: self._text_menu(self.source_text, e, False))

        # Frequently used actions deliberately sit between original and target.
        quick = ttk.Frame(upper)
        quick.pack(fill="x", pady=5)
        ttk.Button(quick, text="◀", width=3, command=lambda: self.navigate(-1)).pack(side=tk.LEFT)
        ttk.Button(quick, text="Оригинал →", width=12, command=self.copy_source_to_translation).pack(side=tk.LEFT, padx=(5, 0))
        ttk.Button(quick, text="Применить", width=10, command=self.apply_current).pack(side=tk.LEFT, padx=(5, 0))
        ttk.Button(quick, text="✓ Готово", width=9, command=self.mark_finished).pack(side=tk.LEFT, padx=(5, 0))
        ttk.Button(quick, text="Удалить", width=8, command=self.delete_current).pack(side=tk.LEFT, padx=(5, 0))
        ttk.Button(quick, text="▶", width=3, command=lambda: self.navigate(1)).pack(side=tk.LEFT, padx=(5, 0))

        translation_box = ttk.LabelFrame(self.paned, text="Перевод")
        self.paned.add(translation_box, weight=1)
        translation_header = ttk.Frame(translation_box)
        translation_header.pack(fill="x", padx=6, pady=(4, 0))
        self.forms_label = ttk.Label(translation_header, text="Текст перевода", style="Muted.TLabel")
        self.forms_label.pack(side=tk.LEFT)
        ttk.Button(translation_header, text="Очистить поле", command=self.clear_active_translation).pack(side=tk.RIGHT)
        ttk.Button(translation_header, text="Вставить", command=self.paste_to_translation).pack(side=tk.RIGHT, padx=(0, 5))
        self.editor_host = ttk.Frame(translation_box)
        self.editor_host.pack(fill="both", expand=True, padx=6, pady=5)

        self.apply_button = None
        self.center_sash_done = False
        self.after(80, self._center_sash)

    def _bind_shortcuts(self):
        self.bind_all("<Control-s>", lambda _e: (self.save(), "break")[1])
        self.bind_all("<Control-S>", lambda _e: (self.save(), "break")[1])
        self.bind_all("<Control-o>", lambda _e: (self.ask_open(), "break")[1])
        self.bind_all("<Control-O>", lambda _e: (self.ask_open(), "break")[1])
        self.bind_all("<Control-Return>", lambda _e: (self.apply_current(), "break")[1])
        self.bind_all("<Control-KP_Enter>", lambda _e: (self.apply_current(), "break")[1])
        self.bind_all("<Control-Shift-Return>", lambda _e: (self.mark_finished(), "break")[1])
        self.bind_all("<F3>", lambda _e: (self.find_next(), "break")[1])
        self.bind_all("<Alt-Up>", lambda _e: (self.navigate(-1), "break")[1])
        self.bind_all("<Alt-Down>", lambda _e: (self.navigate(1), "break")[1])

    def _center_sash(self):
        if self.center_sash_done:
            return
        try:
            self.update_idletasks()
            height = self.paned.winfo_height()
            if height > 80:
                self.paned.sashpos(0, height // 2)
                self.center_sash_done = True
            else:
                self.after(80, self._center_sash)
        except tk.TclError:
            pass

    # ------------------------------ Theme ---------------------------------

    def _apply_theme(self):
        t = DARK if self.dark else LIGHT
        self.configure(bg=t["bg"])
        s = self.style
        s.configure(".", background=t["bg"], foreground=t["fg"])
        s.configure("TFrame", background=t["bg"])
        s.configure("TLabel", background=t["bg"], foreground=t["fg"])
        s.configure("Muted.TLabel", background=t["bg"], foreground=t["muted"])
        s.configure("TLabelframe", background=t["bg"], foreground=t["fg"])
        s.configure("TLabelframe.Label", background=t["bg"], foreground=t["fg"])
        s.configure("TButton", background=t["bg"], foreground=t["fg"], bordercolor=t["border"])
        s.map("TButton", background=[("pressed", t["border"]), ("active", t["select"])],
              foreground=[("active", t["select_fg"])])
        s.configure("TEntry", fieldbackground=t["field"], foreground=t["field_fg"])
        s.configure("TCombobox", fieldbackground=t["field"], background=t["bg"], foreground=t["fg"])
        s.map("TCombobox", fieldbackground=[("readonly", t["field"])],
              foreground=[("readonly", t["field_fg"])])
        s.configure("Treeview", background=t["field"], fieldbackground=t["field"],
                    foreground=t["field_fg"], bordercolor=t["border"])
        s.configure("Treeview.Heading", background=t["bg"], foreground=t["fg"])
        s.map("Treeview", background=[("selected", t["select"])],
              foreground=[("selected", t["select_fg"])])
        s.configure("Status.TLabel", background=t["status_bg"], foreground=t["status_fg"])
        for widget in [self.source_text] + self.editor_widgets:
            try:
                widget.configure(bg=t["field"], fg=t["field_fg"], insertbackground=t["fg"],
                                 selectbackground=t["select"], selectforeground=t["select_fg"],
                                 relief="flat", borderwidth=1, highlightthickness=1,
                                 highlightbackground=t["border"], highlightcolor=t["select"])
            except tk.TclError:
                pass
        self.theme_button.configure(text="☀ Светлая" if self.dark else "🌙 Тёмная")

    def toggle_theme(self):
        self.dark = not self.dark
        self._apply_theme()

    # --------------------------- Dirty state -------------------------------

    def _editor_values(self):
        return [widget.get("1.0", "end-1c") for widget in self.editor_widgets]

    def _has_pending_editor(self):
        return bool(self.cur is not None and self._editor_values() != self.cur["forms"])

    def has_unsaved_changes(self):
        if self._has_pending_editor():
            return True
        return any(m["deleted"] or m["modified"] for m in self.msgs)

    def _update_dirty_ui(self):
        dirty = self.has_unsaved_changes()
        name = os.path.basename(self.path) if self.path else "Безымянный файл"
        self.title("Qt TS Editor — {}{}".format(name, " *" if dirty else ""))
        current_status = self.status_var.get()
        if dirty:
            if self._has_pending_editor():
                self.status_var.set("Есть несохранённый текст перевода — Ctrl+Enter применяет, Ctrl+S сохраняет")
            else:
                self.status_var.set("Есть несохранённые изменения")
        elif current_status.startswith("Есть несохранённые"):
            self.status_var.set("Изменения отменены — документ совпадает с сохранённой версией")

    def _on_editor_modified(self, event=None):
        widget = event.widget if event else None
        if widget is not None:
            try:
                if widget.edit_modified():
                    widget.edit_modified(False)
            except tk.TclError:
                pass
        if not self._loading_editor:
            self._update_dirty_ui()

    def _restore_editor_draft(self):
        if self.cur is not None:
            self._load_message(self.cur_index)

    def _resolve_pending_editor(self):
        if not self._has_pending_editor():
            return True
        answer = messagebox.askyesnocancel(
            "Несохранённый перевод",
            "В текущем поле есть изменения.\n\n"
            "Да — применить к сообщению\nНет — отбросить изменения поля\nОтмена — остаться здесь",
            parent=self,
        )
        if answer is None:
            return False
        if answer:
            return self.apply_current(show_message=False)
        return True

    # ------------------------------- Files ---------------------------------

    def ask_open(self):
        if self.has_unsaved_changes():
            answer = messagebox.askyesnocancel(
                "Несохранённые изменения",
                "Перед открытием другого файла сохранить изменения?\n\n"
                "Да — сохранить\nНет — отбросить\nОтмена — вернуться к редактору",
                parent=self,
            )
            if answer is None:
                return
            if answer and not self.save():
                return
        path = filedialog.askopenfilename(
            parent=self, title="Открыть Qt TS", filetypes=(("Qt Translation", "*.ts"), ("Все файлы", "*.*"))
        )
        if path:
            self.open_file(path, ask_first=False)

    def open_file(self, path, ask_first=True):
        if ask_first and self.has_unsaved_changes():
            answer = messagebox.askyesnocancel("Несохранённые изменения", "Сохранить изменения перед открытием?")
            if answer is None:
                return False
            if answer and not self.save():
                return False
        try:
            content, encoding, digest = decode_file(path)
            messages = parse_ts(content)
        except Exception as exc:
            messagebox.showerror("Ошибка открытия", "Не удалось открыть файл:\n{}".format(exc), parent=self)
            return False

        self.path = os.path.abspath(path)
        self.text = content
        self.encoding = encoding
        self.disk_hash = digest
        self.msgs = messages
        self.cur = None
        self.cur_index = None
        self.find_last = ""
        self.find_pos = -1
        self.filter_var.set("all")
        self._last_filter = "all"
        self._populate_tree()
        self._clear_selection_editor()
        self.status_var.set("Открыт: {}   |   Сообщений с переводом: {}".format(self.path, len(self.msgs)))
        self._update_dirty_ui()
        return True

    def save(self):
        if not self.path:
            return self.save_as()
        return self._save_to(self.path)

    def save_as(self):
        path = filedialog.asksaveasfilename(
            parent=self, title="Сохранить Qt TS как", defaultextension=".ts",
            filetypes=(("Qt Translation", "*.ts"), ("Все файлы", "*.*")),
        )
        if not path:
            return False
        if not os.path.splitext(path)[1]:
            path += ".ts"
        return self._save_to(path)

    def _save_to(self, path):
        try:
            # Save includes any un-applied text currently visible in the editor.
            if self._has_pending_editor() and not self.apply_current(show_message=False):
                return False
            new_text = compose_document(self.text, self.msgs)
            new_messages = parse_ts(new_text)  # validate offsets/XML structure before touching disk

            # Protect against overwriting changes made by another program.
            abs_path = os.path.abspath(path)
            if self.path and abs_path == os.path.abspath(self.path) and os.path.exists(path):
                with open(path, "rb") as stream:
                    current_hash = hashlib.sha256(stream.read()).hexdigest()
                if self.disk_hash and current_hash != self.disk_hash:
                    overwrite = messagebox.askyesno(
                        "Файл изменился на диске",
                        "Файл был изменён другой программой после открытия.\nПерезаписать внешние изменения?",
                        parent=self,
                    )
                    if not overwrite:
                        return False

            digest = atomic_write(path, new_text, self.encoding)
            self.path = abs_path
            self.text = new_text
            self.disk_hash = digest
            # Reparse after save: all positions now refer to the new baseline,
            # and all modified/deleted/dirty flags are reset together.
            self.msgs = new_messages
            self._populate_tree()
            self._clear_selection_editor()
            self.status_var.set("Сохранено: {}".format(self.path))
            self._update_dirty_ui()
            return True
        except Exception as exc:
            messagebox.showerror("Ошибка сохранения", "Не удалось сохранить файл:\n{}".format(exc), parent=self)
            return False

    def on_close(self):
        if self.has_unsaved_changes():
            answer = messagebox.askyesnocancel(
                "Несохранённые изменения",
                "Сохранить изменения перед выходом?\n\nДа — сохранить\nНет — выйти без сохранения\nОтмена — остаться",
                parent=self,
            )
            if answer is None:
                return
            if answer and not self.save():
                return
        self.destroy()

    # ------------------------------- List ----------------------------------

    def _matches_filter(self, message):
        wanted = self.filter_var.get()
        return wanted == "all" or message_status(message) == wanted

    def _populate_tree(self, keep_index=None):
        self._loading_tree = True
        self.tree.delete(*self.tree.get_children())
        for index, message in enumerate(self.msgs):
            if message["deleted"] or not self._matches_filter(message):
                continue
            source_preview = message["src"].replace("\n", " ")
            if len(source_preview) > 180:
                source_preview = source_preview[:177] + "…"
            self.tree.insert("", "end", iid=str(index),
                             values=(message_status(message), message["ctx"], source_preview))
        if keep_index is not None and str(keep_index) in self.tree.get_children():
            iid = str(keep_index)
            self.tree.selection_set(iid)
            self.tree.focus(iid)
            self.tree.see(iid)
        self._loading_tree = False

    def on_filter_change(self, _event=None):
        old = self._last_filter
        if not self._resolve_pending_editor():
            self.filter_var.set(old)
            return
        self._last_filter = self.filter_var.get()
        keep = self.cur_index
        self._populate_tree(keep_index=keep)
        if keep is not None and str(keep) in self.tree.get_children():
            self._load_message(keep)
        elif keep is not None:
            self._clear_selection_editor()
        self.status_var.set("Фильтр: {}".format(self.filter_var.get()))
        self._update_dirty_ui()

    def on_pick(self, _event=None):
        if self._loading_tree:
            return
        selected = self.tree.selection()
        if not selected:
            return
        try:
            new_index = int(selected[0])
        except ValueError:
            return
        if new_index == self.cur_index:
            return
        old_index = self.cur_index
        if not self._resolve_pending_editor():
            if old_index is not None and str(old_index) in self.tree.get_children():
                self._loading_tree = True
                self.tree.selection_set(str(old_index))
                self.tree.focus(str(old_index))
                self._loading_tree = False
            return
        self._load_message(new_index)

    def _clear_selection_editor(self):
        self.cur = None
        self.cur_index = None
        self.info_context.configure(text="Контекст: —")
        self.info_status.configure(text="Статус: —")
        self._loading_editor = True
        for child in self.editor_host.winfo_children():
            child.destroy()
        self.editor_widgets = []
        self.editor_notebook = None
        self.editor_pages = []
        self._loading_editor = False
        self._set_source_text("")
        self.forms_label.configure(text="Текст перевода")
        self._update_dirty_ui()

    def _set_source_text(self, value):
        self.source_text.configure(state="normal")
        self.source_text.delete("1.0", "end")
        if value:
            self.source_text.insert("1.0", value)
        self.source_text.configure(state="disabled")

    def _load_message(self, index):
        if index is None or index < 0 or index >= len(self.msgs):
            self._clear_selection_editor()
            return
        message = self.msgs[index]
        if message["deleted"]:
            self._clear_selection_editor()
            return
        self.cur_index = index
        self.cur = message
        self.info_context.configure(text="Контекст: {}".format(message["ctx"] or "—"))
        self.info_status.configure(text="Статус: {}".format(message_status(message)))
        self._set_source_text(message["src"])

        self._loading_editor = True
        for child in self.editor_host.winfo_children():
            child.destroy()
        self.editor_widgets = []
        self.editor_notebook = None
        self.editor_pages = []

        plural = message["form_tag"] == "numerusform"
        variants = message["form_tag"] == "lengthvariant"
        multiple = len(message["forms"]) > 1
        if multiple:
            notebook = ttk.Notebook(self.editor_host)
            notebook.pack(fill="both", expand=True)
            self.editor_notebook = notebook
            for n, value in enumerate(message["forms"]):
                tab = ttk.Frame(notebook)
                notebook.add(tab, text=("Форма {}".format(n + 1) if plural else "Вариант {}".format(n + 1)))
                self.editor_pages.append(tab)
                widget = self._create_editor(tab, value)
                self.editor_widgets.append(widget)
        else:
            widget = self._create_editor(self.editor_host, message["forms"][0] if message["forms"] else "")
            self.editor_widgets.append(widget)

        if plural:
            label = "Plural forms: {}".format(len(message["forms"]))
        elif variants:
            label = "Length variants: {}".format(len(message["forms"]))
        else:
            label = "Текст перевода"
        self.forms_label.configure(text=label)
        self._loading_editor = False
        self._update_dirty_ui()

    def _create_editor(self, parent, value):
        frame = ttk.Frame(parent)
        frame.pack(fill="both", expand=True)
        frame.rowconfigure(0, weight=1)
        frame.columnconfigure(0, weight=1)
        widget = tk.Text(frame, wrap="word", undo=True, maxundo=-1)
        widget.grid(row=0, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(frame, orient="vertical", command=widget.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        widget.configure(yscrollcommand=scroll.set)
        widget.insert("1.0", value)
        widget.edit_modified(False)
        widget.bind("<<Modified>>", self._on_editor_modified)
        widget.bind("<Button-3>", lambda e, w=widget: self._text_menu(w, e, True))
        widget.bind("<Button-2>", lambda e, w=widget: self._text_menu(w, e, True))
        self._apply_theme_to_editor(widget)
        return widget

    def _apply_theme_to_editor(self, widget):
        t = DARK if self.dark else LIGHT
        widget.configure(bg=t["field"], fg=t["field_fg"], insertbackground=t["fg"],
                         selectbackground=t["select"], selectforeground=t["select_fg"],
                         relief="flat", borderwidth=1, highlightthickness=1,
                         highlightbackground=t["border"], highlightcolor=t["select"])

    # -------------------------- Editor actions -----------------------------

    def _active_editor(self):
        if not self.editor_widgets:
            return None
        focused = self.focus_get()
        if focused in self.editor_widgets:
            return focused
        if self.editor_notebook is not None:
            selected_page = self.editor_notebook.select()
            for page, widget in zip(self.editor_pages, self.editor_widgets):
                if str(page) == selected_page:
                    return widget
        return self.editor_widgets[0]

    def copy_source(self):
        value = self.source_text.get("1.0", "end-1c")
        try:
            self.clipboard_clear()
            self.clipboard_append(value)
            self.status_var.set("Оригинал скопирован в буфер обмена")
        except tk.TclError as exc:
            messagebox.showerror("Буфер обмена", str(exc), parent=self)

    def copy_source_to_translation(self):
        widget = self._active_editor()
        if widget is None or self.cur is None:
            return
        value = self.source_text.get("1.0", "end-1c")
        widget.delete("1.0", "end")
        widget.insert("1.0", value)
        widget.focus_set()
        self._update_dirty_ui()

    def paste_to_translation(self):
        widget = self._active_editor()
        if widget is None:
            return
        try:
            value = self.clipboard_get()
        except tk.TclError:
            self.status_var.set("Буфер обмена пуст или недоступен")
            return
        try:
            if widget.tag_ranges("sel"):
                widget.delete("sel.first", "sel.last")
            widget.insert("insert", value)
            widget.focus_set()
        except tk.TclError as exc:
            messagebox.showerror("Вставка", str(exc), parent=self)

    def clear_active_translation(self):
        widget = self._active_editor()
        if widget is not None:
            widget.delete("1.0", "end")
            widget.focus_set()

    def apply_current(self, show_message=True):
        if self.cur is None:
            if show_message:
                messagebox.showinfo("Нет сообщения", "Сначала выберите сообщение слева.", parent=self)
            return False
        values = self._editor_values()
        try:
            set_message_forms(self.cur, values)
        except Exception as exc:
            messagebox.showerror("Ошибка перевода", str(exc), parent=self)
            return False
        self.info_status.configure(text="Статус: {}".format(message_status(self.cur)))
        iid = str(self.cur_index)
        if iid in self.tree.get_children():
            vals = list(self.tree.item(iid, "values"))
            vals[0] = message_status(self.cur)
            self.tree.item(iid, values=vals)
        self._update_dirty_ui()
        if show_message:
            self.status_var.set("Изменения применены к сообщению (ещё не записаны в файл)")
        return True

    def mark_finished(self):
        if self.cur is None:
            return
        if self._has_pending_editor() and not self.apply_current(show_message=False):
            return
        try:
            mark_message_finished(self.cur)
        except ValueError as exc:
            messagebox.showwarning("Finished", str(exc), parent=self)
            return
        self.info_status.configure(text="Статус: {}".format(message_status(self.cur)))
        iid = str(self.cur_index)
        if iid in self.tree.get_children():
            vals = list(self.tree.item(iid, "values"))
            vals[0] = message_status(self.cur)
            self.tree.item(iid, values=vals)
        self._update_dirty_ui()
        self.status_var.set("Сообщение помечено как finished (ещё не сохранено)")

    def delete_current(self):
        if self.cur is None:
            return
        if not messagebox.askyesno(
            "Удалить сообщение",
            "Удалить выбранное сообщение целиком?\nНесохранённый текст текущего поля также будет отброшен.",
            parent=self,
        ):
            return
        self.cur["deleted"] = True
        self.cur = None
        self.cur_index = None
        self._populate_tree()
        self._clear_selection_editor()
        self.status_var.set("Сообщение помечено на удаление (изменение ещё не сохранено)")
        self._update_dirty_ui()

    def navigate(self, direction):
        children = list(self.tree.get_children())
        if not children:
            return
        current = str(self.cur_index) if self.cur_index is not None else None
        if current in children:
            pos = children.index(current)
        else:
            pos = -1 if direction > 0 else 0
        new_pos = max(0, min(len(children) - 1, pos + direction))
        iid = children[new_pos]
        self.tree.selection_set(iid)
        self.tree.focus(iid)
        self.tree.see(iid)
        self.on_pick()

    # -------------------------- Search / stats -----------------------------

    def find_next(self):
        query = self.search_var.get().strip()
        if not query:
            messagebox.showinfo("Поиск", "Введите текст для поиска.", parent=self)
            return
        visible = [int(iid) for iid in self.tree.get_children()]
        if not visible:
            messagebox.showinfo("Поиск", "Нет сообщений в текущем фильтре.", parent=self)
            return
        if query.casefold() != self.find_last.casefold():
            self.find_last = query
            self.find_pos = -1
        start = 0
        for pos, index in enumerate(visible):
            if index > self.find_pos:
                start = pos
                break
        else:
            start = 0
        found = None
        needle = query.casefold()
        for offset in range(len(visible)):
            index = visible[(start + offset) % len(visible)]
            message = self.msgs[index]
            haystack = "\n".join([message["ctx"], message["src"]] + message["forms"]).casefold()
            if needle in haystack:
                found = index
                break
        if found is None:
            self.status_var.set("Совпадений не найдено: {}".format(query))
            return
        self.find_pos = found
        iid = str(found)
        self.tree.selection_set(iid)
        self.tree.focus(iid)
        self.tree.see(iid)
        self.on_pick()
        self.status_var.set("Найдено в контексте: {}".format(self.msgs[found]["ctx"] or "—"))

    def show_stats(self):
        counts = {"finished": 0, "unfinished": 0, "obsolete": 0, "vanished": 0}
        for message in self.msgs:
            if not message["deleted"]:
                counts[message_status(message)] += 1
        total = sum(counts.values())
        if not total:
            messagebox.showinfo("Статистика", "Нет сообщений с переводом.", parent=self)
            return
        lines = ["Всего: {}".format(total), ""]
        for key in ("finished", "unfinished", "obsolete", "vanished"):
            amount = counts[key]
            lines.append("{:<11} {:>6}  ({:.1f}%)".format(key + ":", amount, amount * 100.0 / total))
        messagebox.showinfo("Статистика", "\n".join(lines), parent=self)

    # ---------------------------- Text menus -------------------------------

    def _copy_widget_selection(self, widget):
        try:
            ranges = widget.tag_ranges("sel")
            value = widget.get(ranges[0], ranges[1]) if ranges else widget.get("1.0", "end-1c")
            self.clipboard_clear()
            self.clipboard_append(value)
        except tk.TclError:
            pass

    def _text_menu(self, widget, event, writable):
        menu = tk.Menu(self, tearoff=0)
        if writable:
            menu.add_command(label="Вырезать", command=lambda: widget.event_generate("<<Cut>>"))
        menu.add_command(label="Копировать", command=lambda: self._copy_widget_selection(widget))
        if writable:
            menu.add_command(label="Вставить", command=lambda: widget.event_generate("<<Paste>>"))
            menu.add_command(label="Удалить", command=lambda: widget.delete("sel.first", "sel.last") if widget.tag_ranges("sel") else None)
        menu.add_separator()
        menu.add_command(label="Выделить всё", command=lambda: (widget.tag_add("sel", "1.0", "end-1c"), widget.mark_set("insert", "1.0")))
        try:
            menu.tk_popup(event.x_root, event.y_root)
        finally:
            menu.grab_release()


if __name__ == "__main__":
    initial_path = sys.argv[1] if len(sys.argv) > 1 else None
    try:
        App(initial_path).mainloop()
    except TsFormatError as exc:
        messagebox.showerror("Qt TS Editor", str(exc))
