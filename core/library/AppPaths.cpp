#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>

AppPaths::AppPaths(QString envsRoot)
    : m_envsRoot(QDir::toNativeSeparators(std::move(envsRoot)))
{
}

QString AppPaths::defaultEnvsRoot()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/envs");
}

QString AppPaths::normalizeStored(const QString &path)
{
    return QDir::cleanPath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

bool AppPaths::ensureRoot(AppError *error) const
{
    QDir dir(m_envsRoot);
    if (dir.exists() || dir.mkpath(QStringLiteral(".")))
        return true;

    if (error != nullptr) {
        *error = AppError::fail(AppError::Io,
                                QStringLiteral("无法创建数据目录：%1").arg(QDir::toNativeSeparators(m_envsRoot)));
    }
    return false;
}

QString AppPaths::configPath() const
{
    return m_envsRoot + QStringLiteral("/config.json");
}

QString AppPaths::libraryDir(const QString &name) const
{
    return m_envsRoot + QLatin1Char('/') + name;
}

QString AppPaths::databasePath(const QString &name) const
{
    return libraryDir(name) + QLatin1Char('/') + name + QStringLiteral(".db");
}

QString AppPaths::coverDir(const QString &name) const
{
    return libraryDir(name) + QStringLiteral("/cover");
}
