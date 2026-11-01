#!/usr/bin/env python3
import json
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = 18081
ID = "dmx-test"

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, status, body, content_type="text/html"):
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/api/info":
            self._send(200, json.dumps({
                "id": ID, "name": ID, "mac": "6a:fe:71:81:68:bf",
                "ip": "127.0.0.1", "port": PORT, "fw": "0.1.0", "claimed": False,
            }), "application/json")
        else:
            self._send(200, '<form action="salvar" method="POST"><input name="univ"></form>')

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode()
        if self.path == "/salvar":
            self._send(200, f"SAVED {body}")
        else:
            self._send(404, "not found")

HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
