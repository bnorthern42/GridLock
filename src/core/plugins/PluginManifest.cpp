/**
 * @file PluginManifest.cpp
 * @brief Multi-forge Git URL normalization, `plugins.toml` parsing, and user-global `plugins-lock.toml` persistence.
 */

#include "PluginManifest.hpp"

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

bool hasWhitespaceOrControl(const QString& s) {
  for (const QChar c : s) {
    if (c.isSpace() || c == QChar('\0') || c.category() == QChar::Other_Control ||
        c.category() == QChar::Other_Format) {
      return true;
    }
  }
  return false;
}

bool isHexCommitRef(const QString& ref) {
  static const QRegularExpression kHexShaRe(QStringLiteral("^[0-9a-fA-F]{7,40}$"));
  return kHexShaRe.match(ref).hasMatch();
}

bool validateHostAuthority(const QString& rawAuthority, bool allowPort, QString* cleanOut) {
  if (rawAuthority.isEmpty() || rawAuthority.contains(QLatin1Char('@')) ||
      rawAuthority.contains(QStringLiteral(".."))) {
    return false;
  }
  QString host = rawAuthority;
  QString portSuffix;
  if (const qsizetype colon = rawAuthority.indexOf(QLatin1Char(':')); colon >= 0) {
    if (!allowPort) return false;
    host = rawAuthority.left(colon);
    const QString portStr = rawAuthority.mid(colon + 1);
    bool ok = false;
    const int port = portStr.toInt(&ok);
    if (!ok || port < 1 || port > 65535 || portStr.startsWith(QLatin1Char('0'))) return false;
    portSuffix = QLatin1Char(':') + portStr;
  }
  if (host.isEmpty() || host.startsWith(QLatin1Char('-')) || host.endsWith(QLatin1Char('-')) ||
      host.startsWith(QLatin1Char('.')) || host.endsWith(QLatin1Char('.'))) {
    return false;
  }
  static const QRegularExpression kHostRe(QStringLiteral("^[A-Za-z0-9.-]+$"));
  if (!kHostRe.match(host).hasMatch()) return false;
  if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) != 0 && !host.contains(QLatin1Char('.'))) {
    return false;
  }
  if (cleanOut) *cleanOut = host.toLower() + portSuffix;
  return true;
}

bool validateRepoSegments(QString rawPath, int minSegs, int maxSegs, QString* joinedOut, QString* slugOut) {
  if (rawPath.endsWith(QLatin1Char('/'))) rawPath.chop(1);
  if (rawPath.isEmpty() || rawPath.startsWith(QLatin1Char('/'))) return false;

  QStringList segs = rawPath.split(QLatin1Char('/'), Qt::KeepEmptyParts);
  if (segs.size() < minSegs || segs.size() > maxSegs) return false;
  if (segs.last().endsWith(QStringLiteral(".git"))) segs.last().chop(4);

  static const QRegularExpression kSegRe(QStringLiteral("^[A-Za-z0-9_.-]+$"));
  for (const QString& seg : segs) {
    if (seg.isEmpty() || seg == QLatin1String(".") || seg == QLatin1String("..") ||
        seg.contains(QStringLiteral("..")) || seg.startsWith(QLatin1Char('-')) || !kSegRe.match(seg).hasMatch()) {
      return false;
    }
  }
  if (joinedOut) *joinedOut = segs.join(QLatin1Char('/'));
  if (slugOut) *slugOut = segs.last();
  return true;
}

void boundsForHost(const QString& lowerHostPort, int* minSegs, int* maxSegs) {
  const QString hostOnly = lowerHostPort.section(QLatin1Char(':'), 0, 0);
  if (hostOnly == QLatin1String("github.com") || hostOnly == QLatin1String("codeberg.org")) {
    *minSegs = 2;
    *maxSegs = 2;
  } else if (hostOnly == QLatin1String("gitlab.com")) {
    *minSegs = 2;
    *maxSegs = 8;
  } else {
    *minSegs = 1;
    *maxSegs = 10;
  }
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
  return ws.isEmpty() ? QString() : QDir(ws).filePath(QStringLiteral(".gridlock/plugins.toml"));
}

QString PluginManifest::userLockFilePath() {
  return QDir(ConfigManager::configDirectory()).filePath(QStringLiteral("plugins-lock.toml"));
}

QString PluginManifest::scopeToString(PluginScope scope) {
  return scope == PluginScope::Workspace ? QStringLiteral("workspace") : QStringLiteral("global");
}

bool PluginManifest::normalizeGitUrl(const QString& rawInput, GitForgeHost defaultForge,
                                     QString* normalizedOut, QString* repoSlugOut, QString* errorOut) {
  if (rawInput.isEmpty()) {
    if (errorOut) *errorOut = QStringLiteral("Repository URL must not be empty.");
    return false;
  }
  if (rawInput.startsWith(QLatin1Char('-'))) {
    if (errorOut) *errorOut = QStringLiteral("Repository URL must not begin with '-'.");
    return false;
  }
  if (hasWhitespaceOrControl(rawInput)) {
    if (errorOut) *errorOut = QStringLiteral("Repository URL must not contain whitespace or control characters.");
    return false;
  }

  if (rawInput.startsWith(QStringLiteral("file:///"))) {
    QString localPath = rawInput.mid(8);
    if (localPath.endsWith(QLatin1Char('/'))) localPath.chop(1);
    const QStringList segs = localPath.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (segs.isEmpty()) {
      if (errorOut) *errorOut = QStringLiteral("Local file:// repository URL has an empty path.");
      return false;
    }
    for (const QString& seg : segs) {
      if (seg.isEmpty() || seg == QLatin1String(".") || seg == QLatin1String("..") ||
          seg.contains(QStringLiteral("..")) || seg.startsWith(QLatin1Char('-'))) {
        if (errorOut) *errorOut = QStringLiteral("Unsafe path segment in file:// repository URL.");
        return false;
      }
    }
    QString slug = segs.last();
    if (slug.endsWith(QStringLiteral(".git"))) slug.chop(4);
    if (slug.isEmpty()) {
      if (errorOut) *errorOut = QStringLiteral("Invalid repository basename in file:// URL.");
      return false;
    }
    if (normalizedOut) *normalizedOut = QStringLiteral("file:///") + segs.join(QLatin1Char('/'));
    if (repoSlugOut) *repoSlugOut = slug;
    return true;
  }

  if (rawInput.startsWith(QStringLiteral("https://"))) {
    const QString rest = rawInput.mid(8);
    const qsizetype slash = rest.indexOf(QLatin1Char('/'));
    QString cleanAuthority;
    if (slash <= 0 || !validateHostAuthority(rest.left(slash), true, &cleanAuthority)) {
      if (errorOut) *errorOut = QStringLiteral("Invalid host in HTTPS Git repository URL.");
      return false;
    }
    int minS = 1, maxS = 10;
    boundsForHost(cleanAuthority, &minS, &maxS);
    QString cleanPath, slug;
    if (!validateRepoSegments(rest.mid(slash + 1), minS, maxS, &cleanPath, &slug)) {
      if (errorOut) *errorOut = QStringLiteral("Invalid or unsafe repository path in HTTPS Git URL.");
      return false;
    }
    if (normalizedOut) *normalizedOut = QStringLiteral("https://%1/%2.git").arg(cleanAuthority, cleanPath);
    if (repoSlugOut) *repoSlugOut = slug;
    return true;
  }

  if (rawInput.startsWith(QStringLiteral("git@"))) {
    const QString rest = rawInput.mid(4);
    const qsizetype colon = rest.indexOf(QLatin1Char(':'));
    QString cleanHost;
    if (colon <= 0 || rest.left(colon).contains(QLatin1Char('/')) ||
        !validateHostAuthority(rest.left(colon), false, &cleanHost)) {
      if (errorOut) *errorOut = QStringLiteral("Invalid host in SSH git@ repository URL.");
      return false;
    }
    int minS = 1, maxS = 10;
    boundsForHost(cleanHost, &minS, &maxS);
    QString cleanPath, slug;
    if (!validateRepoSegments(rest.mid(colon + 1), minS, maxS, &cleanPath, &slug)) {
      if (errorOut) *errorOut = QStringLiteral("Invalid or unsafe repository path in SSH Git URL.");
      return false;
    }
    if (normalizedOut) *normalizedOut = QStringLiteral("git@%1:%2.git").arg(cleanHost, cleanPath);
    if (repoSlugOut) *repoSlugOut = slug;
    return true;
  }

  auto resolveForgeShorthand = [&](const QString& host, int maxSegs, const QString& pathSpec) -> bool {
    QString cleanPath, slug;
    if (!validateRepoSegments(pathSpec, 2, maxSegs, &cleanPath, &slug)) {
      if (errorOut) *errorOut = QStringLiteral("Invalid repository shorthand '%1' for %2.").arg(pathSpec, host);
      return false;
    }
    if (normalizedOut) *normalizedOut = QStringLiteral("https://%1/%2.git").arg(host, cleanPath);
    if (repoSlugOut) *repoSlugOut = slug;
    return true;
  };

  if (rawInput.startsWith(QStringLiteral("github:"))) {
    return resolveForgeShorthand(QStringLiteral("github.com"), 2, rawInput.mid(7));
  }
  if (rawInput.startsWith(QStringLiteral("gitlab:"))) {
    return resolveForgeShorthand(QStringLiteral("gitlab.com"), 8, rawInput.mid(7));
  }
  if (rawInput.startsWith(QStringLiteral("codeberg:"))) {
    return resolveForgeShorthand(QStringLiteral("codeberg.org"), 2, rawInput.mid(9));
  }
  if (rawInput.contains(QLatin1Char(':'))) {
    if (errorOut) {
      *errorOut = QStringLiteral(
          "Unsupported Git URL scheme or prefix (use https://, git@, github:, gitlab:, or codeberg:).");
    }
    return false;
  }

  switch (defaultForge) {
    case GitForgeHost::GitHub:
      return resolveForgeShorthand(QStringLiteral("github.com"), 2, rawInput);
    case GitForgeHost::GitLab:
      return resolveForgeShorthand(QStringLiteral("gitlab.com"), 8, rawInput);
    case GitForgeHost::Codeberg:
      return resolveForgeShorthand(QStringLiteral("codeberg.org"), 2, rawInput);
    case GitForgeHost::CustomUrl:
      if (errorOut) {
        *errorOut = QStringLiteral(
            "Custom Git URL mode requires a full https://<host>/<path>.git or git@<host>:<path>.git URL.");
      }
      return false;
  }
  return false;
}

QString PluginManifest::formatManifestRepoSpec(const QString& rawInput, GitForgeHost defaultForge) {
  const QString trimmed = rawInput.trimmed();
  if (trimmed.contains(QLatin1Char(':'))) return trimmed;
  if (defaultForge == GitForgeHost::GitLab) return QStringLiteral("gitlab:") + trimmed;
  if (defaultForge == GitForgeHost::Codeberg) return QStringLiteral("codeberg:") + trimmed;
  return trimmed;
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
    if (errorOut) {
      *errorOut = QStringLiteral("Invalid TOML in plugins.toml: %1")
                      .arg(QString::fromUtf8(err.description().data(), static_cast<qsizetype>(err.description().size())));
    }
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
    if (!PluginInstaller::normalizeAndValidateUrl(parsed.repo, GitForgeHost::GitHub,
                                                  &parsed.normalizedUrl, &parsed.repoSlug, &err)) {
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
      "# Supports GitHub ('owner/repo' or 'github:owner/repo'), GitLab ('gitlab:group/repo'),\n"
      "# Codeberg ('codeberg:user/repo'), Gitea, or any custom 'https://' / 'git@' Git URL.\n"
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
  QString normUrl, slug, normBuild;
  if (!PluginInstaller::normalizeAndValidateUrl(entry.repo, GitForgeHost::GitHub, &normUrl, &slug, errorOut)) {
    return false;
  }
  if (!PluginInstaller::validateGitRef(entry.ref, errorOut)) return false;
  if (!validateBuildMode(entry.build, &normBuild, errorOut)) return false;

  QVector<PluginManifestEntry> entries;
  if (QFileInfo::exists(filePath) && !loadManifestFile(filePath, &entries, errorOut)) return false;

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
    if (errorOut) {
      *errorOut = QStringLiteral("Invalid TOML in plugins-lock.toml: %1")
                      .arg(QString::fromUtf8(err.description().data(), static_cast<qsizetype>(err.description().size())));
    }
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
  if (QFileInfo::exists(lockFilePath)) (void)loadLockFile(lockFilePath, &entries, nullptr);

  PluginLockEntry clean = entry;
  clean.scope = entry.scope.trimmed().isEmpty() ? QStringLiteral("global") : entry.scope.trimmed().toLower();
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
  QStringList args;
  if (normalizedUrl.startsWith(QStringLiteral("file://"))) {
    args << QStringLiteral("-c") << QStringLiteral("protocol.file.allow=always");
  }
  args << QStringLiteral("ls-remote") << QStringLiteral("--") << normalizedUrl << target;
  proc.start(QStringLiteral("git"), args);
  if (!proc.waitForFinished(8000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    return QString();
  }
  const QString firstLine = QString::fromUtf8(proc.readAllStandardOutput()).trimmed().section(QLatin1Char('\n'), 0, 0);
  const QString sha = firstLine.section(QLatin1Char('\t'), 0, 0).trimmed().toLower();
  return sha.size() == 40 ? sha : QString();
}

bool PluginManifest::isManifestEntryUpToDate(const PluginManifestEntry& entry, PluginScope scope,
                                             const QString& installDir,
                                             const QVector<PluginLockEntry>& lockEntries,
                                             QString* outCanonBinaryPath, QString* outBinarySha256) {
  const QString wantedScope = scopeToString(scope);
  const PluginLockEntry* matched = nullptr;
  for (const PluginLockEntry& cand : lockEntries) {
    if (cand.repo == entry.normalizedUrl && cand.scope == wantedScope) {
      matched = &cand;
      break;
    }
  }
  if (matched == nullptr || matched->commitSha.isEmpty() || matched->binaryName.isEmpty() ||
      matched->binarySha256.isEmpty() || matched->binaryName.contains(QLatin1Char('/')) ||
      matched->binaryName.contains(QLatin1Char('\\')) || matched->binaryName == QLatin1String("..")) {
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
