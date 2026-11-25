/**
 * @file PluginManifest.cpp
 * @brief Implementation of `plugins.toml` parsing and user-global `plugins-lock.toml` lockfile persistence.
 */

#include "PluginManifest.hpp"

#include "PluginInstaller.hpp"

#include <sstream>
#include <string>
#include <string_view>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <toml++/toml.h>

#include "../managers/ConfigManager.hpp"

namespace gridlock::core::plugins {

namespace {

bool isHexCommitRef(const QString& ref) {
  static const QRegularExpression kHexShaRe(QStringLiteral("^[0-9a-fA-F]{7,40}$"));
  return kHexShaRe.match(ref).hasMatch();
}

bool writeTextAtomically(const QString& filePath, const QByteArray& utf8Bytes, QString* errorOut) {
  const QFileInfo fi(filePath);
  if (!QDir().mkpath(fi.absolutePath())) {
    if (errorOut) *errorOut = QStringLiteral("Failed to create directory: %1").arg(fi.absolutePath());
    return false;
  }
  QSaveFile out(filePath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (errorOut) *errorOut = QStringLiteral("Failed to open %1 for writing.").arg(filePath);
    return false;
  }
  if (out.write(utf8Bytes) != utf8Bytes.size() || !out.commit()) {
    if (errorOut) *errorOut = QStringLiteral("Failed to commit atomic write to %1.").arg(filePath);
    return false;
  }
  return true;
}

QByteArray serializeTomlTable(const QByteArray& headerComment, const toml::table& root) {
  std::ostringstream oss;
  oss << root;
  QByteArray data = headerComment;
  const std::string body = oss.str();
  if (!body.empty()) {
    data.append(body.data(), static_cast<qsizetype>(body.size()));
    if (!data.endsWith('\n')) data.append('\n');
  }
  return data;
}

}  // namespace

QString PluginManifest::userManifestPath() {
  return QDir(ConfigManager::configDirectory()).filePath(QStringLiteral("plugins.toml"));
}

QString PluginManifest::workspaceManifestPath() {
  const QString ws = ConfigManager::instance().workspacePath().trimmed();
  if (ws.isEmpty()) return QString();
  return QDir(ws).filePath(QStringLiteral(".gridlock/plugins.toml"));
}

QString PluginManifest::userLockFilePath() {
  return QDir(ConfigManager::configDirectory()).filePath(QStringLiteral("plugins-lock.toml"));
}

QString PluginManifest::scopeToString(PluginScope scope) {
  return scope == PluginScope::Workspace ? QStringLiteral("workspace") : QStringLiteral("global");
}

bool PluginManifest::validateBuildMode(const QString& rawBuild, QString* normalizedOut, QString* errorOut) {
  const QString mode = rawBuild.trimmed().isEmpty() ? QStringLiteral("auto") : rawBuild.trimmed().toLower();
  if (mode != QLatin1String("auto") && mode != QLatin1String("meson") && mode != QLatin1String("cmake") &&
      mode != QLatin1String("make") && mode != QLatin1String("single")) {
    if (errorOut) {
      *errorOut = QStringLiteral("Unsupported plugin build mode '%1' (expected auto, meson, cmake, make, or single).")
                      .arg(rawBuild);
    }
    return false;
  }
  if (normalizedOut) *normalizedOut = mode;
  return true;
}

bool PluginManifest::parseManifestToml(const QString& tomlContent,
                                       QVector<PluginManifestEntry>* outEntries,
                                       QString* errorOut) {
  if (outEntries == nullptr) return false;
  outEntries->clear();
  if (tomlContent.trimmed().isEmpty()) return true;

  toml::table tbl;
  try {
    const QByteArray utf8 = tomlContent.toUtf8();
    tbl = toml::parse(std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())));
  } catch (const toml::parse_error& err) {
    if (errorOut) *errorOut = QStringLiteral("Invalid TOML in plugins.toml: %1").arg(QString::fromUtf8(err.description().data(), static_cast<qsizetype>(err.description().size())));
    return false;
  }

  const toml::node* pluginsNode = tbl.get("plugins");
  if (pluginsNode == nullptr) return true;
  const toml::array* arr = pluginsNode->as_array();
  if (arr == nullptr) {
    if (errorOut) *errorOut = QStringLiteral("Expected [[plugins]] array-of-tables in plugins.toml.");
    return false;
  }

  int idx = 0;
  for (const toml::node& elem : *arr) {
    ++idx;
    const toml::table* item = elem.as_table();
    if (item == nullptr) {
      if (errorOut) *errorOut = QStringLiteral("[[plugins]] entry #%1 must be a table.").arg(idx);
      return false;
    }

    const auto repoOpt = (*item)["repo"].value<std::string>();
    if (!repoOpt.has_value() || repoOpt->empty()) {
      if (errorOut) *errorOut = QStringLiteral("[[plugins]] entry #%1 is missing required string 'repo'.").arg(idx);
      return false;
    }

    PluginManifestEntry parsed;
    parsed.repo = QString::fromStdString(*repoOpt).trimmed();
    QString err;
    if (!PluginInstaller::normalizeAndValidateUrl(parsed.repo, &parsed.normalizedUrl, &parsed.repoSlug, &err)) {
      if (errorOut) *errorOut = QStringLiteral("[[plugins]] entry #%1 ('%2'): %3").arg(idx).arg(parsed.repo, err);
      return false;
    }

    if (const toml::node* refNode = item->get("ref")) {
      const auto refOpt = refNode->value<std::string>();
      if (!refOpt.has_value()) {
        if (errorOut) *errorOut = QStringLiteral("[[plugins]] entry #%1 field 'ref' must be a string.").arg(idx);
        return false;
      }
      parsed.ref = QString::fromStdString(*refOpt).trimmed();
      if (!PluginInstaller::validateGitRef(parsed.ref, &err)) {
        if (errorOut) *errorOut = QStringLiteral("[[plugins]] entry #%1 ('%2'): %3").arg(idx).arg(parsed.repo, err);
        return false;
      }
    }

    if (const toml::node* buildNode = item->get("build")) {
      const auto buildOpt = buildNode->value<std::string>();
      if (!buildOpt.has_value()) {
        if (errorOut) *errorOut = QStringLiteral("[[plugins]] entry #%1 field 'build' must be a string.").arg(idx);
        return false;
      }
      if (!validateBuildMode(QString::fromStdString(*buildOpt), &parsed.build, &err)) {
        if (errorOut) *errorOut = QStringLiteral("[[plugins]] entry #%1 ('%2'): %3").arg(idx).arg(parsed.repo, err);
        return false;
      }
    }

    if (const toml::node* enNode = item->get("enabled")) {
      const auto enOpt = enNode->value<bool>();
      if (!enOpt.has_value()) {
        if (errorOut) *errorOut = QStringLiteral("[[plugins]] entry #%1 field 'enabled' must be a boolean.").arg(idx);
        return false;
      }
      parsed.enabled = *enOpt;
    }

    outEntries->append(parsed);
  }
  return true;
}

bool PluginManifest::loadManifestFile(const QString& filePath,
                                      QVector<PluginManifestEntry>* outEntries,
                                      QString* errorOut) {
  if (outEntries == nullptr) return false;
  outEntries->clear();
  if (filePath.trimmed().isEmpty() || !QFileInfo::exists(filePath)) return true;

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    if (errorOut) *errorOut = QStringLiteral("Failed to read manifest file: %1").arg(filePath);
    return false;
  }
  return parseManifestToml(QString::fromUtf8(file.readAll()), outEntries, errorOut);
}

bool PluginManifest::ensureManifestTemplate(const QString& filePath, QString* errorOut) {
  if (filePath.trimmed().isEmpty()) {
    if (errorOut) *errorOut = QStringLiteral("Manifest path is empty.");
    return false;
  }
  if (QFileInfo::exists(filePath)) return true;

  const QByteArray starter = QByteArrayLiteral(
      "# GridLock Declarative Plugin Manifest (plugins.toml)\n"
      "# Repositories tagged with GitHub topic 'gridlock-plugin' can be declared using\n"
      "# compact 'owner/repo' shorthand or full GitHub HTTPS/SSH URLs.\n"
      "#\n"
      "# [[plugins]]\n"
      "# repo = \"bnorthern42/gridlock-stl-inspector\"\n"
      "# ref = \"main\"          # optional branch, tag, or commit SHA\n"
      "# build = \"auto\"        # optional: \"auto\" | \"meson\" | \"cmake\" | \"make\" | \"single\"\n"
      "# enabled = true\n");
  return writeTextAtomically(filePath, starter, errorOut);
}

bool PluginManifest::upsertManifestEntry(const QString& filePath,
                                         const PluginManifestEntry& entry,
                                         QString* errorOut) {
  QString normUrl;
  QString slug;
  if (!PluginInstaller::normalizeAndValidateUrl(entry.repo, &normUrl, &slug, errorOut)) return false;
  if (!PluginInstaller::validateGitRef(entry.ref, errorOut)) return false;
  QString normBuild;
  if (!validateBuildMode(entry.build, &normBuild, errorOut)) return false;

  QVector<PluginManifestEntry> entries;
  if (QFileInfo::exists(filePath)) {
    if (!loadManifestFile(filePath, &entries, errorOut)) return false;
  }

  PluginManifestEntry clean = entry;
  clean.normalizedUrl = normUrl;
  clean.repoSlug = slug;
  clean.build = normBuild;
  if (clean.repo.trimmed().isEmpty()) clean.repo = normUrl;

  bool updated = false;
  for (PluginManifestEntry& existing : entries) {
    if (existing.normalizedUrl == normUrl) {
      existing.repo = clean.repo;
      existing.ref = clean.ref;
      existing.build = clean.build;
      existing.enabled = clean.enabled;
      updated = true;
      break;
    }
  }
  if (!updated) entries.append(clean);

  toml::array arr;
  for (const PluginManifestEntry& item : entries) {
    toml::table t;
    t.insert("repo", item.repo.toStdString());
    if (!item.ref.isEmpty()) t.insert("ref", item.ref.toStdString());
    t.insert("build", item.build.toStdString());
    t.insert("enabled", item.enabled);
    arr.push_back(std::move(t));
  }
  toml::table root;
  root.insert("plugins", std::move(arr));

  const QByteArray header = QByteArrayLiteral("# GridLock Declarative Plugin Manifest (plugins.toml)\n\n");
  return writeTextAtomically(filePath, serializeTomlTable(header, root), errorOut);
}

bool PluginManifest::loadLockFile(const QString& lockFilePath,
                                  QVector<PluginLockEntry>* outEntries,
                                  QString* errorOut) {
  if (outEntries == nullptr) return false;
  outEntries->clear();
  if (lockFilePath.trimmed().isEmpty() || !QFileInfo::exists(lockFilePath)) return true;

  QFile file(lockFilePath);
  if (!file.open(QIODevice::ReadOnly)) {
    if (errorOut) *errorOut = QStringLiteral("Failed to read lockfile: %1").arg(lockFilePath);
    return false;
  }

  toml::table tbl;
  try {
    const QByteArray utf8 = file.readAll();
    if (utf8.trimmed().isEmpty()) return true;
    tbl = toml::parse(std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())));
  } catch (const toml::parse_error& err) {
    if (errorOut) *errorOut = QStringLiteral("Invalid TOML in plugins-lock.toml: %1").arg(QString::fromUtf8(err.description().data(), static_cast<qsizetype>(err.description().size())));
    return false;
  }

  const toml::node* pluginsNode = tbl.get("plugins");
  if (pluginsNode == nullptr) return true;
  const toml::array* arr = pluginsNode->as_array();
  if (arr == nullptr) return true;

  for (const toml::node& elem : *arr) {
    const toml::table* item = elem.as_table();
    if (item == nullptr) continue;
    PluginLockEntry lock;
    lock.repo = QString::fromStdString((*item)["repo"].value_or(std::string{})).trimmed();
    lock.scope = QString::fromStdString((*item)["scope"].value_or(std::string{"global"})).trimmed().toLower();
    lock.ref = QString::fromStdString((*item)["ref"].value_or(std::string{})).trimmed();
    lock.commitSha = QString::fromStdString((*item)["commit_sha"].value_or(std::string{})).trimmed().toLower();
    lock.binaryName = QString::fromStdString((*item)["binary_name"].value_or(std::string{})).trimmed();
    lock.binarySha256 = QString::fromStdString((*item)["binary_sha256"].value_or(std::string{})).trimmed().toLower();
    if (!lock.repo.isEmpty() && !lock.binaryName.isEmpty() && !lock.binarySha256.isEmpty()) {
      outEntries->append(lock);
    }
  }
  return true;
}

bool PluginManifest::upsertLockEntry(const QString& lockFilePath,
                                     const PluginLockEntry& entry,
                                     QString* errorOut) {
  if (entry.repo.trimmed().isEmpty() || entry.binaryName.trimmed().isEmpty() ||
      entry.binarySha256.trimmed().isEmpty()) {
    if (errorOut) *errorOut = QStringLiteral("Incomplete lockfile entry (repo, binary_name, and binary_sha256 required).");
    return false;
  }

  QVector<PluginLockEntry> entries;
  if (QFileInfo::exists(lockFilePath)) {
    (void)loadLockFile(lockFilePath, &entries, nullptr);
  }

  const QString normScope = entry.scope.trimmed().isEmpty() ? QStringLiteral("global") : entry.scope.trimmed().toLower();
  PluginLockEntry clean = entry;
  clean.scope = normScope;
  clean.commitSha = entry.commitSha.trimmed().toLower();
  clean.binarySha256 = entry.binarySha256.trimmed().toLower();

  bool updated = false;
  for (PluginLockEntry& existing : entries) {
    if (existing.repo == clean.repo && existing.scope == clean.scope) {
      existing = clean;
      updated = true;
      break;
    }
  }
  if (!updated) entries.append(clean);

  toml::array arr;
  for (const PluginLockEntry& item : entries) {
    toml::table t;
    t.insert("repo", item.repo.toStdString());
    t.insert("scope", item.scope.toStdString());
    if (!item.ref.isEmpty()) t.insert("ref", item.ref.toStdString());
    t.insert("commit_sha", item.commitSha.toStdString());
    t.insert("binary_name", item.binaryName.toStdString());
    t.insert("binary_sha256", item.binarySha256.toStdString());
    arr.push_back(std::move(t));
  }
  toml::table root;
  root.insert("plugins", std::move(arr));

  const QByteArray header =
      QByteArrayLiteral("# GridLock Plugin Lockfile (auto-generated in user-global config; do not edit manually)\n\n");
  return writeTextAtomically(lockFilePath, serializeTomlTable(header, root), errorOut);
}

QString PluginManifest::computeFileSha256Hex(const QString& filePath) {
  const QFileInfo fi(filePath);
  if (!fi.exists() || !fi.isFile() || fi.isSymLink()) return QString();
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) return QString();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&f)) return QString();
  return QString::fromLatin1(hash.result().toHex()).toLower();
}

QString PluginManifest::queryLocalHeadSha(const QString& repoDir) {
  if (repoDir.trimmed().isEmpty() || !QDir(repoDir).exists()) return QString();
  QProcess proc;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
  proc.setProcessEnvironment(env);
  proc.setWorkingDirectory(repoDir);
  proc.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
  if (!proc.waitForFinished(5000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    return QString();
  }
  const QString sha = QString::fromUtf8(proc.readAllStandardOutput()).trimmed().toLower();
  return sha.size() == 40 ? sha : QString();
}

QString PluginManifest::queryRemoteRefSha(const QString& normalizedUrl, const QString& gitRef) {
  if (normalizedUrl.trimmed().isEmpty()) return QString();
  const QString target = gitRef.trimmed().isEmpty() ? QStringLiteral("HEAD") : gitRef.trimmed();
  QProcess proc;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
  proc.setProcessEnvironment(env);
  proc.start(QStringLiteral("git"), {QStringLiteral("ls-remote"), QStringLiteral("--"), normalizedUrl, target});
  if (!proc.waitForFinished(8000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    return QString();
  }
  const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
  const QString firstLine = out.section(QLatin1Char('\n'), 0, 0).trimmed();
  const QString sha = firstLine.section(QLatin1Char('\t'), 0, 0).trimmed().toLower();
  return sha.size() == 40 ? sha : QString();
}

bool PluginManifest::isManifestEntryUpToDate(const PluginManifestEntry& entry,
                                             PluginScope scope,
                                             const QString& installDir,
                                             const QVector<PluginLockEntry>& lockEntries,
                                             QString* outCanonBinaryPath,
                                             QString* outBinarySha256) {
  const QString wantedScope = scopeToString(scope);
  const PluginLockEntry* matched = nullptr;
  for (const PluginLockEntry& cand : lockEntries) {
    if (cand.repo == entry.normalizedUrl && cand.scope == wantedScope) {
      matched = &cand;
      break;
    }
  }
  if (matched == nullptr || matched->commitSha.isEmpty() || matched->binaryName.isEmpty() ||
      matched->binarySha256.isEmpty()) {
    return false;
  }
  if (matched->binaryName.contains(QLatin1Char('/')) || matched->binaryName.contains(QLatin1Char('\\')) ||
      matched->binaryName == QLatin1String("..")) {
    return false;
  }

  const QFileInfo binFi(QDir(installDir).filePath(matched->binaryName));
  if (!binFi.exists() || !binFi.isFile() || binFi.isSymLink()) return false;
  const QString canonBin = !binFi.canonicalFilePath().isEmpty() ? binFi.canonicalFilePath() : binFi.absoluteFilePath();
  const QString actualSha = computeFileSha256Hex(canonBin);
  if (actualSha.isEmpty() || actualSha != matched->binarySha256) return false;

  if (isHexCommitRef(entry.ref)) {
    if (!matched->commitSha.startsWith(entry.ref.toLower())) return false;
  } else {
    if (entry.ref != matched->ref) return false;
    const QString remoteSha = queryRemoteRefSha(entry.normalizedUrl, entry.ref);
    if (!remoteSha.isEmpty() && remoteSha != matched->commitSha) return false;
  }

  if (outCanonBinaryPath) *outCanonBinaryPath = canonBin;
  if (outBinarySha256) *outBinarySha256 = actualSha;
  return true;
}

}  // namespace gridlock::core::plugins
