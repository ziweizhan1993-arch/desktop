#include "web.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "display.h"

static WebServer s_server(80);

static const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Dashboard</title>
<style>
body{margin:0;height:100vh;background:#111;color:#999;font:14px system-ui;display:flex;flex-direction:column;align-items:center;justify-content:center}
canvas{width:960px;max-width:94vw;image-rendering:pixelated;border-radius:14px;box-shadow:0 0 0 12px #222,0 12px 48px #000;cursor:grab;touch-action:none}
#s{margin-top:24px}
</style></head><body>
<canvas id="c" width="480" height="320"></canvas>
<div id="s">connecting...</div>
<script>
const c=document.getElementById('c'),ctx=c.getContext('2d'),s=document.getElementById('s');
let img=ctx.createImageData(c.width,c.height),frame=-1,n=0,fps=0,t0=performance.now();

async function loop(){
  try{
    const r=await fetch('/fb?since='+frame,{cache:'no-store'});
    if(r.status===200){
      const w=+r.headers.get('X-Width'),h=+r.headers.get('X-Height');
      if(c.width!==w||c.height!==h){c.width=w;c.height=h;img=ctx.createImageData(w,h);}
      frame=+r.headers.get('X-Frame');
      const px=new Uint16Array(await r.arrayBuffer()),d=img.data;
      for(let i=0,j=0;i<px.length;i++,j+=4){const v=px[i];d[j]=(v>>8)&0xF8;d[j+1]=(v>>3)&0xFC;d[j+2]=(v<<3)&0xF8;d[j+3]=255;}
      ctx.putImageData(img,0,0);n++;
    }
    const now=performance.now();
    if(now-t0>1000){fps=n*1000/(now-t0);n=0;t0=now;}
    s.textContent='实时画面 · '+fps.toFixed(1)+' fps · 鼠标拖动可左右滑动、点击按钮';
  }catch(e){
    s.textContent='连接断开，重试中...';
    await new Promise(r=>setTimeout(r,1000));
  }
  setTimeout(loop,20);
}
loop();

const queue=[];let busy=false,down=false;
function pos(e){const r=c.getBoundingClientRect();return{x:Math.round((e.clientX-r.left)*c.width/r.width),y:Math.round((e.clientY-r.top)*c.height/r.height)};}
function send(p,ev,move){
  const last=queue[queue.length-1];
  const item={...pos(ev),p,move};
  if(move&&last&&last.move)queue[queue.length-1]=item;else queue.push(item);
  pump();
}
async function pump(){
  if(busy||!queue.length)return;
  busy=true;const q=queue.shift();
  try{await fetch(`/touch?x=${q.x}&y=${q.y}&p=${q.p}`,{cache:'no-store'});}catch(e){}
  busy=false;pump();
}
c.addEventListener('pointerdown',e=>{down=true;c.setPointerCapture(e.pointerId);send(1,e,false);});
c.addEventListener('pointermove',e=>{if(down)send(1,e,true);});
c.addEventListener('pointerup',e=>{down=false;send(0,e,false);});
</script></body></html>)HTML";

static void handleFb() {
  const uint16_t* fb;
  int w, h;
  uint32_t frame;
  displayFramebuffer(&fb, &w, &h, &frame);
  if (!fb) {
    s_server.send(503, "text/plain", "no framebuffer");
    return;
  }
  if (s_server.hasArg("since") && strtoul(s_server.arg("since").c_str(), nullptr, 10) == frame) {
    s_server.send(204);
    return;
  }

  const size_t size = (size_t)w * h * sizeof(uint16_t);
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.sendHeader("X-Width", String(w));
  s_server.sendHeader("X-Height", String(h));
  s_server.sendHeader("X-Frame", String(frame));
  s_server.setContentLength(size);
  s_server.send(200, "application/octet-stream", "");

  WiFiClient client = s_server.client();
  const uint8_t* p = (const uint8_t*)fb;
  size_t left = size;
  while (left && client.connected()) {
    const size_t n = client.write(p, min(left, (size_t)8192));
    if (n == 0) break;
    p += n;
    left -= n;
  }
}

static void handleTouch() {
  displayVirtualTouch(s_server.arg("x").toInt(), s_server.arg("y").toInt(), s_server.arg("p") == "1");
  s_server.send(204);
}

static void webTask(void*) {
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(200));

  MDNS.begin("esp32dash");
  s_server.on("/", [] { s_server.send_P(200, "text/html; charset=utf-8", kPage); });
  s_server.on("/fb", handleFb);
  s_server.on("/touch", handleTouch);
  s_server.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("[web] preview: http://%s/  or  http://esp32dash.local/\n",
                WiFi.localIP().toString().c_str());

  for (;;) {
    s_server.handleClient();
    vTaskDelay(1);
  }
}

void webBegin() { xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0); }
