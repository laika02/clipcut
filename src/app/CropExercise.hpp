#pragma once
#include <QQuickWindow>
#include <QQuickItem>
#include <QtTest>
#include "Editor.hpp"
#include "PerformanceLog.hpp"

inline void exerciseCrop(QQuickWindow *window,Editor *editor,Preview *preview,const QString &directory,PerformanceLog *log) {
    auto timer=new QTimer(window);timer->setInterval(100);
    QObject::connect(timer,&QTimer::timeout,window,[=] {
        if(!editor->state()["loaded"].toBool()||preview->duration()<=0)return;
        timer->stop();preview->play(false);window->requestActivate();QDir().mkpath(directory);
        const auto rect=[&]{const auto s=editor->state();return QRect(s["cropX"].toInt(),s["cropY"].toInt(),s["cropW"].toInt(),s["cropH"].toInt());};
        const auto describe=[](QRect r){return QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());};
        const auto fail=[&](const QString &message){window->grabWindow().save(QDir(directory).filePath("failure.png"));log->event("crop_test",{{"success",false},{"message",message},{"actual",describe(rect())}});QCoreApplication::exit(40);};
        auto overlay=window->findChild<QQuickItem*>("cropOverlay");if(!overlay){fail("Crop overlay missing");return;}
        const auto findCorner=[&](auto &&self,QQuickItem *item,int index)->QQuickItem* {
            if(item->objectName()=="editHandle"&&!item->property("cropEdge").isValid()&&item->property("index").isValid()&&item->property("index").toInt()==index)return item;
            for(auto child:item->childItems())if(auto found=self(self,child,index))return found;
            return nullptr;
        };
        const QRect base(384,216,1152,648);
        const auto setBase=[&]{editor->crop(base.x(),base.y(),base.width(),base.height());QTest::qWait(40);};
        const auto undoGesture=[&](QRect final){
            QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(20);
            if(rect()!=base){fail("A drag must undo back to its initial rectangle in one step");return false;}
            QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);QTest::qWait(20);
            if(rect()!=final){fail("Redo must restore the complete drag to "+describe(final));return false;}
            QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(20);return true;
        };
        int cases=0;
        for(const QSize size:{QSize(1180,780),QSize(800,420)}) {
            window->resize(size);QTest::qWait(200);
            for(int index=0;index<4;++index) {
                setBase();auto corner=findCorner(findCorner,overlay,index);if(!corner){fail("Crop corner missing");return;}
                const auto sx=overlay->width()/editor->state()["width"].toDouble(),sy=overlay->height()/editor->state()["height"].toDouble();
                const QPoint grabOffsets[]={QPoint(5,3),QPoint(-4,5),QPoint(3,-5),QPoint(-5,-4)};
                auto press=corner->mapToScene(QPointF(corner->width()/2,corner->height()/2)).toPoint()+grabOffsets[index];
                const int signX=index%2?-1:1,signY=index>=2?-1:1;
                QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);QTest::qWait(25);
                if(rect()!=base){fail(QString("Corner %1 jumped just from grabbing its handle").arg(index));return;}
                QPoint position=press;
                for(int step:{1,2,3,4,5,6,7,8,7,6,5,4,3}) {
                    const QPoint delta(signX*step*3,signY*step*2);position=press+delta;
                    QTest::mouseMove(window,position,10);QTest::qWait(15);const auto actual=rect();
                    const int fixedX=index%2?actual.x():actual.x()+actual.width(),fixedY=index>=2?actual.y():actual.y()+actual.height();
                    const int expectedFixedX=index%2?base.x():base.x()+base.width(),expectedFixedY=index>=2?base.y():base.y()+base.height();
                    if(fixedX!=expectedFixedX||fixedY!=expectedFixedY){fail(QString("Corner %1 moved the opposite corner: expected %2,%3, got %4,%5").arg(index).arg(expectedFixedX).arg(expectedFixedY).arg(fixedX).arg(fixedY));return;}
                    const double expectedX=(index%2?base.x()+base.width():base.x())+delta.x()/sx,expectedY=(index>=2?base.y()+base.height():base.y())+delta.y()/sy;
                    if(std::abs((index%2?actual.x()+actual.width():actual.x())-expectedX)>2||std::abs((index>=2?actual.y()+actual.height():actual.y())-expectedY)>2){fail(QString("Corner %1 did not follow pointer displacement; grab offset or incremental drift").arg(index));return;}
                    QTest::qWait(25);if(rect()!=actual){fail("Crop continued moving with the pointer stationary");return;}
                }
                QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position);QTest::qWait(30);
                if(!undoGesture(rect()))return;++cases;
                // Both image boundaries and minimum size clamp the moving
                // corner without changing its original opposite anchor.
                for(bool outward:{true,false}) {
                    setBase();press=corner->mapToScene(QPointF(8,8)).toPoint();
                    const QPointF target=outward?QPointF(index%2?overlay->width()+20:-20,index>=2?overlay->height()+20:-20)
                        :QPointF((index%2?base.x()-40:base.x()+base.width()+40)*sx,(index>=2?base.y()-40:base.y()+base.height()+40)*sy);
                    position=overlay->mapToScene(target).toPoint();
                    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);QTest::mouseMove(window,position,30);QTest::qWait(30);
                    const int left=outward?(index%2?base.x():0):(index%2?base.x():base.x()+base.width()-16);
                    const int top=outward?(index>=2?base.y():0):(index>=2?base.y():base.y()+base.height()-16);
                    const int right=outward?(index%2?1920:base.x()+base.width()):(index%2?base.x()+16:base.x()+base.width());
                    const int bottom=outward?(index>=2?1080:base.y()+base.height()):(index>=2?base.y()+16:base.y()+base.height());
                    if(rect()!=QRect(left,top,right-left,bottom-top)){fail(QString("Corner %1 failed %2 clamping").arg(index).arg(outward?"image boundary":"minimum size"));return;}
                    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position);if(!undoGesture(rect()))return;++cases;
                }
            }
            for(int edge=0;edge<4;++edge) {
                const bool horizontal=edge<2;
                const auto sx=overlay->width()/1920,sy=overlay->height()/1080;
                const auto edgePoint=[&](QRect r){return overlay->mapToScene(QPointF((horizontal?(edge==0?r.x():r.x()+r.width()):r.x()+r.width()/2.)*sx,(horizontal?r.y()+r.height()/2.:(edge==2?r.y():r.y()+r.height()))*sy)).toPoint();};
                const auto expected=[&](double displacement){
                    QRect r=base;
                    const auto align=[](double value,int low,int high){return std::clamp(qRound(value/2)*2,low,high);};
                    if(edge==0){int x=align(base.x()+displacement,0,base.x()+base.width()-16);r=QRect(x,base.y(),base.x()+base.width()-x,base.height());}
                    if(edge==1)r.setWidth(align(base.x()+base.width()+displacement,base.x()+16,1920)-base.x());
                    if(edge==2){int y=align(base.y()+displacement,0,base.y()+base.height()-16);r=QRect(base.x(),y,base.width(),base.y()+base.height()-y);}
                    if(edge==3)r.setHeight(align(base.y()+base.height()+displacement,base.y()+16,1080)-base.y());
                    return r;
                };
                setBase();auto press=edgePoint(base)+QPoint(4,-3);auto position=press;
                QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);QTest::qWait(25);
                if(rect()!=base){fail("Edge jumped on an off-center grab");return;}
                for(int step:{1,2,3,4,5,6,5,4,3}) {
                    const QPoint delta(step*3,step*2);position=press+delta;
                    QTest::mouseMove(window,position,10);QTest::qWait(30);
                    if(rect()!=expected(horizontal?delta.x()/sx:delta.y()/sy)){fail(QString("Edge %1 drifted or changed the locked axis").arg(edge));return;}
                }
                QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position);if(!undoGesture(rect()))return;++cases;
                for(bool outward:{true,false}) {
                    setBase();press=edgePoint(base)+QPoint(4,-3);
                    const int direction=(edge==0||edge==2)?-1:1;
                    const QPoint delta=horizontal?QPoint((outward?direction:-direction)*int(2200*sx),47):QPoint(47,(outward?direction:-direction)*int(1400*sy));
                    position=press+delta;
                    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);QTest::mouseMove(window,position,30);QTest::qWait(30);
                    if(rect()!=expected(horizontal?delta.x()/sx:delta.y()/sy)){fail(QString("Edge %1 failed image/minimum clamping").arg(edge));return;}
                    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position);if(!undoGesture(rect()))return;++cases;
                }
                // Pointer-selected edges retain keyboard nudges, constrained
                // to their axis; Shift changes the step to ten source pixels.
                setBase();QTest::keyClick(window,horizontal?Qt::Key_Up:Qt::Key_Left);QTest::qWait(25);
                if(rect()!=base){fail("Edge keyboard nudge changed the locked axis");return;}
                QTest::keyClick(window,horizontal?Qt::Key_Left:Qt::Key_Up,Qt::ShiftModifier);QTest::qWait(25);
                if(rect()!=expected(-10)){fail("Focused edge Shift+arrow nudge failed");return;}
                if(!undoGesture(rect()))return;++cases;
                editor->dispatch("resetCrop");QTest::qWait(30);
                const QRect full(0,0,1920,1080);
                press=edgePoint(full)+(horizontal?QPoint(edge==0?-5:5,0):QPoint(0,edge==2?-5:5));
                position=press+(horizontal?QPoint(edge==0?12:-12,15):QPoint(15,edge==2?12:-12));
                QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);QTest::mouseMove(window,position,30);QTest::qWait(30);
                const auto actual=rect();
                if(actual==full||(horizontal?(actual.y()!=0||actual.height()!=1080):(actual.x()!=0||actual.width()!=1920))){fail("Full-frame edge's outer hit area or axis lock failed");return;}
                QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position);++cases;
            }
            // Crossing the picture boundary must clamp just that edge, not move the anchor.
            setBase();auto corner=findCorner(findCorner,overlay,0);
            auto press=corner->mapToScene(QPointF(8,8)).toPoint();auto leftOutside=overlay->mapToScene(QPointF(-20,base.y()*overlay->height()/1080+12)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);QTest::mouseMove(window,leftOutside,60);QTest::qWait(60);
            auto bounded=rect();if(bounded.x()!=0||bounded.x()+bounded.width()!=base.x()+base.width()||bounded.y()<=base.y()||bounded.y()+bounded.height()!=base.y()+base.height()){fail("Clamping one edge moved the opposite anchor or froze the other axis");return;}
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,leftOutside);if(!undoGesture(rect()))return;++cases;
            // Moving the middle must retain size and follow the original press, in both directions.
            setBase();press=overlay->mapToScene(QPointF((base.x()+base.width()/2.)*overlay->width()/1920,(base.y()+base.height()/2.)*overlay->height()/1080)).toPoint();
            const auto sx=overlay->width()/1920,sy=overlay->height()/1080;
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);QPoint position=press;
            for(int step:{1,2,3,4,5,6,5,4,3}) {
                QPoint delta(step*3,-step*2);position=press+delta;QTest::mouseMove(window,position,10);QTest::qWait(30);
                const auto actual=rect();if(actual.size()!=base.size()||std::abs(actual.x()-base.x()-delta.x()/sx)>2||std::abs(actual.y()-base.y()-delta.y()/sy)>2){fail("Middle drag changed crop size or drifted from pointer displacement");return;}
            }
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position);if(!undoGesture(rect()))return;++cases;
            // The outer half of a full-frame handle must remain grabbable.
            editor->dispatch("resetCrop");QTest::qWait(30);corner=findCorner(findCorner,overlay,0);
            press=corner->mapToScene(QPointF(3,3)).toPoint();position=press+QPoint(12,10);
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);QTest::mouseMove(window,position,30);QTest::qWait(30);
            const auto full=rect();if(full.x()<=0||full.y()<=0||full.x()+full.width()!=1920||full.y()+full.height()!=1080){fail("Full-frame corner's outer hit area failed");return;}
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position);++cases;
        }
        setBase();window->grabWindow().save(QDir(directory).filePath("crop.png"));
        log->event("crop_test",{{"success",true},{"cases",cases},{"message","Corners and axis-locked edges, off-center grabs, incremental/reverse motion, stationary pointer, image/minimum clamping, edge keyboard nudges, body move, full-frame hit areas, one-step undo/redo, two window sizes"}});QCoreApplication::exit(0);
    });
    QTimer::singleShot(1500,timer,[timer]{timer->start();});
}
