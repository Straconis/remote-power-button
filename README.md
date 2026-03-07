# Remote Power Button

Turn a **LilyGo T-Display S3** and an **optocoupler** into a
remote-controlled PC power button.

This project allows an ESP32-S3 board to safely trigger a computer's
motherboard **power** or **reset** pins over WiFi while displaying
device status on the onboard screen.

------------------------------------------------------------------------

## Features

-   Remote **PC power control**
-   Remote **reset control**
-   Built-in **WiFi configuration portal**
-   Status display on the **LilyGo T-Display S3**
-   Safe motherboard interface using **optocouplers**
-   Designed for **ESP32-S3**

------------------------------------------------------------------------

## Hardware Requirements

-   LilyGo **T-Display S3**
-   **PC817 optocouplers** (or equivalent)
-   Jumper wires
-   Access to your motherboard **power/reset header**
-   Optional 3D printed enclosure

------------------------------------------------------------------------

## Software Dependencies

Install the following using **Arduino Library Manager**:

-   **LovyanGFX**

You must also install the **ESP32 board support package**.

------------------------------------------------------------------------

## Installing ESP32 Board Support

In the Arduino IDE:

1.  Open **File → Preferences**
2.  Add this to *Additional Board Manager URLs*:

https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

3.  Open **Tools → Board → Boards Manager**
4.  Install **ESP32 by Espressif Systems**

------------------------------------------------------------------------

## Installing Required Libraries

Install via Arduino Library Manager:

LovyanGFX

------------------------------------------------------------------------

## Flashing the Firmware

1.  Open `remotepower.ino`
2.  Select board **ESP32S3 Dev Module**
3.  Connect your **LilyGo T-Display S3**
4.  Upload the firmware

------------------------------------------------------------------------

## Configuration

On first boot the device launches a **WiFi configuration portal**.

1.  Connect to the temporary WiFi network created by the device
2.  Enter your WiFi credentials
3.  The device will reboot and connect to your network

------------------------------------------------------------------------

## Wiring

The ESP32 triggers the motherboard power/reset pins using an
**optocoupler** to electrically isolate the PC from the microcontroller.

Typical connection:

ESP32 GPIO → Optocoupler → Motherboard Power Header

  ESP32 Pin   Function
  ----------- --------------
  GPIO XX     Power Switch
  GPIO XX     Reset Switch

Replace with your final GPIO assignments if needed.

------------------------------------------------------------------------

## Project Structure

remote-power-button │ ├── remotepower.ino ├── ConfigPortal.h ├──
Indicators.h ├── lgfx_tdisplay_s3.h ├── README.md └── LICENSE

------------------------------------------------------------------------

## License

MIT License
