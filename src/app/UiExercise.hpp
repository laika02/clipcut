#pragma once
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QtTest>
#include <QWheelEvent>
#include "Editor.hpp"
#include "PerformanceLog.hpp"
#include "Theme.hpp"

inline void exerciseTheme(QQuickWindow *window,Theme *theme,Preview *preview,const QString &directory,PerformanceLog *profiler,bool expectVideo) {
    QTimer::singleShot(1500,window,[=] {
        const auto original=theme->preset();const auto originalOpacity=theme->backgroundOpacity(),originalPopup=theme->popupOpacity();
        const auto restore=[&]{theme->setPreset(original);theme->setBackgroundOpacity(originalOpacity);theme->setPopupOpacity(originalPopup);};
        const auto fail=[&](const QString &message){restore();profiler->event("theme_test",{{"success",false},{"message",message}});QCoreApplication::exit(31);};
        QDir().mkpath(directory);theme->setPreset("graphite");theme->setBackgroundOpacity(.8);theme->setPopupOpacity(.96);
        window->requestActivate();QTest::keyClick(window,Qt::Key_Comma,Qt::ControlModifier);QTest::qWait(150);
        auto settings=window->findChild<QObject*>("settingsDialog");auto tabs=window->findChild<QQuickItem*>("settingsTabs");
        if(!settings||!tabs||!settings->property("opened").toBool()){fail("Appearance settings did not open");return;}
        tabs->setProperty("currentIndex",3);QTest::qWait(200);
        auto choice=window->findChild<QQuickItem*>("themeChoice");choice->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_Space);QTest::qWait(100);QTest::keyClick(window,Qt::Key_Down);QTest::keyClick(window,Qt::Key_Return);QTest::qWait(100);
        if(theme->preset()!="dark-rose"){fail("Theme picker did not switch presets");return;}
        theme->setPreset("graphite");QTest::qWait(100);
        auto opacity=window->findChild<QQuickItem*>("backgroundOpacity");opacity->forceActiveFocus();QTest::keyClick(window,Qt::Key_Left);QTest::qWait(100);
        if(theme->backgroundOpacity()>=.8||theme->backgroundOpacity()<.75){fail("Background opacity keyboard control failed");return;}
        auto popup=window->findChild<QQuickItem*>("popupOpacity");popup->forceActiveFocus();QTest::keyClick(window,Qt::Key_Left);QTest::qWait(100);
        if(theme->popupOpacity()>=.96){fail("Popup opacity keyboard control failed");return;}
        theme->setBackgroundOpacity(.8);theme->setPopupOpacity(.96);QTest::qWait(200);
        window->grabWindow().save(QDir(directory).filePath("appearance.png"));
        QTest::keyClick(window,Qt::Key_Escape);QTest::qWait(250);
        if(window->opacity()!=1||window->contentItem()->opacity()!=1){fail("Whole-window opacity was changed");return;}
        if(expectVideo){preview->seek(std::min(.5,preview->duration()/2),true);for(int i=0;i<50&&preview->presented()==0;++i)QTest::qWait(100);}
        const auto frame=window->grabWindow();
        if(frame.isNull()){fail("Cannot capture theme framebuffer");return;}
        const auto scale=window->devicePixelRatio();const auto background=frame.pixelColor(qRound(6*scale),qRound(6*scale));
        if(std::abs(background.alpha()-204)>2){fail(QString("Background framebuffer alpha is %1, expected 204").arg(background.alpha()));return;}
        auto brand=window->findChild<QQuickItem*>("brandLabel");
        const auto rect=brand->mapRectToScene(brand->boundingRect());int strongestAlpha=0;
        for(int y=qRound(rect.top()*scale);y<qRound(rect.bottom()*scale);++y)for(int x=qRound(rect.left()*scale);x<qRound(rect.right()*scale);++x)strongestAlpha=std::max(strongestAlpha,frame.pixelColor(x,y).alpha());
        if(strongestAlpha!=255||brand->property("color").value<QColor>().alpha()!=255){fail("Text was made translucent");return;}
        if(expectVideo) {
            auto crop=window->findChild<QQuickItem*>("cropOverlay");const auto center=crop->mapToScene(QPointF(crop->width()/2,crop->height()/2));
            if(preview->presented()==0||frame.pixelColor(qRound(center.x()*scale),qRound(center.y()*scale)).alpha()!=255){fail("Video preview is not opaque");return;}
        }
        frame.save(QDir(directory).filePath("graphite.png"));
        theme->setPreset("dark-rose");QTest::qWait(150);window->grabWindow().save(QDir(directory).filePath("dark-rose.png"));
        restore();profiler->event("theme_test",{{"success",true},{"background_alpha",background.alpha()},{"text_alpha",strongestAlpha},{"video_checked",expectVideo}});QCoreApplication::exit(0);
    });
}

inline void exerciseFiles(QQuickWindow *window,const QString &directory,PerformanceLog *profiler) {
    QTimer::singleShot(500,window,[=] {
        auto fail=[&](const QString &text){profiler->event("file_browser_test",{{"success",false},{"message",text}});QCoreApplication::exit(30);};
        window->requestActivate();QTest::keyClick(window,Qt::Key_O,Qt::ControlModifier);QTest::qWait(100);
        auto browser=window->findChild<QObject*>("openBrowser");
        if(!browser||!browser->property("opened").toBool()){fail("Open shortcut did not open browser");return;}
        browser->setProperty("currentFolder",QUrl::fromLocalFile(QFileInfo(directory).absoluteFilePath()));
        auto search=browser->findChild<QQuickItem*>("fileSearch");auto list=browser->findChild<QQuickItem*>("fileListing");
        if(!search||!list){fail("Browser controls missing");return;}
        QTest::qWait(500);list->forceActiveFocus();QTest::keyClick(window,Qt::Key_Slash);QTest::qWait(100);
        if(!search->hasActiveFocus()||!search->property("text").toString().isEmpty()){fail("Slash did not focus an empty filter");return;}
        for(char c:QByteArray("movie one.mkv"))QTest::keyClick(window,c);QTest::qWait(200);
        if(browser->property("resultCount").toInt()!=1){fail(QString("Filename/extension filter failed: query '%1', count %2").arg(search->property("text").toString()).arg(browser->property("resultCount").toInt()));return;}
        search->setProperty("text","MOVIE ONE.MKV");QTest::qWait(100);
        if(browser->property("resultCount").toInt()!=1){fail("Case insensitive filter failed");return;}
        search->setProperty("text","movie ");QTest::qWait(100);
        QTest::keyClick(window,Qt::Key_Tab);
        if(!search->hasActiveFocus()||search->property("text").toString()!="Movie one.MKV"){fail("Tab did not complete filename with spaces/case");return;}
        QTest::keyClick(window,Qt::Key_Tab);
        if(search->property("text").toString()!="Movie two.MKV"){fail("Tab did not cycle original matches");return;}
        QTest::keyClick(window,Qt::Key_Tab);
        if(search->property("text").toString()!="Movie one.MKV"){fail("Tab completion did not wrap");return;}
        QTest::keyClick(window,Qt::Key_Tab,Qt::ShiftModifier);
        if(!search->hasActiveFocus()||search->property("text").toString()!="Movie two.MKV"){fail("Shift+Tab did not reverse completion");return;}
        QTest::keyClick(window,Qt::Key_Backspace);QTest::keyClick(window,Qt::Key_Tab);
        if(search->property("text").toString()!="Movie two.MKV"){fail("Editing did not reset completion matches");return;}
        QTest::keyClick(window,Qt::Key_Escape);QTest::qWait(100);
        if(!browser->property("opened").toBool()||!search->property("text").toString().isEmpty()){fail("Escape did not clear filter");return;}
        search->setProperty("text","notes");QTest::qWait(100);
        if(browser->property("resultCount").toInt()!=0){fail("Video filter included text files");return;}
        auto types=browser->findChild<QQuickItem*>("fileTypes");types->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_Space);QTest::qWait(100);QTest::keyClick(window,Qt::Key_Down);QTest::keyClick(window,Qt::Key_Return);QTest::qWait(100);
        if(types->property("currentIndex").toInt()!=1||browser->property("resultCount").toInt()!=1){fail("Compact file type popup keyboard selection failed");return;}
        types->setProperty("currentIndex",0);search->forceActiveFocus();
        search->setProperty("text","literal[1]");QTest::qWait(100);
        if(browser->property("resultCount").toInt()!=0){fail("Search treated literal brackets as wildcards");return;}
        QTest::keyClick(window,Qt::Key_Tab);
        if(search->property("text").toString()!="literal[1]"||search->hasActiveFocus()){fail("Unmatched Tab should retain text and advance focus");return;}
        search->forceActiveFocus();search->setProperty("text","sub");QTest::qWait(100);QTest::keyClick(window,Qt::Key_Tab);
        if(search->property("text").toString()!="Subfolder"){fail("Tab did not complete folder");return;}
        QTest::keyClick(window,Qt::Key_Return);QTest::qWait(300);
        if(!browser->property("currentFolder").toUrl().toLocalFile().endsWith("Subfolder")){fail("Enter did not navigate selected folder");return;}
        QTest::keyClick(window,Qt::Key_Up,Qt::AltModifier);QTest::qWait(300);
        if(browser->property("currentFolder").toUrl().toLocalFile().endsWith("Subfolder")){fail("Parent folder shortcut failed");return;}
        // A slash typed while editing a path must remain text.
        QTest::keyClick(window,Qt::Key_L,Qt::ControlModifier);QTest::qWait(100);
        auto focused=window->activeFocusItem();if(focused==search||!focused){fail("Location shortcut failed");return;}
        QTest::keyClick(window,Qt::Key_Slash);if(focused->property("text").toString()!="/"){fail("Slash hijacked path entry");return;}
        QTest::keyClick(window,Qt::Key_Escape);QTest::qWait(100);
        if(browser->property("opened").toBool()){fail("Escape did not close browser");return;}
        auto save=window->findChild<QObject*>("profileBrowser");
        if(!save){fail("Save browser missing");return;}
        save->setProperty("selectedFile",QUrl::fromLocalFile(QDir(directory).absoluteFilePath("suggested.mp4")));
        QMetaObject::invokeMethod(save,"open");QTest::qWait(300);
        auto saveList=save->findChild<QQuickItem*>("fileListing");auto saveSearch=save->findChild<QQuickItem*>("fileSearch");
        saveList->forceActiveFocus();QTest::keyClick(window,Qt::Key_Slash);QTest::qWait(100);
        if(!saveSearch->hasActiveFocus()){fail("Slash did not focus save filter");return;}
        window->grabWindow().save(QDir(directory).absoluteFilePath("file-browser.png"));
        auto filename=save->findChild<QQuickItem*>("fileName");filename->setProperty("text","Movie one.MKV");
        QMetaObject::invokeMethod(save,"choose");QTest::qWait(150);
        auto confirmation=save->findChild<QObject*>("replaceFile");
        if(!confirmation||!confirmation->property("opened").toBool()){fail("Save did not confirm replacement");return;}
        QMetaObject::invokeMethod(confirmation,"reject");QTest::qWait(100);
        if(!save->property("opened").toBool()){fail("Declining overwrite closed browser");return;}
        QMetaObject::invokeMethod(save,"reject");
        profiler->event("file_browser_test",{{"success",true},{"message","Slash, filtering, Tab completion/cycling/reverse/edit reset/no match, folder completion/navigation, Escape, save browser, overwrite confirmation"}});
        QCoreApplication::exit(0);
    });
}
// Deterministic smoke exercise uses the same key events and pointer paths as the UI.
inline void exerciseEditor(QQuickWindow *window,Editor *editor,Preview *preview,const QString &directory) {
    auto timer=new QTimer(window);timer->setInterval(100);
    QObject::connect(timer,&QTimer::timeout,window,[=] {
        if(!editor->state()["loaded"].toBool()||preview->duration()<=0)return;
        timer->stop();preview->play(false);window->requestActivate();
        auto fail=[&](const QString &text){qCritical().noquote()<<text;QCoreApplication::exit(20);};
        auto divider=window->findChild<QQuickItem*>("previewDivider");auto pane=window->findChild<QQuickItem*>("previewPane");
        if(!divider||!pane){fail("Preview divider missing");return;}
        const auto initialHeight=pane->height();auto dividerPoint=divider->mapToScene(QPointF(divider->width()/2,divider->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,dividerPoint);QTest::mouseMove(window,dividerPoint+QPoint(0,30),100);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,dividerPoint+QPoint(0,30));QTest::qWait(100);
        if(pane->height()<initialHeight+5){fail("Preview divider did not resize panes");return;}
        const auto expanded=pane->height();QTest::keyClick(window,Qt::Key_Down,Qt::AltModifier|Qt::ShiftModifier);QTest::qWait(100);
        if(pane->height()>expanded-5){fail("Keyboard resize did not enlarge audio area");return;}
        QTest::keyClick(window,Qt::Key_Up,Qt::AltModifier|Qt::ShiftModifier);QTest::qWait(100);
        if(pane->height()<expanded-1){fail("Keyboard resize did not enlarge preview");return;}
        QTest::keyClick(window,Qt::Key_Comma,Qt::ControlModifier);QTest::qWait(150);
        auto settings=window->findChild<QObject*>("settingsDialog");auto tabs=window->findChild<QQuickItem*>("settingsTabs");
        if(!settings||!tabs||!settings->property("opened").toBool()){fail("Compact settings did not open");return;}
        tabs->setProperty("currentIndex",1);QTest::qWait(100);tabs->setProperty("currentIndex",2);QTest::qWait(100);tabs->setProperty("currentIndex",0);QTest::qWait(100);
        if(settings->property("height").toDouble()>window->height()-39){fail("Settings exceeds small window");return;}
        auto closeSettings=settings->findChild<QQuickItem*>("settingsClose");auto settingsContent=qobject_cast<QQuickItem*>(settings->property("contentItem").value<QObject*>());
        if(!closeSettings||!settingsContent||closeSettings->mapToItem(settingsContent,QPointF(0,closeSettings->height())).y()>settingsContent->height()+1){fail("Settings Close button fell outside dialog");return;}
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,closeSettings->mapToScene(QPointF(closeSettings->width()/2,closeSettings->height()/2)).toPoint());QTest::qWait(100);
        if(settings->property("opened").toBool()){fail("Settings Close button did not close dialog");return;}
        if(editor->state()["tracks"].toList().size()>5){
            auto scroll=window->findChild<QQuickItem*>("audioScroll");
            auto flick=scroll?scroll->property("contentItem").value<QObject*>():nullptr;
            if(!flick){fail("Audio scroll area missing");return;}
            auto item=qobject_cast<QQuickItem*>(flick);auto pos=item->mapToScene(QPointF(item->width()/2,item->height()/2));
            QWheelEvent wheel(pos,window->mapToGlobal(pos),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(window,&wheel);QTest::qWait(100);
            qInfo()<<"Audio scroll"<<flick->property("contentY")<<flick->property("contentHeight")<<item->height();
            if(flick->property("contentHeight").toDouble()>item->height()&&flick->property("contentY").toDouble()<=0){fail("Mouse wheel did not scroll audio rows");return;}
            auto bar=window->findChild<QQuickItem*>("audioScrollBar");auto thumb=bar?qobject_cast<QQuickItem*>(bar->property("contentItem").value<QObject*>()):nullptr;
            if(!thumb||bar->x()+bar->width()>scroll->width()-40){fail("Audio scrollbar is not beside the waveforms");return;}
            const auto beforeDrag=flick->property("contentY").toDouble();const auto thumbPoint=thumb->mapToScene(QPointF(thumb->width()/2,thumb->height()/2)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,thumbPoint);QTest::mouseMove(window,thumbPoint+QPoint(0,14),100);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,thumbPoint+QPoint(0,14));QTest::qWait(100);
            if(flick->property("contentY").toDouble()<=beforeDrag){fail("Moved audio scrollbar thumb did not scroll rows");return;}
        }
        preview->seek(.35);QTest::keyClick(window,Qt::Key_I);
        preview->seek(3.5);QTest::keyClick(window,Qt::Key_O);
        preview->seek(1.2);QTest::keyClick(window,Qt::Key_D,Qt::ControlModifier);
        editor->setDelete(0,1.2,1.8);
        if(std::abs(editor->state()["trimStart"].toDouble()-.35)>.001||editor->state()["deletes"].toList().size()!=1){fail("Keyboard trim/delete failed");return;}
        QTest::qWait(100);
        for(int kind:{2,3}) {
            const auto findEdge=[&](auto &&self,QQuickItem *item)->QQuickItem* {
                if(item->objectName()=="editHandle"&&item->isVisible()&&item->property("kind").toInt()==kind)return item;
                for(auto child:item->childItems())if(auto found=self(self,child))return found;
                return nullptr;
            };
            auto edge=findEdge(findEdge,window->contentItem());
            if(!edge){fail("Ripple edge missing");return;}
            const auto before=edge->property("at").toDouble();const auto point=edge->mapToScene(QPointF(edge->width()/2,edge->height()/2)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(window,point+QPoint(20,0),100);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point+QPoint(20,0));QTest::qWait(100);
            if(edge->property("at").toDouble()<=before){fail("Ripple edge drag failed");return;}
        }
        editor->setDelete(0,1.2,1.8);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);
        const auto findRegion=[&](auto &&self,QQuickItem *item)->QQuickItem* {
            if(item->objectName()=="deleteRegion"&&item->isVisible()&&item->property("width").toDouble()>12)return item;
            for(auto child:item->childItems())if(auto found=self(self,child))return found;
            return nullptr;
        };
        auto region=findRegion(findRegion,window->contentItem());
        if(!region){fail("Ripple region missing");return;}
        const auto point=region->mapToScene(QPointF(region->width()/2,region->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(window,point+QPoint(20,0),100);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point+QPoint(20,0));QTest::qWait(100);
        const auto moved=editor->state()["deletes"].toList().first().toMap();
        if(moved["start"].toDouble()<=1.2||std::abs(moved["end"].toDouble()-moved["start"].toDouble()-.6)>.000001){fail("Middle drag did not preserve ripple length");return;}
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(100);
        if(std::abs(editor->state()["deletes"].toList().first().toMap()["start"].toDouble()-1.2)>.001){fail("Ripple move was not one undo operation");return;}
        editor->moveDelete(0,-10);auto clamped=editor->state()["deletes"].toList().first().toMap();
        if(clamped["start"].toDouble()!=0||std::abs(clamped["end"].toDouble()-.6)>.001){fail("Ripple move clamp failed");return;}
        editor->setDelete(0,1.2,1.8);
        QTest::keyClick(window,Qt::Key_2,Qt::ControlModifier);
        auto crop=window->findChild<QQuickItem*>("cropOverlay");
        if(!crop){fail("Crop overlay absent");return;}
        QTest::qWait(100);
        auto corner=crop->mapToScene(QPointF(3,3));
        qInfo()<<"Crop drag"<<corner<<crop->size()<<window->size();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,corner.toPoint());
        QTest::mouseMove(window,(corner+QPointF(crop->width()*.1,crop->height()*.1)).toPoint(),100);
        QTest::qWait(100);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,(corner+QPointF(crop->width()*.1,crop->height()*.1)).toPoint());
        if(editor->state()["cropX"].toInt()==0){fail("Crop corner drag failed");return;}
        QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier);QTest::qWait(150);
        for(char c:QByteArray("reset crop"))QTest::keyClick(window,c);QTest::keyClick(window,Qt::Key_Return);QTest::qWait(100);
        if(editor->state()["cropX"].toInt()!=0){fail("Command palette failed");return;}
        editor->crop(384,216,1152,648);
        const auto defaultOutput=editor->namedOutput("",false,-1,false);
        if(!defaultOutput["error"].toString().isEmpty()||!editor->namedOutput("../escape")["url"].isNull()||!editor->namedOutput("CON")["url"].isNull()){fail("Output filename validation failed");return;}
        const auto target=window->findChild<QObject*>("compressTarget");if(!target||target->property("text").toString()!="20"){fail("Compression default is not 20 MB");return;}
        const auto url=defaultOutput["url"].toUrl();QDir().mkpath(directory);
        QObject::connect(qobject_cast<clipcut::ExportJob*>(editor->job()),&clipcut::ExportJob::finished,window,[=](bool ok,const QString &,const QString &message){qInfo().noquote()<<"UI_EXERCISE"<<message;window->grabWindow().save(QDir(directory).absoluteFilePath("ui-exercise.png"));QCoreApplication::exit(ok&&editor->state()["exportState"]=="done"?0:21);});
        QMetaObject::invokeMethod(window,"beginOutput",Q_ARG(QVariant,QVariant(QVariantMap{})));QTest::qWait(150);
        const auto nameDialog=window->findChild<QObject*>("outputNameDialog");const auto nameField=window->findChild<QQuickItem*>("exportName");
        if(!nameDialog||!nameDialog->property("opened").toBool()||!nameField||!nameField->hasActiveFocus()||!nameField->property("text").toString().isEmpty()){fail("Filename prompt did not focus default input");return;}
        if(QFileInfo(url.toLocalFile()).absolutePath()==QDir(directory).absolutePath()){
            QTest::keyClick(window,Qt::Key_Return);QTest::qWait(100);
            auto replace=window->findChild<QObject*>("replaceOutputDialog");if(replace&&replace->property("opened").toBool())QMetaObject::invokeMethod(replace,"accept");
        } else {
            QMetaObject::invokeMethod(nameDialog,"close");editor->startExport(QUrl::fromLocalFile(QDir(directory).absoluteFilePath("ui-edited.mp4")),{{"overwrite",true}});
        }
    });timer->start();
}
