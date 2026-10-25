// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
class TextDocument {
public:
    TextDocument();
    void load(const QString&);
    void save(const QString&);
    QString text() const;
    void set_text(const QString&);
    bool imported_office() const { return imported_office_; }
    bool read_only() const { return package_.read_only(); }

private:
    OdfPackage package_ { Kind::Words };
    bool imported_office_ = false;
};
struct SlideBox {
    double x = 1, y = 1, width = 24, height = 3;
    QString text, image;
};
struct Slide {
    QString name;
    QList<SlideBox> boxes;
    double width = 28, height = 15.75;
    int unsupported = 0;
};
class Presentation {
public:
    Presentation();
    void load(const QString&);
    void save(const QString&);
    int count() const;
    Slide slide(int index) const;
    void add_slide(const QString& title = "New slide");
    void remove_slide(int index);
    void set_box_text(int slide, int box, const QString& text);
    QByteArray image(const QString& path) const { return package_.entry(path); }
    bool imported_office() const { return imported_office_; }
    bool read_only() const { return package_.read_only(); }

private:
    OdfPackage package_ { Kind::Stage };
    bool imported_office_ = false;
};
}
