# Halloween Spider Controller

ESP32 project for a Halloween spider prop with:

- A DC motor controlled through a transistor on GPIO 25
- A simple speaker/buzzer on GPIO 33
- Wi-Fi web controls
- MCP tools for controlling the prop from Cursor

The prop can pulse the motor, run repeating motor patterns, play simple tone sequences, and start motor + sound sequences at the same time.

## Hardware

- ESP32
- Yellow TT-style DC motor
- 2N2222 transistor
- 330 ohm resistor marked `331`
- External battery pack for the motor
- Small speaker/buzzer on GPIO 33

Important wiring notes:

- Do not power the motor directly from an ESP32 GPIO pin.
- Motor power should come from the battery pack.
- ESP32 ground and battery negative must be connected together.
- GPIO 25 goes through the resistor to the transistor base.

## Arduino Setup

Create a local `secrets.h` file based on `secrets.example.h`:

```cpp
#pragma once

const char* WIFI_NAME = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

Upload `halloween_spider.ino` to the ESP32.

When the ESP32 starts, open the Serial Monitor at `115200` baud. It prints the IP address and also tries to advertise:

```text
http://esp32-motor.local
```

## Web Controls

The ESP32 serves a local web UI with buttons and sliders for:

- Motor on/off
- Motor modulation
- Tone frequency and volume
- Sound start/stop

## MCP Server

`control_server.py` exposes MCP tools and also serves a local web UI.

Create a virtualenv and install dependencies:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install --no-user -r requirements.txt
```

Run as MCP:

```bash
.venv/bin/python control_server.py --mcp
```

The MCP defaults to:

```text
http://esp32-motor.local
```

## MCP Tools

- `motor_on`
- `motor_off`
- `run_motor`
- `run_motor_sequence`
- `run_motor_and_sound_sequences`
- `start_motor_modulation`
- `stop_motor_modulation`
- `play_tone`
- `play_sound_sequence`
- `stop_sound`

Sequence tools support `repeat=true` to loop until stopped.

## Example Behavior

The current prop can run a repeating spider movement:

- Motor runs for 1 second
- Motor pauses for 10 seconds
- Optional 400 Hz burst starts at the same time as the motor

This is useful for a dangling spider that twitches when someone gets close.

## Demo



https://github.com/user-attachments/assets/0d766b3d-824f-499f-813f-7ea7307b2916






