#!/usr/bin/env python3
"""Convert the project's Markdown docs to .docx (needs: pip install python-docx).

usage: md2docx.py input.md output.docx [--title-page "Title|Subtitle|line|line"]

Supports: # headings, paragraphs, - / 1. lists, ``` code fences, pipe tables,
![caption](image) figures, > quotes, --- rules, **bold**, `code`, *italic*,
and <!-- pagebreak -->. A plain-text table of contents is generated.
"""
import os
import re
import sys

from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

INLINE = re.compile(r"(\*\*[^*]+\*\*|`[^`]+`|\*[^*\s][^*]*\*)")


def shade(cell_or_par, hex_fill):
    el = cell_or_par._tc if hasattr(cell_or_par, "_tc") else cell_or_par._p
    pr = el.get_or_add_tcPr() if hasattr(cell_or_par, "_tc") else el.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), hex_fill)
    pr.append(shd)


def add_runs(par, text, size=None, bold=False):
    for part in INLINE.split(text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            r = par.add_run(part[2:-2]); r.bold = True
        elif part.startswith("`") and part.endswith("`"):
            r = par.add_run(part[1:-1]); r.font.name = "Consolas"
            r._element.rPr.rFonts.set(qn("w:eastAsia"), "Consolas")
            r.font.size = Pt((size or 10.5) - 1)
            r.font.color.rgb = RGBColor(0x9C, 0x1C, 0x1C)
        elif part.startswith("*") and part.endswith("*") and len(part) > 2:
            r = par.add_run(part[1:-1]); r.italic = True
        else:
            r = par.add_run(part)
        if bold:
            r.bold = True
        if size and not (part.startswith("`")):
            r.font.size = Pt(size)


def add_code(doc, lines):
    for ln in lines:
        p = doc.add_paragraph()
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.space_before = Pt(0)
        p.paragraph_format.left_indent = Cm(0.3)
        shade(p, "F3F3F3")
        r = p.add_run(ln if ln else " ")
        r.font.name = "Consolas"
        r._element.rPr.rFonts.set(qn("w:eastAsia"), "Consolas")
        r.font.size = Pt(8)
    doc.add_paragraph().paragraph_format.space_after = Pt(2)


def add_table(doc, rows):
    header = [c.strip() for c in rows[0].strip().strip("|").split("|")]
    body = []
    for r in rows[2:]:
        cells = re.split(r"(?<!\\)\|", r.strip().strip("|"))
        body.append([c.strip().replace("\\|", "|") for c in cells])
    t = doc.add_table(rows=1, cols=len(header))
    t.style = "Table Grid"
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    for i, h in enumerate(header):
        c = t.rows[0].cells[i]
        c.text = ""
        add_runs(c.paragraphs[0], h, size=9, bold=True)
        shade(c, "D9E2F3")
    for row in body:
        cells = t.add_row().cells
        for i in range(len(header)):
            txt = row[i] if i < len(row) else ""
            cells[i].text = ""
            parts = txt.split("<br>")
            add_runs(cells[i].paragraphs[0], parts[0], size=9)
            for extra in parts[1:]:
                add_runs(cells[i].add_paragraph(), extra, size=9)
    doc.add_paragraph().paragraph_format.space_after = Pt(2)


def build(md_path, out_path, title_page=None):
    base = os.path.dirname(os.path.abspath(md_path))
    text = open(md_path, encoding="utf-8").read().splitlines()

    doc = Document()
    sec = doc.sections[0]
    sec.left_margin = sec.right_margin = Cm(2.2)
    sec.top_margin = sec.bottom_margin = Cm(2.0)
    st = doc.styles["Normal"]
    st.font.name = "Calibri"
    st.font.size = Pt(10.5)
    for name, size in (("Heading 1", 18), ("Heading 2", 14), ("Heading 3", 12), ("Heading 4", 11)):
        h = doc.styles[name]
        h.font.name = "Calibri"
        h.font.size = Pt(size)
        h.font.color.rgb = RGBColor(0x1F, 0x4E, 0x78)

    if title_page:
        parts = title_page.split("|")
        for _ in range(7):
            doc.add_paragraph()
        p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = p.add_run(parts[0]); r.bold = True; r.font.size = Pt(26); r.font.color.rgb = RGBColor(0x1F, 0x4E, 0x78)
        if len(parts) > 1:
            p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
            r = p.add_run(parts[1]); r.font.size = Pt(15)
        doc.add_paragraph()
        for extra in parts[2:]:
            p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
            p.add_run(extra).font.size = Pt(12)
        doc.add_page_break()

    # table of contents (levels 1-2)
    heads = []
    in_code = False
    for ln in text:
        if ln.startswith("```"):
            in_code = not in_code
        elif not in_code:
            m = re.match(r"^(#{1,2}) (.+)", ln)
            if m:
                heads.append((len(m.group(1)), m.group(2)))
    if title_page and heads and heads[0][0] == 1:
        heads.pop(0)                      # the document title itself
    if heads:
        doc.add_heading("Contents", level=1)
        for lvl, h in heads:
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(0.7 * (lvl - 1))
            p.paragraph_format.space_after = Pt(0)
            add_runs(p, h.replace("**", ""), size=10, bold=(lvl == 1))
        doc.add_page_break()

    i, n = 0, len(text)
    first_h1_done = False
    while i < n:
        ln = text[i]
        if ln.startswith("```"):
            i += 1
            buf = []
            while i < n and not text[i].startswith("```"):
                buf.append(text[i]); i += 1
            add_code(doc, buf)
            i += 1
            continue
        if ln.strip() == "<!-- pagebreak -->":
            doc.add_page_break(); i += 1; continue
        m = re.match(r"^(#{1,4}) (.+)", ln)
        if m:
            lvl = len(m.group(1))
            if lvl == 1 and not first_h1_done and title_page:
                first_h1_done = True      # document title already on the title page
                i += 1
                continue
            first_h1_done = True
            doc.add_heading(m.group(2).replace("**", ""), level=lvl)
            i += 1
            continue
        m = re.match(r"^!\[(.*?)\]\((.+?)\)", ln)
        if m:
            path = os.path.join(base, m.group(2))
            if os.path.exists(path):
                doc.add_picture(path, width=Cm(16.4))
                doc.paragraphs[-1].alignment = WD_ALIGN_PARAGRAPH.CENTER
                cap = doc.add_paragraph(); cap.alignment = WD_ALIGN_PARAGRAPH.CENTER
                r = cap.add_run(m.group(1)); r.italic = True; r.font.size = Pt(9)
            i += 1
            continue
        if ln.startswith("|") and i + 1 < n and re.match(r"^\|[\s:|-]+\|?$", text[i + 1].strip()):
            rows = []
            while i < n and text[i].startswith("|"):
                rows.append(text[i]); i += 1
            add_table(doc, rows)
            continue
        if re.match(r"^\s*[-*] ", ln):
            indent = (len(ln) - len(ln.lstrip())) // 2
            p = doc.add_paragraph(style="List Bullet" if indent == 0 else "List Bullet 2")
            add_runs(p, re.sub(r"^\s*[-*] ", "", ln))
            p.paragraph_format.space_after = Pt(1)
            i += 1
            continue
        if re.match(r"^\s*\d+\. ", ln):
            p = doc.add_paragraph(style="List Number")
            add_runs(p, re.sub(r"^\s*\d+\. ", "", ln))
            p.paragraph_format.space_after = Pt(1)
            i += 1
            continue
        if ln.startswith("> "):
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(0.8)
            shade(p, "FFF2CC")
            add_runs(p, ln[2:])
            i += 1
            continue
        if ln.strip() == "---":
            i += 1
            continue
        if ln.strip() == "":
            i += 1
            continue
        # paragraph: merge following plain lines
        buf = [ln.strip()]
        i += 1
        while i < n and text[i].strip() and not re.match(r"^(#|```|\||!\[|\s*[-*] |\s*\d+\. |> |---|<!--)", text[i]):
            buf.append(text[i].strip()); i += 1
        p = doc.add_paragraph()
        add_runs(p, " ".join(buf))
        p.paragraph_format.space_after = Pt(6)

    doc.save(out_path)
    print("wrote", out_path)


if __name__ == "__main__":
    args = sys.argv[1:]
    tp = None
    if "--title-page" in args:
        k = args.index("--title-page")
        tp = args[k + 1]
        del args[k:k + 2]
    build(args[0], args[1], tp)
