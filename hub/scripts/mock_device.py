#!/usr/bin/env python3
"""Dispositivo simulado para o smoke test do hub (claim + HMAC)."""

from __future__ import annotations

import hashlib
import hmac
import json
import secrets
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qsl

PORT = 18081
ID = "dmx-test"
CLAIM_CODE = "TESTCLAIMCODE123"
AUTH_HEADER = "X-EasyArtNet-Auth"


def mac(key: bytes, data: bytes) -> bytes:
    return hmac.new(key, data, hashlib.sha256).digest()


def canonical_form(body: bytes) -> str:
    pairs = sorted(parse_qsl(body.decode(), keep_blank_values=True))
    return "&".join(f"{key}={value}" for key, value in pairs)


class Handler(BaseHTTPRequestHandler):
    session: bytes | None = None
    counter = 0

    def log_message(self, *args):
        pass

    def _send(self, status, body, content_type="text/html"):
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(length)

    def _authorized(self, method: str, path: str, body: bytes) -> bool:
        if Handler.session is None:
            return False
        header = self.headers.get(AUTH_HEADER, "")
        try:
            counter_text, tag_hex = header.split(":", 1)
            counter = int(counter_text)
        except ValueError:
            return False
        if counter <= Handler.counter:
            return False
        canonical = canonical_form(body) if method == "POST" else ""
        data = f"{method}\n{path}\n{counter}\n{canonical}".encode()
        expected = mac(Handler.session, data)
        if not hmac.compare_digest(expected, bytes.fromhex(tag_hex)):
            return False
        Handler.counter = counter
        return True

    def do_GET(self):
        if self.path == "/api/info":
            self._send(200, json.dumps({
                "id": ID, "name": ID, "mac": "6a:fe:71:81:68:bf",
                "ip": "127.0.0.1", "port": PORT, "fw": "0.1.0",
                "claimed": Handler.session is not None,
            }), "application/json")
        elif self.path == "/":
            if not self._authorized("GET", "/", b""):
                self._send(401, "unauthorized")
                return
            self._send(200, '<form action="salvar" method="POST"><input name="univ"></form>')
        else:
            self._send(404, "not found")

    def do_POST(self):
        body = self._read_body()
        if self.path == "/api/claim":
            try:
                payload = json.loads(body)
                nonce = bytes.fromhex(payload["nonce"])
                cipher = bytes.fromhex(payload["cipher"])
                tag = bytes.fromhex(payload["tag"])
            except (KeyError, ValueError, json.JSONDecodeError):
                self._send(400, "bad claim")
                return
            key = mac(CLAIM_CODE.encode(), b"easyartnet-claim")
            stream = mac(key, b"keystream" + nonce)
            session = bytes(a ^ b for a, b in zip(cipher, stream))
            expected = mac(key, b"tag" + nonce + cipher)
            if not hmac.compare_digest(tag, expected):
                self._send(401, "bad claim code")
                return
            Handler.session = session
            Handler.counter = 0
            self._send(200, '{"ok":true}', "application/json")
            return

        if self.path == "/salvar":
            if not self._authorized("POST", "/salvar", body):
                self._send(401, "unauthorized")
                return
            self._send(200, f"SAVED {body.decode()}")
            return

        if self.path == "/api/diagnostics/enable":
            if not self._authorized("POST", "/api/diagnostics/enable", b""):
                self._send(401, "unauthorized")
                return
            self._send(200, '{"ok":true}', "application/json")
            return

        self._send(404, "not found")


HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
