#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>      // Librería para el display
#include <JPEGDecoder.h>   // Librería para decodificar JPEG

// ----------------- Configuración del Access Point -----------------
const char* ssid     = "ESP32-WIFI-video";
const char* password = "TRC12345678";

// ----------------- Creación del Servidor HTTP y WebSocket -----------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ----------------- Configuración del Display TFT -----------------
TFT_eSPI tft = TFT_eSPI();

// ----------------- Página HTML/JS (Frontend) -----------------
// Se sirve desde la memoria flash (PROGMEM)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Frame Capture via WebSocket</title>
  <style>
    #canvasFrame { display: none; }
  </style>
</head>
<body>
  <h1>Select Video and Capture Frames</h1>
  <input type="file" id="videoInput" accept="video/*">
  <br><br>
  <video id="videoPlayer" width="240" height="135" controls></video>
  <br><br>
  <canvas id="canvasFrame" width="135" height="240"></canvas>
  <br>
  <button id="startCapture">Start Capture</button>
  <button id="stopCapture">Stop Capture</button>
  <br>
  <div style="margin-top: 100px;">
    <h2 style="font-weight: 500;">Developed by <b>Pablo Toledo</b></h2>
    <div>
      <ul>
        <li><a href="https://github.com/pablotoledom/">My github</a></li>
        <li><a href="https://theretrocenter.com">The Retro Center website</a></li>
      </ul>
    </div>
    <div style="font-size: 10pt; font-family: Monospace; white-space: pre;">
    ░▒▓████████▓▒░▒▓███████▓▒░ ░▒▓██████▓▒░  
       ░▒▓█▓▒░   ░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░  
       ░▒▓█▓▒░   ░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░         
       ░▒▓█▓▒░   ░▒▓███████▓▒░░▒▓█▓▒░         
       ░▒▓█▓▒░   ░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░         
       ░▒▓█▓▒░   ░▒▓█▓▒░░▒▓█▓▒░░▒▓██████▓▒░  
    </div>
  </div>
  <script>
    const videoInput = document.getElementById('videoInput');
    const videoPlayer = document.getElementById('videoPlayer');
    const canvasFrame = document.getElementById('canvasFrame');
    const startCapture = document.getElementById('startCapture');
    const stopCapture = document.getElementById('stopCapture');

    const ws = new WebSocket('ws://' + location.hostname + '/ws');
    ws.binaryType = 'blob';
    ws.onopen = () => { console.log("WebSocket connected"); };
    ws.onclose = () => { console.log("WebSocket disconnected"); };
    ws.onerror = (e) => { console.error("WebSocket error:", e); };

    ws.onmessage = (event) => {
      if (event.data === "requestFrame") {
        captureFrameAndSend();
      } else {
        console.log("Server message:", event.data);
      }
    };

    videoInput.addEventListener('change', (e) => {
      const file = e.target.files[0];
      if (file) {
        const url = URL.createObjectURL(file);
        videoPlayer.src = url;
      }
    });

    startCapture.addEventListener('click', () => {
      videoPlayer.play();
      captureFrameAndSend();
    });

    stopCapture.addEventListener('click', () => {
      videoPlayer.pause();
    });

    function captureFrameAndSend() {
      const ctx = canvasFrame.getContext('2d');
      ctx.clearRect(0, 0, canvasFrame.width, canvasFrame.height);
      ctx.save();
      ctx.translate(canvasFrame.width, 0);
      ctx.rotate(Math.PI / 2);
      ctx.drawImage(videoPlayer, 0, 0, 240, 135);
      ctx.restore();
      const dataURL = canvasFrame.toDataURL('image/jpeg', 0.25);
      const blob = dataURItoBlob(dataURL);
      if (ws.readyState === WebSocket.OPEN) {
        ws.send(blob);
        console.log("Frame sent via WebSocket");
      } else {
        console.log("WebSocket is not open");
      }
    }

    function dataURItoBlob(dataURI) {
      const byteString = atob(dataURI.split(',')[1]);
      const mimeString = dataURI.split(',')[0].split(':')[1].split(';')[0];
      const ab = new ArrayBuffer(byteString.length);
      const ia = new Uint8Array(ab);
      for (let i = 0; i < byteString.length; i++) {
        ia[i] = byteString.charCodeAt(i);
      }
      return new Blob([ab], { type: mimeString });
    }
  </script>
</body>
</html>
)rawliteral";

// ----------------- Buffers para JPEG -----------------
#define MAX_IMAGE_SIZE 30000
uint8_t imageBufferStatic[MAX_IMAGE_SIZE];
uint8_t imageBufferProcesamiento[MAX_IMAGE_SIZE];
size_t imageBufferLength = 0;
volatile bool imageReady = false;
volatile bool processing = false;

// ----------------- Definición del Pin de Señal -----------------
#define SIGNAL_PIN 12

// ----------------- Definición del Enum para la Máquina de Estados -----------------
enum Estado {
  ESTADO_INICIAL,
  ESTADO_ESPERANDO,
  ESTADO_CUENTA_REGRESIVA,
  ESTADO_STREAMING,
  ESTADO_CUENTA_ASCENDENTE
};

// ----------------- Variables Globales para la Máquina de Estados -----------------
Estado estadoActual = ESTADO_INICIAL;
Estado previousState = ESTADO_INICIAL;  // Para detectar cambios de estado
int currentCount = 0;
unsigned long lastCountUpdate = 0;
unsigned long delayStartTime = 0;
#define UPDATE_INTERVAL 20
#define STEP_COUNT 10

// Variable para controlar la rotación actual del display
int currentRotation = -1;

// ----------------- Función para actualizar el contador en el display -----------------
// Se modificó para que el número se alinee a la derecha y se muestre en color verde,
// utilizando (si se dispone) una fuente de 7 segmentos LED personalizada.
// Variable global para almacenar el último valor mostrado
// Define las coordenadas y dimensiones del área del contador.
// Define las constantes para el área del contador.
const int counterRight = 226;    // Borde derecho donde se alinea el texto
const int counterY     = -66;      // Coordenada y del área del contador
const int counterWidth = 240;      // Ancho del área (suficiente para hasta 4 dígitos)
const int counterH     = 175;      // Altura del área del contador

// Variable global para almacenar el último número mostrado.
int lastDisplayedNumber = -1;

void updateCounterDisplay(int count) {
  // Si el número no cambió, no se redibuja.
  if (count == lastDisplayedNumber) return;
  lastDisplayedNumber = count;

  // Limpia el área donde se mostrará el contador.
  // Esto borra desde (counterRight - counterWidth, counterY) con ancho counterWidth y altura counterH.
  tft.fillRect(counterRight - counterWidth, counterY, counterWidth, counterH, TFT_BLACK);
  
  // Configura la fuente, tamaño y color.
  tft.setTextFont(7);     // Selecciona la fuente incorporada nº7 (o la que prefieras)
  tft.setTextSize(2);     // Escala el texto (en este ejemplo, 2 veces el tamaño original)
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  
  // Configura la alineación a la derecha (TR_DATUM: datum = top right).
  tft.setTextDatum(TR_DATUM);
  
  // Dibuja el número. La coordenada (counterRight, counterY + counterH/2)
  // indica que el extremo derecho del texto estará en counterRight y la parte superior se
  // ubicará aproximadamente en el centro vertical del área.
  tft.drawString(String(count), counterRight, counterY + (counterH / 2));
}

// ----------------- Función para la Máquina de Estados -----------------
void updateState() {
  unsigned long currentMillis = millis();
  bool signalState = (digitalRead(SIGNAL_PIN) == HIGH);

  // Detectar cambio de estado y limpiar la pantalla al salir del modo streaming.
  // Forzamos el redibujado reiniciando lastDisplayedNumber y llamando a updateCounterDisplay().
  if (estadoActual != previousState) {
    if (estadoActual != ESTADO_STREAMING) {
      tft.fillScreen(TFT_BLACK);
      lastDisplayedNumber = -1;  // Reiniciamos para forzar el redibujado
      updateCounterDisplay(currentCount); // Fuerza el redibujado inmediato del contador
    }
    previousState = estadoActual;
  }

  // Actualizar la rotación:
  if (estadoActual == ESTADO_STREAMING) {
    if (currentRotation != 0) {
      tft.setRotation(0);
      currentRotation = 0;
    }
  } else {
    if (currentRotation != 1) {
      tft.setRotation(1);
      currentRotation = 1;
    }
  }

  // Máquina de Estados
  switch (estadoActual) {
    case ESTADO_INICIAL:
      if (currentMillis - lastCountUpdate >= UPDATE_INTERVAL) {
        lastCountUpdate = currentMillis;
        currentCount += STEP_COUNT;
        if (currentCount >= 1400) {
          currentCount = 1400;
          estadoActual = ESTADO_ESPERANDO;
        }
        updateCounterDisplay(currentCount);
      }
      break;

    case ESTADO_ESPERANDO:
      updateCounterDisplay(currentCount);
      if (signalState) {
        estadoActual = ESTADO_CUENTA_REGRESIVA;
        currentCount = 1400;
        lastCountUpdate = currentMillis;
      }
      break;

    case ESTADO_CUENTA_REGRESIVA:
      if (currentMillis - lastCountUpdate >= UPDATE_INTERVAL) {
        lastCountUpdate = currentMillis;
        currentCount -= STEP_COUNT;
        if (currentCount <= 666) {
          currentCount = 666;
          updateCounterDisplay(currentCount);
          if (delayStartTime == 0) {
            delayStartTime = currentMillis;
          }
          if (currentMillis - delayStartTime >= 3000) {
            estadoActual = ESTADO_STREAMING;
            delayStartTime = 0;
          }
        } else {
          updateCounterDisplay(currentCount);
        }
      }
      if (!signalState) {
        estadoActual = ESTADO_CUENTA_ASCENDENTE;
        currentCount = 666;
        lastCountUpdate = currentMillis;
        updateCounterDisplay(currentCount);
        delayStartTime = 0;
      }
      break;

    case ESTADO_STREAMING:
      if (!signalState) {
        estadoActual = ESTADO_CUENTA_ASCENDENTE;
        currentCount = 666;
        lastCountUpdate = currentMillis;
        updateCounterDisplay(currentCount);
      }
      break;

    case ESTADO_CUENTA_ASCENDENTE:
      if (currentMillis - lastCountUpdate >= UPDATE_INTERVAL) {
        lastCountUpdate = currentMillis;
        currentCount += STEP_COUNT;
        if (currentCount >= 1400) {
          currentCount = 1400;
        }
        updateCounterDisplay(currentCount);
      }
      if (signalState) {
        estadoActual = ESTADO_CUENTA_REGRESIVA;
        currentCount = 1400;
        lastCountUpdate = currentMillis;
      }
      break;
  }
}


// ----------------- Callback del WebSocket -----------------
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
  }
  else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*) arg;
    if (info->opcode == WS_BINARY) {
      if (processing) return;  // Evitar sobrescribir mientras se procesa una imagen
      if (info->index == 0) {
        imageBufferLength = 0; // Reinicia el buffer
      }
      if (imageBufferLength + len <= MAX_IMAGE_SIZE) {
        memcpy(imageBufferStatic + imageBufferLength, data, len);
        imageBufferLength += len;
      } else {
        Serial.println("Error: Buffer de imagen excedido");
        imageBufferLength = 0;
      }
      if (info->final) {
        // Verifica que el JPEG esté completo comprobando el marcador final (0xFF, 0xD9)
        if (imageBufferLength >= 2 &&
            imageBufferStatic[imageBufferLength-2] == 0xFF &&
            imageBufferStatic[imageBufferLength-1] == 0xD9) {
          memcpy(imageBufferProcesamiento, imageBufferStatic, imageBufferLength);
          imageReady = true;
        } else {
          Serial.println("JPEG incompleto (no se encontró el marcador final)");
        }
      }
    }
  }
}

// ----------------- Función para procesar la imagen -----------------
void processImage() {
  // Solo se procesa en modo streaming
  if (estadoActual != ESTADO_STREAMING) return;
  if (imageReady && !processing) {
    processing = true;
    int decodeResult = JpegDec.decodeArray(imageBufferProcesamiento, imageBufferLength);
    if (decodeResult == 1) {
      Serial.println("Decodificación exitosa, mostrando imagen");
      uint16_t mcu_w = JpegDec.MCUWidth;
      uint16_t mcu_h = JpegDec.MCUHeight;
      uint16_t *pImg;
      while (JpegDec.read()) {
        int mcu_x = JpegDec.MCUx * mcu_w;
        int mcu_y = JpegDec.MCUy * mcu_h;
        uint16_t block_w = mcu_w;
        uint16_t block_h = mcu_h;
        if (mcu_x + block_w > JpegDec.width) {
          block_w = JpegDec.width - mcu_x;
        }
        if (mcu_y + block_h > JpegDec.height) {
          block_h = JpegDec.height - mcu_y;
        }
        pImg = JpegDec.pImage;
        tft.pushImage(mcu_x, mcu_y, block_w, block_h, pImg);
      }
    } else {
      Serial.printf("Error al decodificar la imagen, código: %d\n", decodeResult);
    }
    imageReady = false;
    processing = false;
    // Solicita el siguiente frame
    ws.textAll("requestFrame");
  }
}

// ----------------- Setup y Loop -----------------
void setup() {
  Serial.begin(115200);
  // Configurar el pin de señal con resistencia interna pull-down
  pinMode(SIGNAL_PIN, INPUT_PULLDOWN);

  // Iniciar el Access Point
  if (WiFi.softAP(ssid, password)) {
    Serial.println("Access Point iniciado");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Error al iniciar Access Point");
  }

  // Iniciar SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error montando SPIFFS");
    return;
  }

  // Inicializar el display
  tft.init();
  tft.setSwapBytes(true);
  // Configura inicialmente para el modo contador (rotación 1)
  tft.setRotation(1);
  currentRotation = 1;
  tft.fillScreen(TFT_BLACK);

  // Configurar WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Servir la página HTML en "/"
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.begin();
  Serial.println("Servidor HTTP y WebSocket iniciados");

  // Inicializar variables de la máquina de estados
  estadoActual = ESTADO_INICIAL;
  previousState = ESTADO_INICIAL;
  currentCount = 0;
  lastCountUpdate = millis();
}

void loop() {
  updateState();
  processImage();
}
