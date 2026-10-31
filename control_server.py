import argparse
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Any
from urllib.parse import parse_qs, quote, urlparse
from urllib.request import urlopen


PORT = 8000
DEFAULT_ESP32_BASE_URL = os.environ.get("ESP32_BASE_URL", "http://esp32-motor.local")


def normalize_base_url(base_url: str) -> str:
    base_url = base_url.strip()
    if not base_url.startswith(("http://", "https://")):
        base_url = f"http://{base_url}"
    return base_url.rstrip("/")


def call_esp32(path: str, base_url: str | None = None, timeout: int = 10) -> str:
    target = normalize_base_url(base_url or DEFAULT_ESP32_BASE_URL)
    with urlopen(f"{target}{path}", timeout=timeout) as response:
        response.read()
    return f"Sent {path} to {target}"


def sound_steps_to_query(steps: list[dict[str, Any]]) -> str:
    encoded_steps = []
    for step in steps[:64]:
        frequency = max(0, min(5000, int(step.get("frequency", step.get("freq", 0)))))
        duration_ms = max(10, min(10000, int(step.get("duration_ms", step.get("duration", step.get("ms", 100))))))
        volume = max(0, min(100, int(step.get("volume", 50))))
        encoded_steps.append(f"{frequency},{duration_ms},{volume}")
    return quote(";".join(encoded_steps), safe="")


def motor_steps_to_query(steps: list[dict[str, Any]]) -> str:
    encoded_steps = []
    for step in steps[:64]:
        power = max(0, min(100, int(step.get("power", step.get("speed", 100)))))
        duration_ms = max(10, min(10000, int(step.get("duration_ms", step.get("duration", step.get("ms", 250))))))
        encoded_steps.append(f"{power},{duration_ms}")
    return quote(";".join(encoded_steps), safe="")


def repeat_value(repeat: bool) -> int:
    return 1 if repeat else 0


def page(message=""):
    return f"""<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {{ font-family: Arial, sans-serif; text-align: center; margin-top: 40px; }}
    input {{ font-size: 20px; padding: 10px; width: 220px; }}
    input[type=range] {{ width: 260px; }}
    button {{ font-size: 22px; padding: 16px 24px; margin: 10px; border-radius: 10px; border: 0; }}
    .on {{ background: #4caf50; color: white; }}
    .off {{ background: #f44336; color: white; }}
    .jiggle {{ background: #ff9800; color: white; }}
    .sound {{ background: #673ab7; color: white; }}
    .stopSound {{ background: #607d8b; color: white; }}
  </style>
  <script>
    function sendCommand(action) {{
      const ip = encodeURIComponent(document.getElementById("ip").value);
      const frequency = encodeURIComponent(document.getElementById("frequency").value);
      const volume = encodeURIComponent(document.getElementById("volume").value);
      const motorSpeed = encodeURIComponent(document.getElementById("motorSpeed").value);
      const spin = encodeURIComponent(document.getElementById("spin").value);
      const pause = encodeURIComponent(document.getElementById("pause").value);
      window.location = `/command?ip=${{ip}}&action=${{action}}&frequency=${{frequency}}&volume=${{volume}}&motorSpeed=${{motorSpeed}}&spin=${{spin}}&pause=${{pause}}`;
    }}
  </script>
</head>
<body>
  <h1>ESP32 Motor Control</h1>
  <p>
    <label>ESP32 IP:</label><br>
      <input id="ip" value="{DEFAULT_ESP32_BASE_URL}" required>
  </p>
  <button type="button" class="on" onclick="sendCommand('on')">Motor On</button>
  <button type="button" class="off" onclick="sendCommand('off')">Motor Off</button>
  <button type="button" class="jiggle" onclick="sendCommand('jiggle')">Pulse Once</button>
  <h2>Motor Modulation</h2>
  <p>Speed: <span id="motorSpeedValue">100</span>%</p>
  <input id="motorSpeed" type="range" min="0" max="100" value="100" oninput="motorSpeedValue.textContent=this.value">
  <p>Spin Duration: <span id="spinValue">250</span> ms</p>
  <input id="spin" type="range" min="10" max="3000" value="250" oninput="spinValue.textContent=this.value">
  <p>Pause Duration: <span id="pauseValue">750</span> ms</p>
  <input id="pause" type="range" min="10" max="5000" value="750" oninput="pauseValue.textContent=this.value">
  <p>
    <button type="button" class="jiggle" onclick="sendCommand('motorMod')">Start Motor Modulation</button>
    <button type="button" class="off" onclick="sendCommand('motorModStop')">Stop Motor Modulation</button>
  </p>
  <button type="button" class="sound" onclick="sendCommand('sound')">Play Sound</button>
  <button type="button" class="stopSound" onclick="sendCommand('stopSound')">Stop Sound</button>
  <h2>Custom Tone</h2>
  <p>Frequency: <span id="freqValue">400</span> Hz</p>
  <input id="frequency" type="range" min="50" max="5000" value="400" oninput="freqValue.textContent=this.value">
  <p>Volume: <span id="volumeValue">50</span>%</p>
  <input id="volume" type="range" min="0" max="100" value="50" oninput="volumeValue.textContent=this.value">
  <p><button type="button" class="sound" onclick="sendCommand('tone')">Send Tone</button></p>
  <p>{message}</p>
</body>
</html>"""


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)

        if parsed.path == "/command":
            params = parse_qs(parsed.query)
            ip = params.get("ip", [""])[0].strip()
            action = params.get("action", [""])[0].strip()
            frequency = params.get("frequency", ["400"])[0].strip()
            volume = params.get("volume", ["50"])[0].strip()
            motor_speed = params.get("motorSpeed", ["100"])[0].strip()
            spin = params.get("spin", ["250"])[0].strip()
            pause = params.get("pause", ["750"])[0].strip()

            routes = {
                "on": "/motor/on",
                "off": "/motor/off",
                "jiggle": "/motor/jiggle",
                "motorMod": f"/motor/modulate?speed={motor_speed}&spin={spin}&pause={pause}",
                "motorModStop": "/motor/modulate/stop",
                "sound": "/sound/play",
                "stopSound": "/sound/stop",
                "tone": f"/tone/send?frequency={frequency}&volume={volume}",
            }

            if action not in routes:
                self.send_page("Unknown action.")
                return

            try:
                message = call_esp32(routes[action], ip, timeout=10)
                self.send_page(message)
            except Exception as error:
                self.send_page(f"Could not reach ESP32 at {ip}: {error}")
            return

        self.send_page()

    def send_page(self, message=""):
        html = page(message).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(html)))
        self.end_headers()
        self.wfile.write(html)


def run_web_ui(port: int = PORT):
    server = HTTPServer(("localhost", port), Handler)
    print(f"Open http://localhost:{port}", file=sys.stderr)
    server.serve_forever()


def build_mcp_server():
    try:
        from mcp.server.fastmcp import FastMCP
    except ImportError as error:
        raise SystemExit(
            "The Python MCP package is not installed. Install it with:\n"
            "python3 -m pip install mcp"
        ) from error

    mcp = FastMCP("esp32-spider-controller")

    @mcp.tool()
    def motor_on() -> str:
        """Turn the ESP32 motor on."""
        return call_esp32("/motor/on")

    @mcp.tool()
    def motor_off() -> str:
        """Turn the ESP32 motor off."""
        return call_esp32("/motor/off")

    @mcp.tool()
    def start_motor_modulation(speed: int = 100, spin_ms: int = 250, pause_ms: int = 750) -> str:
        """Start a repeating motor pattern with speed, spin duration, and pause duration."""
        speed = max(0, min(100, int(speed)))
        spin_ms = max(10, min(10000, int(spin_ms)))
        pause_ms = max(10, min(10000, int(pause_ms)))
        return call_esp32(f"/motor/modulate?speed={speed}&spin={spin_ms}&pause={pause_ms}")

    @mcp.tool()
    def stop_motor_modulation() -> str:
        """Stop the repeating motor modulation pattern."""
        return call_esp32("/motor/modulate/stop")

    def run_motor_step(power: int = 100, duration_ms: int = 250, repeat: bool = False) -> str:
        power = max(0, min(100, int(power)))
        duration_ms = max(10, min(10000, int(duration_ms)))
        return call_esp32(f"/motor/run?power={power}&duration={duration_ms}&repeat={repeat_value(repeat)}")

    @mcp.tool()
    def run_motor(power: int = 100, duration_ms: int = 250, repeat: bool = False) -> str:
        """Run the motor at a power percentage for a fixed duration, then stop it unless repeat is true."""
        return run_motor_step(power, duration_ms, repeat)

    @mcp.tool()
    def run_motor_sequence(steps: list[dict[str, Any]], repeat: bool = False) -> str:
        """Run a sequence of motor actions. Each step supports power and duration_ms. Use power 0 for a pause. Set repeat true to loop."""
        query = motor_steps_to_query(steps)
        return call_esp32(f"/motor/sequence?steps={query}&repeat={repeat_value(repeat)}", timeout=15)

    @mcp.tool()
    def run_motor_and_sound_sequences(
        motor_steps: list[dict[str, Any]],
        sound_steps: list[dict[str, Any]],
        repeat: bool = False,
    ) -> str:
        """Start motor and sound sequences at the same time on the ESP32. Set repeat true to loop both sequences."""
        motor_query = motor_steps_to_query(motor_steps)
        sound_query = sound_steps_to_query(sound_steps)
        return call_esp32(f"/combo/sequence?motor={motor_query}&sound={sound_query}&repeat={repeat_value(repeat)}", timeout=15)

    @mcp.tool()
    def stop_sound() -> str:
        """Stop any sound currently playing."""
        return call_esp32("/sound/stop")

    def play_tone_step(frequency: int = 400, volume: int = 50, duration_ms: int = 0) -> str:
        frequency = max(50, min(5000, int(frequency)))
        volume = max(0, min(100, int(volume)))
        duration_ms = max(0, min(10000, int(duration_ms)))

        if duration_ms > 0:
            return call_esp32(f"/sound/sequence?steps={frequency}%2C{duration_ms}%2C{volume}")

        return call_esp32(f"/tone/send?frequency={frequency}&volume={volume}")

    @mcp.tool()
    def play_tone(frequency: int = 400, volume: int = 50, duration_ms: int = 0) -> str:
        """Play a tone at a frequency and approximate volume. If duration_ms is greater than 0, stop it after that time."""
        return play_tone_step(frequency, volume, duration_ms)

    @mcp.tool()
    def play_sound_sequence(steps: list[dict[str, Any]], repeat: bool = False) -> str:
        """Play a sound sequence on the ESP32. Each step supports frequency, duration_ms, and volume. Use frequency 0 for silence. Set repeat true to loop."""
        query = sound_steps_to_query(steps)
        return call_esp32(f"/sound/sequence?steps={query}&repeat={repeat_value(repeat)}", timeout=15)

    return mcp


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ESP32 web UI and MCP bridge")
    parser.add_argument("--mcp", action="store_true", help="Run as an MCP server and also start the web UI")
    parser.add_argument("--port", type=int, default=PORT, help="Local web UI port")
    args = parser.parse_args()

    if args.mcp:
        threading.Thread(target=run_web_ui, args=(args.port,), daemon=True).start()
        build_mcp_server().run()
    else:
        run_web_ui(args.port)
