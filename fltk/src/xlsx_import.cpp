// SPDX-License-Identifier: GPL-2.0-or-later
#include "xlsx_import.h"
#include <cmath>
#include "native_formula.h"
#include "native_time.h"

#include <stdexcept>

namespace DomeOffice {
namespace {
    const Text MAIN = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
    const Text STRICT = "http://purl.oclc.org/ooxml/spreadsheetml/main";
    const Text REL = "http://schemas.openxmlformats.org/package/2006/relationships";
    const Text DOCREL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    const Text STRICTREL = "http://purl.oclc.org/ooxml/officeDocument/relationships";
    [[noreturn]] void fail(const Text& message)
    {
        throw std::runtime_error(("XLSX import: " + message).toStdString());
    }
    struct Link {
        Text type, target;
        bool external;
    };
    struct Archive {
        Map<Text, Bytes> entries;
        XmlDocument xml(const Text& name) const
        {
            if (!entries.contains(name))
                fail("Missing package part: " + name);
            return read_document_xml(entries.value(name));
        }
        Map<Text, Link> links(const Text& part) const
        {
            const auto slash = part.lastIndexOf('/');
            const auto path = part.isEmpty() ? Text("_rels/.rels")
                                             : part.left(slash + 1) + "_rels/" + part.mid(slash + 1) + ".rels";
            Map<Text, Link> result;
            for (auto e : children(xml(path).documentElement(), REL, "Relationship")) {
                auto id = e.attribute("Id");
                if (id.isEmpty() || result.contains(id))
                    fail("Invalid relationship ID");
                bool external = e.attribute("TargetMode") == "External";
                Text target = e.attribute("Target");
                if (!external) {
                    if (target.contains('\\') || target.contains('%') || target.contains(':') || target.contains('#'))
                        fail("Unsupported package target");
                    target = Dir::cleanPath(target.startsWith('/') ? target.mid(1) : part.left(slash + 1) + target);
                    if (target == ".." || target.startsWith("../") || target.isEmpty())
                        fail("Package target escapes archive");
                }
                result[id] = { e.attribute("Type"), target, external };
            }
            return result;
        }
    };
    Text find_part(const Map<Text, Link>& links, const Text& kind, bool required = true)
    {
        Text result;
        for (const auto& link : links) {
            if (link.type != DOCREL + '/' + kind && link.type != STRICTREL + '/' + kind)
                continue;
            if (link.external || !result.isEmpty())
                fail("Invalid " + kind + " relationship");
            result = link.target;
        }
        if (required && result.isEmpty())
            fail("Missing " + kind + " relationship");
        return result;
    }
    Pair<int, int> address(const Text& ref)
    {
        static const Regex pattern("^([A-Z]{1,3})([1-9][0-9]{0,6})$");
        auto match = pattern.match(ref);
        if (!match.hasMatch())
            fail("Invalid cell address: " + ref);
        int col = 0;
        for (auto c : match.captured(1))
            col = col * 26 + c - 'A' + 1;
        int row = match.captured(2).toInt();
        if (col > 16384 || row > 1048576)
            fail("Cell address exceeds XLSX limits");
        return { col, row };
    }
    Text rich_text(const XmlElement& element, const Text& ns)
    {
        Text result;
        for (auto e : children(element, ns)) {
            if (e.localName() == "t")
                result += e.text();
            else if (e.localName() == "r")
                result += child(e, ns, "t").text();
        }
        return result;
    }
    XmlElement append(XmlElement parent, const Text& ns, const Text& name)
    {
        auto e = parent.ownerDocument().createElementNS(ns, name);
        parent.appendChild(e);
        return e;
    }
    void text_cell(XmlElement cell, const Text& text)
    {
        cell.setAttributeNS(OFFICE, "office:value-type", "string");
        set_plain_text(append(cell, TEXT, "text:p"), text);
    }
} // namespace
OdfPackage import_xlsx(const Text& path)
{
    Archive archive { read_document_archive(path) };
    for (const auto& name : archive.entries.keys()) {
        if (name.startsWith("_xmlsignatures/") || name.endsWith("vbaProject.bin", CaseInsensitive))
            fail("Signed or macro-enabled workbooks are not supported");
    }
    const Text workbookPath = find_part(archive.links({ }), "officeDocument");
    auto types = archive.xml("[Content_Types].xml");
    bool correctType = false;
    for (auto e :
        children(types.documentElement(), "http://schemas.openxmlformats.org/package/2006/content-types", "Override"))
        if (e.attribute("PartName") == '/' + workbookPath)
            correctType = e.attribute("ContentType")
                == "application/"
                   "vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml";
    if (!correctType)
        fail("Not a standard XLSX workbook");
    auto workbook = archive.xml(workbookPath);
    const auto root = workbook.documentElement();
    const auto ns = root.namespaceURI();
    if ((ns != MAIN && ns != STRICT) || root.localName() != "workbook")
        fail("Unsupported workbook namespace");
    auto links = archive.links(workbookPath);
    bool epoch1904 = child(root, ns, "workbookPr").attribute("date1904") == "1"
        || child(root, ns, "workbookPr").attribute("date1904") == "true";
    TextList strings;
    auto stringsPath = find_part(links, "sharedStrings", false);
    if (!stringsPath.isEmpty()) {
        auto doc = archive.xml(stringsPath);
        for (auto e : children(doc.documentElement(), ns, "si")) {
            if (strings.size() >= 1000000)
                fail("Too many shared strings");
            strings << rich_text(e, ns);
        }
    }
    // Classify date/time formats; ordinary visual formatting is not imported.
    List<int> dateStyles; // 0: number, 1: date/datetime, 2: time/duration
    auto stylesPath = find_part(links, "styles", false);
    if (!stylesPath.isEmpty()) {
        auto doc = archive.xml(stylesPath);
        Map<int, Text> custom;
        for (auto e : children(child(doc.documentElement(), ns, "numFmts"), ns, "numFmt"))
            custom[e.attribute("numFmtId").toInt()] = e.attribute("formatCode");
        for (auto e : children(child(doc.documentElement(), ns, "cellXfs"), ns, "xf")) {
            int id = e.attribute("numFmtId").toInt();
            Text format = custom.value(id);
            format.remove(Regex("\"[^\"]*\"|\\\\.|\\[[^\\]]*\\]"));
            bool date = (id >= 14 && id <= 22) || (id >= 27 && id <= 36) || (id >= 45 && id <= 47)
                || (id >= 50 && id <= 58)
                || format.contains(Regex("[ymdhs]", Regex::CaseInsensitiveOption));
            bool time = (id >= 18 && id <= 21) || (id >= 45 && id <= 47)
                || (!format.contains(Regex("[yd]", Regex::CaseInsensitiveOption))
                    && format.contains(Regex("[hs]", Regex::CaseInsensitiveOption)))
                || custom.value(id).contains(
                    Regex("\\[[hms]+\\]", Regex::CaseInsensitiveOption));
            dateStyles << (time ? 2 : date ? 1 : 0);
        }
    }
    OdfPackage output(Kind::Sheets);
    if (epoch1904) {
      // Keep unformatted serials, formula constants and typed dates in the
      // same workbook epoch. Rebasing only styled date cells loses that link.
      auto settings =
          append(output.body(), TABLE, "table:calculation-settings");
      auto date = append(settings, TABLE, "table:null-date");
      date.setAttributeNS(TABLE, "table:date-value", "1904-01-01");
    }
    Set<Text> names;
    int cells = 0;
    Int64 matrixCells = 0;
    Int64 textSize = 0;
    auto account = [&](const Text& text) {
        textSize += text.size();
        if (textSize > 32 * 1024 * 1024)
            fail("Expanded cell text exceeds the import limit");
    };
    auto sheets = children(child(root, ns, "sheets"), ns, "sheet");
    if (sheets.isEmpty() || sheets.size() > 1024)
        fail("Invalid worksheet count");
    for (auto sheet : sheets) {
        Text name = sheet.attribute("name");
        if (name.isEmpty() || names.contains(name.toCaseFolded()))
            fail("Invalid or duplicate sheet name");
        names.insert(name.toCaseFolded());
        auto id = sheet.attributeNS(DOCREL, "id", sheet.attributeNS(STRICTREL, "id"));
        if (!links.contains(id) || links[id].external || !links[id].type.endsWith("/worksheet"))
            fail("Unsupported worksheet relationship");
        auto document = archive.xml(links[id].target);
        auto worksheet = document.documentElement();
        if (worksheet.localName() != "worksheet" || worksheet.namespaceURI() != ns)
            fail("Invalid worksheet");
        auto table = append(output.body(), TABLE, "table:table");
        table.setAttributeNS(TABLE, "table:name", name);
        if (!child(worksheet, ns, "sheetProtection").isNull())
            table.setAttributeNS(TABLE, "table:protected", "true");
        struct Shared {
            Text formula;
            int col, row;
        };
        Map<Text, Shared> shared;
        using Position = std::pair<int, int>; // row, column
        struct Extent {
            int columns, rows;
        };
        std::map<Position, Extent> matrices;
        std::map<Position, Position> matrixOwners;
        auto rows = children(child(worksheet, ns, "sheetData"), ns, "row");
        // Discover masters before reading followers or cached result cells.
        for (auto row : rows)
            for (auto c : children(row, ns, "c")) {
                if (++cells > 1000000)
                    fail("Worksheet exceeds the import cell limit");
                if (children(c, ns, "f").size() > 1)
                    fail("Multiple formulas in one cell");
                auto f = child(c, ns, "f");
                if (f.attribute("t") == "shared" && !f.text().isEmpty()) {
                    auto at = address(c.attribute("r"));
                    auto index = f.attribute("si");
                    if (index.isEmpty() || shared.contains(index))
                        fail("Invalid shared formula group");
                    shared[index] = { Formula::from_excel(f.text()), at.first, at.second };
                }
                if (f.attribute("t") == "array" && f.hasAttribute("ref")) {
                    auto refs = f.attribute("ref").split(':');
                    if (refs.size() < 1 || refs.size() > 2 || f.text().trimmed().isEmpty())
                        fail("Invalid array formula master");
                    auto first = address(refs[0]);
                    auto last = address(refs.size() == 1 ? refs[0] : refs[1]);
                    if (first != address(c.attribute("r")) || last.first < first.first || last.second < first.second)
                        fail("Invalid array formula range");
                    int columns = last.first - first.first + 1, height = last.second - first.second + 1;
                    matrixCells += Int64(columns) * height;
                    if (matrixCells > 1000000)
                        fail("Array formulas exceed the import cell limit");
                    Position master { first.second, first.first };
                    for (int y = first.second; y <= last.second; ++y)
                        for (int x = first.first; x <= last.first; ++x)
                            if (!matrixOwners.emplace(Position { y, x }, master).second)
                                fail("Overlapping array formulas");
                    matrices.emplace(master, Extent { columns, height });
                }
            }
        int lastRow = 0;
        for (auto row : rows) {
            bool ok;
            int r = row.attribute("r", Text::number(lastRow + 1)).toInt(&ok);
            if (!ok || r <= lastRow || r > 1048576)
                fail("Invalid worksheet row order");
            if (r > lastRow + 1)
                append(table, TABLE, "table:table-row")
                    .setAttributeNS(TABLE, "table:number-rows-repeated", r - lastRow - 1);
            auto odfRow = append(table, TABLE, "table:table-row");
            lastRow = r;
            int lastCol = 0;
            for (auto c : children(row, ns, "c")) {
                auto at = address(c.attribute("r"));
                if (at.second != r || at.first <= lastCol)
                    fail("Invalid worksheet cell order");
                if (at.first > lastCol + 1)
                    append(odfRow, TABLE, "table:table-cell")
                        .setAttributeNS(TABLE, "table:number-columns-repeated", at.first - lastCol - 1);
                auto cell = append(odfRow, TABLE, "table:table-cell");
                lastCol = at.first;
                auto f = child(c, ns, "f");
                Text value = child(c, ns, "v").text(), type = c.attribute("t");
                Position position { r, at.first };
                // Array followers may carry a cache or an empty array marker,
                // but cannot contain an independent formula inside the block.
                if (auto owner = matrixOwners.find(position);
                    owner != matrixOwners.end() && owner->second != position) {
                    if (!f.isNull()
                        && (f.attribute("t") != "array" || !f.text().trimmed().isEmpty() || f.hasAttribute("ref")))
                        fail("Formula conflicts with an array result cell");
                    f = { };
                }
                if (!f.isNull()) {
                    const auto kind = f.attribute("t", "normal");
                    if (kind != "normal" && kind != "shared" && kind != "array")
                        fail("Unsupported formula type: " + kind);
                    Text formula;
                    if (kind == "shared") {
                        if (!shared.contains(f.attribute("si")))
                            fail("Missing shared formula master");
                        auto master = shared.value(f.attribute("si"));
                        formula = Formula::shift(master.formula, at.first-master.col, r-master.row);
                    } else {
                        if (kind == "array") {
                            auto matrix = matrices.find(position);
                            if (matrix == matrices.end())
                                fail("Missing array formula master or range");
                            cell.setAttributeNS(TABLE, "table:number-matrix-columns-spanned", matrix->second.columns);
                            cell.setAttributeNS(TABLE, "table:number-matrix-rows-spanned", matrix->second.rows);
                        }
                        formula = Formula::from_excel(f.text());
                    }
                    account(formula);
                    cell.setAttributeNS(TABLE, "table:formula", "of:" + Formula::encode(formula));
                    // Keep the source cache for formulas the engine cannot evaluate.
                    // Supported formulas receive fresh caches on ODS save.
                    if (type == "str" || type == "e") {
                        account(value);
                        text_cell(cell, value);
                    } else if (type == "b") {
                        if (value != "0" && value != "1")
                            fail("Invalid Boolean formula cache");
                        cell.setAttributeNS(OFFICE, "office:value-type", "boolean");
                        cell.setAttributeNS(OFFICE, "office:boolean-value", value == "1" ? "true" : "false");
                    } else {
                        bool numeric;
                        double cache = value.isEmpty() ? 0 : value.toDouble(&numeric);
                        if (!value.isEmpty() && (!numeric || !std::isfinite(cache)))
                            fail("Invalid formula cache");
                        cell.setAttributeNS(OFFICE, "office:value-type", "float");
                        cell.setAttributeNS(OFFICE, "office:value", value.isEmpty() ? Text("0") : value);
                    }
                } else if (type == "s") {
                    int index = value.toInt(&ok);
                    if (!ok || index < 0 || index >= strings.size())
                        fail("Invalid shared string index");
                    account(strings[index]);
                    text_cell(cell, strings[index]);
                } else if (type == "inlineStr") {
                    auto text = rich_text(child(c, ns, "is"), ns);
                    account(text);
                    text_cell(cell, text);
                } else if (type == "str" || type == "e") {
                    account(value);
                    text_cell(cell, value);
                } else if (type == "b") {
                    if (value != "0" && value != "1")
                        fail("Invalid Boolean cell");
                    cell.setAttributeNS(OFFICE, "office:value-type", "boolean");
                    cell.setAttributeNS(OFFICE, "office:boolean-value", value == "1" ? "true" : "false");
                } else if (type == "d") {
                    if (!DateTime::fromString(value, ISODate).isValid()
                        && !Date::fromString(value, ISODate).isValid())
                        fail("Invalid ISO date");
                    cell.setAttributeNS(OFFICE, "office:value-type", "date");
                    cell.setAttributeNS(OFFICE, "office:date-value", value);
                } else if (type.isEmpty() || type == "n") {
                    if (value.isEmpty())
                        continue;
                    double number = value.toDouble(&ok);
                    if (!ok || !std::isfinite(number))
                        fail("Invalid numeric cell");
                    int style = c.attribute("s", "0").toInt(&ok);
                    if (!ok || style < 0 || (!dateStyles.isEmpty() && style >= dateStyles.size()))
                        fail("Invalid cell style");
                    if (style < dateStyles.size() && dateStyles[style] == 2) {
                        if (number < 0 || number > 2958465)
                            fail("Unsupported time serial");
                        cell.setAttributeNS(OFFICE, "office:value-type", "time");
                        cell.setAttributeNS(
                            OFFICE, "office:time-value", "PT" + Text::number(number * 86400, 'g', 15) + "S");
                    } else if (style < dateStyles.size() && dateStyles[style] == 1) {
                        if (number < 0 || number > 2958465 || (!epoch1904 && std::floor(number) == 60))
                            fail("Unsupported Excel date serial");
                        Int64 days = std::floor(number);
                        auto date = (epoch1904 ? Date(1904, 1, 1) : Date(1899, 12, 31))
                                        .addDays(days - (!epoch1904 && days > 60 ? 1 : 0));
                        auto time = Time(0, 0).addMSecs(std::llround((number - days) * 86400000));
                        cell.setAttributeNS(OFFICE, "office:value-type", "date");
                        cell.setAttributeNS(
                            OFFICE, "office:date-value", DateTime(date, time, UTC).toString(ISODateWithMs));
                    } else {
                        cell.setAttributeNS(OFFICE, "office:value-type", "float");
                        cell.setAttributeNS(OFFICE, "office:value", value);
                    }
                } else
                    fail("Unsupported cell type: " + type);
            }
        }
    }
    // Map workbook-global defined ranges using the same formula translator.
    for (auto e : children(child(root, ns, "definedNames"), ns, "definedName")) {
        if (e.hasAttribute("localSheetId") || e.attribute("name").startsWith("_xlnm."))
            continue;
        Text formula = Formula::from_excel(e.text());
        if (!Regex("^='?[^=+*/()]+![\\$A-Z0-9:]+$").match(formula).hasMatch())
            continue;
        auto range = append(child(output.body(), TABLE, "named-expressions").isNull()
                ? append(output.body(), TABLE, "table:named-expressions")
                : child(output.body(), TABLE, "named-expressions"),
            TABLE, "table:named-range");
        range.setAttributeNS(TABLE, "table:name", e.attribute("name"));
        range.setAttributeNS(TABLE, "table:cell-range-address", Formula::save_region(formula.mid(1)));
    }
    return output;
}
} // namespace DomeOffice
