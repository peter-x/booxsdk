
#include "onyx/base/base.h"
#include "onyx/sys/font_conf.h"


namespace sys
{

/// TODO, not fully correct.
static bool getSystemFontDir(QString & path)
{
#if defined(_WIN32) || defined(WIN32) || defined(__OS2__)
    path = "c:\\windows\\fonts\\";
    return true;
#endif

    path = getenv("QTDIR");
    if (path.isEmpty())
    {
        path = "/usr/lib/fonts";
        return false;
    }
    path += "/lib/fonts";
    return true;
}

FontConfig::FontConfig()
{
}

FontConfig::~FontConfig()
{
}

QString FontConfig::getFontFamily(QSqlDatabase &database,
                                  QFontDatabase::WritingSystem system)
{
    /*
    QString ret;
    QString family;
    QSqlQuery query(database);
    st  << "SELECT family from fonts where writing = :writing",
        into(family),
        use(static_cast<int>(system));
    if (st.exec())
    {
        ret.fromUtf8(family.c_str());
    }
    return ret;
    */
    return QString();
}

void FontConfig::setFontFamily(QSqlDatabase & database,
                               QFontDatabase::WritingSystem system,
                               const QString& value)
{
    /*
    base::string family(value.toUtf8().data());
    statement st(database);
    st  << "INSERT OR REPLACE into fonts values (:family, :system)",
        use(family),
        use(static_cast<int>(system));
    st.exec();
    */
}

bool FontConfig::installFont(QSqlDatabase& database,
                             const QString& path)
{

    static QString SYSTEM_FONT_DIR;
    if (SYSTEM_FONT_DIR.isEmpty())
    {
        getSystemFontDir(SYSTEM_FONT_DIR);
    }

    /*
    // Just create a symbol link. Don't need to copy it.
    boost::filesystem::path src(path);
    boost::filesystem::path dst(SYSTEM_FONT_DIR + src.file_string());
    if (boost::filesystem::exists(dst) &&
        boost::filesystem::is_symlink(dst))
    {
        boost::filesystem::remove(dst);
    }

    boost::filesystem::create_symlink(dst, src);

    // Also need to clean the font database cache. TODO
    return boost::filesystem::exists(dst);
    */
    return true;
}

bool FontConfig::removeFont(QSqlDatabase& database,
                            const QString &)
{
    return true;
}


void FontConfig::reset(QSqlDatabase& database)
{
    /*
    typedef std::map<QFontDatabase::WritingSystem, QString> FontsTable;
    typedef FontsTable::iterator FontsTableIter;
    static FontsTable fonts;
    if (fonts.size() <= 0)
    {
        fonts[QFontDatabase::Latin] = "DejaVu";
        fonts[QFontDatabase::Greek] = "DejaVu";
        fonts[QFontDatabase::Armenian] = "DejaVu";
        fonts[QFontDatabase::SimplifiedChinese] = "Simsun";
        fonts[QFontDatabase::TraditionalChinese] = "Simsun";
        fonts[QFontDatabase::Other] = "Simsun";
    }

    statement st(database);
    st << "TRUNCATE TABLE fonts";

    for(FontsTableIter it = fonts.begin(); it != fonts.end(); ++it)
    {
        statement st(database);
        st << "INSERT INTO fonts (family, writing) values (:family, :writing)",
            use(static_cast<int>(it->first)),
            use(it->second.toStdString());
    }
    */
}

}
