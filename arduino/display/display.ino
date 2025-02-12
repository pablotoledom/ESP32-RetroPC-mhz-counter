#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>      // Library for the TFT display
#include <JPEGDecoder.h>   // Library for decoding JPEG images

// ----------------- WiFi Access Point Configuration -----------------
const char* ssid     = "ESP32-WIFI-video";
const char* password = "TRC12345678";

// ----------------- Create HTTP Server and WebSocket -----------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ----------------- TFT Display Configuration -----------------
TFT_eSPI tft = TFT_eSPI();

// ----------------- HTML/JS Page (Frontend) -----------------
// The page is stored in flash memory (PROGMEM)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
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
        <li><a href="https://github.com/pablotoledom/">My GitHub</a></li>
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

// ----------------- JPEG Buffers -----------------
#define MAX_IMAGE_SIZE 30000
uint8_t imageBufferStatic[MAX_IMAGE_SIZE];
uint8_t imageBufferProcess[MAX_IMAGE_SIZE];
size_t imageBufferLength = 0;
volatile bool imageReady = false;
volatile bool processing = false;

// ----------------- Signal Pin Definition -----------------
#define SIGNAL_PIN 12

// ----------------- Enum for the State Machine -----------------
enum State {
  INITIAL_STATE,
  WAITING_STATE,
  COUNTDOWN_STATE,
  STREAMING_STATE,
  COUNT_STATE
};

// ----------------- Global Variables for the State Machine -----------------
State currentState = INITIAL_STATE;
State previousState = INITIAL_STATE;  // To detect state changes
int currentCount = 0;
unsigned long lastCountUpdate = 0;
unsigned long delayStartTime = 0;
#define UPDATE_INTERVAL 20
#define STEP_COUNT 10

// Variable to control the current display rotation
int currentRotation = -1;

// ----------------- Counter Display Area Configuration -----------------
const int counterRight = 226;    // Right edge for text alignment
const int counterY     = -66;      // Y coordinate of the counter area
const int counterWidth = 240;      // Width of the counter area (enough for up to 4 digits)
const int counterH     = 175;      // Height of the counter area

// Global variable to store the last displayed number
int lastDisplayedNumber = -1;

// Global flag to force red color for 666 after 3 seconds
bool forceRed666 = false;

// ----------------- Function to Update the Counter Display -----------------
void updateCounterDisplay(int count) {
  // Determine whether to show blinking or force red for 666
  bool blinking = (currentState == COUNTDOWN_STATE && count == 666 && delayStartTime != 0);
  bool forceRed = (forceRed666 && count == 666);

  // If not blinking or forcing red, avoid redrawing if the number hasn't changed
  if (!blinking && !forceRed) {
    if (count == lastDisplayedNumber) return;
    lastDisplayedNumber = count;
  }

  // Clear the counter display area
  tft.fillRect(counterRight - counterWidth, counterY, counterWidth, counterH, TFT_BLACK);

  // Set font, size, and alignment
  tft.setTextFont(7);         // 7-segment font
  tft.setTextSize(2);         // Size 2
  tft.setTextDatum(TR_DATUM);  // Right alignment

  if (blinking) {
    // Blinking effect in red during the countdown
    if ((millis() / 500) % 2 == 0) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString(String(count), counterRight, counterY + (counterH / 2));
    }
  } else if (forceRed) {
    // Force red: always display in red
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(String(count), counterRight, counterY + (counterH / 2));
  } else {
    // Normal mode: display in green
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(String(count), counterRight, counterY + (counterH / 2));
  }
}

// ----------------- State Machine Function -----------------
void updateState() {
  unsigned long currentMillis = millis();
  bool signalState = (digitalRead(SIGNAL_PIN) == HIGH);

  // Detect state change and force redraw on each transition
  if (currentState != previousState) {
    // If the new state is not STREAMING, clear the screen and reset the force flag
    if (currentState != STREAMING_STATE) {
      tft.fillScreen(TFT_BLACK);
      forceRed666 = false;
    }
    // Force the counter to be redrawn
    lastDisplayedNumber = -1;
    updateCounterDisplay(currentCount);
    previousState = currentState;
  }

  // Update display rotation based on state
  if (currentState == STREAMING_STATE) {
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

  // State Machine
  switch (currentState) {
    case INITIAL_STATE:
      if (currentMillis - lastCountUpdate >= UPDATE_INTERVAL) {
        lastCountUpdate = currentMillis;
        currentCount += STEP_COUNT;
        if (currentCount >= 1400) {
          currentCount = 1400;
          currentState = WAITING_STATE;
        }
        updateCounterDisplay(currentCount);
      }
      break;

    case WAITING_STATE:
      updateCounterDisplay(currentCount);
      if (signalState) {
        currentState = COUNTDOWN_STATE;
        currentCount = 1400;
        lastCountUpdate = currentMillis;
      }
      break;

    case COUNTDOWN_STATE:
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
            // After 3 seconds: force the display of 666 in red
            forceRed666 = true;
            currentState = STREAMING_STATE;
            delayStartTime = 0;
          }
        } else {
          updateCounterDisplay(currentCount);
        }
      }
      if (!signalState) {
        currentState = COUNT_STATE;
        currentCount = 666;
        lastCountUpdate = currentMillis;
        updateCounterDisplay(currentCount);
        delayStartTime = 0;
      }
      break;

    case STREAMING_STATE:
      if (!signalState) {
        currentState = COUNT_STATE;
        currentCount = 666;
        lastCountUpdate = currentMillis;
        updateCounterDisplay(currentCount);
      }
      break;

    case COUNT_STATE:
      if (currentMillis - lastCountUpdate >= UPDATE_INTERVAL) {
        lastCountUpdate = currentMillis;
        currentCount += STEP_COUNT;
        if (currentCount >= 1400) {
          currentCount = 1400;
        }
        updateCounterDisplay(currentCount);
      }
      if (signalState) {
        currentState = COUNTDOWN_STATE;
        currentCount = 1400;
        lastCountUpdate = currentMillis;
      }
      break;
  }
}

// ----------------- WebSocket Callback -----------------
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
      if (processing) return;  // Avoid overwriting while processing an image
      if (info->index == 0) {
        imageBufferLength = 0; // Reset the buffer
      }
      if (imageBufferLength + len <= MAX_IMAGE_SIZE) {
        memcpy(imageBufferStatic + imageBufferLength, data, len);
        imageBufferLength += len;
      } else {
        Serial.println("Error: Image buffer exceeded");
        imageBufferLength = 0;
      }
      if (info->final) {
        // Check that the JPEG is complete by verifying the end marker (0xFF, 0xD9)
        if (imageBufferLength >= 2 &&
            imageBufferStatic[imageBufferLength-2] == 0xFF &&
            imageBufferStatic[imageBufferLength-1] == 0xD9) {
          memcpy(imageBufferProcess, imageBufferStatic, imageBufferLength);
          imageReady = true;
        } else {
          Serial.println("Incomplete JPEG (end marker not found)");
        }
      }
    }
  }
}

// ----------------- Function to Process the Image -----------------
void processImage() {
  // Only process in STREAMING mode
  if (currentState != STREAMING_STATE) return;
  if (imageReady && !processing) {
    processing = true;
    int decodeResult = JpegDec.decodeArray(imageBufferProcess, imageBufferLength);
    if (decodeResult == 1) {
      Serial.println("Decoding successful, displaying image");
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
      Serial.printf("Error decoding image, code: %d\n", decodeResult);
    }
    imageReady = false;
    processing = false;
    // Request the next frame
    ws.textAll("requestFrame");
  }
}

// ----------------- Setup and Loop -----------------
void setup() {
  Serial.begin(115200);
  // Configure the signal pin with internal pull-down resistor
  pinMode(SIGNAL_PIN, INPUT_PULLDOWN);

  // Start the Access Point
  if (WiFi.softAP(ssid, password)) {
    Serial.println("Access Point started");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Error starting Access Point");
  }

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error mounting SPIFFS");
    return;
  }

  // Initialize the display
  tft.init();
  tft.setSwapBytes(true);
  // Initially configure for counter mode (rotation 1)
  tft.setRotation(1);
  currentRotation = 1;
  tft.fillScreen(TFT_BLACK);

  // Configure WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve the HTML page at "/"
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.begin();
  Serial.println("HTTP Server and WebSocket started");

  // Initialize state machine variables
  currentState = INITIAL_STATE;
  previousState = INITIAL_STATE;
  currentCount = 0;
  lastCountUpdate = millis();
}

void loop() {
  updateState();
  processImage();
}
