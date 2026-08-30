#!/usr/bin/env python3
"""Serve web/ with synthetic /api fixtures and capture README screenshots.

Fixtures are UI orientation only — not bench telemetry and not ISR/OUTEN proof.
Requires: pip install playwright; python -m playwright install chromium
"""

from __future__ import annotations

import json
import socket
import struct
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "web"
OUT = ROOT / "docs" / "images"

STATUS_INHIBITED = {
    "uptimeMs": 3723000,
    "version": "fixture",
    "resetCause": "POWER_ON",
    "hostname": "teg-fixture",
    "crash": False,
    "active": False,
    "fault": False,
    "restartInhibit": True,
    "hardwareInhibit": False,
    "provisioningInhibit": False,
    "outputsInhibited": True,
    "configPersistPending": False,
    "pwmConfigValid": True,
    "ota": False,
    "otaEnabled": False,
    "mtp": False,
    "ocLimit": False,
    "isrCycles": 0,
    "missedIsrCycles": 0,
    "thermalMissedCycles": 0,
    "applyMicros": 842,
    "modMilliHz": 50000,
    "indexMilli": 0,
    "targetMilli": 800,
    "dtcmFree": 280000,
    "stackLowWater": 12000,
    "ocramFree": 420000,
    "captureActive": False,
    "captureFrozen": False,
    "captureSamples": 0,
    "captureVoltageMisses": 0,
    "captureCurrentMisses": 0,
    "mqttConnected": False,
    "mqttPublishFailures": 0,
    "mpptEnabled": False,
    "pllEnabled": False,
    "meterActive": False,
    "hotDeciC": 245,
    "coldDeciC": 238,
    "chipDeciC": 412,
    "derateMilli": 1000,
    "feedbackMv": 0,
    "streamUnderruns": 0,
    "auxMonEnabled": False,
}

# Second fixture: latched software fault banner (still inhibited / no OUTEN).
STATUS_FAULT = dict(STATUS_INHIBITED)
STATUS_FAULT.update({"restartInhibit": False, "fault": True})

CONFIG = {
    "Config": {
        "SchemaVersion": 1,
        "Pwm": {
            "Tm2": {
                "UseSpwm": True,
                "ModulationScheme": 1,
                "SpwmCarrierFrequency": 20000,
                "SpwmModulationFrequency": 50,
                "ModulationIndexMilli": 800,
                "ModulationCells": 2,
                "CarrierDisposition": 0,
                "DpwmVariant": 0,
                "DpwmClampAngleDeg": 0,
                "ReferenceWaveform": 0,
                "SoftStartMs": 500,
                "CarrierDitherMode": 0,
                "CarrierDitherPercent": 0,
                "NearestLevelModulation": False,
                "Sm20": {"Pair": 1, "DeadTime": 500, "ChannelA": {"DutyCycle": 32768}, "ChannelB": {"DutyCycle": 32768}},
                "Sm21": {"Pair": 0, "DeadTime": 500, "ChannelA": {"DutyCycle": 32768}},
                "Sm22": {"Pair": 1, "DeadTime": 500, "ChannelA": {"DutyCycle": 32768}, "ChannelB": {"DutyCycle": 32768}},
                "Sm23": {"Pair": 1, "DeadTime": 500, "ChannelA": {"DutyCycle": 32768}, "ChannelB": {"DutyCycle": 32768}},
            },
            "Tm1": {"Sm13": {"PwmFrequency": 20000, "Pair": 1, "DeadTime": 500, "ChannelA": {"DutyCycle": 0}, "ChannelB": {"DutyCycle": 0}}},
            "Tm3": {"Sm31": {"PwmFrequency": 20000, "Pair": 1, "DeadTime": 500, "ChannelA": {"DutyCycle": 0}, "ChannelB": {"DutyCycle": 0}}},
            "Tm4": {
                "Sm40": {"PwmFrequency": 20000, "Pair": 0, "DeadTime": 0, "ChannelA": {"DutyCycle": 0}},
                "Sm41": {"PwmFrequency": 20000, "Pair": 0, "DeadTime": 0, "ChannelA": {"DutyCycle": 0}},
                "Sm42": {"PwmFrequency": 20000, "Pair": 1, "DeadTime": 500, "ChannelA": {"DutyCycle": 0}, "ChannelB": {"DutyCycle": 0}},
            },
            "Verbose": False,
        },
        "Feedback": {
            "Enabled": False,
            "SetpointMillivolts": 0,
            "FullScaleMillivolts": 3300,
            "KpMilli": 100,
            "KiMilli": 10,
            "AnalogPin": 41,
            "LoopHz": 250,
        },
        "Meter": {
            "Enabled": False,
            "CurrentPin": 40,
            "VoltageZeroMillivolts": 1650,
            "CurrentZeroMillivolts": 1650,
            "CurrentMilliampPerVolt": 1000,
            "VoltageRatioMilli": 1000,
        },
        "Thermal": {
            "Enabled": True,
            "OneWirePin": 14,
            "DerateStartC": 70,
            "DerateFullC": 85,
        },
        "PowerMon": {"Enabled": False, "IntervalMs": 250},
        "Capture": {"Enabled": True},
        "CurrentLimit": {"Enabled": False},
        "Mppt": {"Enabled": False},
        "Pll": {"Enabled": False},
        "Mtp": {"Enabled": True},
        "Mqtt": {"Enabled": False, "Host": "", "Port": 1883, "Username": "", "Password": "", "BaseTopic": "teg", "DiscoveryPrefix": "homeassistant"},
        "Influx": {"IntervalSeconds": 0, "Host": "", "Port": 8086, "Org": "", "Bucket": "", "Token": ""},
        "Security": {"WritePin": ""},
    }
}

LOG = {
    "clockValid": True,
    "ntpSynced": False,
    "now": 0,
    "bootId": 1,
    "newest": 3,
    "gap": False,
    "events": [
        {"seq": 1, "uptimeMs": 1200, "level": "info", "text": "fixture: outputs inhibited (restart interlock)"},
        {"seq": 2, "uptimeMs": 1800, "level": "info", "text": "fixture: thermal enabled — waiting for DS18B20 sample"},
        {"seq": 3, "uptimeMs": 2400, "level": "warn", "text": "fixture: UI chrome only — not bench proof"},
    ],
}

# Mutable view for the handler.
_current_status = dict(STATUS_INHIBITED)


class FixtureHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB), **kwargs)

    def log_message(self, fmt, *args):
        pass

    def _json(self, code: int, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _bytes(self, code: int, data: bytes, content_type: str):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path.startswith("/api/status"):
            self._json(200, _current_status)
            return
        if path == "/api/config" or path.startswith("/api/config?"):
            self._json(200, CONFIG)
            return
        if path.startswith("/api/log"):
            self._json(200, LOG)
            return
        if path.startswith("/api/presets"):
            self._json(200, {"presets": []})
            return
        if path.startswith("/api/waveform"):
            self._json(200, {"type": "none", "count": 0, "streaming": False, "underruns": 0})
            return
        if path.startswith("/api/scope"):
            self._json(200, {"state": "idle", "source": "v", "edge": "rising", "levelMv": 1650, "postSamples": 0, "trigSample": 0, "totalSamples": 0, "frozen": False})
            return
        if path.startswith("/api/spectrum"):
            # 32-byte LE TEGS header, flags=0 (unavailable) — matches production unavailable contract.
            header = bytearray(32)
            header[0:4] = b"TEGS"
            header[4:8] = struct.pack("<I", 1)  # version
            self._bytes(200, bytes(header), "application/octet-stream")
            return
        if path.startswith("/api/"):
            self._json(404, {"error": "fixture stub"})
            return
        return super().do_GET()

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length:
            self.rfile.read(length)
        path = self.path.split("?", 1)[0]
        if path == "/api/fault/clear":
            global _current_status
            _current_status = dict(STATUS_INHIBITED)
            self._json(200, {"ok": True})
            return
        if path.startswith("/api/"):
            self._json(200, {"ok": True, "applyMicros": 842, "persistPending": True})
            return
        self.send_error(405)


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main() -> None:
    from playwright.sync_api import sync_playwright

    OUT.mkdir(parents=True, exist_ok=True)
    port = free_port()
    server = ThreadingHTTPServer(("127.0.0.1", port), FixtureHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{port}"
    time.sleep(0.2)

    global _current_status
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page(viewport={"width": 1280, "height": 900}, color_scheme="dark")

            _current_status = dict(STATUS_INHIBITED)
            page.goto(f"{base}/", wait_until="networkidle")
            page.wait_for_timeout(1200)
            page.screenshot(
                path=str(OUT / "readme-ui-settings-inhibited.png"),
                clip={"x": 0, "y": 0, "width": 1280, "height": 820},
            )

            page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
            # Scroll MTP into view
            page.locator("text=USB File Access").scroll_into_view_if_needed()
            page.wait_for_timeout(400)
            page.screenshot(
                path=str(OUT / "readme-ui-settings-mtp.png"),
                clip={"x": 0, "y": 0, "width": 1280, "height": 820},
            )

            _current_status = dict(STATUS_FAULT)
            page.goto(f"{base}/", wait_until="networkidle")
            page.wait_for_timeout(1200)
            page.screenshot(
                path=str(OUT / "readme-ui-settings-fault-banner.png"),
                clip={"x": 0, "y": 0, "width": 1280, "height": 520},
            )

            _current_status = dict(STATUS_INHIBITED)
            page.goto(f"{base}/stats.html", wait_until="networkidle")
            page.wait_for_timeout(2500)
            page.screenshot(
                path=str(OUT / "readme-ui-stats.png"),
                clip={"x": 0, "y": 0, "width": 1280, "height": 900},
            )

            browser.close()
    finally:
        server.shutdown()

    for name in (
        "readme-ui-settings-inhibited.png",
        "readme-ui-settings-mtp.png",
        "readme-ui-settings-fault-banner.png",
        "readme-ui-stats.png",
    ):
        path = OUT / name
        print(f"{name}: {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
