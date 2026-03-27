#!/usr/bin/env python3
"""
play.py -- Interactive web-based play mode for pokefirered-native
Open http://192.168.0.26:53581/ in browser to play.
"""

import sys, os, time, signal, threading, io, json, base64
import numpy as np

sys.path.insert(0, "/home/spark-advantage/pokefirered-native")
sys.path.insert(0, "/home/spark-advantage/pufferlib-4.0/.venv/lib/python3.12/site-packages")

from pfrn_native import binding
from PIL import Image
from http.server import HTTPServer, BaseHTTPRequestHandler

GBA_W, GBA_H = 240, 160
SCALE = 3
A_NOOP = 0
ACTION_NAMES = ["noop","UP","DOWN","LEFT","RIGHT","A","B","START"]

def argb_to_rgb(argb_array):
    return argb_array[:, :, 2::-1].copy()

class GameState:
    def __init__(self):
        self.frame_b64 = ""
        self.obs = None
        self.step_count = 0
        self.total_reward = 0.0
        self.last_action = 0
        self.lock = threading.Lock()
        self.pending_action = 0

class WebHandler(BaseHTTPRequestHandler):
    game = None
    def log_message(self, format, *args): pass

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(HTML_PAGE.encode())
        elif self.path.startswith("/frame_b64"):
            with self.game.lock:
                data = self.game.frame_b64
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Cache-Control", "no-cache, no-store")
            self.end_headers()
            self.wfile.write(data.encode())
        elif self.path == "/status":
            with self.game.lock:
                info = {
                    "step": self.game.step_count,
                    "reward": round(self.game.total_reward, 2),
                    "action": ACTION_NAMES[self.game.last_action],
                }
                if self.game.obs is not None:
                    o = self.game.obs
                    info["pos"] = "({},{})".format(
                        int(np.int16(o[0]|(o[1]<<8))),
                        int(np.int16(o[2]|(o[3]<<8))))
                    info["map"] = "{}.{}".format(o[4], o[5])
                    info["party_species"] = int(o[13] | (o[14] << 8))
                    info["party_level"] = int(o[15])
                    info["badges"] = int(o[48])
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(info).encode())
        elif self.path.startswith("/action/"):
            try:
                act = int(self.path.split("/")[-1])
                if 0 <= act <= 7:
                    self.game.pending_action = act
            except:
                pass
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(b"ok")
        else:
            self.send_response(404)
            self.end_headers()

HTML_PAGE = r"""<!DOCTYPE html>
<html>
<head>
<title>Pokemon FireRed - Human Play</title>
<style>
  * { box-sizing: border-box; }
  body { background: #111; color: #eee; font-family: monospace; text-align: center; margin: 0; padding: 10px; }
  canvas { border: 2px solid #444; display: block; margin: 10px auto; image-rendering: pixelated; }
  #controls { margin: 10px auto; max-width: 720px; }
  .btn {
    font-size: 20px; padding: 14px 22px; margin: 4px; cursor: pointer;
    background: #333; color: #eee; border: 2px solid #555; border-radius: 8px;
    min-width: 65px; user-select: none; touch-action: none;
  }
  .btn:hover { background: #555; }
  .btn.pressed { background: #0a0; color: #fff; border-color: #0f0; }
  #status { color: #aaa; margin: 8px; font-size: 14px; }
  .dpad { display: inline-grid; grid-template-columns: 68px 68px 68px; gap: 4px; vertical-align: middle; margin-right: 30px; }
  .dpad .blank { visibility: hidden; }
  .buttons { display: inline-block; vertical-align: middle; }
  #held { color: #0f0; font-size: 13px; margin-top: 6px; }
  .keys-hint { color: #555; font-size: 11px; margin-top: 10px; }
</style>
</head>
<body>
<h2>Pokemon FireRed - Human Play</h2>
<canvas id="screen" width="720" height="480"></canvas>
<div id="status">Connecting...</div>
<div id="controls">
  <div class="dpad">
    <div class="blank"></div>
    <button class="btn" id="b1" data-a="1">&#9650;</button>
    <div class="blank"></div>
    <button class="btn" id="b3" data-a="3">&#9664;</button>
    <div class="blank"></div>
    <button class="btn" id="b4" data-a="4">&#9654;</button>
    <div class="blank"></div>
    <button class="btn" id="b2" data-a="2">&#9660;</button>
    <div class="blank"></div>
  </div>
  <div class="buttons">
    <button class="btn" id="b5" data-a="5">A</button>
    <button class="btn" id="b6" data-a="6">B</button>
    <br>
    <button class="btn" id="b7" data-a="7">START</button>
  </div>
</div>
<div id="held"></div>
<div class="keys-hint">Keyboard: Arrows=D-pad, Z=A, X=B, Enter=Start</div>

<script>
const canvas = document.getElementById("screen");
const ctx2d = canvas.getContext("2d");
const NAMES = ["noop","UP","DOWN","LEFT","RIGHT","A","B","START"];
const KEY_MAP = {"ArrowUp":1,"ArrowDown":2,"ArrowLeft":3,"ArrowRight":4,"z":5,"Z":5,"x":6,"X":6,"Enter":7," ":5};
let heldAction = 0;
let frameImg = new Image();

document.querySelectorAll(".btn[data-a]").forEach(btn => {
  let a = parseInt(btn.dataset.a);
  function down(e) { e.preventDefault(); setHeld(a); btn.classList.add("pressed"); }
  function up(e) { e.preventDefault(); setHeld(0); btn.classList.remove("pressed"); }
  btn.addEventListener("mousedown", down);
  btn.addEventListener("mouseup", up);
  btn.addEventListener("mouseleave", up);
  btn.addEventListener("touchstart", down);
  btn.addEventListener("touchend", up);
  btn.addEventListener("touchcancel", up);
});

document.addEventListener("keydown", e => {
  if (KEY_MAP[e.key] !== undefined) { e.preventDefault(); setHeld(KEY_MAP[e.key]); let b=document.getElementById("b"+KEY_MAP[e.key]); if(b) b.classList.add("pressed"); }
});
document.addEventListener("keyup", e => {
  if (KEY_MAP[e.key] !== undefined) { e.preventDefault(); setHeld(0); let b=document.getElementById("b"+KEY_MAP[e.key]); if(b) b.classList.remove("pressed"); }
});

function setHeld(a) {
  heldAction = a;
  fetch("/action/" + a).catch(function(){});
  document.getElementById("held").textContent = a > 0 ? "Holding: " + NAMES[a] : "";
}

setInterval(function() { fetch("/action/" + heldAction).catch(function(){}); }, 80);

function refreshFrame() {
  fetch("/frame_b64?t=" + Date.now())
    .then(function(r) { return r.text(); })
    .then(function(b64) {
      if (b64.length > 100) {
        frameImg.onload = function() { ctx2d.drawImage(frameImg, 0, 0, 720, 480); };
        frameImg.src = "data:image/png;base64," + b64;
      }
    })
    .catch(function() {});
}
setInterval(refreshFrame, 80);

setInterval(function() {
  fetch("/status").then(function(r){return r.json();}).then(function(d) {
    document.getElementById("status").textContent =
      "Step:" + d.step + " | Reward:" + d.reward +
      " | Pos:" + d.pos + " | Map:" + d.map +
      " | Mon:#" + d.party_species + " Lv" + d.party_level +
      " | Badges:" + d.badges + " | " + d.action;
  }).catch(function(){});
}, 250);
</script>
</body>
</html>"""

def run_web_server(game, port):
    WebHandler.game = game
    server = HTTPServer(("0.0.0.0", port), WebHandler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server

_quit_flag = False

def main():
    global _quit_flag
    so_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "libpfr_game.so")

    print("Initializing game...", flush=True)
    binding.init_instances(so_path, "/tmp/pfrn_play", 1)

    obs = np.zeros((1, 226), dtype=np.uint8)
    actions = np.zeros((1,), dtype=np.float32)
    rewards = np.zeros((1,), dtype=np.float32)
    terminals = np.zeros((1,), dtype=np.uint8)
    truncations = np.zeros((1,), dtype=np.uint8)

    c_envs = binding.vec_init(obs, actions, rewards, terminals, truncations,
                              1, 42, frames_per_step=4, max_steps=999999, savestate_path="")
    binding.vec_reset(c_envs, 42)

    # Settle
    actions[0] = 0
    for _ in range(10):
        binding.vec_step(c_envs)

    game = GameState()
    game.obs = obs[0].copy()

    # Initial frame
    f = binding.capture_frame(0)
    rgb = argb_to_rgb(f)
    img = Image.fromarray(rgb).resize((GBA_W * SCALE, GBA_H * SCALE), Image.NEAREST)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    game.frame_b64 = base64.b64encode(buf.getvalue()).decode()

    port = 53581
    run_web_server(game, port)
    print("", flush=True)
    print("  >>> OPEN: http://192.168.0.26:{}/".format(port), flush=True)
    print("", flush=True)

    def handle_sigint(sig, frame):
        global _quit_flag
        _quit_flag = True
    signal.signal(signal.SIGINT, handle_sigint)

    while not _quit_flag:
        action = game.pending_action
        actions[0] = action
        binding.vec_step(c_envs)
        game.step_count += 1
        game.total_reward += rewards[0]
        game.last_action = action

        f = binding.capture_frame(0)
        rgb = argb_to_rgb(f)
        img = Image.fromarray(rgb).resize((GBA_W * SCALE, GBA_H * SCALE), Image.NEAREST)
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        b64 = base64.b64encode(buf.getvalue()).decode()

        with game.lock:
            game.frame_b64 = b64
            game.obs = obs[0].copy()

        time.sleep(1.0 / 30.0)  # 30 agent steps/sec = 120 GBA fps (2x speed)

    print("Done! Steps: {}".format(game.step_count), flush=True)
    binding.vec_close(c_envs)
    binding.destroy_instances()

if __name__ == "__main__":
    main()
