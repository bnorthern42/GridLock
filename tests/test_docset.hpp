/**
 * @file test_docset.hpp
 * @brief Unit tests for Dash/Zeal docset indexing and keyword search.
 *
 * Verifies SQLite index exploration, symbol query matching, and HTML documentation
 * path resolution via DocsetManager.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestDocset
 * @brief Test suite validating local SQLite docset search capabilities.
 */
class TestDocset : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies docset SQLite schema parsing and symbol keyword search.
     */
    void testSearch();
};
