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
<!DOCTYPE HTML><html><head>
<title>Control Coche RC</title>
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
<style>
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;overflow:hidden}
body{background:#1a1a1a;font-family:Arial,sans-serif;color:#fff;display:flex;flex-direction:column;user-select:none;-webkit-user-select:none;touch-action:none}
h1{color:#00ff88;text-align:center;font-size:1em;padding:4px 0;text-shadow:0 0 10px #00ff88;letter-spacing:2px;flex-shrink:0}
.lay{display:grid;grid-template-columns:1fr auto;flex:1;min-height:0;padding:6px;gap:10px}
.cl{display:flex;flex-direction:column;min-height:0}
.cr{display:flex;flex-direction:row;align-items:stretch;gap:65px;min-height:0}
.sp{flex:1;min-height:0}
.cb{background:#252525;border-radius:10px;padding:8px 12px;border:1px solid #333;flex-shrink:0;max-width:48vw}
.ch{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}
.lbl{font-size:.68em;color:#00ff88;text-transform:uppercase;letter-spacing:1px}
.num{font-size:.85em;font-weight:bold;color:#00ff88;background:#1a1a1a;padding:2px 8px;border-radius:4px;border:1px solid #333;font-family:monospace}
.hsl{position:relative;height:36px;display:flex;align-items:center;touch-action:none;cursor:pointer}
.hsl .trk{position:absolute;left:0;right:0;height:8px;background:#333;border-radius:4px}
.hsl .thm{position:absolute;width:30px;height:30px;background:#00ff88;border-radius:50%;box-shadow:0 0 8px #00ff88;top:50%;transform:translate(-50%,-50%);pointer-events:none}
.cbtn{display:flex;flex-direction:row;align-items:center;justify-content:center;gap:5px;flex-shrink:0;align-self:flex-end;padding-bottom:6px}
.cvl{display:flex;flex-direction:column;align-items:center;min-height:0;flex-shrink:0;padding-right:50px}
.vw{flex:1;display:flex;flex-direction:column;align-items:center;width:100%;min-height:0;padding-bottom:22px}
.vsl{width:40px;flex:1;position:relative;touch-action:none;cursor:pointer}
.vsl .trk{position:absolute;top:0;bottom:0;left:50%;width:8px;transform:translateX(-50%);background:#333;border-radius:4px}
.vsl .thm{position:absolute;width:38px;height:38px;background:#00ff88;border-radius:50%;box-shadow:0 0 10px #00ff88;left:50%;transform:translate(-50%,-50%);pointer-events:none}
.btn{background:#252525;color:#00ff88;border:1px solid #444;border-radius:8px;padding:14px 6px;font-size:.62em;font-weight:bold;cursor:pointer;text-transform:uppercase;letter-spacing:.3px;touch-action:manipulation;width:72px}
.btn.on{background:#00ff88;color:#1a1a1a;box-shadow:0 0 10px rgba(0,255,136,.5)}
</style>
</head>
<body>
<h1>Carro RC</h1>
<div class="lay">
  <div class="cl">
    <div class="sp"></div>
    <div class="cb">
      <div class="ch"><span class="lbl">Direccion</span><span class="num" id="vDir">90</span></div>
      <div class="hsl" id="slDir"><div class="trk"></div><div class="thm"></div></div>
    </div>
    <div class="sp"></div>
    <div class="cb">
      <div class="ch"><span class="lbl">Servo Auxiliar</span><span class="num" id="vAux">90</span></div>
      <div class="hsl" id="slAux"><div class="trk"></div><div class="thm"></div></div>
    </div>
  </div>
  <div class="cr">
    <div class="cbtn">
      <button class="btn" id="bRev">Reversa</button>
      <button class="btn on" id="bAde">Adelante</button>
    </div>
    <div class="cvl">
      <span class="lbl">Velocidad</span>
      <span class="num" id="vVel">0</span>
      <div class="vw">
        <div class="vsl" id="slVel"><div class="trk"></div><div class="thm"></div></div>
      </div>
    </div>
  </div>
</div>
<script>
function throttle(fn,ms){var t=0,p=null;var f=function(){var n=Date.now(),r=ms-(n-t),a=arguments;clearTimeout(p);if(r<=0){t=n;fn.apply(null,a)}else p=setTimeout(function(){t=Date.now();fn.apply(null,a)},r)};f.cancel=function(){clearTimeout(p)};return f}
function r5(v){return Math.round(v/5)*5}
var curDir=1,curRaw=0;
function applyMotor(){var a=curRaw===0?0:r5(85+curRaw);fetch('/motor?value='+(a===0?0:curDir*a)).catch(function(){})}
function setDir(d){curDir=d==='avanzar'?1:-1;document.getElementById('bAde').className='btn'+(curDir===1?' on':'');document.getElementById('bRev').className='btn'+(curDir===-1?' on':'');applyMotor()}
document.getElementById('bRev').addEventListener('pointerdown',function(e){e.preventDefault();setDir('reversa')});
document.getElementById('bAde').addEventListener('pointerdown',function(e){e.preventDefault();setDir('avanzar')});
// Direction slider — custom pointer handling with explicit capture
(function(){
  var el=document.getElementById('slDir'),thm=el.querySelector('.thm');
  var pid=null,rect;
  function pos(v){thm.style.left=((v-20)/140*100)+'%'}
  pos(90);
  var tDir=throttle(function(s){document.getElementById('vDir').textContent=s;fetch('/direccion?value='+s).catch(function(){})},80);
  function upd(cx){var r=Math.max(0,Math.min(1,(cx-rect.left)/rect.width));var v=Math.round(r*28)*5+20;pos(v);tDir(180-v)}
  function rel(){pid=null;pos(90);tDir.cancel();document.getElementById('vDir').textContent=90;fetch('/direccion?value=90').catch(function(){})}
  el.addEventListener('pointerdown',function(e){if(pid!==null)return;pid=e.pointerId;el.setPointerCapture(pid);rect=el.getBoundingClientRect();upd(e.clientX)});
  el.addEventListener('pointermove',function(e){if(e.pointerId!==pid)return;upd(e.clientX)});
  el.addEventListener('pointerup',function(e){if(e.pointerId!==pid)return;rel()});
  el.addEventListener('pointercancel',function(e){if(e.pointerId!==pid)return;rel()});
})();
// Velocity slider — custom pointer handling, thumb bottom=stop top=max
(function(){
  var el=document.getElementById('slVel'),thm=el.querySelector('.thm');
  var pid=null,rect;
  function pos(raw){thm.style.top=((1-raw/145)*100)+'%'}
  pos(0);
  var tVel=throttle(function(a){fetch('/motor?value='+(a===0?0:curDir*a)).catch(function(){})},80);
  function upd(cy){var r=1-Math.max(0,Math.min(1,(cy-rect.top)/rect.height));curRaw=Math.round(r*29)*5;pos(curRaw);var a=curRaw===0?0:r5(85+curRaw);document.getElementById('vVel').textContent=a;tVel(a)}
  function end(e){if(e.pointerId!==pid)return;pid=null}
  el.addEventListener('pointerdown',function(e){if(pid!==null)return;pid=e.pointerId;el.setPointerCapture(pid);rect=el.getBoundingClientRect();upd(e.clientY)});
  el.addEventListener('pointermove',function(e){if(e.pointerId!==pid)return;upd(e.clientY)});
  el.addEventListener('pointerup',end);
  el.addEventListener('pointercancel',end);
})();
// Aux slider — custom pointer handling
(function(){
  var el=document.getElementById('slAux'),thm=el.querySelector('.thm');
  var pid=null,rect;
  function pos(v){thm.style.left=((v-20)/140*100)+'%'}
  pos(90);
  var tAux=throttle(function(v){document.getElementById('vAux').textContent=v;fetch('/servoaux?value='+v).catch(function(){})},80);
  function upd(cx){var r=Math.max(0,Math.min(1,(cx-rect.left)/rect.width));var v=Math.round(r*28)*5+20;pos(v);tAux(v)}
  function end(e){if(e.pointerId!==pid)return;pid=null}
  el.addEventListener('pointerdown',function(e){if(pid!==null)return;pid=e.pointerId;el.setPointerCapture(pid);rect=el.getBoundingClientRect();upd(e.clientX)});
  el.addEventListener('pointermove',function(e){if(e.pointerId!==pid)return;upd(e.clientX)});
  el.addEventListener('pointerup',end);
  el.addEventListener('pointercancel',end);
})();
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