/* This file is part of the KDE project
   SPDX-FileCopyrightText: 1998-2016 The Calligra Team <calligra-devel@kde.org>
   SPDX-FileCopyrightText: 2016 Tomas Mecir <mecirt@gmail.com>
   SPDX-FileCopyrightText: 2010 Marijn Kruisselbrink <mkruisselbrink@kde.org>
   SPDX-FileCopyrightText: 2007 Stefan Nikolaus <stefan.nikolaus@kdemail.net>
   SPDX-FileCopyrightText: 2007 Thorsten Zachmann <zachmann@kde.org>
   SPDX-FileCopyrightText: 2005-2006 Inge Wallin <inge@lysator.liu.se>
   SPDX-FileCopyrightText: 2004 Ariya Hidayat <ariya@kde.org>
   SPDX-FileCopyrightText: 2002-2003 Norbert Andres <nandres@web.de>
   SPDX-FileCopyrightText: 2000-2002 Laurent Montel <montel@kde.org>
   SPDX-FileCopyrightText: 2002 John Dailey <dailey@vt.edu>
   SPDX-FileCopyrightText: 2002 Phillip Mueller <philipp.mueller@gmx.de>
   SPDX-FileCopyrightText: 2000 Werner Trobin <trobin@kde.org>
   SPDX-FileCopyrightText: 1999-2000 Simon Hausmann <hausmann@kde.org>
   SPDX-FileCopyrightText: 1999 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 1998-2000 Torben Weis <weis@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

/* This file is part of the KDE project
   SPDX-FileCopyrightText: 1998-2016 The Calligra Team <calligra-devel@kde.org>
   SPDX-FileCopyrightText: 2016 Tomas Mecir <mecirt@gmail.com>
   SPDX-FileCopyrightText: 2010 Marijn Kruisselbrink <mkruisselbrink@kde.org>
   SPDX-FileCopyrightText: 2007 Stefan Nikolaus <stefan.nikolaus@kdemail.net>
   SPDX-FileCopyrightText: 2007 Thorsten Zachmann <zachmann@kde.org>
   SPDX-FileCopyrightText: 2005-2006 Inge Wallin <inge@lysator.liu.se>
   SPDX-FileCopyrightText: 2004 Ariya Hidayat <ariya@kde.org>
   SPDX-FileCopyrightText: 2002-2003 Norbert Andres <nandres@web.de>
   SPDX-FileCopyrightText: 2000-2002 Laurent Montel <montel@kde.org>
   SPDX-FileCopyrightText: 2002 John Dailey <dailey@vt.edu>
   SPDX-FileCopyrightText: 2002 Phillip Mueller <philipp.mueller@gmx.de>
   SPDX-FileCopyrightText: 2000 Werner Trobin <trobin@kde.org>
   SPDX-FileCopyrightText: 1999-2000 Simon Hausmann <hausmann@kde.org>
   SPDX-FileCopyrightText: 1999 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 1998-2000 Torben Weis <weis@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "OdfFormula.h"
#include "Formula.h"
#include "Localization.h"
#include "Region.h"
#include "Util.h"
#include <QRegularExpression>

namespace Calligra::Sheets {
using namespace Qt::StringLiterals;

static void append(const QChar *from, const QChar *to, QChar **dest)
{
    while (from < to) {
        **dest = *from;
        ++from;
        ++*dest;
    }
}

void Odf::loadRegion(const QChar *&data, const QChar *&end, QChar *&out)
{
    enum {
        Start,
        InQuotes
    } state = Start;

    if (*data == QChar('$', 0)) {
        ++data;
    }

    bool isRange = false;

    const QChar *pos = data;
    while (data < end) {
        switch (state) {
        case Start:
            switch (data->unicode()) {
            case '\'': // quoted sheet name or named area
                state = InQuotes;
                break;
            case '.': // sheet name separator
                if (pos != data && !isRange) {
                    append(pos, data, &out);
                    *out = QChar('!', 0);
                    ++out;
                }
                pos = data;
                ++pos;
                break;
            case ':': { // cell separator
                isRange = true;
                append(pos, data, &out);
                *out = *data; // append :
                ++out;
                const QChar *next = data + 1;
                if (!next->isNull()) {
                    const QChar *nextnext = next + 1;
                    if (!nextnext->isNull() && *next == QChar('$', 0) && *nextnext != QChar('.', 0)) {
                        ++data;
                    }
                }
                pos = data + 1;
            } break;
            case ' ': // range separator
                append(pos, data, &out);
                *out = QChar(';', 0);
                ++out;
                pos = data;
                break;
            default:
                break;
            }
            break;
        case InQuotes:
            if (data->unicode() == '\'') {
                // an escaped apostrophe?
                // As long as Calligra Sheets does not support fixed sheets eat the dollar sign.
                const QChar *next = data + 1;
                if (!next->isNull() && *next == QChar('\'', 0)) {
                    ++data;
                } else { // the end
                    state = Start;
                }
            }
            break;
        }
        ++data;
    }
    append(pos, data, &out);
}

QString Odf::loadRegion(const QString &expression)
{
    QString result;
    QString temp;
    bool isRange = false;
    enum {
        Start,
        InQuotes
    } state = Start;
    int i = 0;
    // NOTE Stefan: As long as Calligra Sheets does not support fixed sheets eat the dollar sign.
    if (expression[i] == '$')
        ++i;
    while (i < expression.size()) {
        switch (state) {
        case Start: {
            if (expression[i] == '\'') { // quoted sheet name or named area
                temp.append(expression[i]);
                state = InQuotes;
            } else if (expression[i] == '.') { // sheet name separator
                // was there already a sheet name?
                if (!temp.isEmpty() && !isRange) {
                    result.append(temp);
                    result.append('!');
                }
                temp.clear();
            } else if (expression[i] == ':') { // cell separator
                isRange = true;
                result.append(temp);
                result.append(':');
                temp.clear();
                // NOTE Stefan: As long as Calligra Sheets does not support fixed sheets eat the dollar sign.
                if (i + 2 < expression.size() && expression[i + 1] == '$' && expression[i + 2] != '.')
                    ++i;
            } else if (expression[i] == ' ') { // range separator
                result.append(temp);
                result.append(';');
                temp.clear();
            } else
                temp.append(expression[i]);
            ++i;
            break;
        }
        case InQuotes: {
            temp.append(expression[i]);
            if (expression[i] == '\'') {
                // an escaped apostrophe?
                if (i + 1 < expression.size() && expression[i + 1] == '\'')
                    ++i; // eat it
                else // the end
                    state = Start;
            }
            ++i;
            break;
        }
        }
    }
    return result + temp;
}

QString Odf::saveRegion(const QString &expression)
{
    QString result;
    QString sheetName;
    QString temp;
    enum {
        Start,
        InQuotes
    } state = Start;
    int i = 0;
    while (i < expression.size()) {
        switch (state) {
        case Start: {
            if (expression[i] == '\'') {
                temp.append(expression[i]);
                state = InQuotes;
            } else if (expression[i] == '!') { // sheet name separator
                // There has to be a sheet name.
                if (temp.isEmpty())
                    return expression; // error
                if (temp.size() > 2 && (temp[0] != '\'' && temp[temp.size() - 1] != '\'')) {
                    temp.replace('\'', "''");
                    if (temp.contains(' ') || temp.contains('.') || temp.contains(';') || temp.contains('!') || temp.contains('$') || temp.contains(']'))
                        temp = '\'' + temp + '\'';
                }
                sheetName = temp;
                result.append(temp);
                result.append('.');
                temp.clear();
            } else if (expression[i] == ':') { // cell separator
                if (result.isEmpty())
                    result = '.';
                result.append(temp);
                result.append(':');
                result.append(sheetName);
                result.append('.');
                temp.clear();
            } else if (expression[i] == ';') { // range separator
                result.append(temp);
                result.append(' ');
                temp.clear();
            } else
                temp.append(expression[i]);
            ++i;
            break;
        }
        case InQuotes: {
            temp.append(expression[i]);
            if (expression[i] == '\'') {
                // an escaped apostrophe?
                if (i + 1 < expression.size() && expression[i + 1] == '\'')
                    ++i; // eat it
                else // the end
                    state = Start;
            }
            ++i;
            break;
        }
        }
    }
    if (result.isEmpty())
        result = '.';
    return result + temp;
}

QString Odf::saveRegion(Region *region)
{
    return saveRegion(region->name());
}


QString Odf::decodeFormula(const QString &expression_, const Localization *locale, const QString &namespacePrefix)
{
    // parsing state
    enum {
        Start,
        InNumber,
        InString,
        InIdentifier,
        InReference,
        InSheetName
    } state = Start;

    QString expression = expression_;
    if (namespacePrefix == "msoxl:") {
        expression = convertMSOOXMLFormula(expression);
    }

    // use locale settings
    QString decimal = locale ? locale->decimalSymbol() : ".";

    const QChar *data = expression.constData();
    const QChar *start = data;

    if (data->isNull()) {
        return QString();
    }

    int length = expression.length() * 2;
    QString result(length, QChar());
    result.reserve(length);
    QChar *out = result.data();
    QChar *outStart = result.data();

    if (*data == QChar('=', 0)) {
        *out = *data;
        ++data;
        ++out;
    }

    const QChar *pos = data;
    while (!data->isNull()) {
        switch (state) {
        case Start: {
            if (data->isDigit()) { // check for number
                state = InNumber;
                *out++ = *data++;
            } else if (*data == QChar('.', 0)) {
                state = InNumber;
                *out = decimal[0];
                ++out;
                ++data;
            } else if (Formula::isIdentifier(*data)) {
                // beginning with alphanumeric ?
                // could be identifier, cell, range, or function...
                QStringView view(expression);
                state = InIdentifier;
                int i = data - start;
                const static QString errorTypeReplacement("ERRORTYPE");
                const static QString legacyNormsdistReplacement("LEGACYNORMSDIST");
                const static QString legacyNormsinvReplacement("LEGACYNORMSINV");
                const static QString multipleOperations("MULTIPLE.OPERATIONS");
                if (view.mid(i, 10).compare(QLatin1String("ERROR.TYPE")) == 0) {
                    // replace it
                    int outPos = out - outStart;
                    result.replace(outPos, 9, errorTypeReplacement);
                    data += 10; // number of characters in "ERROR.TYPE"
                    out += 9;
                } else if (view.mid(i, 12).compare(QLatin1String("LEGACY.NORMS")) == 0) {
                    if (view.mid(i + 12, 4).compare(QLatin1String("DIST")) == 0) {
                        // replace it
                        int outPos = out - outStart;
                        result.replace(outPos, 15, legacyNormsdistReplacement);
                        data += 16; // number of characters in "LEGACY.NORMSDIST"
                        out += 15;
                    } else if (view.mid(i + 12, 3).compare(QLatin1String("INV")) == 0) {
                        // replace it
                        int outPos = out - outStart;
                        result.replace(outPos, 14, legacyNormsinvReplacement);
                        data += 15; // number of characters in "LEGACY.NORMSINV"
                        out += 14;
                    }
                } else if (namespacePrefix == "oooc:" && view.mid(i, 5).compare(QLatin1String("TABLE")) == 0 && !Formula::isIdentifier(expression[i + 5])) {
                    int outPos = out - outStart;
                    result.replace(outPos, 19, multipleOperations);
                    data += 5;
                    out += 19;
                } else if (view.mid(i, 3).compare(QLatin1String("NEG")) == 0) {
                    *out = QChar('-', 0);
                    data += 3;
                    ++out;
                }
            } else {
                switch (data->unicode()) {
                case '"': // a string ?
                    state = InString;
                    *out++ = *data++;
                    break;
                case '[': // [ marks sheet name for 3-d cell, e.g ['Sales Q3'.A4]
                    state = InReference;
                    ++data;
                    // NOTE: As long as Calligra::Sheets does not support fixed sheets eat the dollar sign.
                    if (*data == QChar('$', 0)) {
                        ++data;
                    }
                    pos = data;
                    break;
                default:
                    const QChar *operatorStart = data;
                    if (!Formula::parseOperator(data, out)) {
                        *out++ = *data++;
                    } else if (*operatorStart == QChar('=', 0) && data - operatorStart == 1) { // only one =
                        *out++ = QChar('=', 0);
                    }
                    break;
                }
            }
        } break;
        case InNumber:
            if (data->isDigit()) {
                *out++ = *data++;
            } else if (*data == QChar('.', 0)) {
                for (const QChar &decimalChar: decimal) {
                    *out++ = decimalChar;
                }
                ++data;
            } else if (*data == QChar('E', 0) || *data == QChar('e', 0)) {
                *out++ = QChar('E', 0);
                ++data;
            } else {
                state = Start;
            }

            break;
        case InString:
            if (*data == QChar('"', 0)) {
                state = Start;
            }
            *out++ = *data++;
            break;
        case InIdentifier: {
            if (Formula::isIdentifier(*data) || data->isDigit()) {
                *out++ = *data++;
            } else {
                state = Start;
            }
        } break;
        case InReference:
            switch (data->unicode()) {
            case ']':
                Odf::loadRegion(pos, data, out);
                pos = data;
                state = Start;
                break;
            case '\'':
                state = InSheetName;
                break;
            default:
                break;
            }
            ++data;
            break;
        case InSheetName:
            if (*data == QChar('\'', 0)) {
                ++data;
                if (!data->isNull() && *data == QChar('\'', 0)) {
                    ++data;
                } else {
                    state = InReference;
                }
            } else {
                ++data;
            }
            break;
        }
    }
    result.resize(out - outStart);
    return result;
}

QString Odf::encodeFormula(const QString &expr, const Localization *locale)
{
    // use locale settings
    const QString decimal = locale ? locale->decimalSymbol() : ".";

    QString result('=');

    Formula formula;
    Tokens tokens = formula.scan(expr, locale);

    if (!tokens.valid() || tokens.size() == 0)
        return expr; // no altering on error

    for (int i = 0; i < tokens.size(); ++i) {
        const QString tokenText = tokens[i].text();
        const Token::Type type = tokens[i].type();

        switch (type) {
        case Token::Cell:
        case Token::Range: {
            result.append('[');
            // FIXME Stefan: Hack to get the apostrophes right. Fix and remove!
            const int pos = tokenText.lastIndexOf('!');
            if (pos != -1 && QStringView{tokenText}.left(pos).contains(' '))
                result.append(Odf::saveRegion('\'' + tokenText.left(pos) + '\'' + tokenText.mid(pos)));
            else
                result.append(Odf::saveRegion(tokenText));
            result.append(']');
            break;
        }
        case Token::Float: {
            QString tmp(tokenText);
            result.append(tmp.replace(decimal, "."));
            break;
        }
        case Token::Operator: {
            if (tokens[i].asOperator() == Token::Equal)
                result.append('=');
            else
                result.append(tokenText);
            break;
        }
        case Token::Identifier: {
            if (tokenText == "ERRORTYPE") {
                // need to replace this
                result.append("ERROR.TYPE");
            } else if (tokenText == "LEGACYNORMSDIST") {
                result.append("LEGACY.NORMSDIST");
            } else if (tokenText == "LEGACYNORMSINV") {
                result.append("LEGACY.NORMSINV");
            } else {
                // dump it out unchanged
                result.append(tokenText);
            }
            break;
        }
        case Token::Boolean:
        case Token::Integer:
        case Token::String:
        default:
            result.append(tokenText);
            break;
        }
    }
    return result;
}

QString Odf::convertMSOOXMLFormula(const QString &formula)
{
    if (formula.isEmpty())
        return QString();
    enum {
        InStart,
        InArguments,
        InParenthesizedArgument,
        InString,
        InSheetOrAreaName,
        InCellReference
    } state;
    state = InStart;
    int cellReferenceStart = 0;
    int sheetOrAreaNameDelimiterCount = 0;
    QString result = formula.startsWith('=') ? formula : '=' + formula;
    for (int i = 1; i < result.length(); ++i) {
        QChar ch = result[i];
        switch (state) {
        case InStart:
            if (ch == '(')
                state = InArguments;
            break;
        case InArguments:
            if (ch == '"')
                state = InString;
            else if (ch.unicode() == '\'') {
                sheetOrAreaNameDelimiterCount = 1;
                for (int j = i + 1; j < result.length(); ++j) {
                    if (result[j].unicode() != '\'')
                        break;
                    ++sheetOrAreaNameDelimiterCount;
                }
                if (sheetOrAreaNameDelimiterCount >= 2)
                    result.remove(i + 1, sheetOrAreaNameDelimiterCount - 1);
                state = InSheetOrAreaName;
            } else if (Util::isCellnameCharacter(ch)) {
                state = InCellReference;
                cellReferenceStart = i;
            } else if (ch == ',')
                result[i] = ';'; // replace argument delimiter
            else if (ch == '(' && !result[i - 1].isLetterOrNumber())
                state = InParenthesizedArgument;
            else if (ch == ' ') {
                // check if it might be an intersection operator
                // for it to be an intersection operator the next non-space char must be a cell-name-character or '
                // and previous converted char cannot be ';'
                int firstNonSpace = i + 1;
                while (firstNonSpace < result.length() && result[firstNonSpace] == ' ') {
                    firstNonSpace++;
                }
                bool wasDelimeter = (i - 1 > 0) && (result[i - 1] == ';');
                bool isIntersection = !wasDelimeter && firstNonSpace < result.length()
                    && (result[firstNonSpace].isLetter() || result[firstNonSpace] == '$' || result[firstNonSpace] == '\'');
                if (isIntersection) {
                    result[i] = '!';
                    i = firstNonSpace - 1;
                }
            }
            break;
        case InParenthesizedArgument:
            if (ch == ',')
                result[i] = '~'; // union operator
            else if (ch == ' ')
                result[i] = '!'; // intersection operator
            else if (ch == ')')
                state = InArguments;
            break;
        case InString:
            if (ch == '"')
                state = InArguments;
            break;
        case InSheetOrAreaName:
            Q_ASSERT(i >= 1);
            if (ch == '\'' && result[i - 1].unicode() != '\\') {
                int count = 1;
                for (int j = i + 1; count < sheetOrAreaNameDelimiterCount && j < result.length(); ++j) {
                    if (result[j].unicode() != '\'')
                        break;
                    ++count;
                }
                if (count == sheetOrAreaNameDelimiterCount) {
                    if (sheetOrAreaNameDelimiterCount >= 2)
                        result.remove(i + 1, sheetOrAreaNameDelimiterCount - 1);
                    state = InArguments;
                } else {
                    result.insert(i, '\'');
                    ++i;
                }
            }
            break;
        case InCellReference:
            if (!Util::isCellnameCharacter(ch)) {
                if (ch != '(') /* skip formula-names */ {
                    // Excel is able to use only the column-name to define a column
                    // where all rows are selected. Since that is not supported in
                    // ODF we add to such definitions the minimum/maximum row-number.
                    // So, something like "A:B" would become "A$1:B$65536". Note that
                    // such whole column-definitions are only allowed for ranges like
                    // "A:B" but not for single column definitions like "A" or "B".
                    const QString ref = result.mid(qMax(0, cellReferenceStart - 1), i - cellReferenceStart + 2);
                    QRegularExpression rxStart(".*(|\\$)[A-Za-z]+\\:");
                    QRegularExpression rxEnd("\\:(|\\$)[A-Za-z]+(|(|\\$)[0-9]+).*");
                    QRegularExpressionMatch match = rxEnd.match(ref);
                    if (match.hasMatch() && match.captured(2).isEmpty()) {
                        result.insert(i, "$65536");
                        i += 6;
                    } else if (rxStart.match(ref).hasMatch()) {
                        result.insert(i, "$1");
                        i += 2;
                    }
                }
                state = InArguments;
                --i; // decrement again to handle the current char in the InArguments-switch.
            }
            break;
        };
    };
    return result;
}


} // namespace Calligra::Sheets
