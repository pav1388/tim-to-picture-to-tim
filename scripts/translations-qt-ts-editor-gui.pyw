#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Минимальный редактор .ts: правит только <translation>, остальное — байт-в-байт."""

import os, re, sys, tkinter as tk
from tkinter import ttk, filedialog, messagebox

MSG_RE  = re.compile(r"<message\b[^>]*>.*?</message>", re.S)
SRC_RE  = re.compile(r"<source>(.*?)</source>", re.S)
TR_RE   = re.compile(r"<translation\b([^>]*?)(?:/>|>(.*?)</translation>)", re.S)
CTX_RE  = re.compile(r"<context\b[^>]*>.*?</context>", re.S)
NAME_RE = re.compile(r"<name>(.*?)</name>", re.S)
NUM_RE  = re.compile(r"(<numerusform[^>]*>)(.*?)(</numerusform>)", re.S)
TATTR   = re.compile(r'\btype\s*=\s*"([^"]*)"')

L = dict(bg="#f0f0f0", fg="#1a1a1a", fb="#ffffff", ff="#1a1a1a",
         sb="#4a90d9", sf="#ffffff", bd="#c0c0c0", stbg="#e0e0e0", stfg="#1a1a1a",
         mut="#666666", bb="#e6e6e6", bh="#d6d6d6", bp="#c8c8c8", bd2="#f0f0f0", bf2="#9a9a9a")
D = dict(bg="#2b2b2b", fg="#e0e0e0", fb="#1e1e1e", ff="#e0e0e0",
         sb="#3a6ea5", sf="#ffffff", bd="#555555", stbg="#1e1e1e", stfg="#b0b0b0",
         mut="#888888", bb="#3a3a3a", bh="#4a4a4a", bp="#2a2a2a", bd2="#2b2b2b", bf2="#666666")

# Windows virtual-key codes
VK_V, VK_C, VK_X, VK_A, VK_INSERT, VK_RETURN = 0x56, 0x43, 0x58, 0x41, 0x2D, 0x0D

def esc(s):   return s.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;")
def unesc(s): return (s.replace("&lt;","<").replace("&gt;",">")
                       .replace("&quot;",'"').replace("&apos;","'").replace("&amp;","&"))

def parse(text):
    out = []
    for cm in CTX_RE.finditer(text):
        cname = (NAME_RE.search(cm.group(0)) or [None,""])[1] if NAME_RE.search(cm.group(0)) else ""
        for mm in MSG_RE.finditer(cm.group(0)):
            body = mm.group(0)
            base = cm.start() + mm.start()
            src = SRC_RE.search(body)
            trm = TR_RE.search(body)
            if not trm:
                continue
            is_sc = trm.group(2) is None
            tm = TATTR.search(trm.group(1))
            nf = [(base+trm.start(2)+m.start(2), base+trm.start(2)+m.end(2), m.group(2))
                  for m in NUM_RE.finditer(trm.group(2) or "")]
            out.append(dict(
                ctx=cname,
                src=unesc(src.group(1)) if src else "",
                tr_raw=trm.group(2) or "",
                tr=unesc(nf[0][2]) if nf else unesc(trm.group(2) or ""),
                attrs=trm.group(1).strip(),
                selfclose=is_sc,
                type=tm.group(1) if tm else "",
                nforms=nf,
                ms=base, me=base+len(body),
                s=base+trm.start(), e=base+trm.end(),
                cs=base+trm.start(2) if not is_sc else base+trm.end(),
                ce=base+trm.end(2) if not is_sc else base+trm.end(),
            ))
    return out

def status(m):
    if m["type"] in ("unfinished","obsolete","vanished"): return m["type"]
    return "finished" if (m["tr"].strip() or m["nforms"]) else "unfinished"

def make_tr(m):
    body = m["tr_raw"]
    empty = not body.strip() and not m["nforms"]
    attrs = m["attrs"]
    if empty:
        if TATTR.search(attrs): attrs = TATTR.sub('type="unfinished"', attrs)
        else: attrs = (attrs + ' type="unfinished"').strip()
    else:
        attrs = TATTR.sub("", attrs).strip()
    if empty: return f"<translation{' '+attrs if attrs else ''}/>"
    return f"<translation{' '+attrs if attrs else ''}>{body}</translation>"

def rebuild(text, msgs):
    for m in sorted(msgs, key=lambda x: x["s"], reverse=True):
        text = text[:m["s"]] + make_tr(m) + text[m["e"]:]
    return text

def strip_tr(text):
    return TR_RE.sub("⟨TR⟩", text)

def set_text(m, new):
    e = esc(new)
    if m["nforms"]:
        first = [True]
        def rep(x):
            if first[0]:
                first[0] = False
                return x.group(1) + e + x.group(3)
            return x.group(0)
        m["tr_raw"] = NUM_RE.sub(rep, m["tr_raw"])
    else:
        m["tr_raw"] = e
    m["tr"] = new
    m["type"] = "" if new.strip() else "unfinished"

def remove_msg(text, m):
    return text[:m["ms"]] + text[m["me"]:]

# ---------- GUI ----------

class App(tk.Tk):
    def __init__(self, path=None):
        super().__init__()
        # СРАЗУ ставим обработчик закрытия — до всего остального
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        self.title("ts editor"); self.geometry("960x600"); self.minsize(500, 300)
        self.path = self.text = ""; self.msgs = []; self.cur = None
        self.dark = False; self.dirty = False; self.centered = False
        self.find_last = ""; self.find_pos = -1
        self.style = ttk.Style(self)
        try: self.style.theme_use("clam")
        except tk.TclError: pass
        self._ui(); self._theme()
        if path: self.open(path)

    # ---------- буфер обмена: нативные команды + VK ----------
    def _paste(self, w):
        try: w.tk.call("tk_textPaste", w._w)
        except tk.TclError: pass
        return "break"

    def _copy(self, w):
        try: w.tk.call("tk_textCopy", w._w)
        except tk.TclError: pass
        return "break"

    def _cut(self, w):
        try: w.tk.call("tk_textCut", w._w)
        except tk.TclError: pass
        return "break"

    def _select_all(self, w):
        w.tag_add("sel", "1.0", "end")
        return "break"

    def _on_key(self, w, e, writable):
        """Единый обработчик клавиш через VK-коды (event.keycode на Windows = VK)."""
        ctrl = bool(e.state & 0x4)   # Control
        shift = bool(e.state & 0x1)  # Shift
        vk = e.keycode
        ks = (e.keysym or "").upper()

        if ctrl and (vk == VK_C or ks == "C"):
            return self._copy(w)
        if ctrl and (vk == VK_A or ks == "A"):
            return self._select_all(w)
        if writable:
            if ctrl and (vk == VK_V or ks == "V"):
                return self._paste(w)
            if ctrl and (vk == VK_X or ks == "X"):
                return self._cut(w)
            # Shift+Insert = вставка (стандарт Windows)
            if shift and (vk == VK_INSERT or ks == "INSERT"):
                return self._paste(w)
            # Ctrl+Insert = копировать
            if ctrl and (vk == VK_INSERT or ks == "INSERT"):
                return self._copy(w)
            # Ctrl+Enter = применить
            if ctrl and (vk == VK_RETURN or ks == "RETURN"):
                self.apply()
                return "break"
        return None

    def _bind_text(self, w, writable):
        w.bind("<KeyPress>", lambda e, w=w, wr=writable: self._on_key(w, e, wr), add="+")
        w.bind("<Button-3>", lambda e, w=w, wr=writable: self._text_menu(w, e, wr))
        w.bind("<Button-2>", lambda e, w=w, wr=writable: self._text_menu(w, e, wr))

    def _text_menu(self, w, e, writable):
        m = tk.Menu(self, tearoff=0)
        if writable:
            m.add_command(label="Вырезать",  command=lambda: self._cut(w))
        m.add_command(label="Копировать", command=lambda: self._copy(w))
        if writable:
            m.add_command(label="Вставить", command=lambda: self._paste(w))
        m.add_separator()
        m.add_command(label="Выделить всё", command=lambda: self._select_all(w))
        try:
            m.tk_popup(e.x_root, e.y_root)
        finally:
            m.grab_release()

    # ---------- UI ----------
    def _ui(self):
        self.status = ttk.Label(self, textvariable=tk.StringVar(value="Готов"),
                                relief=tk.SUNKEN, anchor="w", style="S.TLabel")
        self.status.pack(side=tk.BOTTOM, fill=tk.X)

        tb = ttk.Frame(self); tb.pack(side=tk.TOP, fill=tk.X, padx=6, pady=(6,3))

        r1 = ttk.Frame(tb); r1.pack(side=tk.TOP, fill=tk.X)
        ttk.Button(r1, text="Открыть…", command=self.ask_open).pack(side=tk.LEFT)
        ttk.Button(r1, text="Сохранить", command=self.save).pack(side=tk.LEFT, padx=(6,0))
        ttk.Button(r1, text="Сохранить как…", command=self.save_as).pack(side=tk.LEFT, padx=(6,0))
        ttk.Separator(r1, orient="vertical").pack(side=tk.LEFT, fill="y", padx=8)
        ttk.Label(r1, text="Фильтр:").pack(side=tk.LEFT)
        self.fv = tk.StringVar(value="all")
        cb = ttk.Combobox(r1, textvariable=self.fv, state="readonly", width=12,
                          values=["all","unfinished","finished","obsolete","vanished"])
        cb.pack(side=tk.LEFT, padx=(4,0)); cb.bind("<<ComboboxSelected>>", lambda e: self.fill())
        self.tbtn = ttk.Button(r1, text="🌙 Тёмная", command=self.toggle); self.tbtn.pack(side=tk.RIGHT)

        r2 = ttk.Frame(tb); r2.pack(side=tk.TOP, fill=tk.X, pady=(4,0))
        ttk.Button(r2, text="Применить", command=self.apply).pack(side=tk.LEFT)
        ttk.Button(r2, text="Удалить", command=self.delete).pack(side=tk.LEFT, padx=(6,0))
        ttk.Button(r2, text="Finished", command=self.mark).pack(side=tk.LEFT, padx=(6,0))
        ttk.Button(r2, text="Статистика", command=self.stats).pack(side=tk.LEFT, padx=(6,0))
        ttk.Separator(r2, orient="vertical").pack(side=tk.LEFT, fill="y", padx=8)
        ttk.Label(r2, text="Поиск:").pack(side=tk.LEFT)
        self.sv = tk.StringVar()
        self.se = ttk.Entry(r2, textvariable=self.sv, width=28)
        self.se.pack(side=tk.LEFT, padx=(4,0))
        self.se.bind("<Return>", lambda e: (self.find(), "break")[1])
        ttk.Button(r2, text="Найти", command=self.find).pack(side=tk.LEFT, padx=(6,0))
        ttk.Label(r2, text="Ctrl+Enter — применить", style="M.TLabel").pack(side=tk.RIGHT)

        p = ttk.Panedwindow(self, orient="horizontal")
        p.pack(fill="both", expand=True, padx=6, pady=(3,6))

        left = ttk.Frame(p); p.add(left, weight=1)
        ttk.Label(left, text="Строки .ts").pack(anchor="w", padx=2, pady=(0,2))
        lf = ttk.Frame(left); lf.pack(fill="both", expand=True)
        lf.rowconfigure(0, weight=1); lf.columnconfigure(0, weight=1)
        self.lb = ttk.Treeview(lf, columns=("s","c","t"), show="headings", selectmode="browse")
        for k,w,tt in (("s",80,"Статус"),("c",120,"Контекст"),("t",300,"Исходник")):
            self.lb.heading(k, text=tt); self.lb.column(k, width=w, stretch=(k=="t"), anchor="w")
        self.lb.grid(row=0, column=0, sticky="nsew")
        vs = ttk.Scrollbar(lf, orient="vertical", command=self.lb.yview); vs.grid(row=0, column=1, sticky="ns")
        hs = ttk.Scrollbar(lf, orient="horizontal", command=self.lb.xview); hs.grid(row=1, column=0, sticky="ew")
        self.lb.configure(yscrollcommand=vs.set, xscrollcommand=hs.set)
        self.lb.bind("<<TreeviewSelect>>", self.on_pick)

        right = ttk.Frame(p); p.add(right, weight=1)
        info = ttk.Frame(right); info.pack(fill="x", padx=2, pady=(0,4)); info.columnconfigure(1, weight=1)
        ttk.Label(info, text="Контекст:").grid(row=0,column=0,sticky="w",padx=(0,4))
        self.li1 = ttk.Label(info, text="—"); self.li1.grid(row=0,column=1,sticky="w")
        ttk.Label(info, text="Статус:").grid(row=1,column=0,sticky="w",padx=(0,4))
        self.li2 = ttk.Label(info, text="—"); self.li2.grid(row=1,column=1,sticky="w")

        self.pv = ttk.Panedwindow(right, orient="vertical"); self.pv.pack(fill="both", expand=True)

        # Источник: normal, но ввод блокируется через VK-обработчик (writable=False)
        sb = ttk.LabelFrame(self.pv, text="Источник"); self.pv.add(sb, weight=1)
        sf = ttk.Frame(sb); sf.pack(fill="both", expand=True, padx=6, pady=6)
        sf.rowconfigure(0, weight=1); sf.columnconfigure(0, weight=1)
        self.ts = tk.Text(sf, height=4, wrap="word", state="normal"); self.ts.grid(row=0,column=0,sticky="nsew")
        sv = ttk.Scrollbar(sf, orient="vertical", command=self.ts.yview); sv.grid(row=0,column=1,sticky="ns")
        sh = ttk.Scrollbar(sf, orient="horizontal", command=self.ts.xview); sh.grid(row=1,column=0,sticky="ew")
        self.ts.configure(yscrollcommand=sv.set, xscrollcommand=sh.set)
        self._bind_text(self.ts, writable=False)
        # ввод в источник: гасим всё, кроме навигации/выделения/копирования
        self.ts.bind("<Key>", self._ro_key, add="+")
        self.ts.bind("<<Paste>>", lambda e: "break", add="+")
        self.ts.bind("<<Cut>>",   lambda e: "break", add="+")

        # Перевод
        eb = ttk.LabelFrame(self.pv, text="Перевод"); self.pv.add(eb, weight=1)
        ef = ttk.Frame(eb); ef.pack(fill="both", expand=True, padx=6, pady=6)
        ef.rowconfigure(0, weight=1); ef.columnconfigure(0, weight=1)
        self.te = tk.Text(ef, wrap="word", undo=True); self.te.grid(row=0,column=0,sticky="nsew")
        ev = ttk.Scrollbar(ef, orient="vertical", command=self.te.yview); ev.grid(row=0,column=1,sticky="ns")
        eh = ttk.Scrollbar(ef, orient="horizontal", command=self.te.xview); eh.grid(row=1,column=0,sticky="ew")
        self.te.configure(yscrollcommand=ev.set, xscrollcommand=eh.set)
        self._bind_text(self.te, writable=True)

        self.after(50, self.center_sash)

    def _ro_key(self, e):
        """Только-чтение: пропускаем навигацию, выделение, копирование, меню."""
        # модификаторы и навигация
        if e.keysym in ("Left","Right","Up","Down","Home","End","Prior","Next",
                        "Shift_L","Shift_R","Control_L","Control_R","Alt_L","Alt_R",
                        "Caps_Lock","Tab","Escape"):
            return None
        # Ctrl+... — отдаём в _on_key (там копирование/выделение)
        if e.state & 0x4: return None
        # Shift+стрелки — выделение
        if (e.state & 0x1) and e.keysym in ("Left","Right","Up","Down","Home","End","Prior","Next"):
            return None
        # всё прочее (буквы, BackSpace, Delete, Return) — блокируем
        return "break"

    def center_sash(self):
        if self.centered: return
        try:
            self.update_idletasks()
            h = self.pv.winfo_height()
            if h > 20: self.pv.sashpos(0, h // 2); self.centered = True
            else: self.after(50, self.center_sash)
        except tk.TclError: self.after(50, self.center_sash)

    def _theme(self):
        t = D if self.dark else L
        s = self.style
        self.configure(bg=t["bg"])
        s.configure(".", background=t["bg"], foreground=t["fg"])
        s.configure("TFrame", background=t["bg"]); s.configure("TLabel", background=t["bg"], foreground=t["fg"])
        s.configure("M.TLabel", background=t["bg"], foreground=t["mut"])
        s.configure("TLabelframe", background=t["bg"], foreground=t["fg"])
        s.configure("TLabelframe.Label", background=t["bg"], foreground=t["fg"])
        s.configure("TButton", background=t["bb"], foreground=t["fg"], bordercolor=t["bd"])
        s.map("TButton",
              background=[("pressed",t["bp"]),("active",t["bh"]),("disabled",t["bd2"])],
              foreground=[("pressed",t["sf"]),("active",t["sf"]),("disabled",t["bf2"])])
        s.configure("TCombobox", fieldbackground=t["fb"], background=t["bg"], foreground=t["fg"])
        s.map("TCombobox", fieldbackground=[("readonly",t["fb"])], foreground=[("readonly",t["fg"])])
        s.configure("Treeview", background=t["fb"], fieldbackground=t["fb"], foreground=t["ff"], bordercolor=t["bd"])
        s.configure("Treeview.Heading", background=t["bg"], foreground=t["fg"])
        s.map("Treeview", background=[("selected",t["sb"])], foreground=[("selected",t["sf"])])
        s.configure("TSeparator", background=t["bd"])
        s.configure("S.TLabel", background=t["stbg"], foreground=t["stfg"])
        s.configure("TEntry", fieldbackground=t["fb"], foreground=t["fg"])
        s.map("TEntry", fieldbackground=[("readonly",t["fb"])], foreground=[("readonly",t["fg"])])
        for w in (self.ts, self.te):
            w.configure(bg=t["fb"], fg=t["ff"], insertbackground=t["fg"],
                        selectbackground=t["sb"], selectforeground=t["sf"],
                        relief="flat", borderwidth=1, highlightthickness=1,
                        highlightbackground=t["bd"], highlightcolor=t["bd"])
        self.status.configure(background=t["stbg"], foreground=t["stfg"])
        self.tbtn.configure(text="☀ Светлая" if self.dark else "🌙 Тёмная")

    def toggle(self): self.dark = not self.dark; self._theme()

    # ---------- файл ----------
    def open(self, path):
        try: self.text = open(path, "r", encoding="utf-8").read()
        except Exception as e: messagebox.showerror("Ошибка", f"Не открыть:\n{e}"); return
        self.path = path; self.msgs = parse(self.text)
        self.dirty = False; self.title(f"ts editor — {os.path.basename(path)}")
        self.find_last = ""; self.find_pos = -1
        self.fill(); self.status.config(text=f"Открыт: {path}")

    def ask_open(self):
        if self.dirty and not messagebox.askyesno("Не сохранено", "Продолжить?"): return
        p = filedialog.askopenfilename(filetypes=[("Qt TS","*.ts"),("All","*.*")])
        if p: self.open(p)

    def save(self):
        if not self.path: return self.save_as()
        new = rebuild(self.text, self.msgs)
        if strip_tr(new) != strip_tr(self.text):
            bad = self.path + ".corrupt"
            open(bad, "w", encoding="utf-8", newline="").write(new)
            messagebox.showerror("Отказ", f"Изменения затронули не только <translation>.\nФайл не записан.\nСм.: {bad}")
            return
        try: open(self.path, "w", encoding="utf-8", newline="").write(new)
        except Exception as e: messagebox.showerror("Ошибка", f"Не сохранить:\n{e}"); return
        self.text = new; self.msgs = parse(self.text); self.dirty = False
        self.title(f"ts editor — {os.path.basename(self.path)}")
        self.status.config(text=f"Сохранено: {self.path}")

    def save_as(self):
        p = filedialog.asksaveasfilename(defaultextension=".ts",
                                         filetypes=[("Qt TS","*.ts"),("All","*.*")])
        if p: self.path = p; self.save()

    def on_close(self):
        """Запрос на сохранение при закрытии. Ловит любые исключения."""
        try:
            if self.dirty:
                ans = messagebox.askyesnocancel("Не сохранено",
                    "Есть несохранённые изменения.\n\nСохранить перед выходом?")
                if ans is None:     # Отмена
                    return
                if ans:             # Да
                    self.save()
                    if self.dirty:  # не удалось сохранить — не выходим
                        return
        except Exception as e:
            # даже если что-то упало — покажем и не закроемся молча
            try: messagebox.showerror("Ошибка при закрытии", str(e))
            except Exception: pass
            return
        self.destroy()

    # ---------- список ----------
    def fill(self):
        self.lb.delete(*self.lb.get_children()); self.cur = None
        self.li1.config(text="—"); self.li2.config(text="—")
        self._set_src("")
        self.te.delete("1.0","end")
        want = self.fv.get(); shown = 0
        for i, m in enumerate(self.msgs):
            if want != "all" and status(m) != want: continue
            src = m["src"].replace("\n"," ")
            self.lb.insert("", "end", iid=str(i), values=(status(m), m["ctx"], src[:120]))
            shown += 1
        self.status.config(text=f"Показано: {shown} / {len(self.msgs)} (фильтр: {want})")

    def _set_src(self, txt):
        self.ts.config(state="normal")
        self.ts.delete("1.0","end")
        if txt: self.ts.insert("1.0", txt)

    def on_pick(self, _=None):
        sel = self.lb.selection()
        if not sel: return
        i = int(sel[0]); m = self.msgs[i]; self.cur = m
        self.li1.config(text=m["ctx"] or "—"); self.li2.config(text=status(m))
        self._set_src(m["src"])
        self.te.delete("1.0","end"); self.te.insert("1.0", m["tr"])
        self.te.focus_set()

    # ---------- поиск ----------
    def find(self):
        q = self.sv.get().strip()
        if not q:
            messagebox.showinfo("Поиск", "Введите строку для поиска.")
            return
        if not self.msgs:
            messagebox.showinfo("Поиск", "Файл не открыт.")
            return
        if q != self.find_last:
            self.find_last = q
            self.find_pos = -1
        ql = q.lower()
        visible = [i for i in range(len(self.msgs))
                   if self.fv.get() == "all" or status(self.msgs[i]) == self.fv.get()]
        n = len(visible)
        start = 0
        if self.find_pos >= 0:
            for k, gi in enumerate(visible):
                if gi > self.find_pos:
                    start = k; break
            else:
                start = 0
        found = None
        for off in range(n):
            k = (start + off) % n
            gi = visible[k]
            m = self.msgs[gi]
            if ql in m["src"].lower() or ql in m["tr"].lower():
                found = (gi, k)
                break
        if found is None:
            self.find_pos = -1
            messagebox.showinfo("Поиск", f"Совпадений не найдено: «{q}»")
            return
        gi, k = found
        self.find_pos = gi
        iid = str(gi)
        if iid not in self.lb.get_children():
            self.fv.set("all"); self.fill()
            if iid not in self.lb.get_children():
                messagebox.showinfo("Поиск", "Совпадение есть, но строку не удалось отобразить.")
                return
        self.lb.selection_set(iid)
        self.lb.see(iid)
        self.lb.focus(iid)
        self.on_pick()
        self.status.config(text=f"Найдено: {self.msgs[gi]['ctx'] or '—'} — «{q}»")

    # ---------- действия ----------
    def apply(self):
        if not self.cur:
            messagebox.showinfo("Нет строки", "Сначала выберите строку в списке слева.")
            return
        self.dirty = True; self.title(self.title().rstrip("*") + " *")
        set_text(self.cur, self.te.get("1.0","end").rstrip("\n"))
        keep = self.cur["ctx"], self.cur["src"]
        self.fill()
        for iid in self.lb.get_children():
            m = self.msgs[int(iid)]
            if (m["ctx"], m["src"]) == keep:
                self.lb.selection_set(iid); self.lb.see(iid); break
        self.status.config(text="Перевод применён")

    def mark(self):
        if not self.cur: return
        self.cur["type"] = ""; self.dirty = True; self.fill()
        self.status.config(text="Помечено как finished")

    def delete(self):
        if not self.cur: return
        if not messagebox.askyesno("Удалить", "Удалить строку?"): return
        self.text = remove_msg(self.text, self.cur)
        self.msgs = parse(self.text); self.cur = None
        self.dirty = True; self.fill(); self.status.config(text="Удалено")

    def stats(self):
        if not self.msgs: return messagebox.showinfo("Статистика", "Файл не открыт.")
        c = dict(finished=0, unfinished=0, obsolete=0, vanished=0)
        for m in self.msgs: c[status(m)] += 1
        tot = len(self.msgs)
        p = lambda n: f"{n*100/tot:.1f}%"
        messagebox.showinfo("Статистика",
            f"Всего: {tot}\n\nfinished:   {c['finished']:>5} ({p(c['finished'])})\n"
            f"unfinished: {c['unfinished']:>5} ({p(c['unfinished'])})\n"
            f"obsolete:   {c['obsolete']:>5} ({p(c['obsolete'])})\n"
            f"vanished:   {c['vanished']:>5} ({p(c['vanished'])})")

if __name__ == "__main__":
    App(sys.argv[1] if len(sys.argv) > 1 else None).mainloop()