/**
 * @file GdbMiParser.hpp
 * @brief Quote- and escape-aware balanced-delimiter scanner for GDB/MI3 tuples, lists, and fields.
 *
 * Provides linear-time, bounds-safe helpers for extracting nested `{...}` tuples,
 * `[...]` lists, and top-level `key="value"` fields from GDB/MI3 records without
 * flat-brace regular expressions that break on pretty-printed containers or multi-arg frames.
 */

#pragma once

#include <QHash>
#include <QString>
#include <QVector>

namespace gridlock::mi {

/**
 * @brief Unescapes a GDB/MI C-style quoted string body in a single linear pass.
 */
inline QString unescapeMiString(const QString& raw) {
  QString out;
  out.reserve(raw.size());
  for (int i = 0; i < raw.size(); ++i) {
    const QChar ch = raw.at(i);
    if (ch == QLatin1Char('\\') && i + 1 < raw.size()) {
      const QChar next = raw.at(++i);
      switch (next.unicode()) {
        case 'n': out.append(QLatin1Char('\n')); break;
        case 't': out.append(QLatin1Char('\t')); break;
        case 'r': out.append(QLatin1Char('\r')); break;
        case '"': out.append(QLatin1Char('"')); break;
        case '\\': out.append(QLatin1Char('\\')); break;
        default: out.append(next); break;
      }
    } else {
      out.append(ch);
    }
  }
  return out;
}

/**
 * @brief Finds @p needle in @p text strictly outside double-quoted string literals.
 * @return Starting character index of @p needle, or `-1` if not found.
 */
inline int findOutsideQuotes(const QString& text, const QString& needle, int from = 0) {
  if (needle.isEmpty() || from < 0 || from >= text.size()) return -1;
  bool inQuotes = false;
  bool escaped = false;
  const int limit = text.size() - needle.size();
  for (int i = 0; i < text.size(); ++i) {
    const QChar ch = text.at(i);
    if (inQuotes) {
      if (escaped) { escaped = false; continue; }
      if (ch == QLatin1Char('\\')) escaped = true;
      else if (ch == QLatin1Char('"')) inQuotes = false;
      continue;
    }
    if (ch == QLatin1Char('"')) {
      inQuotes = true;
      continue;
    }
    if (i >= from && i <= limit && QStringView(text).mid(i, needle.size()) == QStringView(needle)) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Extracts the interior between matching @p openCh at @p openPos and @p closeCh,
 *        ignoring delimiters inside double-quoted strings with backslash escapes.
 *
 * Returns an empty string and sets @p *outFound = `false` if @p openPos is invalid
 * or if the delimiter is unclosed (terminating safely in linear time).
 */
inline QString extractBalancedInterior(const QString& text,
                                       int openPos,
                                       QChar openCh,
                                       QChar closeCh,
                                       bool* outFound = nullptr,
                                       int* outClosePos = nullptr) {
  if (outFound) *outFound = false;
  if (outClosePos) *outClosePos = -1;
  if (openPos < 0 || openPos >= text.size() || text.at(openPos) != openCh) return QString();

  bool inQuotes = false;
  bool escaped = false;
  int depth = 1;
  for (int i = openPos + 1; i < text.size(); ++i) {
    const QChar ch = text.at(i);
    if (inQuotes) {
      if (escaped) { escaped = false; continue; }
      if (ch == QLatin1Char('\\')) escaped = true;
      else if (ch == QLatin1Char('"')) inQuotes = false;
      continue;
    }
    if (ch == QLatin1Char('"')) {
      inQuotes = true;
    } else if (ch == openCh) {
      ++depth;
    } else if (ch == closeCh) {
      if (--depth == 0) {
        if (outFound) *outFound = true;
        if (outClosePos) *outClosePos = i;
        return text.mid(openPos + 1, i - openPos - 1);
      }
    }
  }
  return QString();
}

/**
 * @brief Extracts the balanced `{...}` body immediately following @p keyPrefix (e.g., `"frame={"`).
 */
inline QString extractMiTupleBody(const QString& line,
                                  const QString& keyPrefix,
                                  bool* outFound = nullptr) {
  if (outFound) *outFound = false;
  if (!keyPrefix.endsWith(QLatin1Char('{'))) return QString();
  const int pos = findOutsideQuotes(line, keyPrefix);
  if (pos < 0) return QString();
  const int bracePos = pos + keyPrefix.size() - 1;
  return extractBalancedInterior(line, bracePos, QLatin1Char('{'), QLatin1Char('}'), outFound);
}

/**
 * @brief Extracts the balanced `[...]` body immediately following @p keyPrefix (e.g., `"variables=["`).
 */
inline QString extractMiListBody(const QString& line,
                                 const QString& keyPrefix,
                                 bool* outFound = nullptr) {
  if (outFound) *outFound = false;
  if (!keyPrefix.endsWith(QLatin1Char('['))) return QString();
  const int pos = findOutsideQuotes(line, keyPrefix);
  if (pos < 0) return QString();
  const int bracketPos = pos + keyPrefix.size() - 1;
  return extractBalancedInterior(line, bracketPos, QLatin1Char('['), QLatin1Char(']'), outFound);
}

/**
 * @brief Extracts every complete top-level `{...}` tuple interior from an MI list body.
 *
 * Braces inside double-quoted string literals (`value="{1, 2}"`) and nested tuples
 * are ignored. Unclosed tuples or strings terminate cleanly without out-of-bounds reads.
 */
inline QVector<QString> extractTopLevelMiTuples(const QString& listBody) {
  QVector<QString> tuples;
  bool inQuotes = false;
  bool escaped = false;
  int braceDepth = 0;
  int tupleStart = -1;
  for (int i = 0; i < listBody.size(); ++i) {
    const QChar ch = listBody.at(i);
    if (inQuotes) {
      if (escaped) { escaped = false; continue; }
      if (ch == QLatin1Char('\\')) escaped = true;
      else if (ch == QLatin1Char('"')) inQuotes = false;
      continue;
    }
    if (ch == QLatin1Char('"')) {
      inQuotes = true;
    } else if (ch == QLatin1Char('{')) {
      if (braceDepth == 0) tupleStart = i + 1;
      ++braceDepth;
    } else if (ch == QLatin1Char('}')) {
      if (braceDepth > 0 && --braceDepth == 0 && tupleStart >= 0) {
        tuples.append(listBody.mid(tupleStart, i - tupleStart));
        tupleStart = -1;
      }
    }
  }
  return tuples;
}

/**
 * @brief Parses top-level `key="value"` string fields from a `{...}` MI tuple interior,
 *        skipping nested `{...}` tuples and `[...]` lists without leaking inner keys.
 */
inline QHash<QString, QString> parseMiTupleFields(const QString& tupleBody) {
  QHash<QString, QString> fields;
  const int n = tupleBody.size();
  int i = 0;
  while (i < n) {
    while (i < n && (tupleBody.at(i).isSpace() || tupleBody.at(i) == QLatin1Char(','))) ++i;
    if (i >= n) break;

    const int keyStart = i;
    while (i < n && tupleBody.at(i) != QLatin1Char('=') && tupleBody.at(i) != QLatin1Char(',') &&
           tupleBody.at(i) != QLatin1Char('{') && tupleBody.at(i) != QLatin1Char('[')) {
      ++i;
    }
    if (i >= n || tupleBody.at(i) != QLatin1Char('=')) {
      // Advance past unexpected token or nested block to guarantee linear progress.
      if (i < n && tupleBody.at(i) == QLatin1Char('{')) {
        int closePos = -1;
        extractBalancedInterior(tupleBody, i, QLatin1Char('{'), QLatin1Char('}'), nullptr, &closePos);
        i = (closePos >= 0) ? (closePos + 1) : n;
      } else if (i < n && tupleBody.at(i) == QLatin1Char('[')) {
        int closePos = -1;
        extractBalancedInterior(tupleBody, i, QLatin1Char('['), QLatin1Char(']'), nullptr, &closePos);
        i = (closePos >= 0) ? (closePos + 1) : n;
      } else {
        ++i;
      }
      continue;
    }

    const QString key = tupleBody.mid(keyStart, i - keyStart).trimmed();
    ++i;  // skip '='
    while (i < n && tupleBody.at(i).isSpace()) ++i;
    if (i >= n) break;

    const QChar valStart = tupleBody.at(i);
    if (valStart == QLatin1Char('"')) {
      ++i;  // skip opening '"'
      const int strStart = i;
      bool escaped = false;
      bool closed = false;
      while (i < n) {
        const QChar ch = tupleBody.at(i);
        if (escaped) { escaped = false; ++i; continue; }
        if (ch == QLatin1Char('\\')) { escaped = true; ++i; continue; }
        if (ch == QLatin1Char('"')) { closed = true; break; }
        ++i;
      }
      if (!closed) break;  // Unterminated string literal: stop safely.
      if (!key.isEmpty()) fields.insert(key, unescapeMiString(tupleBody.mid(strStart, i - strStart)));
      ++i;  // skip closing '"'
    } else if (valStart == QLatin1Char('{')) {
      int closePos = -1;
      extractBalancedInterior(tupleBody, i, QLatin1Char('{'), QLatin1Char('}'), nullptr, &closePos);
      i = (closePos >= 0) ? (closePos + 1) : n;
    } else if (valStart == QLatin1Char('[')) {
      int closePos = -1;
      extractBalancedInterior(tupleBody, i, QLatin1Char('['), QLatin1Char(']'), nullptr, &closePos);
      i = (closePos >= 0) ? (closePos + 1) : n;
    } else {
      while (i < n && tupleBody.at(i) != QLatin1Char(',')) ++i;
    }
  }
  return fields;
}

}  // namespace gridlock::mi
