#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "app/Theme.hpp"

class ThemeTest:public QObject {
    Q_OBJECT
private slots:
    void presetsOpacityAndPersistence() {
        QTemporaryDir dir;
        const auto settings=dir.filePath("settings.ini"),themes=dir.filePath("themes");
        {
            Theme theme(settings,themes);
            QCOMPARE(theme.preset(),QString("graphite"));
            QCOMPARE(theme.colors()["window"].toString(),QString("#191a1c"));
            QCOMPARE(theme.backgroundOpacity(),.8);
            QCOMPARE(theme.backgrounds()["window"].value<QColor>().alpha(),204);
            for(auto color:theme.colors())QCOMPARE(QColor(color.toString()).alpha(),255);
            theme.setBackgroundOpacity(.35);theme.setPopupOpacity(.7);
            QCOMPARE(theme.backgrounds()["window"].value<QColor>().alpha(),89);
            QCOMPARE(theme.popupBackgrounds()["panel"].value<QColor>().alpha(),179);
            theme.setPreset("dark-rose");QCOMPARE(theme.backgroundOpacity(),1.);
            theme.setPreset("graphite");QCOMPARE(theme.backgroundOpacity(),.35);
        }
        Theme restored(settings,themes);
        QCOMPARE(restored.backgroundOpacity(),.35);QCOMPARE(restored.popupOpacity(),.7);
        restored.setBackgroundOpacity(-1);QCOMPARE(restored.backgroundOpacity(),0.);
        restored.setBackgroundOpacity(2);QCOMPARE(restored.backgroundOpacity(),1.);
        restored.resetOpacity();QCOMPARE(restored.backgroundOpacity(),.8);QCOMPARE(restored.popupOpacity(),.96);
    }
    void importExportValidationAndReload() {
        QTemporaryDir dir;Theme theme(dir.filePath("settings.ini"),dir.filePath("themes"));
        const auto jsonFile=dir.filePath("input.json");
        const auto write=[&](const QByteArray &json){QFile file(jsonFile);QVERIFY(file.open(QIODevice::WriteOnly));file.write(json);};
        const auto url=QUrl::fromLocalFile(jsonFile);
        write("{\"version\":1,\"name\":\"Custom test\",\"backgroundOpacity\":0.4,\"colors\":{\"accent\":\"#8090a0\",\"text\":\"#ffffff\"}}");
        QVERIFY(theme.importTheme(url).isEmpty());QCOMPARE(theme.backgroundOpacity(),.4);
        QCOMPARE(theme.colors()["accent"].toString(),QString("#8090a0"));
        const auto selected=theme.preset();
        const auto exported=dir.filePath("export.json");
        QVERIFY(theme.exportTheme(QUrl::fromLocalFile(exported)).isEmpty());
        QFile output(exported);QVERIFY(output.open(QIODevice::ReadOnly));
        const auto obj=QJsonDocument::fromJson(output.readAll()).object();
        QCOMPARE(obj["backgroundOpacity"].toDouble(),.4);QCOMPARE(obj["colors"].toObject()["text"].toString(),QString("#ffffff"));
        for(const auto &bad:{QByteArray("not json"),QByteArray("{\"version\":9,\"name\":\"Bad\",\"colors\":{}}"),QByteArray("{\"version\":1,\"name\":\"Bad\",\"colors\":{\"text\":\"#80ffffff\"}}"),QByteArray("{\"version\":1,\"name\":\"Bad\",\"backgroundOpacity\":2,\"colors\":{}}"),QByteArray("{\"version\":1,\"name\":\"Bad\",\"colors\":{\"typo\":\"#ffffff\"}}")}) {
            write(bad);QVERIFY(!theme.importTheme(url).isEmpty());QCOMPARE(theme.preset(),selected);
        }
        const auto stored=QDir(theme.directory()).entryList({"*.json"},QDir::Files).first();
        QFile edit(QDir(theme.directory()).filePath(stored));QVERIFY(edit.open(QIODevice::WriteOnly));edit.write("{\"version\":1,\"name\":\"Reloaded\",\"colors\":{\"text\":\"#aabbcc\"}}");edit.close();
        QVERIFY(theme.reload().isEmpty());QCOMPARE(theme.name(),QString("Reloaded"));QCOMPARE(theme.colors()["text"].toString(),QString("#aabbcc"));
        QVERIFY(edit.open(QIODevice::WriteOnly));edit.write("invalid");edit.close();
        QVERIFY(!theme.reload().isEmpty());QCOMPARE(theme.name(),QString("Reloaded"));
    }
};
QTEST_GUILESS_MAIN(ThemeTest)
#include "ThemeTest.moc"
