import QtQuick

Canvas {
    id:ruler
    property real viewStart:0
    property real viewEnd:1
    implicitHeight:20
    function label(time,digits) {
        const s=Math.max(0,time)
        return(s>=3600?Math.floor(s/3600)+":":"")+String(Math.floor(s/60)%60).padStart(2,"0")+":"+String(Math.floor(s)%60).padStart(2,"0")+(digits?"."+String(Math.floor((s%1)*Math.pow(10,digits)+1e-6)).padStart(digits,"0"):"")
    }
    onPaint:{
        const c=getContext("2d");c.clearRect(0,0,width,height)
        if(width<1)return
        const span=Math.max(.001,viewEnd-viewStart)
        const intervals=[.1,.25,.5,1,2,5,10,15,30,60,120,300,600,1800,3600,7200,14400]
        const target=span/Math.max(1,width/95)
        let step=intervals[intervals.length-1]
        for(const interval of intervals){if(interval>=target){step=interval;break}}
        c.font='10px "'+appTheme.fontFamily+'"';c.strokeStyle=appTheme.colors.ruler;c.fillStyle=appTheme.colors.muted;c.lineWidth=1
        const startText=label(viewStart,3),endText=label(viewEnd,3)
        const startWidth=c.measureText(startText).width,endWidth=c.measureText(endText).width
        c.fillText(startText,0,18);c.fillText(endText,width-endWidth,18)
        c.beginPath()
        for(let t=Math.ceil(viewStart/step)*step;t<=viewEnd;t+=step) {
            const x=(t-viewStart)/span*width
            c.moveTo(Math.round(x)+.5,0);c.lineTo(Math.round(x)+.5,4)
            const text=label(t,step<.5?2:step<1?1:0),tw=c.measureText(text).width
            if(x-tw/2>startWidth+12&&x+tw/2<width-endWidth-12)c.fillText(text,x-tw/2,18)
        }
        c.stroke()
    }
    onViewStartChanged:requestPaint()
    onViewEndChanged:requestPaint()
    onWidthChanged:requestPaint()
    onHeightChanged:requestPaint()
    Connections{target:appTheme;function onChanged(){ruler.requestPaint()}}
}
