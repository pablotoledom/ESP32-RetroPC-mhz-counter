# ESP32 Video Streaming & Counter Display Project

## Overview
This project uses an **ESP32** to create a standalone **video streaming and counter display system**. It serves a web page where users can **upload a video**, capture frames, and send them via **WebSocket** to be displayed on a **TFT screen**. Additionally, it implements a **state machine** with a countdown that highlights a specific number (666) in **red** after a delay.

## Features
- **ESP32 as an Access Point** (AP) to host a local web server.
- **WebSocket communication** for real-time image transfer.
- **JPEG decoding** and rendering on a **TFT display**.
- **State Machine** to control counter logic and transitions.
- **Automatic countdown** that turns red when reaching 666 and stays red after a delay.

## Components Used
- **ESP32** (with Wi-Fi capability)
- **TFT Display** (supported by the TFT_eSPI library)
- **Push Button / Signal Input** (for triggering state changes)

## Project Architecture
### 1. **Web Interface**
- User uploads a **video file**.
- The browser captures frames and **converts them to JPEG**.
- The **JPEG frames are sent to ESP32** via WebSocket.

### 2. **ESP32 Server**
- Runs an **Access Point (AP)** mode.
- Serves an **HTML page** stored in PROGMEM.
- Handles WebSocket communication **(receiving image data)**.
- Uses **JPEGDecoder** to decode and render images on the TFT.

### 3. **State Machine**
| State               | Description |
|--------------------|-------------|
| INITIAL           | Counts up to 1400. |
| WAITING           | Waits for input signal. |
| COUNTDOWN        | Decreases to 666, blinks in red. |
| STREAMING        | Displays incoming video frames. |
| COUNT         | Increases back to 1400 if signal is lost. |

## Key Functionalities
### 1. **WebSocket Communication**
- Browser captures video frames → **Encodes as JPEG**.
- Sends **binary data over WebSocket**.
- ESP32 **decodes** and renders image to TFT.

### 2. **Counter Display Logic**
- Uses **a 7-segment font** for display.
- Counts **down from 1400 to 666**.
- At 666, **blinks in red for 3 seconds**.
- After delay, **remains red permanently**.

### 3. **TFT Display Rendering**
- Uses **TFT_eSPI** for graphics.
- Clears and updates display based on **state changes**.
- JPEG frames **streamed dynamically** to display.

## Installation & Setup
### 1. **Hardware Setup**
- Connect **TFT display** to ESP32 (SPI interface).
- Connect a **push button or signal trigger** to GPIO 12.

### 2. **Software Setup**
- Install **Arduino IDE** with ESP32 board support.
- Install required libraries:
  ```sh
  Arduino Library Manager:
  - ESPAsyncWebServer
  - AsyncTCP
  - TFT_eSPI
  - JPEGDecoder
  ```
- Configure **TFT_eSPI** library (`User_Setup.h`).
- Flash the firmware to ESP32.

## Usage
1. Connect to **ESP32 Wi-Fi AP** (SSID: `ESP32-WIFI-video`).
2. Open browser and visit `http://192.168.4.1/`.
3. Upload a video file.
4. Click **Start Capture** to begin frame streaming.
5. Observe counter countdown and color change logic.

## Future Enhancements
- Support for **multiple clients**.
- Improved **frame compression** to enhance performance.
- Add **real-time frame adjustments** (brightness, contrast).
- Integrate **SD card storage** for captured frames.

## Credits
**Developed by:** Pablo Toledo  
GitHub: [pablotoledom](https://github.com/pablotoledom/)  
Website: [The Retro Center](https://theretrocenter.com)
