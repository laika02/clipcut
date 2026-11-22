#pragma once
#include "Editor.hpp"
#include "PerformanceLog.hpp"
#include <QQuickWindow>
#include <QQuickItem>
#include <QtTest>
#include <QWheelEvent>

inline void exerciseMutes(QQuickWindow *window,Editor *editor,Preview *preview,const QString &directory,PerformanceLog *log) {
    auto timer=new QTimer(window);timer->setInterval(100);
    QObject::connect(timer,&QTimer::timeout,window,[=]{
        if(!editor->state()["loaded"].toBool()||preview->duration()<=0)return;
        timer->stop();preview->play(false);window->requestActivate();QDir().mkpath(directory);
        const auto fail=[&](QString message){window->grabWindow().save(QDir(directory).filePath("failure.png"));log->event("mute_ui_test",{{"success",false},{"message",message}});QCoreApplication::exit(41);};
        const auto regions=[&](int track){return editor->state()["tracks"].toList().value(track).toMap()["mutes"].toList();};
        const auto range=[&](int track,int index){return regions(track).value(index).toMap();};
        const auto findTrack=[&](int track){
            const auto find=[&](auto &&self,QQuickItem *item)->QQuickItem*{if(item->objectName()=="audioTimeline"&&item->property("track").toInt()==track)return item;for(auto child:item->childItems())if(auto found=self(self,child))return found;return nullptr;};
            return find(find,window->contentItem());
        };
        auto main=window->findChild<QQuickItem*>("editTimeline"),first=findTrack(0),second=findTrack(1);
        if(!main||!first||!second){fail("Two audio timelines are required");return;}
        const auto point=[&](QQuickItem *item,double time){return item->mapToScene(QPointF((time-item->property("viewStart").toDouble())/(item->property("viewEnd").toDouble()-item->property("viewStart").toDouble())*item->width(),item->height()/2)).toPoint();};
        const auto drag=[&](QQuickItem *item,double from,double to,Qt::KeyboardModifiers modifiers){auto a=point(item,from),b=point(item,to);QTest::mousePress(window,Qt::LeftButton,modifiers,a);for(int i=1;i<=5;++i){QTest::mouseMove(window,a+(b-a)*i/5,10);QTest::qWait(20);}QTest::mouseRelease(window,Qt::LeftButton,modifiers,b);QTest::qWait(60);};
        const auto undo=[&]{QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(40);};
        const auto redo=[&]{QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);QTest::qWait(40);};
        const auto near=[](double a,double b){return std::abs(a-b)<.025;};
        int cases=0;QTest::qWait(150);
        drag(main,2,2.6,Qt::ControlModifier);
        auto deletes=editor->state()["deletes"].toList();
        if(deletes.size()!=1||!near(deletes[0].toMap()["start"].toDouble(),2)||!near(deletes[0].toMap()["end"].toDouble(),2.6)||!regions(0).isEmpty()){fail("Ctrl-drag on main timeline did not create only a ripple delete");return;}
        undo();if(!editor->state()["deletes"].toList().isEmpty()){fail("Ctrl-drag creation was not one undo operation");return;}redo();if(editor->state()["deletes"].toList().size()!=1){fail("Ripple creation redo failed");return;}
        drag(main,2.4,2.9,Qt::ControlModifier);deletes=editor->state()["deletes"].toList();
        if(deletes.size()!=1||!near(deletes[0].toMap()["start"].toDouble(),2)||!near(deletes[0].toMap()["end"].toDouble(),2.9)){fail("Overlapping deletes were not combined");return;}
        undo();deletes=editor->state()["deletes"].toList();if(deletes.size()!=1||!near(deletes[0].toMap()["end"].toDouble(),2.6)){fail("Delete merge did not undo in one action");return;}
        for(auto item:{main,first}) {
            QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,point(item,2.3));QTest::qWait(40);
            if(!editor->state()["deletes"].toList().isEmpty()){fail("Right-click did not remove the delete region");return;}undo();
        }
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,point(main,deletes[0].toMap()["start"].toDouble()));QTest::qWait(40);
        if(!editor->state()["deletes"].toList().isEmpty()){fail("Right-click on a delete edge did not remove it");return;}undo();++cases;
        undo();++cases;
        drag(first,.8,1.4,Qt::ControlModifier);
        if(regions(0).size()!=1||!regions(1).isEmpty()||!editor->state()["deletes"].toList().isEmpty()||!near(range(0,0)["start"].toDouble(),.8)||!near(range(0,0)["end"].toDouble(),1.4)){fail("Ctrl-drag on audio did not create a track-local mute");return;}
        if(preview->position()>.03){fail("Ctrl-drag scrubbed the video unexpectedly");return;}
        undo();if(!regions(0).isEmpty()){fail("Mute creation was not one undo operation");return;}redo();if(regions(0).size()!=1){fail("Mute creation redo failed");return;}++cases;
        drag(first,.95,1.65,Qt::ControlModifier);
        if(regions(0).size()!=1||!near(range(0,0)["start"].toDouble(),.8)||!near(range(0,0)["end"].toDouble(),1.65)){fail("Overlapping mutes did not combine into their union");return;}undo();++cases;
        drag(first,range(0,0)["start"].toDouble(),.5,Qt::ControlModifier);
        if(regions(0).size()!=1||!near(range(0,0)["start"].toDouble(),.5)||!near(range(0,0)["end"].toDouble(),1.4)){fail("Reverse Ctrl-drag over a mute edge did not merge");return;}undo();++cases;
        drag(second,2.8,2.2,Qt::ControlModifier);
        if(regions(1).size()!=1||regions(0).size()!=1||!near(range(1,0)["start"].toDouble(),2.2)||!near(range(1,0)["end"].toDouble(),2.8)){fail("Reverse creation on a second track failed");return;}++cases;
        auto original=range(0,0);const auto length=original["end"].toDouble()-original["start"].toDouble();
        drag(first,1.1,1.4,Qt::NoModifier);
        if(!near(range(0,0)["start"].toDouble(),original["start"].toDouble()+.3)||std::abs(range(0,0)["end"].toDouble()-range(0,0)["start"].toDouble()-length)>.000001){fail("Mute body drag did not preserve length");return;}undo();if(range(0,0)["start"]!=original["start"]){fail("Mute body drag did not undo once");return;}++cases;
        drag(first,original["start"].toDouble(),original["start"].toDouble()+.1,Qt::NoModifier);
        if(!near(range(0,0)["start"].toDouble(),original["start"].toDouble()+.1)||range(0,0)["end"]!=original["end"]){fail("Mute start edge moved the opposite edge");return;}undo();++cases;
        drag(first,original["end"].toDouble(),original["end"].toDouble()+.15,Qt::NoModifier);
        if(!near(range(0,0)["end"].toDouble(),original["end"].toDouble()+.15)||range(0,0)["start"]!=original["start"]){fail("Mute end edge moved the opposite edge");return;}undo();++cases;
        QTest::keyClick(window,Qt::Key_Left);QTest::qWait(30);
        if(std::abs(range(0,0)["end"].toDouble()-original["end"].toDouble()+1/editor->state()["fps"].toDouble())>.002){fail("Mute edge arrow nudge failed");return;}undo();
        QTest::keyClick(window,Qt::Key_Delete);QTest::qWait(30);if(!regions(0).isEmpty()||regions(1).size()!=1){fail("Delete did not remove only the focused mute");return;}undo();++cases;
        drag(first,1.1,-1,Qt::NoModifier);
        if(range(0,0)["start"].toDouble()!=0||std::abs(range(0,0)["end"].toDouble()-length)>.000001){fail("Mute body boundary clamp changed length");return;}undo();++cases;
        drag(second,.1,.3,Qt::NoModifier);
        if(regions(1).size()!=1||!editor->state()["tracks"].toList()[1].toMap()["solo"].toBool()||!near(preview->position(),.3)){fail("Plain audio dragging no longer scrubs/solos that track");return;}++cases;
        QTest::keyClick(window,Qt::Key_M,Qt::ControlModifier|Qt::ShiftModifier);QTest::qWait(40);
        if(regions(1).size()!=2){fail("Add-mute hotkey did not use the active audio track");return;}undo();++cases;
        // A window-owned drag survives leaving the row, touchpad scrolling,
        // and even explicit cancellation of its local Quick mouse grab.
        auto scroll=window->findChild<QQuickItem*>("audioScroll");
        auto flick=scroll?scroll->property("contentItem").value<QObject*>():nullptr;
        const auto outsideDrag=[&](const QString &name,double at,const std::function<double()> &value){
            const auto initial=value(),scrollY=flick?flick->property("contentY").toDouble():0;
            const auto press=point(first,at);QPoint position=press;
            const bool nativePointer=qEnvironmentVariableIsSet("CLIPCUT_TEST_NATIVE_MOUSE");
            const auto seat=[&](const QString &command){
                const auto socket=qEnvironmentVariable("CLIPCUT_TEST_POINTER_SOCKET");
                if(!socket.isEmpty())clipcut::runTool(QCoreApplication::applicationDirPath()+"/wayland-pointer",{"send",socket,command});
                else clipcut::runTool("swaymsg",{"-s",qEnvironmentVariable("CLIPCUT_TEST_SWAYSOCK"),"seat seat0 cursor "+command});
                QTest::qWait(50);
            };
            const auto movePointer=[&](QPoint p){if(nativePointer){const auto global=window->mapToGlobal(p);seat(QString("set %1 %2").arg(global.x()).arg(global.y()));}else QTest::mouseMove(window,p,10);};
            if(nativePointer){movePointer(press);seat("press button1");}else QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);
            QTest::qWait(20);
            QPointer<QQuickItem> grabber=window->mouseGrabberItem();
            if(!grabber||!grabber->property("pressed").toBool()||!editor->muteDragging()){fail(name+": no initial mute drag");return false;}
            // Exercise the stealing parent explicitly as well as ScrollView's
            // desktop mouse mode (which can disable mouse flicking).
            const bool interactive=flick&&flick->property("interactive").toBool();
            if(flick)flick->setProperty("interactive",true);
            auto touchpad=new QPointingDevice("Test touchpad",173,QInputDevice::DeviceType::TouchPad,QPointingDevice::PointerType::Finger,QInputDevice::Capability::Position|QInputDevice::Capability::PixelScroll|QInputDevice::Capability::Scroll,5,2);
            touchpad->setParent(window);
            int step=0;
            const bool cancelGrab=qEnvironmentVariableIsSet("CLIPCUT_TEST_CANCEL_MUTE_GRAB");
            for(int y:{-70,-130,80,140,-50}) {
                if(qEnvironmentVariableIsSet("CLIPCUT_TEST_TOUCHPAD_SCROLL")) {
                    if(nativePointer)seat("scroll 30");
                    else {QWheelEvent wheel(press,window->mapToGlobal(press),QPoint(0,-30),QPoint(),Qt::LeftButton,Qt::NoModifier,step==0?Qt::ScrollBegin:Qt::ScrollUpdate,false,Qt::MouseEventNotSynthesized,touchpad);QCoreApplication::sendEvent(window,&wheel);QTest::qWait(30);}
                }
                if(cancelGrab&&step==1){grabber->ungrabMouse();editor->endGesture();QTest::qWait(30);}
                position=press+QPoint(++step*8,y);movePointer(position);QTest::qWait(40);
                if(!editor->muteDragging()){fail(name+": window-owned drag ended before release");return false;}
                if(!cancelGrab&&(!grabber||window->mouseGrabberItem()!=grabber||!grabber->property("pressed").toBool())){fail(name+": mouse grab was lost outside the audio row");return false;}
                const auto expected=initial+step*8/first->width()*(first->property("viewEnd").toDouble()-first->property("viewStart").toDouble());
                if(std::abs(value()-expected)>.02){fail(name+": horizontal movement stopped outside the audio row");return false;}
                if(flick&&std::abs(flick->property("contentY").toDouble()-scrollY)>.5){fail(name+": mute dragging scrolled the audio list");return false;}
            }
            if(nativePointer)seat("release button1");else QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position);
            if(nativePointer&&qEnvironmentVariableIsSet("CLIPCUT_TEST_TOUCHPAD_SCROLL"))seat("scroll-end");
            QTest::qWait(40);
            if(flick)flick->setProperty("interactive",interactive);
            if(editor->muteDragging()){fail(name+": drag remained active after release");return false;}
            const auto final=value();movePointer(position+QPoint(30,20));QTest::qWait(30);
            if(std::abs(value()-final)>.000001){fail(name+": movement after release still changed the mute");return false;}
            undo();if(std::abs(value()-initial)>.000001){fail(name+": outside drag was not one undo action");return false;}
            redo();if(std::abs(value()-final)>.000001){fail(name+": outside drag redo failed");return false;}undo();++cases;return true;
        };
        if(!outsideDrag("Mute start edge",range(0,0)["start"].toDouble(),[&]{return range(0,0)["start"].toDouble();}))return;
        if(!outsideDrag("Mute end edge",range(0,0)["end"].toDouble(),[&]{return range(0,0)["end"].toDouble();}))return;
        if(!outsideDrag("Mute body",(range(0,0)["start"].toDouble()+range(0,0)["end"].toDouble())/2,[&]{return range(0,0)["start"].toDouble();}))return;
        if(flick&&qEnvironmentVariableIsSet("CLIPCUT_TEST_TOUCHPAD_SCROLL")&&flick->property("contentHeight").toDouble()>flick->property("height").toDouble()+1) {
            const double before=flick->property("contentY").toDouble();
            const QPoint p=point(first,.1);
            QWheelEvent wheel(p,window->mapToGlobal(p),QPoint(0,-30),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(window,&wheel);QTest::qWait(150);
            if(flick->property("contentY").toDouble()<=before+.5){fail("Audio scrolling did not resume after mute release");return;}
            flick->setProperty("contentY",before);QTest::qWait(50);
        }
        for(double at:{1.1,range(0,0)["start"].toDouble(),range(0,0)["end"].toDouble()}) {
            QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,point(first,at));QTest::qWait(40);
            if(!regions(0).isEmpty()||regions(1).size()!=1){fail("Right-click did not remove only its mute section");return;}undo();
            if(regions(0).size()!=1){fail("Right-click mute removal did not undo");return;}++cases;
        }
        editor->addMute(0,1.8,2.2);QTest::qWait(40);
        drag(first,2,1.3,Qt::NoModifier);
        if(regions(0).size()!=1||!near(range(0,0)["start"].toDouble(),.8)||!near(range(0,0)["end"].toDouble(),1.5)||!range(0,0)["selected"].toBool()){fail("Moving overlapping mutes did not merge or retain selection");return;}
        QTest::keyClick(window,Qt::Key_Delete);QTest::qWait(40);
        if(!regions(0).isEmpty()){fail("Merged mute selection did not follow the combined region");return;}undo();undo();
        if(regions(0).size()!=2){fail("Undo did not restore the two pre-merge mutes");return;}undo();++cases;
        editor->addMute(0,1.8,2.2);QTest::qWait(40);
        drag(first,range(0,0)["end"].toDouble(),1.95,Qt::NoModifier);
        if(regions(0).size()!=1||!near(range(0,0)["start"].toDouble(),.8)||!near(range(0,0)["end"].toDouble(),2.2)){fail("Resizing into another mute did not combine the ranges");return;}undo();
        if(regions(0).size()!=2){fail("Resize merge did not undo in one action");return;}undo();++cases;
        editor->addMute(0,1.8,2.2);QTest::qWait(40);
        drag(first,1.1,2.8,Qt::NoModifier);QTest::keyClick(window,Qt::Key_Delete);QTest::qWait(40);
        if(regions(0).size()!=1||!near(range(0,0)["start"].toDouble(),1.8)||!near(range(0,0)["end"].toDouble(),2.2)){fail("Keyboard focus did not follow a mute moved past another section");return;}undo();undo();undo();++cases;
        editor->addDelete(.6,1);editor->addDelete(2.2,2.6);QTest::qWait(40);
        drag(main,.8,3.2,Qt::NoModifier);QTest::keyClick(window,Qt::Key_Delete);QTest::qWait(40);deletes=editor->state()["deletes"].toList();
        if(deletes.size()!=1||!near(deletes[0].toMap()["start"].toDouble(),2.2)||!near(deletes[0].toMap()["end"].toDouble(),2.6)){fail("Keyboard focus did not follow a delete moved past another section");return;}undo();undo();undo();undo();++cases;
        preview->seek(.1,true);editor->dispatch("deleteStart");
        if(editor->state()["deletes"].toList().size()!=1){fail("Merging discarded an unfinished delete start marker");return;}
        preview->seek(.5,true);editor->dispatch("deleteEnd");
        deletes=editor->state()["deletes"].toList();if(deletes.size()!=1||!near(deletes[0].toMap()["start"].toDouble(),.1)||!near(deletes[0].toMap()["end"].toDouble(),.5)){fail("Marked delete did not retain its start after merging");return;}undo();undo();++cases;
        for(auto item:window->findChildren<QObject*>())if(item->property("text").toString()=="+ Delete"){fail("Legacy delete toolbar button remains");return;}
        const auto summary=editor->exportSummary({{"extractTrack",0}});
        if(!summary["summary"].toString().contains("encode")){fail("Muted extraction offered audio copying");return;}
        window->grabWindow().save(QDir(directory).filePath("mutes.png"));
        // Exercise actual solo playback with the per-track mute filter active.
        editor->dispatch("mix");editor->solo(0,false);preview->seek(0,true);QTest::qWait(200);preview->play(true);QTest::qWait(3200);preview->play(false);QTest::qWait(100);
        if(!preview->error().isEmpty()){fail("Muted preview failed: "+preview->error());return;}
        QObject::connect(qobject_cast<clipcut::ExportJob*>(editor->job()),&clipcut::ExportJob::finished,window,[=](bool ok,const QString&,const QString &message){log->event("mute_ui_test",{{"success",ok},{"cases",cases},{"message",message}});QCoreApplication::exit(ok?0:42);});
        editor->startExport(QUrl::fromLocalFile(QDir(directory).absoluteFilePath("muted.mp4")),{{"overwrite",true}});
    });QTimer::singleShot(1500,timer,[timer]{timer->start();});
}
