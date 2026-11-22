#pragma once
#include <QObject>
#include <QColor>
#include <QSettings>
#include <QVariantMap>
#include <QUrl>
#include <memory>

// Only background colors receive alpha. Foreground tokens are validated RGB.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QVariantMap backgrounds READ backgrounds NOTIFY changed)
    Q_PROPERTY(QVariantMap popupBackgrounds READ popupBackgrounds NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY changed)
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity NOTIFY changed)
    Q_PROPERTY(QString directory READ directory CONSTANT)
public:
    explicit Theme(const QString &settingsFile={},const QString &themeDirectory={},QObject *parent=nullptr);
    QVariantMap colors() const{return m_colors;}
    QVariantMap backgrounds() const;
    QVariantMap popupBackgrounds() const;
    QVariantList presets() const{return m_presets;}
    QString preset() const{return m_preset;}
    QString name() const{return m_name;}
    QString fontFamily() const{return m_fontFamily;}
    QString directory() const{return m_directory;}
    qreal backgroundOpacity() const{return m_backgroundOpacity;}
    qreal popupOpacity() const{return m_popupOpacity;}
    void setPreset(const QString &id);
    void setBackgroundOpacity(qreal value);
    void setPopupOpacity(qreal value);
    Q_INVOKABLE QColor background(const QString &role) const{return tint(role,m_backgroundOpacity);}
    Q_INVOKABLE QColor popupBackground(const QString &role) const{return tint(role,m_popupOpacity);}
    Q_INVOKABLE QColor tint(const QString &role,qreal alpha) const;
    Q_INVOKABLE QColor withAlpha(QColor color,qreal alpha) const;
    Q_INVOKABLE QString importTheme(const QUrl &url);
    Q_INVOKABLE QString exportTheme(const QUrl &url) const;
    Q_INVOKABLE QString reload();
    Q_INVOKABLE void resetOpacity();
signals:
    void changed();
    void presetsChanged();
private:
    struct Definition {QString id,name,path,fontFamily="sans-serif";QVariantMap colors;qreal opacity=1,popupOpacity=1;};
    QString read(const QString &file,Definition &definition) const;
    void discover();
    void apply(const Definition &definition);
    std::unique_ptr<QSettings> m_settings;
    QString m_directory,m_preset,m_name,m_fontFamily;
    QVariantMap m_colors,m_defaults;
    QVariantList m_presets;
    QList<Definition> m_definitions;
    qreal m_backgroundOpacity=1,m_popupOpacity=1;
};
