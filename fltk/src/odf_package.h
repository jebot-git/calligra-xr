// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDomDocument>
#include <QMap>
#include <QStringList>
namespace DomeOffice {
inline const QString OFFICE = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:office:1.0");
inline const QString TEXT = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0");
inline const QString TABLE = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:table:1.0");
inline const QString DRAW = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:drawing:1.0");
inline const QString SVG = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0");
inline const QString XLINK = QStringLiteral("http://www.w3.org/1999/xlink");
enum class Kind { Words, Sheets, Stage };
QString mime(Kind kind);
QString suffix(Kind kind);
QDomElement child(const QDomElement&, const QString& ns, const QString& local);
QList<QDomElement> children(const QDomElement&, const QString& ns, const QString& local = { });
QString plain_text(const QDomElement&);
void set_plain_text(QDomElement element, const QString& value);
class OdfPackage {
public:
    explicit OdfPackage(Kind kind);
    void load(const QString& path);
    void save(const QString& path);
    QDomElement body() const;
    QByteArray entry(const QString& name) const { return entries_.value(name); }
    Kind kind() const { return kind_; }
    bool read_only() const { return read_only_; }
    QDomDocument xml;

private:
    Kind kind_;
    QMap<QString, QByteArray> entries_;
    bool read_only_ = false;
};
}
