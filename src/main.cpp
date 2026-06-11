#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <esp_wifi.h>

// ==========================
// Configuración del AP
// ==========================
const char* ssid = "COCHERC_B";
const char* password = "87654321";
const int canal = 2;

// Configuración IP fija
IPAddress local_IP(192, 168, 4, 5);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// ==========================
// Objetos globales
// ==========================
WebServer server(80);
Servo servoDireccion;
Servo servoAuxiliar;

// ==========================
// Pines
// ==========================
const int In2 = 25;       // Dirección 1 motor
const int In3 = 33;       // Dirección 2 motor
const int ENB = 32;       // PWM del motor
const int servoPin = 13;  // Servo dirección principal
const int servoAuxPin = 27; // Servo auxiliar nuevo

// ==========================
// Estado del sistema
// ==========================
char modo = 'P';
int velocidad = 0;

// ==========================
// Servo principal
// ==========================
int anguloServo = 90;
volatile int anguloObjetivo = 90;
bool servoAdjunto = false;

unsigned long ultimoPasoServo = 0;
const unsigned long intervaloServoMs = 15;
const int pasoServo = 2;

unsigned long servoQuietoDesde = 0;
const unsigned long detachServoDelayMs = 300;

// ==========================
// Servo auxiliar
// ==========================
int anguloServoAux = 90;
volatile int anguloObjetivoAux = 90;
bool servoAuxAdjunto = false;

unsigned long ultimoPasoServoAux = 0;
const unsigned long intervaloServoAuxMs = 15;
const int pasoServoAux = 2;

unsigned long servoAuxQuietoDesde = 0;
const unsigned long detachServoAuxDelayMs = 300;

// ==========================
// Rate limit backend
// ==========================
unsigned long ultimaDireccionAceptadaMs = 0;
unsigned long ultimaVelocidadAceptadaMs = 0;
unsigned long ultimaDireccionAuxAceptadaMs = 0;

const unsigned long minIntervaloDireccionMs = 25;
const unsigned long minIntervaloVelocidadMs = 40;
const unsigned long minIntervaloDireccionAuxMs = 25;

// ==========================
// Watchdog del AP
// ==========================
volatile bool apIniciado = false;
unsigned long ultimoChequeoAP = 0;
const unsigned long intervaloChequeoAPMs = 3000;

// ==========================
// Utilidades
// ==========================
int redondearA5(int valor) {
    return ((valor + 2) / 5) * 5;
}

// ==========================
// Frontend
// ==========================
const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
<title>Control Coche RC</title>
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#1a1a1a;font-family:Arial,sans-serif;color:#fff;height:100vh;overflow:hidden;touch-action:none;user-select:none;-webkit-user-select:none}
h1{color:#00ff88;text-align:center;font-size:1.3em;padding:7px 0;text-shadow:0 0 10px #00ff88;letter-spacing:2px}
.pad{display:grid;grid-template-areas:"ax ax""jd jv";grid-template-columns:1fr 1fr;grid-template-rows:auto 1fr;height:calc(100vh - 44px);padding:8px;gap:12px}
.jd{grid-area:jd}.jv{grid-area:jv}.ax{grid-area:ax}
.jw{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:8px}
.jl{font-size:.68em;color:#777;text-transform:uppercase;letter-spacing:1px}
.jp{width:150px;height:150px;border-radius:50%;background:#252525;border:2px solid #00ff88;box-shadow:0 0 18px rgba(0,255,136,.22);position:relative;display:flex;align-items:center;justify-content:center;cursor:pointer;touch-action:none}
.jp::before{content:'';position:absolute;background:rgba(0,255,136,.18)}
.jd .jp::before{width:88%;height:2px;border-radius:1px}
.jv .jp::before{width:2px;height:88%;border-radius:1px}
.jt{width:52px;height:52px;border-radius:50%;background:radial-gradient(circle at 35% 35%,#44ffbb,#00bb55);box-shadow:0 0 14px #00ff88;pointer-events:none;will-change:transform;transition:box-shadow .1s}
.jt.on{box-shadow:0 0 24px #00ff88,0 0 50px rgba(0,255,136,.4)}
.jnum{font-size:.95em;font-weight:bold;color:#00ff88;background:#222;padding:4px 12px;border-radius:6px;min-width:64px;text-align:center;border:1px solid #444;font-family:monospace}
.ax{display:flex;align-items:center;justify-content:center}
.axb{background:#2b2b2b;border-radius:12px;padding:12px 18px;box-shadow:0 0 14px rgba(0,255,136,.12);width:100%;max-width:340px}
.axh{display:flex;justify-content:space-between;margin-bottom:10px}
.axh span{font-size:.82em;color:#00ff88}
.axh span:last-child{background:#1a1a1a;padding:1px 8px;border-radius:4px;border:1px solid #333;font-family:monospace}
input[type=range]{width:100%;height:8px;background:#333;border-radius:4px;outline:none;-webkit-appearance:none;cursor:pointer}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:26px;height:26px;background:#00ff88;border-radius:50%;box-shadow:0 0 8px #00ff88;cursor:pointer}
@media(orientation:landscape){
h1{font-size:1em;padding:4px 0}
.pad{grid-template-areas:"jd ax jv";grid-template-columns:auto 1fr auto;grid-template-rows:1fr;height:calc(100vh - 32px);padding:6px;gap:14px;align-items:center}
.jp{width:120px;height:120px}
.jt{width:44px;height:44px}
.axb{max-width:100%}
}
</style>
</head>
<body>
<h1>Carro RC</h1>
<div class="pad">
  <div class="jw jd">
    <span class="jl">Direccion</span>
    <div class="jp" id="joyDir"><div class="jt" id="tDir"></div></div>
    <span class="jnum" id="vDir">90</span>
  </div>
  <div class="ax">
    <div class="axb">
      <div class="axh">
        <span>Servo Auxiliar</span>
        <span id="vAux">90</span>
      </div>
      <input type="range" min="20" max="160" step="5" value="90" id="slAux">
    </div>
  </div>
  <div class="jw jv">
    <span class="jl">Avanzar</span>
    <div class="jp" id="joyVel"><div class="jt" id="tVel"></div></div>
    <span class="jnum" id="vVel">0</span>
    <span class="jl">Reversa</span>
  </div>
</div>
<script>
function throttle(fn,ms){
  var t=0,p=null;
  var f=function(){var n=Date.now(),r=ms-(n-t),a=arguments;clearTimeout(p);if(r<=0){t=n;fn.apply(null,a)}else p=setTimeout(function(){t=Date.now();fn.apply(null,a)},r)};
  f.cancel=function(){clearTimeout(p)};
  return f;
}
function r5(v){return Math.round(v/5)*5}
function joy(pid,thid,axis,onMove,onRelease){
  var pad=document.getElementById(pid),th=document.getElementById(thid);
  var on=false,cx=0,cy=0,tId=-1;
  function R(){return(pad.clientWidth-th.clientWidth)/2}
  function move(x,y){
    var r=R(),dx=x-cx,dy=y-cy;
    if(axis==='x'){dx=Math.max(-r,Math.min(r,dx));dy=0;onMove(dx/r)}
    else{dy=Math.max(-r,Math.min(r,dy));dx=0;onMove(dy/r)}
    th.style.transform='translate('+dx+'px,'+dy+'px)';
  }
  function release(){on=false;tId=-1;th.classList.remove('on');th.style.transform='translate(0,0)';onRelease()}
  pad.addEventListener('touchstart',function(e){
    e.preventDefault();
    if(on)return;
    var t=e.changedTouches[0];
    tId=t.identifier;on=true;th.classList.add('on');
    var rc=pad.getBoundingClientRect();cx=rc.left+rc.width/2;cy=rc.top+rc.height/2;
    move(t.clientX,t.clientY);
  },{passive:false});
  document.addEventListener('touchmove',function(e){
    if(!on)return;
    for(var i=0;i<e.changedTouches.length;i++){
      if(e.changedTouches[i].identifier===tId){move(e.changedTouches[i].clientX,e.changedTouches[i].clientY);return}
    }
  },{passive:false});
  document.addEventListener('touchend',function(e){
    if(!on)return;
    for(var i=0;i<e.changedTouches.length;i++){if(e.changedTouches[i].identifier===tId){release();return}}
  });
  document.addEventListener('touchcancel',function(e){
    if(!on)return;
    for(var i=0;i<e.changedTouches.length;i++){if(e.changedTouches[i].identifier===tId){release();return}}
  });
  pad.addEventListener('mousedown',function(e){
    e.preventDefault();on=true;th.classList.add('on');
    var rc=pad.getBoundingClientRect();cx=rc.left+rc.width/2;cy=rc.top+rc.height/2;
    move(e.clientX,e.clientY);
  });
  document.addEventListener('mousemove',function(e){if(on&&tId===-1)move(e.clientX,e.clientY)});
  document.addEventListener('mouseup',function(){if(on&&tId===-1)release()});
}
var sDir=throttle(function(n){
  var a=+(Math.max(20,Math.min(160,90-n*70)).toFixed(1));
  document.getElementById('vDir').textContent=a;
  fetch('/direccion?value='+a).catch(function(){});
},80);
function rDir(){sDir.cancel();document.getElementById('vDir').textContent=90;fetch('/direccion?value=90').catch(function(){})}
function mapV(n){var d=.05,a=Math.abs(n);if(a<d)return 0;var s=r5(Math.round(85+(a-d)/(1-d)*170));return n<0?s:-s}
var lm=null;
var sVel=throttle(function(n){
  var v=mapV(n);
  document.getElementById('vVel').textContent=v;
  if(v!==lm){lm=v;fetch('/motor?value='+v).catch(function(){})}
},80);
function rVel(){sVel.cancel();lm=0;document.getElementById('vVel').textContent=0;fetch('/motor?value=0').catch(function(){})}
joy('joyDir','tDir','x',sDir,rDir);
joy('joyVel','tVel','y',sVel,rVel);
var sl=document.getElementById('slAux');
var sAux=throttle(function(v){v=r5(Math.max(20,Math.min(160,v)));document.getElementById('vAux').textContent=v;fetch('/servoaux?value='+v).catch(function(){})},80);
sl.addEventListener('input',function(e){sAux(parseInt(e.target.value))});
</script>
</body>
</html>
)rawliteral";

// ==========================
// Eventos WiFi
// ==========================
void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_START:
            apIniciado = true;
            break;

        case ARDUINO_EVENT_WIFI_AP_STOP:
            apIniciado = false;
            break;

        default:
            break;
    }
}

// ==========================
// Utilidades AP
// ==========================
void iniciarAccessPoint() {
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);

    WiFi.softAPdisconnect(true);
    delay(100);

    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(ssid, password, canal, 0, 1);

    apIniciado = true;
}

void vigilarAccessPoint() {
    unsigned long ahora = millis();
    if (ahora - ultimoChequeoAP < intervaloChequeoAPMs) {
        return;
    }
    ultimoChequeoAP = ahora;

    wifi_mode_t modoWifi;
    esp_wifi_get_mode(&modoWifi);

    if (modoWifi != WIFI_MODE_AP || !apIniciado) {
        iniciarAccessPoint();
    }
}

// ==========================
// Servo principal
// ==========================
void adjuntarServoSiHaceFalta() {
    if (!servoAdjunto) {
        servoDireccion.setPeriodHertz(50);
        servoDireccion.attach(servoPin, 500, 2400);
        servoDireccion.write(anguloServo);
        servoAdjunto = true;
    }
}

void desadjuntarServoSiQuieto() {
    if (!servoAdjunto) return;
    if (anguloServo != anguloObjetivo) return;

    unsigned long ahora = millis();
    if (servoQuietoDesde == 0) {
        servoQuietoDesde = ahora;
        return;
    }

    if (ahora - servoQuietoDesde >= detachServoDelayMs) {
        servoDireccion.detach();
        servoAdjunto = false;
    }
}

void actualizarServoSuave() {
    unsigned long ahora = millis();

    if (anguloServo == anguloObjetivo) {
        desadjuntarServoSiQuieto();
        return;
    }

    servoQuietoDesde = 0;

    if (ahora - ultimoPasoServo < intervaloServoMs) {
        return;
    }
    ultimoPasoServo = ahora;

    adjuntarServoSiHaceFalta();

    if (anguloServo < anguloObjetivo) {
        anguloServo += pasoServo;
        if (anguloServo > anguloObjetivo) {
            anguloServo = anguloObjetivo;
        }
    } else {
        anguloServo -= pasoServo;
        if (anguloServo < anguloObjetivo) {
            anguloServo = anguloObjetivo;
        }
    }

    servoDireccion.write(anguloServo);
}

// ==========================
// Servo auxiliar
// ==========================
void adjuntarServoAuxSiHaceFalta() {
    if (!servoAuxAdjunto) {
        servoAuxiliar.setPeriodHertz(50);
        servoAuxiliar.attach(servoAuxPin, 500, 2400);
        servoAuxiliar.write(anguloServoAux);
        servoAuxAdjunto = true;
    }
}

void desadjuntarServoAuxSiQuieto() {
    if (!servoAuxAdjunto) return;
    if (anguloServoAux != anguloObjetivoAux) return;

    unsigned long ahora = millis();
    if (servoAuxQuietoDesde == 0) {
        servoAuxQuietoDesde = ahora;
        return;
    }

    if (ahora - servoAuxQuietoDesde >= detachServoAuxDelayMs) {
        servoAuxiliar.detach();
        servoAuxAdjunto = false;
    }
}

void actualizarServoAuxSuave() {
    unsigned long ahora = millis();

    if (anguloServoAux == anguloObjetivoAux) {
        desadjuntarServoAuxSiQuieto();
        return;
    }

    servoAuxQuietoDesde = 0;

    if (ahora - ultimoPasoServoAux < intervaloServoAuxMs) {
        return;
    }
    ultimoPasoServoAux = ahora;

    adjuntarServoAuxSiHaceFalta();

    if (anguloServoAux < anguloObjetivoAux) {
        anguloServoAux += pasoServoAux;
        if (anguloServoAux > anguloObjetivoAux) {
            anguloServoAux = anguloObjetivoAux;
        }
    } else {
        anguloServoAux -= pasoServoAux;
        if (anguloServoAux < anguloObjetivoAux) {
            anguloServoAux = anguloObjetivoAux;
        }
    }

    servoAuxiliar.write(anguloServoAux);
}

// ==========================
// Rutas
// ==========================
void configurarRutas() {
    server.on("/", []() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.send_P(200, "text/html", html);
    });

    server.on("/command", []() {
        if (!server.hasArg("cmd")) {
            server.send(400, "text/plain", "Falta cmd");
            return;
        }

        String cmd = server.arg("cmd");

        if (cmd == "avanzar") {
            modo = 'A';
            digitalWrite(In2, LOW);
            digitalWrite(In3, HIGH);
            ledcWrite(0, velocidad);
            server.send(200, "text/plain", "OK");
            return;
        }

        if (cmd == "reversa") {
            modo = 'R';
            digitalWrite(In2, HIGH);
            digitalWrite(In3, LOW);
            ledcWrite(0, velocidad);
            server.send(200, "text/plain", "OK");
            return;
        }

        if (cmd == "parar") {
            modo = 'P';
            digitalWrite(In2, LOW);
            digitalWrite(In3, LOW);
            ledcWrite(0, 0);
            server.send(200, "text/plain", "OK");
            return;
        }

        server.send(400, "text/plain", "Comando invalido");
    });

    server.on("/velocidad", []() {
        if (!server.hasArg("value")) {
            server.send(400, "text/plain", "Falta value");
            return;
        }

        unsigned long ahora = millis();
        if (ahora - ultimaVelocidadAceptadaMs < minIntervaloVelocidadMs) {
            server.send(200, "text/plain", "OK");
            return;
        }
        ultimaVelocidadAceptadaMs = ahora;

        int nuevaVelocidad = server.arg("value").toInt();
        nuevaVelocidad = redondearA5(nuevaVelocidad);
        nuevaVelocidad = constrain(nuevaVelocidad, 85, 230);

        if (nuevaVelocidad != velocidad) {
            velocidad = nuevaVelocidad;

            if (modo != 'P') {
                ledcWrite(0, velocidad);
            }
        }

        server.send(200, "text/plain", "OK");
    });

    server.on("/direccion", []() {
        if (!server.hasArg("value")) {
            server.send(400, "text/plain", "Falta value");
            return;
        }

        float nuevoAnguloF = server.arg("value").toFloat();
        nuevoAnguloF = constrain(nuevoAnguloF, 20.0f, 160.0f);
        int nuevoAngulo = (int)round(nuevoAnguloF);

        if (nuevoAngulo != 90) {
            unsigned long ahora = millis();
            if (ahora - ultimaDireccionAceptadaMs < minIntervaloDireccionMs) {
                server.send(200, "text/plain", "OK");
                return;
            }
            ultimaDireccionAceptadaMs = ahora;
        }

        if (nuevoAngulo == 90 || abs(anguloObjetivo - nuevoAngulo) >= 2) {
            anguloObjetivo = nuevoAngulo;
        }

        server.send(200, "text/plain", "OK");
    });

    server.on("/servoaux", []() {
        if (!server.hasArg("value")) {
            server.send(400, "text/plain", "Falta value");
            return;
        }

        unsigned long ahora = millis();
        if (ahora - ultimaDireccionAuxAceptadaMs < minIntervaloDireccionAuxMs) {
            server.send(200, "text/plain", "OK");
            return;
        }
        ultimaDireccionAuxAceptadaMs = ahora;

        int nuevoAnguloAux = server.arg("value").toInt();
        nuevoAnguloAux = redondearA5(nuevoAnguloAux);
        nuevoAnguloAux = constrain(nuevoAnguloAux, 20, 160);

        if (abs(anguloObjetivoAux - nuevoAnguloAux) >= 2) {
            anguloObjetivoAux = nuevoAnguloAux;
        }

        server.send(200, "text/plain", "OK");
    });

    server.on("/motor", []() {
        if (!server.hasArg("value")) {
            server.send(400, "text/plain", "Falta value");
            return;
        }

        int val = server.arg("value").toInt();
        val = constrain(val, -255, 255);

        if (val != 0) {
            unsigned long ahora = millis();
            if (ahora - ultimaVelocidadAceptadaMs < minIntervaloVelocidadMs) {
                server.send(200, "text/plain", "OK");
                return;
            }
            ultimaVelocidadAceptadaMs = ahora;
        }

        if (val == 0) {
            modo = 'P';
            velocidad = 0;
            digitalWrite(In2, LOW);
            digitalWrite(In3, LOW);
            ledcWrite(0, 0);
        } else if (val > 0) {
            modo = 'A';
            velocidad = val;
            digitalWrite(In2, LOW);
            digitalWrite(In3, HIGH);
            ledcWrite(0, velocidad);
        } else {
            modo = 'R';
            velocidad = -val;
            digitalWrite(In2, HIGH);
            digitalWrite(In3, LOW);
            ledcWrite(0, velocidad);
        }

        server.send(200, "text/plain", "OK");
    });

    server.onNotFound([]() {
        server.send(404, "text/plain", "Not found");
    });
}

// ==========================
// Setup
// ==========================
void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);

    WiFi.onEvent(onWiFiEvent);

    pinMode(In2, OUTPUT);
    pinMode(In3, OUTPUT);
    pinMode(ENB, OUTPUT);

    digitalWrite(In2, LOW);
    digitalWrite(In3, LOW);

    ledcSetup(0, 10000, 8);
    ledcAttachPin(ENB, 0);
    ledcWrite(0, 0);

    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    adjuntarServoSiHaceFalta();
    servoDireccion.write(anguloServo);

    adjuntarServoAuxSiHaceFalta();
    servoAuxiliar.write(anguloServoAux);

    iniciarAccessPoint();

    configurarRutas();
    server.begin();
}

// ==========================
// Loop
// ==========================
void loop() {
    server.handleClient();
    actualizarServoSuave();
    actualizarServoAuxSuave();
    vigilarAccessPoint();
    delay(1);
}