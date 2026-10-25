// SPDX-License-Identifier: LGPL-2.0-or-later
#pragma once
#include "sheets_engine_export.h"
#include <QString>
namespace Calligra::Sheets {
class Localization;
class Region;
namespace Odf {
CALLIGRA_SHEETS_ENGINE_EXPORT QString loadRegion(const QString& expression);
CALLIGRA_SHEETS_ENGINE_EXPORT void loadRegion(const QChar*& data, const QChar*& end, QChar*& out);
CALLIGRA_SHEETS_ENGINE_EXPORT QString saveRegion(const QString& expression);
CALLIGRA_SHEETS_ENGINE_EXPORT QString saveRegion(Region* region);
CALLIGRA_SHEETS_ENGINE_EXPORT QString decodeFormula(const QString& expression, const Localization* locale = nullptr, const QString& prefix = QString());
CALLIGRA_SHEETS_ENGINE_EXPORT QString encodeFormula(const QString& expression, const Localization* locale = nullptr);
CALLIGRA_SHEETS_ENGINE_EXPORT QString convertMSOOXMLFormula(const QString& expression);
}
}
