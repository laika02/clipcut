#include "Theme.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

Theme::Theme(const QString &settingsFile,const QString &themeDirectory,QObject *parent):QObject(parent) {
    m_settings=settingsFile.isEmpty()?std::make_unique<QSettings>():std::make_unique<QSettings>(settingsFile,QSettings::IniFormat);
    m_directory=themeDirectory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/themes":themeDirectory;
    QFile defaults(":/themes/dark-rose.json");
    if(defaults.open(QIODevice::ReadOnly))m_defaults=QJsonDocument::fromJson(defaults.readAll()).object()["colors"].toObject().toVariantMap();
    discover();
    setPreset(m_settings->value("appearance/preset","graphite").toString());
    if(m_preset.isEmpty())setPreset("graphite");
}
QString Theme::read(const QString &file,Definition &out) const {
    QFile input(file);
    if(!input.open(QIODevice::ReadOnly))return "Cannot open theme: "+input.errorString();
    if(input.size()>128*1024)return "Theme files must be smaller than 128 KiB.";
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(input.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject())return "Invalid theme JSON: "+error.errorString();
    const auto obj=document.object();
    if(obj["version"]!=QJsonValue(1))return "Unsupported theme version; expected version 1.";
    if(!obj["name"].isString()||obj["name"].toString().trimmed().isEmpty())return "A theme needs a name.";
    if(!obj["colors"].isObject())return "A theme needs a colors object.";
    if(obj.contains("fontFamily")&&(!obj["fontFamily"].isString()||obj["fontFamily"].toString().trimmed().isEmpty()))return "fontFamily must be a font name.";
    out.fontFamily=obj["fontFamily"].toString("sans-serif");
    const auto opacity=[&](const char *key,qreal fallback,qreal &value){
        const auto v=obj[key];value=v.isUndefined()?fallback:v.toDouble(-1);
        return (v.isUndefined()||v.isDouble())&&std::isfinite(value)&&value>=0&&value<=1;
    };
    if(!opacity("backgroundOpacity",1,out.opacity)||!opacity("popupOpacity",1,out.popupOpacity))return "Opacity must be a number between 0 and 1.";
    auto colors=m_defaults;
    const auto overrides=obj["colors"].toObject();
    static const QRegularExpression rgb("^#[0-9a-fA-F]{6}$");
    for(auto it=overrides.begin();it!=overrides.end();++it) {
        if(!colors.contains(it.key()))return "Unknown color role: "+it.key();
        if(!it.value().isString()||!rgb.match(it.value().toString()).hasMatch())return "Color "+it.key()+" must use opaque #RRGGBB. Set transparency with backgroundOpacity.";
        colors[it.key()]=it.value().toString();
    }
    out.name=obj["name"].toString().trimmed();out.colors=colors;out.path=file;
    return {};
}
void Theme::discover() {
    m_definitions.clear();m_presets.clear();
    for(const auto &id:{QString("graphite"),QString("dark-rose")}) {
        Definition d;d.id=id;
        if(read(":/themes/"+id+".json",d).isEmpty())m_definitions.append(d);
    }
    const QDir dir(m_directory);
    for(const auto &file:dir.entryList({"*.json"},QDir::Files,QDir::Name)) {
        Definition d;d.id="custom-"+file;
        if(read(dir.filePath(file),d).isEmpty())m_definitions.append(d);
    }
    for(const auto &d:m_definitions)m_presets.append(QVariantMap{{"id",d.id},{"label",d.name}});
    emit presetsChanged();
}
void Theme::apply(const Definition &d) {
    m_preset=d.id;m_name=d.name;m_colors=d.colors;m_fontFamily=d.fontFamily;
    const auto saved=[&](const QString &key,qreal fallback){const auto value=m_settings->value(key,fallback).toDouble();return std::isfinite(value)?std::clamp(value,qreal(0),qreal(1)):fallback;};
    m_backgroundOpacity=saved("appearance/"+d.id+"/backgroundOpacity",d.opacity);
    m_popupOpacity=saved("appearance/"+d.id+"/popupOpacity",d.popupOpacity);
    m_settings->setValue("appearance/preset",d.id);
    emit changed();
}
void Theme::setPreset(const QString &id) {
    for(const auto &d:m_definitions)if(d.id==id){apply(d);return;}
}
void Theme::setBackgroundOpacity(qreal value) {
    if(!std::isfinite(value))return;
    value=std::clamp(value,qreal(0),qreal(1));
    if(qFuzzyCompare(value,m_backgroundOpacity))return;
    m_backgroundOpacity=value;m_settings->setValue("appearance/"+m_preset+"/backgroundOpacity",value);emit changed();
}
void Theme::setPopupOpacity(qreal value) {
    if(!std::isfinite(value))return;
    value=std::clamp(value,qreal(0),qreal(1));
    if(qFuzzyCompare(value,m_popupOpacity))return;
    m_popupOpacity=value;m_settings->setValue("appearance/"+m_preset+"/popupOpacity",value);emit changed();
}
QColor Theme::tint(const QString &role,qreal alpha) const {
    return withAlpha(QColor(m_colors.value(role,"#000000").toString()),alpha);
}
QColor Theme::withAlpha(QColor color,qreal alpha) const {
    color.setAlphaF(std::isfinite(alpha)?std::clamp(alpha,qreal(0),qreal(1)):1);
    return color;
}
QVariantMap Theme::backgrounds() const {
    auto result=m_colors;
    for(auto it=result.begin();it!=result.end();++it)it.value()=withAlpha(QColor(it.value().toString()),m_backgroundOpacity);
    return result;
}
QVariantMap Theme::popupBackgrounds() const {
    auto result=m_colors;
    for(auto it=result.begin();it!=result.end();++it)it.value()=withAlpha(QColor(it.value().toString()),m_popupOpacity);
    return result;
}
QString Theme::importTheme(const QUrl &url) {
    if(!url.isLocalFile())return "Choose a local JSON theme file.";
    Definition d;auto error=read(url.toLocalFile(),d);if(!error.isEmpty())return error;
    const auto json=QJsonDocument(QJsonObject{{"version",1},{"name",d.name},{"fontFamily",d.fontFamily},{"backgroundOpacity",d.opacity},{"popupOpacity",d.popupOpacity},{"colors",QJsonObject::fromVariantMap(d.colors)}}).toJson();
    const auto filename=QCryptographicHash::hash(json,QCryptographicHash::Sha256).toHex().left(16)+".json";
    if(!QDir().mkpath(m_directory))return "Cannot create the user theme directory.";
    QSaveFile output(QDir(m_directory).filePath(filename));
    if(!output.open(QIODevice::WriteOnly)||output.write(json)!=json.size()||!output.commit())return "Cannot save imported theme: "+output.errorString();
    discover();setPreset("custom-"+filename);return {};
}
QString Theme::exportTheme(const QUrl &url) const {
    if(!url.isLocalFile())return "Choose a local destination.";
    const auto json=QJsonDocument(QJsonObject{{"version",1},{"name",m_name},{"fontFamily",m_fontFamily},{"backgroundOpacity",m_backgroundOpacity},{"popupOpacity",m_popupOpacity},{"colors",QJsonObject::fromVariantMap(m_colors)}}).toJson();
    QSaveFile output(url.toLocalFile());
    if(!output.open(QIODevice::WriteOnly)||output.write(json)!=json.size()||!output.commit())return "Cannot export theme: "+output.errorString();
    return {};
}
QString Theme::reload() {
    for(const auto &d:m_definitions)if(d.id==m_preset) {
        Definition fresh=d;const auto error=read(d.path,fresh);if(!error.isEmpty())return error;
        discover();apply(fresh);return {};
    }
    return "Selected theme no longer exists.";
}
void Theme::resetOpacity() {
    m_settings->remove("appearance/"+m_preset+"/backgroundOpacity");
    m_settings->remove("appearance/"+m_preset+"/popupOpacity");
    setPreset(m_preset);
}
