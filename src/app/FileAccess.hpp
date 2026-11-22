#pragma once
#include <QObject>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantList>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QDesktopServices>

class BrowserFilter : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QStringList patterns READ patterns WRITE setPatterns NOTIFY patternsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit BrowserFilter(QObject *parent=nullptr):QSortFilterProxyModel(parent) {
        connect(this,&QAbstractItemModel::modelReset,this,&BrowserFilter::countChanged);
        connect(this,&QAbstractItemModel::rowsInserted,this,&BrowserFilter::countChanged);
        connect(this,&QAbstractItemModel::rowsRemoved,this,&BrowserFilter::countChanged);
        connect(this,&QAbstractItemModel::layoutChanged,this,&BrowserFilter::countChanged);
    }
    QString query() const{return m_query;}
    QStringList patterns() const{return m_patterns;}
    int count() const{return rowCount();}
    void setSourceModel(QAbstractItemModel *model) override {
        m_nameRole=m_dirRole=-1;
        if(model){const auto roles=model->roleNames();for(auto it=roles.begin();it!=roles.end();++it){if(it.value()=="fileName")m_nameRole=it.key();else if(it.value()=="fileIsDir")m_dirRole=it.key();}}
        QSortFilterProxyModel::setSourceModel(model);
    }
    void setQuery(const QString &value){if(m_query==value)return;m_query=value;invalidate();emit queryChanged();}
    void setPatterns(const QStringList &value){
        if(m_patterns==value)return;m_patterns=value;m_regex.clear();
        for(const auto &pattern:value)m_regex.append(QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern),QRegularExpression::CaseInsensitiveOption));
        invalidate();emit patternsChanged();
    }
    Q_INVOKABLE QVariant get(int row,const QString &name) const {
        if(!sourceModel())return {};
        const auto roles=roleNames();for(auto it=roles.begin();it!=roles.end();++it)if(it.value()==name.toUtf8())return data(index(row,0),it.key());
        return {};
    }
    Q_INVOKABLE bool isFolder(int row) const{return get(row,"fileIsDir").toBool();}
signals:
    void queryChanged();void patternsChanged();void countChanged();
protected:
    bool filterAcceptsRow(int row,const QModelIndex &parent) const override {
        const auto idx=sourceModel()->index(row,0,parent);
        const auto name=sourceModel()->data(idx,m_nameRole).toString();
        if(!name.contains(m_query,Qt::CaseInsensitive))return false;
        if(sourceModel()->data(idx,m_dirRole).toBool())return true;
        for(const auto &pattern:m_regex)if(pattern.match(name).hasMatch())return true;
        return m_patterns.isEmpty();
    }
private:
    QString m_query;QStringList m_patterns;QList<QRegularExpression> m_regex;
    int m_nameRole=-1,m_dirRole=-1;
};

// Keep local path handling in Qt rather than constructing file URLs in JavaScript.
class FileAccess : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl initialFolder READ initialFolder CONSTANT)
    Q_PROPERTY(QVariantList places READ places CONSTANT)
public:
    using QObject::QObject;
    QUrl initialFolder() const {
        const auto saved=QSettings().value("browserFolder").toString();
        if(QFileInfo(saved).isDir())return QUrl::fromLocalFile(saved);
        auto path=QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if(!QFileInfo(path).isDir())path=QDir::homePath();
        return QUrl::fromLocalFile(path);
    }
    QVariantList places() const {
        QVariantList result;
        auto add=[&](const QString &label,const QString &path){if(!path.isEmpty()&&QFileInfo(path).isDir())result.append(QVariantMap{{"label",label},{"url",QUrl::fromLocalFile(path)}});};
        add("Home",QDir::homePath());
        add("Videos",QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
        add("Downloads",QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        for(auto drive:QDir::drives())add(QDir::toNativeSeparators(drive.absoluteFilePath()),drive.absoluteFilePath());
        return result;
    }
    Q_INVOKABLE QString path(const QUrl &url) const {return QDir::toNativeSeparators(url.toLocalFile());}
    Q_INVOKABLE QString name(const QUrl &url) const {return QFileInfo(url.toLocalFile()).fileName();}
    Q_INVOKABLE QUrl parent(const QUrl &url) const {return QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absolutePath());}
    Q_INVOKABLE bool isDirectory(const QUrl &url) const {return QFileInfo(url.toLocalFile()).isDir();}
    Q_INVOKABLE bool exists(const QUrl &url) const {return QFileInfo::exists(url.toLocalFile());}
    Q_INVOKABLE QUrl resolve(QString text,const QUrl &folder) const {
        if(text.startsWith("file:"))return QUrl(text);
        text.replace('\\','/');
        if(text=="~")text=QDir::homePath();
        else if(text.startsWith("~/"))text=QDir::homePath()+text.mid(1);
        return QUrl::fromLocalFile(QDir::cleanPath(QDir(folder.toLocalFile()).absoluteFilePath(text)));
    }
    Q_INVOKABLE void remember(const QUrl &folder) {QSettings().setValue("browserFolder",folder.toLocalFile());}
    Q_INVOKABLE void revealFolder(const QString &file) {QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));}
};
