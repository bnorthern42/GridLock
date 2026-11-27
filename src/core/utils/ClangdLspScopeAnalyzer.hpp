/**
 * @file ClangdLspScopeAnalyzer.hpp
 * @brief Lightweight structural C++ struct/STL scope completion analyzer for offline fallback.
 *
 * Extracted from `ClangdLspClient.cpp` to keep translation units modular and strictly
 * under the 800-line ceiling while providing sub-millisecond member and local variable
 * completion candidates before live `clangd` responses arrive.
 */

#pragma once

#include "ClangdLspClient.hpp"

#include <algorithm>

#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gridlock::core::detail {

/**
 * @brief Strips array subscripts (`[i]`) and call parentheses (`()`) from a C++ receiver segment.
 */
inline QString stripSubscriptsAndCalls(const QString& segment) {
  QString clean = segment.trimmed();
  const int bracketIdx = clean.indexOf(QLatin1Char('['));
  if (bracketIdx >= 0) {
    clean = clean.left(bracketIdx).trimmed();
  }
  const int parenIdx = clean.indexOf(QLatin1Char('('));
  if (parenIdx >= 0) {
    clean = clean.left(parenIdx).trimmed();
  }
  return clean;
}

/**
 * @brief Extracts declared identifier names from a single C++ field or variable declaration statement.
 */
inline QStringList parseDeclaredNames(const QString& declStmt, QString* outType = nullptr) {
  QString trimmed = declStmt.trimmed();
  if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) ||
      trimmed.startsWith(QStringLiteral("//")) || trimmed.startsWith(QStringLiteral("return ")) ||
      trimmed.startsWith(QStringLiteral("MPI_")) || trimmed.startsWith(QStringLiteral("printf"))) {
    return {};
  }

  static const QRegularExpression kQualifiersRe(
      QStringLiteral("^(?:(?:const|constexpr|static|volatile|unsigned|signed|struct|class)\\s+)+"));
  trimmed.replace(kQualifiersRe, QString());

  static const QRegularExpression kTypeAndRestRe(
      QStringLiteral("^([A-Za-z_][A-Za-z0-9_:]*(?:\\s*<[^>]+>)?(?:\\s*[\\*&])?)\\s+(.+)$"));
  const QRegularExpressionMatch match = kTypeAndRestRe.match(trimmed);
  if (!match.hasMatch()) {
    return {};
  }

  if (outType) {
    *outType = match.captured(1).trimmed();
  }
  const QString declarators = match.captured(2).trimmed();

  QStringList names;
  int parenDepth = 0;
  int startIdx = 0;
  for (int i = 0; i <= declarators.size(); ++i) {
    const QChar ch = (i < declarators.size()) ? declarators.at(i) : QLatin1Char(',');
    if (ch == QLatin1Char('(') || ch == QLatin1Char('{') || ch == QLatin1Char('<')) {
      ++parenDepth;
    } else if (ch == QLatin1Char(')') || ch == QLatin1Char('}') || ch == QLatin1Char('>')) {
      parenDepth = std::max(0, parenDepth - 1);
    } else if (ch == QLatin1Char(',') && parenDepth == 0) {
      QString part = declarators.mid(startIdx, i - startIdx).trimmed();
      startIdx = i + 1;
      const int eqIdx = part.indexOf(QLatin1Char('='));
      if (eqIdx >= 0) part = part.left(eqIdx).trimmed();
      part = stripSubscriptsAndCalls(part);
      while (part.startsWith(QLatin1Char('*')) || part.startsWith(QLatin1Char('&'))) {
        part.remove(0, 1);
      }
      part = part.trimmed();
      static const QRegularExpression kIdentRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
      if (kIdentRe.match(part).hasMatch()) {
        names.append(part);
      }
    }
  }
  return names;
}

/**
 * @brief Performs fast structural C++ struct/nested-struct/STL scope completion analysis.
 */
inline QVector<ClangdLspClient::LspCompletionItem> analyzeStructuralCompletions(
    const QString& sourceCode,
    int targetLineOneBased,
    const QString& partialExpression) {
  using LspCompletionItem = ClangdLspClient::LspCompletionItem;

  QMap<QString, QStringList> structMembers;
  QMap<QString, QString> memberNestedTypes;
  QMap<QString, QString> varTypes;
  QMap<QString, QVector<LspCompletionItem>> structDetailedMembers;

  const QStringList lines = sourceCode.split(QLatin1Char('\n'));
  QString currentOuterStruct;
  QString currentAnonStructFieldsPending;
  bool inAnonNestedStruct = false;

  for (int idx = 0; idx < lines.size(); ++idx) {
    QString line = lines.at(idx).trimmed();
    const int commentIdx = line.indexOf(QStringLiteral("//"));
    if (commentIdx >= 0) line = line.left(commentIdx).trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('*')) || line.startsWith(QStringLiteral("/*"))) {
      continue;
    }

    static const QRegularExpression kStructStartRe(QStringLiteral("^struct\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\{"));
    const QRegularExpressionMatch structMatch = kStructStartRe.match(line);
    if (structMatch.hasMatch() && currentOuterStruct.isEmpty()) {
      currentOuterStruct = structMatch.captured(1);
      continue;
    }

    if (!currentOuterStruct.isEmpty()) {
      if (line.startsWith(QStringLiteral("struct")) && line.contains(QLatin1Char('{'))) {
        inAnonNestedStruct = true;
        currentAnonStructFieldsPending.clear();
        continue;
      }
      if (inAnonNestedStruct) {
        static const QRegularExpression kAnonEndRe(QStringLiteral("^\\}\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*;"));
        const QRegularExpressionMatch anonEnd = kAnonEndRe.match(line);
        if (anonEnd.hasMatch()) {
          const QString nestedFieldName = anonEnd.captured(1);
          const QString syntheticType = currentOuterStruct + QStringLiteral("::") + nestedFieldName;
          structMembers[currentOuterStruct].append(nestedFieldName);
          memberNestedTypes[currentOuterStruct + QLatin1Char('.') + nestedFieldName] = syntheticType;

          LspCompletionItem anonItem;
          anonItem.label = nestedFieldName;
          anonItem.insertText = nestedFieldName;
          anonItem.kindLabel = QStringLiteral("Field");
          anonItem.detailType = QStringLiteral("struct ") + nestedFieldName;
          structDetailedMembers[currentOuterStruct].append(anonItem);

          for (const QString& stmt : currentAnonStructFieldsPending.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            QString innerType;
            for (const QString& f : parseDeclaredNames(stmt, &innerType)) {
              if (!structMembers[syntheticType].contains(f)) {
                structMembers[syntheticType].append(f);
                LspCompletionItem innerItem;
                innerItem.label = f;
                innerItem.insertText = f;
                innerItem.kindLabel = QStringLiteral("Field");
                innerItem.detailType = innerType.isEmpty() ? QStringLiteral("float") : innerType;
                structDetailedMembers[syntheticType].append(innerItem);
              }
            }
          }
          inAnonNestedStruct = false;
        } else {
          currentAnonStructFieldsPending += QLatin1Char(' ') + line;
        }
        continue;
      }
      if (line.startsWith(QStringLiteral("};"))) {
        currentOuterStruct.clear();
        continue;
      }
      for (const QString& stmt : line.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        QString fieldType;
        for (const QString& f : parseDeclaredNames(stmt, &fieldType)) {
          if (!structMembers[currentOuterStruct].contains(f)) {
            structMembers[currentOuterStruct].append(f);
          }
          QString effectiveType = fieldType;
          if (stmt.contains(f + QStringLiteral("["))) {
            const int lb = stmt.indexOf(f + QStringLiteral("["));
            const int rb = stmt.indexOf(QLatin1Char(']'), lb);
            if (rb > lb) {
              effectiveType = fieldType + stmt.mid(lb + f.length(), rb - (lb + f.length()) + 1);
            }
          }
          if (effectiveType.isEmpty()) effectiveType = QStringLiteral("int");
          memberNestedTypes[currentOuterStruct + QLatin1Char('.') + f] = effectiveType;

          LspCompletionItem item;
          item.label = f;
          item.insertText = f;
          item.kindLabel = QStringLiteral("Field");
          item.detailType = effectiveType;
          structDetailedMembers[currentOuterStruct].append(item);
        }
      }
      continue;
    }

    if (idx < targetLineOneBased && idx >= std::max(0, targetLineOneBased - 200)) {
      static const QRegularExpression kForLoopRe(QStringLiteral("for\\s*\\(([^;]+);"));
      const QRegularExpressionMatch forMatch = kForLoopRe.match(line);
      if (forMatch.hasMatch()) {
        QString loopType;
        for (const QString& v : parseDeclaredNames(forMatch.captured(1), &loopType)) {
          varTypes[v] = loopType.isEmpty() ? QStringLiteral("int") : loopType;
        }
      }
      for (const QString& stmt : line.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        QString declType;
        for (const QString& v : parseDeclaredNames(stmt, &declType)) {
          varTypes[v] = declType.isEmpty() ? QStringLiteral("int") : declType;
        }
      }
    }
  }

  const QString trimmedExpr = partialExpression.trimmed();
  const int lastDot = trimmedExpr.lastIndexOf(QLatin1Char('.'));
  const int lastArrow = trimmedExpr.lastIndexOf(QStringLiteral("->"));
  const int sepPos = std::max(lastDot, (lastArrow >= 0) ? (lastArrow + 1) : -1);

  QVector<LspCompletionItem> candidates;
  QString memberPrefix;

  if (sepPos >= 0) {
    const int receiverEnd = (lastArrow > lastDot) ? lastArrow : lastDot;
    QString receiverChain = trimmedExpr.left(receiverEnd).trimmed();
    memberPrefix = trimmedExpr.mid(sepPos + 1).trimmed();

    static const QRegularExpression kTrailingChainRe(QStringLiteral("([A-Za-z_][A-Za-z0-9_\\[\\]\\.\\->]*)$"));
    const QRegularExpressionMatch chainMatch = kTrailingChainRe.match(receiverChain);
    if (chainMatch.hasMatch()) {
      receiverChain = chainMatch.captured(1);
    }

    QString normChain = receiverChain;
    normChain.replace(QStringLiteral("->"), QStringLiteral("."));
    const QStringList segments = normChain.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (!segments.isEmpty()) {
      const QString rootVar = stripSubscriptsAndCalls(segments.first());
      QString activeType = varTypes.value(rootVar);
      for (int s = 1; s < segments.size() && !activeType.isEmpty(); ++s) {
        const QString fieldName = stripSubscriptsAndCalls(segments.at(s));
        activeType = memberNestedTypes.value(activeType + QLatin1Char('.') + fieldName);
      }

      if (structDetailedMembers.contains(activeType)) {
        candidates = structDetailedMembers.value(activeType);
      } else if (activeType.contains(QStringLiteral("vector"))) {
        candidates = {
            {QStringLiteral("data()"), QStringLiteral("data()"), QStringLiteral("Method"), QStringLiteral("int*")},
            {QStringLiteral("size()"), QStringLiteral("size()"), QStringLiteral("Method"), QStringLiteral("size_t")},
            {QStringLiteral("empty()"), QStringLiteral("empty()"), QStringLiteral("Method"), QStringLiteral("bool")},
            {QStringLiteral("front()"), QStringLiteral("front()"), QStringLiteral("Method"), QStringLiteral("int&")},
            {QStringLiteral("back()"), QStringLiteral("back()"), QStringLiteral("Method"), QStringLiteral("int&")},
            {QStringLiteral("at()"), QStringLiteral("at()"), QStringLiteral("Method"), QStringLiteral("int&")},
            {QStringLiteral("capacity()"), QStringLiteral("capacity()"), QStringLiteral("Method"), QStringLiteral("size_t")},
            {QStringLiteral("clear()"), QStringLiteral("clear()"), QStringLiteral("Method"), QStringLiteral("void")}
        };
      } else if (activeType.contains(QStringLiteral("string"))) {
        candidates = {
            {QStringLiteral("c_str()"), QStringLiteral("c_str()"), QStringLiteral("Method"), QStringLiteral("const char*")},
            {QStringLiteral("data()"), QStringLiteral("data()"), QStringLiteral("Method"), QStringLiteral("const char*")},
            {QStringLiteral("size()"), QStringLiteral("size()"), QStringLiteral("Method"), QStringLiteral("size_t")},
            {QStringLiteral("length()"), QStringLiteral("length()"), QStringLiteral("Method"), QStringLiteral("size_t")},
            {QStringLiteral("empty()"), QStringLiteral("empty()"), QStringLiteral("Method"), QStringLiteral("bool")},
            {QStringLiteral("substr()"), QStringLiteral("substr()"), QStringLiteral("Method"), QStringLiteral("std::string")}
        };
      }
    }
  } else {
    memberPrefix = trimmedExpr;
    QStringList locals = varTypes.keys();
    locals.sort();
    for (const QString& v : locals) {
      candidates.append({v, v, QStringLiteral("Variable"), varTypes.value(v, QStringLiteral("int"))});
    }
  }

  QVector<LspCompletionItem> filtered;
  for (const auto& c : candidates) {
    if (memberPrefix.isEmpty() || c.label.startsWith(memberPrefix, Qt::CaseInsensitive)) {
      filtered.append(c);
    }
  }
  return filtered;
}

}  // namespace gridlock::core::detail
