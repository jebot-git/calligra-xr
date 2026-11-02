// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_table_layout.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace DomeOffice;
namespace {
const Text XML = "http://www.w3.org/XML/1998/namespace";
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
OdfPackage fixture(bool two = true) {
  OdfPackage p(Kind::Words);
  std::string xml =
      R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:automatic-styles><style:style style:name="Bold" style:family="text"><style:text-properties fo:font-weight="700" fo:color="#165aa7"/></style:style></office:automatic-styles><office:body><office:text><text:p>Before</text:p><table:table table:name="Grid"><table:table-column table:number-columns-repeated="3"/><table:table-row table:number-rows-repeated="3" xml:id="row"><table:table-cell table:number-columns-repeated="3" xml:id="cell" office:value-type="string" office:string-value="cached"><text:p xml:id="para"><text:span text:style-name="Bold">Bold tail</text:span></text:p>)";
  if (two)
    xml += "<text:p>Second</text:p>";
  xml += "</table:table-cell></table:table-row></table:table><text:p>After</"
         "text:p></office:text></office:body></office:document-content>";
  p.xml = read_document_xml(Bytes(xml));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
const TextTableCell &cell(const TextTable &table, size_t row, size_t column) {
  for (const auto &c : table.cells)
    if (c.row == row && c.column == column)
      return c;
  throw std::runtime_error("Missing test cell");
}
std::vector<Text> texts(const TextFlow &flow, const TextTableCell &c) {
  std::vector<Text> out;
  for (const auto &b : c.blocks)
    if (b.kind == TextBlock::Paragraph)
      out.push_back(block_paragraph(flow, b).text);
  return out;
}
TextSelection select(const TextDocument &doc, bool reverse = false) {
  WordsDisplayMap map(doc.flow());
  const auto &ps = map.paragraphs();
  const WordsDisplayParagraph *a = nullptr, *b = nullptr;
  for (const auto &p : ps) {
    if (p.cells.size() != 1)
      continue;
    if (p.cells[0].row == 1 && p.cells[0].column == 1 && !a)
      a = &p;
    if (p.cells[0].row == 2 && p.cells[0].column == 0)
      b = &p;
  }
  assert(a && b);
  TextSelection out{a->begin + 2, b->begin + 3};
  out.cells = b->cells;
  out.cell = out.cells.back();
  out.anchor_cells = a->cells;
  if (reverse) {
    std::swap(out.anchor, out.cursor);
    std::swap(*out.anchor_cells, out.cells);
    out.cell = out.cells.back();
  }
  return out;
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
void unique(const OdfPackage &p) {
  std::set<Text> ids;
  auto walk = [&](auto &&self, XmlElement n) -> void {
    auto id = n.attributeNS(XML, "id");
    assert(id.empty() || ids.insert(id).second);
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        self(self, c);
  };
  walk(walk, p.xml.documentElement());
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("words-range-edit-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto source = fixture();
  source.save(Text((dir / "source.odt").string()));
  auto xml = source.xml.toByteArray();
  auto load = [&] {
    TextDocument doc;
    doc.load(Text((dir / "source.odt").string()));
    return doc;
  };
  auto save = [&](TextDocument &doc, const char *name) {
    auto p = doc.style_snapshot();
    unique(p);
    assert(p.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
    doc.save(Text((dir / (std::string(name) + ".odt")).string()));
  };
  auto doc = load();
  auto old = select(doc);
  assert(doc.selection_crosses_cells(old));
  auto prepared = doc.prepare_cell_edit(old);
  assert(prepared && prepared->collapsed() && doc.prepared_range_edit());
  // No delivered input and cancelled preedit both restore exact source/history.
  assert(doc.end_edit(*prepared) == old);
  assert(doc.style_snapshot().xml.toByteArray() == xml && !doc.modified() &&
         !doc.can_undo());
  prepared = doc.prepare_cell_edit(old);
  doc.replace(prepared->cursor, 0, "preedit");
  doc.cancel_edit();
  assert(doc.end_edit() == old);
  assert(doc.style_snapshot().xml.toByteArray() == xml);
  prepared = doc.prepare_cell_edit(old);
  doc.replace(prepared->cursor, 0, "NEW\nline");
  auto next = *prepared;
  next.anchor = next.cursor = prepared->cursor + 8;
  doc.end_edit(next);
  save(doc, "typed");
  auto f = doc.flow();
  assert(f.tables.size() == 1 && f.tables[0].rows.size() == 3 &&
         f.tables[0].columns.size() == 3);
  for (const auto &c : f.tables[0].cells) {
    auto t = texts(f, c);
    if (c.row == 1 && c.column == 1) {
      assert((t == std::vector<Text>{"BoNEW", "line"}));
      assert(!c.source.hasAttributeNS(OFFICE, "string-value"));
    } else if (c.row == 1 && c.column == 2) {
      assert((t == std::vector<Text>{""}));
      assert(!c.source.hasAttributeNS(OFFICE, "string-value"));
    } else if (c.row == 2 && c.column == 0) {
      assert((t == std::vector<Text>{"ond"}));
      assert(!c.source.hasAttributeNS(OFFICE, "string-value"));
    } else {
      assert((t == std::vector<Text>{"Bold tail", "Second"}));
      assert(c.source.attributeNS(OFFICE, "string-value") == "cached");
    }
  }
  auto typed = doc.style_snapshot().xml.toByteArray();
  assert(doc.undo() == old);
  assert(doc.style_snapshot().xml.toByteArray() == xml);
  assert(doc.redo() == next);
  assert(doc.style_snapshot().xml.toByteArray() == typed);
  auto reverse = load();
  auto rs = select(reverse, true);
  auto rp = reverse.prepare_cell_edit(rs);
  assert(rp->cells == rs.cells);
  reverse.replace(rp->cursor, 0, "R");
  auto rn = *rp;
  rn.anchor = rn.cursor = rp->cursor + 1;
  reverse.end_edit(rn);
  save(reverse, "reverse");
  assert(reverse.undo() == rs);
  auto deletion = load();
  auto ds = select(deletion);
  auto dp = deletion.prepare_cell_edit(ds);
  deletion.accept_prepared_selection();
  deletion.end_edit(dp);
  save(deletion, "deleted");
  assert(deletion.can_undo());
  assert(deletion.undo() == ds &&
         deletion.style_snapshot().xml.toByteArray() == xml);
  // A whole selected inline span retains its insertion style after preparation.
  auto rich = load();
  auto full = select(rich);
  WordsDisplayMap rm(rich.flow());
  for (const auto &p : rm.paragraphs())
    if (p.cells == *full.anchor_cells) {
      full.anchor = p.begin;
      break;
    }
  auto richp = rich.prepare_cell_edit(full);
  assert(rich.format_info(*richp).bold == true);
  rich.replace(richp->cursor, 0, "X");
  auto richn = *richp;
  richn.anchor = richn.cursor = richp->cursor + 1;
  rich.end_edit(richn);
  auto info = read_words_format(rich.style_snapshot(), richp->cursor,
                                richp->cursor + 1);
  assert(info.bold == true && info.character.color == Text("#165aa7"));
  save(rich, "rich");
  // Single-cell edits invalidate only that displayed cell's string cache.
  auto single = load();
  WordsDisplayMap sm(single.flow());
  auto first = sm.paragraphs()[1];
  TextSelection one{first.begin, first.begin};
  one.cells = first.cells;
  one.cell = one.cells.back();
  one.anchor_cells = one.cells;
  auto sp = single.prepare_cell_edit(one);
  assert(sp);
  single.replace(sp->cursor, 0, "Q");
  auto sn = *sp;
  sn.anchor = sn.cursor = sp->cursor + 1;
  single.end_edit(sn);
  auto sf = single.flow();
  for (const auto &c : sf.tables[0].cells)
    assert(c.source.hasAttributeNS(OFFICE, "string-value") !=
           (c.row == 0 && c.column == 0));
  save(single, "single-cell");
  // Nested tables between selected paragraphs remain in place, and only their
  // selected display instances lose text. Outer paragraphs do not join across
  // them.
  auto nested = fixture();
  auto outer_cell = nested.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  auto inner = nested.xml.createElementNS(TABLE, "table:table");
  inner.setAttributeNS(TABLE, "table:name", "Inside");
  auto column = nested.xml.createElementNS(TABLE, "table:table-column");
  column.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
  inner.appendChild(column);
  auto row = nested.xml.createElementNS(TABLE, "table:table-row");
  row.setAttributeNS(TABLE, "table:number-rows-repeated", "2");
  inner.appendChild(row);
  auto ic = nested.xml.createElementNS(TABLE, "table:table-cell");
  ic.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
  row.appendChild(ic);
  auto ip = nested.xml.createElementNS(TEXT, "text:p");
  set_plain_text(ip, "Inside");
  ic.appendChild(ip);
  outer_cell.insertAfter(inner, outer_cell.firstChild());
  nested.save(Text((dir / "nested-source.odt").string()));
  TextDocument nd;
  nd.load(Text((dir / "nested-source.odt").string()));
  auto ns = select(nd);
  auto np = nd.prepare_cell_edit(ns);
  nd.replace(np->cursor, 0, "X");
  auto nn = *np;
  nn.anchor = nn.cursor = np->cursor + 1;
  nd.end_edit(nn);
  save(nd, "nested-typed");
  auto nf = nd.flow();
  for (const auto &t : nf.tables)
    if (t.name == "Grid")
      for (const auto &c : t.cells) {
        bool changed =
            (c.row == 1 && c.column >= 1) || (c.row == 2 && c.column == 0);
        auto values = texts(nf, c);
        if (c.row == 1 && c.column == 1)
          assert((values == std::vector<Text>{"BoX", ""}));
        else if (c.row == 1 && c.column == 2)
          assert((values == std::vector<Text>{"", ""}));
        else if (c.row == 2 && c.column == 0)
          assert((values == std::vector<Text>{"", "ond"}));
        else
          assert((values == std::vector<Text>{"Bold tail", "Second"}));
        for (const auto &b : c.blocks)
          if (b.kind == TextBlock::Table)
            for (const auto &v : block_table(nf, b).cells)
              assert((texts(nf, v) ==
                      std::vector<Text>{changed ? Text("") : Text("Inside")}));
      }
  assert(nd.undo() == ns);
  assert(nd.style_snapshot().xml.toByteArray() == nested.xml.toByteArray());
  // Adjacent sibling list items join within each cell, retaining one item and
  // its numbering rather than flattening the list or crossing a cell boundary.
  auto lists = fixture();
  auto lc = lists.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  while (!lc.firstChild().isNull())
    lc.removeChild(lc.firstChild());
  auto list = lists.xml.createElementNS(TEXT, "text:list");
  list.setAttributeNS(TEXT, "text:style-name", "Numbers");
  lc.appendChild(list);
  for (const char *value : {"One", "Two", "Three"}) {
    auto item = lists.xml.createElementNS(TEXT, "text:list-item");
    auto paragraph = lists.xml.createElementNS(TEXT, "text:p");
    set_plain_text(paragraph, value);
    item.appendChild(paragraph);
    list.appendChild(item);
  }
  auto style = lists.xml.createElementNS(TEXT, "text:list-style");
  style.setAttributeNS(STYLE, "style:name", "Numbers");
  auto level = lists.xml.createElementNS(TEXT, "text:list-level-style-number");
  level.setAttributeNS(TEXT, "text:level", "1");
  level.setAttributeNS(STYLE, "style:num-format", "1");
  level.setAttributeNS(STYLE, "style:num-suffix", ".");
  style.appendChild(level);
  child(lists.xml.documentElement(), OFFICE, "automatic-styles")
      .appendChild(style);
  lists.save(Text((dir / "list-source.odt").string()));
  TextDocument ld;
  ld.load(Text((dir / "list-source.odt").string()));
  auto ls = select(ld);
  auto lp = ld.prepare_cell_edit(ls);
  ld.replace(lp->cursor, 0, "X");
  auto ln = *lp;
  ln.anchor = ln.cursor = lp->cursor + 1;
  ld.end_edit(ln);
  save(ld, "list-typed");
  auto lf = ld.flow();
  for (const auto &c : lf.tables[0].cells) {
    auto values = texts(lf, c);
    if (c.row == 1 && c.column == 1)
      assert((values == std::vector<Text>{"OnX"}));
    else if (c.row == 1 && c.column == 2)
      assert((values == std::vector<Text>{""}));
    else if (c.row == 2 && c.column == 0)
      assert((values == std::vector<Text>{"ee"}));
    else
      assert((values == std::vector<Text>{"One", "Two", "Three"}));
    assert(c.source.firstChild().namespaceURI() == TEXT &&
           c.source.firstChild().localName() == "list");
  }
  // Body endpoints can enclose cells. Text replacement keeps the grid in place.
  auto body = load();
  TextSelection whole{0, body.text().size()};
  whole.anchor_cells = std::vector<TextCellAddress>{};
  assert(body.selection_crosses_cells(whole));
  auto bp = body.prepare_cell_edit(whole);
  body.replace(bp->cursor, 0, "Whole");
  auto bn = *bp;
  bn.anchor = bn.cursor = bp->cursor + 5;
  body.end_edit(bn);
  save(body, "body-typed");
  auto bf = body.flow();
  assert(bf.tables.size() == 1 && bf.tables[0].cells.size() == 9);
  for (const auto &c : bf.tables[0].cells)
    assert((texts(bf, c) == std::vector<Text>{""}));
  assert(body.undo() == whole &&
         body.style_snapshot().xml.toByteArray() == xml);
  // Cell boundaries are retained. Deleting only a boundary is a true no-op.
  auto boundary = load();
  WordsDisplayMap bm(boundary.flow());
  const WordsDisplayParagraph *ba = nullptr, *bb = nullptr;
  for (const auto &record : bm.paragraphs())
    if (record.cells.size() == 1) {
      if (record.cells[0].row == 1 && record.cells[0].column == 1)
        ba = &record;
      if (record.cells[0].row == 1 && record.cells[0].column == 2 && !bb)
        bb = &record;
    }
  assert(ba && bb);
  TextSelection gap{ba->begin + ba->length, bb->begin};
  gap.cells = bb->cells;
  gap.cell = gap.cells.back();
  gap.anchor_cells = ba->cells;
  auto gp = boundary.prepare_cell_edit(gap);
  boundary.accept_prepared_selection();
  assert(boundary.end_edit(gp) == gap);
  assert(!boundary.can_undo() && !boundary.modified() &&
         boundary.style_snapshot().xml.toByteArray() == xml);
  // Unsupported objects and numeric metadata reject after staging, atomically.
  auto bad = fixture();
  auto cs = bad.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  cs.setAttributeNS(OFFICE, "office:value-type", "float");
  cs.setAttributeNS(OFFICE, "office:value", "12");
  cs.removeAttributeNS(OFFICE, "string-value");
  bad.save(Text((dir / "numeric-source.odt").string()));
  TextDocument numeric;
  numeric.load(Text((dir / "numeric-source.odt").string()));
  auto before = numeric.style_snapshot().xml.toByteArray();
  rejects([&] { numeric.prepare_cell_edit(select(numeric)); });
  assert(!numeric.editing() && !numeric.can_undo() &&
         numeric.style_snapshot().xml.toByteArray() == before);
  auto object = fixture();
  auto ps = object.xml.elementsByTagNameNS(TEXT, "p");
  auto frame = object.xml.createElementNS(DRAW, "draw:frame");
  const Text SVG = "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0";
  frame.setAttributeNS(SVG, "svg:width", "12pt");
  frame.setAttributeNS(SVG, "svg:height", "12pt");
  auto box = object.xml.createElementNS(DRAW, "draw:text-box");
  box.appendChild(object.xml.createElementNS(TEXT, "text:p"));
  frame.appendChild(box);
  ps[1].appendChild(frame);
  object.save(Text((dir / "object-source.odt").string()));
  TextDocument unsupported;
  unsupported.load(Text((dir / "object-source.odt").string()));
  before = unsupported.style_snapshot().xml.toByteArray();
  rejects([&] { unsupported.prepare_cell_edit(select(unsupported)); });
  assert(!unsupported.editing() &&
         unsupported.style_snapshot().xml.toByteArray() == before);
  std::cout << "PASS cell-preserving range replacement, rich insertion, cache "
               "invalidation, cancellation, rejection and exact history\n";
}
