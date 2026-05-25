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
int velocidad = 170; // ahora arranca alineado al paso de 5

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
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <style>
        body {
            display: flex;
            flex-direction: column;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
            background: #1a1a1a;
            font-family: Arial, sans-serif;
            color: white;
            overflow: hidden;
            touch-action: none;
        }

        h1 {
            color: #00ff88;
            margin: 20px 0;
            font-size: 2em;
            text-shadow: 0 0 10px #00ff88;
        }

        .panel-control {
            background: #2d2d2d;
            padding: 25px;
            border-radius: 15px;
            width: 100%;
            max-width: 400px;
            box-shadow: 0 0 20px rgba(0,255,136,0.2);
        }

        .control-deslizante {
            margin: 20px 0;
        }

        label {
            display: block;
            margin-bottom: 10px;
            font-size: 1.1em;
            color: #00ff88;
        }

        input[type="range"] {
            width: 100%;
            height: 10px;
            background: #4a4a4a;
            border-radius: 5px;
            outline: none;
            -webkit-appearance: none;
        }

        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 25px;
            height: 25px;
            background: #00ff88;
            border-radius: 50%;
            cursor: pointer;
            box-shadow: 0 0 10px #00ff88;
        }

        .valor-actual {
            display: inline-block;
            padding: 5px 15px;
            background: #00ff88;
            color: #1a1a1a;
            border-radius: 5px;
            margin-left: 10px;
            font-weight: bold;
        }

        .botones-mando {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 15px;
            margin-top: 30px;
        }

        .boton {
            padding: 20px;
            border: none;
            border-radius: 10px;
            font-size: 1.2em;
            cursor: pointer;
            transition: all 0.2s;
            background: #4a4a4a;
            color: white;
        }

        .boton:hover {
            transform: scale(1.05);
            box-shadow: 0 0 15px #00ff88;
        }

        #avanzar { background: #00cc66; }
        #reversa  { background: #ff4444; }
        #parar    { background: #ffaa00; }
    </style>
</head>
<body>
    <h1>Control Remoto</h1>
    
    <div class="panel-control">
        <div class="control-deslizante">
            <label>Velocidad (85-230):</label>
            <input type="range" min="85" max="230" step="5" value="170" id="velocidadSlider">
            <span class="valor-actual" id="velocidadValor">170</span>
        </div>

        <div class="control-deslizante">
            <label>Direccion (20-160):</label>
            <input type="range" min="20" max="160" step="0.1" value="90" id="direccionSlider">
            <span class="valor-actual" id="direccionValor">90</span>
        </div>

        <div class="botones-mando">
            <button class="boton" id="avanzar">Avanzar</button>
            <button class="boton" id="parar">Parar</button>
            <button class="boton" id="reversa">Reversa</button>
        </div>

        <div class="control-deslizante">
            <label>Servo auxiliar (20-160):</label>
            <input type="range" min="20" max="160" step="5" value="90" id="servoAuxSlider">
            <span class="valor-actual" id="servoAuxValor">90</span>
        </div>
    </div>

    <script>
        const velocidadSlider = document.getElementById('velocidadSlider');
        const direccionSlider = document.getElementById('direccionSlider');
        const servoAuxSlider = document.getElementById('servoAuxSlider');

        function throttle(fn, ms) {
            let ultimo = 0;
            let pendiente = null;
            return function(...args) {
                const ahora = Date.now();
                const restante = ms - (ahora - ultimo);
                clearTimeout(pendiente);
                if (restante <= 0) {
                    ultimo = ahora;
                    fn(...args);
                } else {
                    pendiente = setTimeout(() => {
                        ultimo = Date.now();
                        fn(...args);
                    }, restante);
                }
            };
        }

        function redondearA5(valor) {
            return Math.round(valor / 5) * 5;
        }

        function enviarComando(comando) {
            fetch(`/command?cmd=${comando}`).catch(() => {});
        }

        function _enviarVelocidad(valor) {
            valor = redondearA5(valor);
            valor = Math.max(85, Math.min(230, valor));
            fetch(`/velocidad?value=${valor}`).catch(() => {});
            document.getElementById("velocidadValor").textContent = valor;
        }

        function _enviarDireccion(valor) {
            valor = Math.max(20, Math.min(160, valor));
            fetch(`/direccion?value=${valor}`).catch(() => {});
            document.getElementById("direccionValor").textContent = valor;
        }

        function _enviarServoAux(valor) {
            valor = redondearA5(valor);
            valor = Math.max(20, Math.min(160, valor));
            fetch(`/servoaux?value=${valor}`).catch(() => {});
            document.getElementById("servoAuxValor").textContent = valor;
        }

        const actualizarVelocidad = throttle(_enviarVelocidad, 80);
        const actualizarDireccion = throttle(_enviarDireccion, 80);
        const actualizarServoAux = throttle(_enviarServoAux, 80);

        velocidadSlider.addEventListener('input', (e) => {
            actualizarVelocidad(parseInt(e.target.value));
        });

        direccionSlider.addEventListener('input', (e) => {
            actualizarDireccion(parseFloat(e.target.value));
        });

        servoAuxSlider.addEventListener('input', (e) => {
            actualizarServoAux(parseInt(e.target.value));
        });

        document.getElementById('avanzar').addEventListener('click', () => {
            enviarComando('avanzar');
        });

        document.getElementById('reversa').addEventListener('click', () => {
            enviarComando('reversa');
        });

        document.getElementById('parar').addEventListener('click', () => {
            enviarComando('parar');
        });
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

        unsigned long ahora = millis();
        if (ahora - ultimaDireccionAceptadaMs < minIntervaloDireccionMs) {
            server.send(200, "text/plain", "OK");
            return;
        }
        ultimaDireccionAceptadaMs = ahora;

        float nuevoAnguloF = server.arg("value").toFloat();
        nuevoAnguloF = constrain(nuevoAnguloF, 20.0f, 160.0f);
        int nuevoAngulo = (int)round(nuevoAnguloF);

        if (abs(anguloObjetivo - nuevoAngulo) >= 2) {
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