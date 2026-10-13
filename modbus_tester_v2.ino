
#include <WiFi.h>
#include <WebServer.h>
#include <ModbusMaster.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <math.h>
#include <stdint.h>

// =====================================================
// WIFI
// =====================================================
const char* WIFI_NAME = "ModbusTester";
const char* WIFI_PASS = "Tester123";

WebServer server(80);

// =====================================================
// NOKIA 5110
// CLK=18, DIN=23, DC=16, CE=5, RST=4
// =====================================================
Adafruit_PCD8544 display(18, 23, 16, 5, 4);

// =====================================================
// RS485 MAX485
// RO -> divisor de tension -> GPIO17
// DI -> GPIO22
// DE y /RE unidos -> GPIO21
// =====================================================
const int RX485 = 17;
const int TX485 = 22;
const int CTRL485 = 21;

ModbusMaster mb;

// =====================================================
// CONFIGURACION
// =====================================================
int slaveID = 5;
uint32_t baudRate = 19200;
char parity = 'E';
int stopBits = 1;

int functionCode = 3;
uint16_t startReg = 768;
uint16_t regCount = 1;

// Interpretacion de datos
int combineWords = 2;
char dataType = 'U';
char wordOrder = 'H';
double scaleFactor = 1.0;

String lastStatus = "Preparado";
String lastValues = "Sin lecturas";

// =====================================================
// MONITOR PASIVO DE TRAMAS
// =====================================================
bool monitorActive = false;

const int MAX_FRAME_SIZE = 256;
const int FRAME_HISTORY = 12;

uint8_t rxFrame[MAX_FRAME_SIZE];
int rxFrameLength = 0;
uint32_t lastRxMicros = 0;

struct CapturedFrame {
  String hex;
  uint32_t timeMs;
  int length;
  int id;
  int function;
  bool crcValid;
};

CapturedFrame frames[FRAME_HISTORY];
int frameCount = 0;
int nextFrameIndex = 0;

// Tiempo aproximado de silencio entre tramas Modbus RTU.
// Se calcula para 3,5 caracteres, suponiendo hasta 11 bits.
uint32_t getFrameGapUs() {
  if (baudRate == 0) return 2000;

  uint32_t gap = 38500000UL / baudRate;

  if (gap < 1000) gap = 1000;

  return gap;
}

// =====================================================
// CRC16 MODBUS
// =====================================================
uint16_t calculateCRC(const uint8_t* data, int length) {
  uint16_t crc = 0xFFFF;

  for (int i = 0; i < length; i++) {
    crc ^= data[i];

    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

// =====================================================
// GUARDAR TRAMA RECIBIDA
// =====================================================
void saveFrame(bool truncated = false) {
  if (rxFrameLength <= 0) return;

  CapturedFrame& f = frames[nextFrameIndex];

  f.hex = "";
  f.timeMs = millis();
  f.length = rxFrameLength;
  f.id = rxFrameLength >= 1 ? rxFrame[0] : -1;
  f.function = rxFrameLength >= 2 ? rxFrame[1] : -1;
  f.crcValid = false;

  for (int i = 0; i < rxFrameLength; i++) {
    if (i > 0) f.hex += " ";

    char byteText[4];
    snprintf(byteText, sizeof(byteText), "%02X", rxFrame[i]);
    f.hex += byteText;
  }

  if (!truncated && rxFrameLength >= 4) {
    uint16_t receivedCRC =
      (uint16_t)rxFrame[rxFrameLength - 2] |
      ((uint16_t)rxFrame[rxFrameLength - 1] << 8);

    uint16_t calculatedCRC =
      calculateCRC(rxFrame, rxFrameLength - 2);

    f.crcValid = receivedCRC == calculatedCRC;
  }

  nextFrameIndex = (nextFrameIndex + 1) % FRAME_HISTORY;

  if (frameCount < FRAME_HISTORY) {
    frameCount++;
  }

  Serial.print("[MONITOR] ");
  Serial.println(f.hex);

  rxFrameLength = 0;
}

// =====================================================
// PROCESAR RECEPCION PASIVA
// =====================================================
void processMonitor() {
  if (!monitorActive) return;

  while (Serial2.available() > 0) {
    uint32_t now = micros();

    // Si ha habido suficiente silencio, comienza otra trama.
    if (rxFrameLength > 0 &&
        (uint32_t)(now - lastRxMicros) >= getFrameGapUs()) {
      saveFrame();
    }

    int value = Serial2.read();

    if (value < 0) break;

    if (rxFrameLength < MAX_FRAME_SIZE) {
      rxFrame[rxFrameLength++] = (uint8_t)value;
      lastRxMicros = micros();
    } else {
      // Evitar desbordar el buffer.
      saveFrame(true);

      rxFrame[rxFrameLength++] = (uint8_t)value;
      lastRxMicros = micros();
    }
  }

  // Cerrar la trama cuando aparece el silencio entre mensajes.
  if (rxFrameLength > 0 &&
      (uint32_t)(micros() - lastRxMicros) >= getFrameGapUs()) {
    saveFrame();
  }
}

// =====================================================
// DIRECCION RS485
// =====================================================
void preTransmission() {
  digitalWrite(CTRL485, HIGH);
}

void postTransmission() {
  digitalWrite(CTRL485, LOW);
}

// =====================================================
// PANTALLA NOKIA
// =====================================================
void lcdMessage(String a, String b = "",
                String c = "", String d = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0, 0);
  display.println(a);
  display.println(b);
  display.println(c);
  display.println(d);
  display.display();
}

void showReady() {
  lcdMessage("Modbus Tester",
             "ID: " + String(slaveID),
             String(baudRate) + " " + parity,
             "Reg: " + String(startReg));
}

// =====================================================
// CONFIGURACION UART
// =====================================================
uint32_t getSerialConfig() {
  if (parity == 'E') {
    return stopBits == 2 ? SERIAL_8E2 : SERIAL_8E1;
  } else if (parity == 'O') {
    return stopBits == 2 ? SERIAL_8O2 : SERIAL_8O1;
  } else {
    return stopBits == 2 ? SERIAL_8N2 : SERIAL_8N1;
  }
}

// =====================================================
// INICIALIZAR MODBUS
// =====================================================
void startModbus() {
  monitorActive = false;
  rxFrameLength = 0;

  // Con DE y /RE unidos, LOW habilita el receptor.
  digitalWrite(CTRL485, LOW);

  Serial2.end();
  Serial2.begin(baudRate, getSerialConfig(), RX485, TX485);

  mb.begin(slaveID, Serial2);
  mb.preTransmission(preTransmission);
  mb.postTransmission(postTransmission);
}

// =====================================================
// INICIAR MONITOR PASIVO
// =====================================================
void startMonitor() {
  if (monitorActive) return;

  // No se utiliza ModbusMaster para transmitir durante el monitor.
  digitalWrite(CTRL485, LOW);

  Serial2.end();
  Serial2.begin(baudRate, getSerialConfig(), RX485, TX485);

  while (Serial2.available() > 0) {
    Serial2.read();
  }

  rxFrameLength = 0;
  frameCount = 0;
  nextFrameIndex = 0;
  lastRxMicros = micros();

  monitorActive = true;

  lastStatus = "Monitor pasivo activo";
  lastValues = "Escuchando el bus RS485";

  lcdMessage("Monitor RS485",
             "Modo pasivo",
             String(baudRate) + " baudios",
             "Escuchando...");
}

// =====================================================
// HTML COMUN
// =====================================================
String pageStart(String title) {
  String s = R"HTML(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Modbus Tester</title>
<style>
body {
  font-family: Arial, sans-serif;
  max-width: 650px;
  margin: auto;
  padding: 14px;
  background: #f1f4f8;
  color: #18212f;
}
h1 { font-size: 25px; }
h2 { font-size: 19px; }
.card {
  background: white;
  padding: 15px;
  margin: 12px 0;
  border-radius: 12px;
  box-shadow: 0 2px 7px #0001;
}
a, button {
  display: block;
  box-sizing: border-box;
  width: 100%;
  padding: 13px;
  margin: 8px 0;
  border: 0;
  border-radius: 8px;
  background: #1769aa;
  color: white;
  text-decoration: none;
  font-size: 16px;
  text-align: center;
}
input, select {
  box-sizing: border-box;
  width: 100%;
  padding: 10px;
  margin: 5px 0 12px;
  font-size: 16px;
}
label { font-weight: bold; }
small { color: #526174; }
pre { white-space: pre-wrap; overflow-wrap: anywhere; }
</style>
</head>
<body>
)HTML";

  s += "<h1>" + title + "</h1>";
  return s;
}

String pageEnd() {
  return "</body></html>";
}

// =====================================================
// MENU PRINCIPAL
// =====================================================
void sendHome() {
  String s = pageStart("Modbus Tester");

  s += R"HTML(
<div class="card">
<h2>Menu principal</h2>
<a href="/comm">1. Comunicacion</a>
<a href="/readform">2. Leer registros</a>
<a href="/scanform">3. Escanear ID</a>
<a href="/config">4. Configuracion</a>
<a href="/monitor">5. Monitor de tramas</a>
</div>
)HTML";

  s += "<div class='card'><b>Estado:</b> " + lastStatus;
  s += "<p><b>Ultimo resultado:</b></p><pre>";
  s += lastValues;
  s += "</pre></div>";

  s += "<div class='card'><small>Wi-Fi local: ModbusTester</small></div>";
  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

// =====================================================
// PAGINA MONITOR DE TRAMAS
// =====================================================
void sendMonitorPage() {
  startMonitor();

  String s = pageStart("Monitor de tramas");

  s += R"HTML(
<div class="card">
<h2>Escucha pasiva RS485</h2>
<p>El ESP32 escucha las comunicaciones que ya existen en el bus.</p>
<p><b>Baudios:</b> )HTML";
  s += String(baudRate);
  s += R"HTML(</p>
<p><b>Paridad:</b> )HTML";
  s += String(parity);
  s += R"HTML(</p>
<p><b>Bits de parada:</b> )HTML";
  s += String(stopBits);
  s += R"HTML(</p>
<p id="estadoMonitor">Esperando tramas...</p>
<small>Actualizacion automatica cada 500 ms. Se conservan las ultimas 12 tramas.</small>
</div>

<div class="card">
<h2>Tramas recibidas</h2>
<pre id="frames">Esperando datos del bus RS485...</pre>
</div>

<div class="card">
<h2>Informacion</h2>
<p>CRC OK: la comprobacion de la trama es correcta.</p>
<p>CRC ERROR: la trama recibida no supera la comprobacion.</p>
<p>CRC N/D: no se puede comprobar, o la trama se ha truncado.</p>
<p>La funcion y el contenido se muestran en hexadecimal.</p>
</div>

<a href="/stopmonitor">Detener monitor y volver al menu</a>

<script>
async function actualizarTramas() {
  try {
    const respuesta = await fetch('/api/frames', {cache: 'no-store'});
    const datos = await respuesta.json();

    const salida = document.getElementById('frames');
    const estado = document.getElementById('estadoMonitor');

    if (!datos.active) {
      estado.textContent = 'Monitor detenido';
      return;
    }

    estado.textContent =
      'Monitor activo. Tramas almacenadas: ' + datos.frames.length;

    if (datos.frames.length === 0) {
      salida.textContent =
        'Todavia no se han recibido tramas.\\n' +
        'Comprueba que hay comunicacion en el bus.';
      return;
    }

    salida.textContent = datos.frames.map(function(f) {
      let crc = f.crc === 1 ? 'CRC OK' :
                f.crc === 0 ? 'CRC ERROR' : 'CRC N/D';

      return 'Tiempo: ' + f.time + ' ms\\n' +
             'ID: ' + f.id +
             ' | Funcion: 0x' + Number(f.fc).toString(16).toUpperCase().padStart(2, '0') +
             ' | Bytes: ' + f.length + '\\n' +
             crc + '\\n' +
             f.hex;
    }).join('\\n\\n------------------------------\\n\\n');

  } catch (e) {
    document.getElementById('estadoMonitor').textContent =
      'Esperando conexion con el ESP32...';
  }
}

actualizarTramas();
setInterval(actualizarTramas, 500);
</script>
)HTML";

  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

// =====================================================
// API: DEVOLVER TRAMAS EN JSON
// =====================================================
void sendFramesJSON() {
  if (monitorActive) {
    processMonitor();
  }

  String json = "{\"active\":";
  json += monitorActive ? "true" : "false";
  json += ",\"frames\":[";

  for (int i = 0; i < frameCount; i++) {
    int index = (nextFrameIndex - frameCount + i + FRAME_HISTORY)
                % FRAME_HISTORY;

    CapturedFrame& f = frames[index];

    if (i > 0) json += ",";

    json += "{\"time\":";
    json += String(f.timeMs);

    json += ",\"length\":";
    json += String(f.length);

    json += ",\"id\":";
    json += String(f.id);

    json += ",\"fc\":";
    json += String(f.function);

    json += ",\"crc\":";
    if (f.length < 4) {
      json += "-1";
    } else {
      json += f.crcValid ? "1" : "0";
    }

    json += ",\"hex\":\"";
    json += f.hex;
    json += "\"}";
  }

  json += "]}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}

// =====================================================
// DETENER MONITOR
// =====================================================
void stopMonitor() {
  if (monitorActive) {
    processMonitor();

    if (rxFrameLength > 0) {
      saveFrame();
    }

    startModbus();
    showReady();

    lastStatus = "Monitor detenido";
    lastValues = "Modbus preparado";
  }
}

void handleStopMonitor() {
  stopMonitor();

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Volviendo al menu");
}

// =====================================================
// CONFIGURACION DE COMUNICACION
// =====================================================
void sendConfig() {
  String s = pageStart("Configuracion");

  s += "<form class='card' action='/setconfig' method='GET'>";

  s += "<label>ID esclavo (1-247)</label>";
  s += "<input name='id' type='number' min='1' max='247' value='";
  s += String(slaveID) + "' required>";

  s += "<label>Baudios</label><select name='baud'>";
  uint32_t speeds[] = {
    1200, 2400, 4800, 9600,
    19200, 38400, 57600, 115200
  };

  for (int i = 0; i < 8; i++) {
    s += "<option value='" + String(speeds[i]) + "'";
    if (baudRate == speeds[i]) s += " selected";
    s += ">" + String(speeds[i]) + "</option>";
  }
  s += "</select>";

  s += "<label>Paridad</label><select name='par'>";
  char pars[] = {'N', 'E', 'O'};
  const char* names[] = {"Ninguna", "Par", "Impar"};

  for (int i = 0; i < 3; i++) {
    s += "<option value='" + String(pars[i]) + "'";
    if (parity == pars[i]) s += " selected";
    s += ">" + String(names[i]) + "</option>";
  }
  s += "</select>";

  s += "<label>Bits de parada</label><select name='stop'>";
  for (int n = 1; n <= 2; n++) {
    s += "<option value='" + String(n) + "'";
    if (stopBits == n) s += " selected";
    s += ">" + String(n) + "</option>";
  }
  s += "</select>";

  s += "<button type='submit'>Guardar configuracion</button>";
  s += "</form><a href='/'>Volver al menu</a>";
  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

void saveConfig() {
  if (server.hasArg("id")) {
    int n = server.arg("id").toInt();
    if (n >= 1 && n <= 247) slaveID = n;
  }

  if (server.hasArg("baud")) {
    uint32_t n = server.arg("baud").toInt();
    uint32_t valid[] = {
      1200, 2400, 4800, 9600,
      19200, 38400, 57600, 115200
    };
    for (int i = 0; i < 8; i++) {
      if (n == valid[i]) baudRate = n;
    }
  }

  if (server.hasArg("par")) {
    String p = server.arg("par");
    if (p == "N" || p == "E" || p == "O") parity = p[0];
  }

  if (server.hasArg("stop")) {
    int n = server.arg("stop").toInt();
    if (n == 1 || n == 2) stopBits = n;
  }

  startModbus();
  lastStatus = "Configuracion aplicada";
  showReady();

  String s = pageStart("Configuracion guardada");
  s += "<div class='card'>ID: " + String(slaveID);
  s += "<br>Baudios: " + String(baudRate);
  s += "<br>Paridad: " + String(parity);
  s += "<br>Bits de parada: " + String(stopBits);
  s += "</div><a href='/'>Volver al menu</a>";
  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

// =====================================================
// FORMULARIO DE LECTURA E INTERPRETACION
// =====================================================
void sendReadForm() {
  String s = pageStart("Leer e interpretar registros");

  s += "<form class='card' action='/read' method='GET'>";

  s += "<label>Funcion Modbus</label><select name='fc'>";
  s += "<option value='3'>03 - Holding Registers</option>";
  s += "<option value='4'>04 - Input Registers</option>";
  s += "</select>";

  s += "<label>Registro inicial (decimal)</label>";
  s += "<input name='reg' type='number' min='0' max='65535' value='";
  s += String(startReg) + "' required>";

  s += "<label>Cantidad total de registros (1-10)</label>";
  s += "<input name='qty' type='number' min='1' max='10' value='";
  s += String(regCount) + "' required>";

  s += "<hr><h2>Valor combinado</h2>";

  s += "<label>Palabras que combinar (1, 2 o 4)</label>";
  s += "<select name='words'>";

  int wordChoices[] = {1, 2, 4};
  for (int i = 0; i < 3; i++) {
    s += "<option value='" + String(wordChoices[i]) + "'";
    if (combineWords == wordChoices[i]) s += " selected";
    s += ">" + String(wordChoices[i]) + " registro(s)</option>";
  }
  s += "</select>";

  s += "<label>Tipo de dato</label><select name='type'>";

  s += "<option value='U'";
  if (dataType == 'U') s += " selected";
  s += ">Entero sin signo</option>";

  s += "<option value='S'";
  if (dataType == 'S') s += " selected";
  s += ">Entero con signo</option>";

  s += "<option value='F'";
  if (dataType == 'F') s += " selected";
  s += ">Float IEEE 754 (32 bits, 2 registros)</option>";

  s += "</select>";

  s += "<label>Orden de palabras</label><select name='order'>";
  s += "<option value='H'";
  if (wordOrder == 'H') s += " selected";
  s += ">Palabra alta primero</option>";

  s += "<option value='L'";
  if (wordOrder == 'L') s += " selected";
  s += ">Palabra baja primero</option>";
  s += "</select>";

  s += "<label>Factor de escala</label>";
  s += "<input name='scale' type='number' step='any' value='";
  s += String(scaleFactor, 6) + "' required>";
  s += "<small>Ejemplo: 0.001 para convertir mV a V.</small>";

  s += "<button type='submit'>Leer registros</button>";
  s += "</form><a href='/'>Volver al menu</a>";
  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

// =====================================================
// CONVERTIR REGISTROS EN VALOR COMBINADO
// =====================================================
String hex64(uint64_t value) {
  char buf[19];
  snprintf(buf, sizeof(buf), "0x%016llX",
           (unsigned long long)value);
  return String(buf);
}

String interpretRegisters() {
  uint16_t words[4] = {0, 0, 0, 0};

  for (int i = 0; i < combineWords; i++) {
    int sourceIndex = (wordOrder == 'H')
                      ? i
                      : combineWords - 1 - i;

    words[i] = mb.getResponseBuffer(sourceIndex);
  }

  uint64_t raw = 0;

  for (int i = 0; i < combineWords; i++) {
    raw = (raw << 16) | words[i];
  }

  String result = "";
  result += "Palabras combinadas: " + String(combineWords);
  result += "\nOrden: ";
  result += (wordOrder == 'H') ? "Alta primero" : "Baja primero";
  result += "\nHexadecimal: " + hex64(raw);

  double numericValue = 0;
  bool isNumeric = true;

  if (dataType == 'F') {
    if (combineWords != 2) {
      result += "\nERROR: Float necesita exactamente 2 registros.";
      isNumeric = false;
    } else {
      union {
        uint32_t bits;
        float value;
      } converter;

      converter.bits = (uint32_t)raw;
      float f = converter.value;

      if (!isfinite(f)) {
        result += "\nFloat no finito: revisa orden y formato.";
        isNumeric = false;
      } else {
        numericValue = f;
        result += "\nTipo: Float IEEE 754";
        result += "\nValor original: " + String(f, 7);
      }
    }
  } else if (dataType == 'S') {
    int64_t signedValue;

    if (combineWords == 1) {
      signedValue = (int16_t)(uint16_t)raw;
    } else if (combineWords == 2) {
      signedValue = (int32_t)(uint32_t)raw;
    } else {
      signedValue = (int64_t)raw;
    }

    numericValue = (double)signedValue;
    result += "\nTipo: Entero con signo";
    result += "\nValor original: " + String((long long)signedValue);
  } else {
    numericValue = (double)raw;
    result += "\nTipo: Entero sin signo";

    char dec[24];
    snprintf(dec, sizeof(dec), "%llu",
             (unsigned long long)raw);
    result += "\nValor original: " + String(dec);
  }

  if (isNumeric) {
    double scaled = numericValue * scaleFactor;
    result += "\nFactor: " + String(scaleFactor, 6);
    result += "\nValor escalado: " + String(scaled, 6);
  }

  return result;
}

// =====================================================
// LEER REGISTROS
// =====================================================
void doRead() {
  if (monitorActive) startModbus();

  if (server.hasArg("fc")) {
    int f = server.arg("fc").toInt();
    if (f == 3 || f == 4) functionCode = f;
  }

  if (server.hasArg("reg")) {
    long r = server.arg("reg").toInt();
    if (r >= 0 && r <= 65535) startReg = r;
  }

  if (server.hasArg("qty")) {
    int q = server.arg("qty").toInt();
    if (q >= 1 && q <= 10) regCount = q;
  }

  if (server.hasArg("words")) {
    int n = server.arg("words").toInt();
    if (n == 1 || n == 2 || n == 4) combineWords = n;
  }

  if (server.hasArg("type")) {
    String t = server.arg("type");
    if (t == "U" || t == "S" || t == "F") dataType = t[0];
  }

  if (server.hasArg("order")) {
    String o = server.arg("order");
    if (o == "H" || o == "L") wordOrder = o[0];
  }

  if (server.hasArg("scale")) {
    double f = server.arg("scale").toDouble();
    if (isfinite(f) && fabs(f) < 1.0e12) scaleFactor = f;
  }

  if (dataType == 'F') combineWords = 2;

  if (regCount < combineWords) {
    regCount = combineWords;
  }

  if (regCount > 10) regCount = 10;

  if ((uint32_t)startReg + regCount - 1 > 65535) {
    server.send(400, "text/plain", "La lectura supera el registro 65535.");
    return;
  }

  uint8_t result;

  if (functionCode == 3) {
    result = mb.readHoldingRegisters(startReg, regCount);
  } else {
    result = mb.readInputRegisters(startReg, regCount);
  }

  String s = pageStart("Resultado de lectura");
  s += "<div class='card'>";
  s += "<b>ID:</b> " + String(slaveID);
  s += "<br><b>Funcion:</b> 0" + String(functionCode);
  s += "<br><b>Registro inicial:</b> " + String(startReg);

  if (result == mb.ku8MBSuccess) {
    lastStatus = "RESPUESTA OK";
    lastValues = "";

    s += "<h2>Registros individuales</h2>";

    for (int i = 0; i < regCount; i++) {
      uint16_t v = mb.getResponseBuffer(i);
      char hex[7];
      snprintf(hex, sizeof(hex), "0x%04X", v);

      s += "<p>Registro " + String(startReg + i);
      s += ": <b>" + String(v) + "</b> (" + String(hex) + ")</p>";

      if (i > 0) lastValues += "\n";
      lastValues += String(startReg + i) + ": " + String(v);
    }

    String combined = interpretRegisters();

    s += "<hr><h2>Valor combinado</h2><pre>";
    s += combined;
    s += "</pre>";

    lastValues += "\n\n--- Valor combinado ---\n";
    lastValues += combined;

    lcdMessage("Modbus OK",
               "ID:" + String(slaveID) + " Reg:" + String(startReg),
               "Palabras:" + String(combineWords),
               "Resultado en movil");

    Serial.println("\n--- REGISTROS ---");
    Serial.println(lastValues);
  } else {
    lastStatus = "Error Modbus";
    lastValues = "Codigo de error: 0x" + String(result, HEX);

    s += "<h2>Error Modbus</h2>";
    s += "<p>Codigo: 0x" + String(result, HEX) + "</p>";
    s += "<p>Comprueba ID, velocidad, paridad, funcion y registro.</p>";

    lcdMessage("Error Modbus",
               "ID: " + String(slaveID),
               "Reg: " + String(startReg),
               "Cod: 0x" + String(result, HEX));
  }

  s += "</div><a href='/readform'>Otra lectura</a>";
  s += "<a href='/'>Volver al menu</a>";
  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

// =====================================================
// PRUEBA DE COMUNICACION
// =====================================================
void doComm() {
  if (monitorActive) startModbus();

  uint8_t result = mb.readHoldingRegisters(startReg, 1);

  String s = pageStart("Prueba de comunicacion");

  if (result == mb.ku8MBSuccess) {
    lastStatus = "RESPUESTA OK";
    lastValues = "Registro " + String(startReg) + " = " +
                 String(mb.getResponseBuffer(0));

    s += "<div class='card'><h2>RESPUESTA OK</h2>";
    s += "ID: " + String(slaveID);
    s += "<br>Registro: " + String(startReg);
    s += "<br>Valor: " + String(mb.getResponseBuffer(0));
    s += "</div>";

    lcdMessage("COMUNICACION",
               "RESPUESTA OK",
               "ID: " + String(slaveID),
               "Valor recibido");
  } else {
    lastStatus = "Error de comunicacion";
    lastValues = "Codigo: 0x" + String(result, HEX);

    s += "<div class='card'><h2>Error de comunicacion</h2>";
    s += "Codigo: 0x" + String(result, HEX);
    s += "<p>Comprueba cableado, ID y parametros serie.</p></div>";

    lcdMessage("COMUNICACION",
               "Error Modbus",
               "ID: " + String(slaveID),
               "Cod: 0x" + String(result, HEX));
  }

  s += "<a href='/'>Volver al menu</a>";
  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

// =====================================================
// ESCANEO DE DIRECCIONES
// =====================================================
void sendScanForm() {
  String s = pageStart("Escanear ID");

  s += R"HTML(
<div class="card">
<p>Escanea direcciones utilizando la funcion 03
y el registro configurado.</p>
<form action="/scan" method="GET">
<label>ID inicial</label>
<input name="first" type="number" min="1" max="247" value="1">
<label>ID final</label>
<input name="last" type="number" min="1" max="247" value="20">
<button type="submit">Iniciar escaneo</button>
</form>
</div>
)HTML";

  s += "<a href='/'>Volver al menu</a>";
  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

void doScan() {
  if (monitorActive) startModbus();

  int first = server.arg("first").toInt();
  int last = server.arg("last").toInt();

  if (first < 1) first = 1;
  if (last > 247) last = 247;

  if (last < first || last - first > 50) {
    server.send(400, "text/plain",
                "Rango invalido. Maximo 50 direcciones por prueba.");
    return;
  }

  String found = "";
  int countFound = 0;

  lcdMessage("Escaneando IDs",
             String(first) + " a " + String(last),
             "Registro: " + String(startReg),
             "Esperando...");

  for (int id = first; id <= last; id++) {
    mb.begin(id, Serial2);
    mb.preTransmission(preTransmission);
    mb.postTransmission(postTransmission);

    uint8_t r = mb.readHoldingRegisters(startReg, 1);

    if (r < 0xE0) {
      found += "ID " + String(id);

      if (r == mb.ku8MBSuccess) {
        found += " - Valor: ";
        found += String(mb.getResponseBuffer(0));
      } else {
        found += " - Respuesta/error Modbus: 0x";
        found += String(r, HEX);
      }

      found += "\n";
      countFound++;
    }

    lcdMessage("Escaneando IDs",
               "Probando ID " + String(id),
               "Encontrados: " + String(countFound),
               "Espera...");
    delay(20);
  }

  mb.begin(slaveID, Serial2);
  mb.preTransmission(preTransmission);
  mb.postTransmission(postTransmission);

  lastStatus = "Escaneo terminado";
  lastValues = "Encontrados: " + String(countFound);

  String s = pageStart("Resultado del escaneo");
  s += "<div class='card'><h2>Encontrados: ";
  s += String(countFound) + "</h2><pre>";
  s += found.length() ? found : "Ninguna respuesta reconocida";
  s += "</pre><small>Se ha probado la funcion 03, registro ";
  s += String(startReg);
  s += ". Los equipos que no admitan ese registro pueden no detectarse.</small></div>";

  lcdMessage("Escaneo terminado",
             "Encontrados: " + String(countFound),
             countFound ? "Ver lista en movil" : "Sin respuestas",
             "ID base: " + String(slaveID));

  s += "<a href='/scanform'>Nuevo escaneo</a>";
  s += "<a href='/'>Volver al menu</a>";
  s += pageEnd();

  server.send(200, "text/html; charset=utf-8", s);
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(CTRL485, OUTPUT);
  digitalWrite(CTRL485, LOW);

  display.begin();
  display.setContrast(60);

  lcdMessage("Modbus Tester", "Iniciando...");

  startModbus();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_NAME, WIFI_PASS);

  Serial.println();
  Serial.println("Red WiFi: " + String(WIFI_NAME));
  Serial.println("Contrasena: " + String(WIFI_PASS));
  Serial.print("Direccion web: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", sendHome);
  server.on("/config", sendConfig);
  server.on("/setconfig", saveConfig);
  server.on("/readform", sendReadForm);
  server.on("/read", doRead);
  server.on("/comm", doComm);
  server.on("/scanform", sendScanForm);
  server.on("/scan", doScan);

  // Nuevas rutas del monitor
  server.on("/monitor", sendMonitorPage);
  server.on("/api/frames", sendFramesJSON);
  server.on("/stopmonitor", handleStopMonitor);

  server.onNotFound([]() {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Volviendo al menu");
  });

  server.begin();

  lcdMessage("Modbus Tester",
             "WiFi preparado",
             "192.168.4.1",
             "Conecta movil");
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  server.handleClient();

  if (monitorActive) {
    processMonitor();
  }

  delay(1);
}
