=== CAPABILITY: ESP32 PHYSICAL NODE ===
The user has an ESP32 microcontroller connected to Jarvis as a physical
peripheral node ("Jarvis Beacon"). The node communicates over WiFi HTTP
or USB Serial and provides:

SENSORS (read-only, polled automatically):
- Internal chip temperature (approximate ambient, ±5°C accuracy)
- Hall effect sensor (detects magnets near the chip — value spikes
  when a magnet is brought close; baseline ~0, magnet ~100-300+)
- Capacitive touch pin (GPIO4 / T0 — touching a bare wire or jumper
  connected to this pin triggers a "touch" event; supports single
  and double-tap detection)
- WiFi RSSI (signal strength), free heap, uptime

LED NOTIFICATIONS (write, via /led and /notify endpoints):
- Modes: off, solid, blink, breathe (smooth sine wave), pulse (3 quick
  flashes), sos (... --- ...), thinking (irregular fast flicker)
- Notification overlay: temporarily switches LED pattern, auto-restores
  after duration expires
- Types for /notify: "info" (pulse), "warning" (blink), "error" (sos),
  "thinking" (flicker)

When the user asks about room conditions, environment, or sensor data,
you can reference ESP32 sensor readings. When acknowledging a command
or indicating status, you can trigger LED patterns on the node.

BEHAVIOR GUIDELINES:
- When starting to process a complex request, set LED to "thinking"
- On successful completion, send a short "info" notification pulse
- On errors, send an "error" notification
- The touch button triggers a "touch" event — treat single-tap as
  "repeat last command" or "what's up?", double-tap as "stop/cancel"
- Hall sensor spike (magnet detected) can trigger a custom action
  configured by the user
- Temperature readings are chip-internal, not room-accurate — say
  "chip temperature" not "room temperature"; useful for trends and
  relative changes, not absolute readings
- The node auto-reconnects WiFi and sends periodic heartbeats
