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
class OdfPackage {
public:
    explicit OdfPackage(Kind kind);
    void load(const Text& path);
    void save(const Text& path);
    void set_entry(const Text& name, const Bytes& data, const Text& mediaType);
    XmlElement body() const;
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
    mutable std::map<Text, std::weak_ptr<const ImageAsset>> image_assets_;
    void put(const Text &name, Bytes bytes) {
        entries_[name] = std::make_shared<const Bytes>(std::move(bytes));
        image_assets_.erase(name);
    }
};
} // namespace DomeOffice
