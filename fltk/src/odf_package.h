// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_xml.h"
namespace DomeOffice {
struct ImageAsset;
inline const Text OFFICE = Text("urn:oasis:names:tc:opendocument:xmlns:office:1.0");
inline const Text TEXT = Text("urn:oasis:names:tc:opendocument:xmlns:text:1.0");
inline const Text TABLE = Text("urn:oasis:names:tc:opendocument:xmlns:table:1.0");
inline const Text DRAW = Text("urn:oasis:names:tc:opendocument:xmlns:drawing:1.0");
inline const Text SVG = Text("urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0");
inline const Text XLINK = Text("http://www.w3.org/1999/xlink");
enum class Kind { Words, Sheets, Stage };
Text mime(Kind kind);
Text suffix(Kind kind);
XmlElement child(const XmlElement&, const Text& ns, const Text& local);
List<XmlElement> children(const XmlElement&, const Text& ns, const Text& local = { });
Text plain_text(const XmlElement&);
void set_plain_text(XmlElement element, const Text& value);
Map<Text, Bytes> read_document_archive(const Text& path);
XmlDocument read_document_xml(const Bytes& data);
// Byte deltas for non-content archive parts. Missing and empty parts are
// distinct; content.xml remains owned by the document DOM/history.
struct ArchivePartChange {
    Text name;
    bool before_exists = false, after_exists = false;
    size_t offset = 0, before_size = 0, after_size = 0;
    Bytes before, after;
};
class OdfPackage {
public:
    explicit OdfPackage(Kind kind);
    void load(const Text& path);
    void save(const Text& path);
    void set_entry(const Text& name, const Bytes& data, const Text& mediaType);
    XmlElement body() const;
    std::vector<ArchivePartChange> part_changes_from(const OdfPackage&) const;
    // Validates every source part before atomically replaying the changes.
    // Restores manifest bytes as recorded, without regenerating other parts.
    void replay_part_changes(const std::vector<ArchivePartChange>&, bool redo);
    Bytes entry(const Text &name) const {
        auto data = entry_data(name);
        return data ? *data : Bytes();
    }
    std::shared_ptr<const Bytes> entry_data(const Text &name) const { return entries_.value(name); }
    std::shared_ptr<const ImageAsset> image_asset(const Text &reference) const;
    Kind kind() const { return kind_; }
    bool read_only() const { return read_only_; }
    XmlDocument xml;

private:
    Kind kind_;
    Map<Text, std::shared_ptr<const Bytes>> entries_;
    bool read_only_ = false;
    // Copies share this identity until an archive part changes. Text-only
    // editing can bypass archive comparisons, even in image-heavy documents.
    std::shared_ptr<const char> parts_identity_ = std::make_shared<const char>(0);
    mutable std::map<Text, std::weak_ptr<const ImageAsset>> image_assets_;
    void put(const Text &name, Bytes bytes) {
        auto identity = std::make_shared<const char>(0);
        auto data = std::make_shared<const Bytes>(std::move(bytes));
        entries_[name] = std::move(data);
        parts_identity_ = std::move(identity);
        image_assets_.erase(name);
    }
};
} // namespace DomeOffice
