"""
pfr_stream.py - WebSocket streaming module for pokerl-map-viz

Sends agent positions from PokemonFireRed to the pokerl-map-viz visualizer
at pwhiddy.github.io/pokerl-map-viz via WebSocket broadcast.

Usage:
    streamer = PfrMapStreamer(user="my-agent", env_id="0")
    # In training loop:
    streamer.on_step(obs_bytes)
    # At end:
    streamer.flush()
"""

import json
import struct
import os
import time
import threading

WS_URL = "wss://transdimensional.xyz/broadcast"


class PfrMapStreamer:
    """Streams agent positions to pokerl-map-viz via WebSocket.

    Parses player position from observation bytes, converts FireRed
    map IDs to Red map IDs, and sends coordinate batches over WebSocket.
    """

    def __init__(self, user="pfrn-agent", env_id="0", color="#e6194b",
                 upload_interval=150, map_json_path=None):
        """
        Args:
            user: Username displayed in the visualizer.
            env_id: Environment ID string.
            color: Hex color for the agent dot on the map.
            upload_interval: Send coords every N steps.
            map_json_path: Path to pfr_to_red_map.json. Auto-detected if None.
        """
        self.user = user
        self.env_id = str(env_id)
        self.color = color
        self.upload_interval = upload_interval
        self.step_count = 0
        self.coord_buffer = []
        self.ws = None
        self._connected = False

        # Load FireRed -> Red map ID mapping
        if map_json_path is None:
            map_json_path = os.path.join(
                os.path.dirname(os.path.abspath(__file__)),
                "pfr_to_red_map.json"
            )
        with open(map_json_path) as f:
            self._pfr_to_red = json.load(f)

        # Connect WebSocket in background to avoid blocking training
        self._connect_thread = threading.Thread(target=self._connect, daemon=True)
        self._connect_thread.start()

    def _connect(self):
        """Establish WebSocket connection."""
        try:
            import websocket
            self.ws = websocket.create_connection(
                WS_URL,
                timeout=10,
                header={"User-Agent": "pfr-stream/1.0"}
            )
            self._connected = True
            print("[PfrStream] Connected to %s" % WS_URL)
        except Exception as e:
            print("[PfrStream] WebSocket connection failed: %s" % e)
            self._connected = False

    def _get_red_map_id(self, map_group, map_num):
        """Convert FireRed (map_group, map_num) to Red map ID string."""
        pfr_id = str(map_group * 256 + map_num)
        red_id = self._pfr_to_red.get(pfr_id, "-1")
        return red_id

    def on_step(self, obs_bytes):
        """Process one observation and buffer the coordinate.

        Args:
            obs_bytes: Raw observation bytes/array. First 6 bytes contain:
                bytes 0-1: player x (int16 LE)
                bytes 2-3: player y (int16 LE)
                byte 4: map_group
                byte 5: map_num
        """
        if not isinstance(obs_bytes, (bytes, bytearray)):
            # numpy array or similar - convert to bytes
            obs_bytes = bytes(obs_bytes[:6])

        if len(obs_bytes) < 6:
            return

        px = struct.unpack_from("<h", obs_bytes, 0)[0]
        py = struct.unpack_from("<h", obs_bytes, 2)[0]
        map_group = obs_bytes[4]
        map_num = obs_bytes[5]

        red_map_id = self._get_red_map_id(map_group, map_num)

        # Skip unmapped maps (Sevii Islands etc.)
        if red_map_id == "-1":
            self.step_count += 1
            return

        self.coord_buffer.append([px, py, int(red_map_id)])
        self.step_count += 1

        if self.step_count % self.upload_interval == 0 and self.coord_buffer:
            self._send_batch()

    def _send_batch(self):
        """Send buffered coordinates via WebSocket."""
        if not self.coord_buffer:
            return

        if not self._connected or self.ws is None:
            # Try reconnecting
            if not self._connected:
                self._connect()
            if not self._connected:
                self.coord_buffer.clear()
                return

        message = {
            "metadata": {
                "user": self.user,
                "env_id": self.env_id,
                "color": self.color,
                "extra": "pokefirered-native"
            },
            "coords": self.coord_buffer
        }

        try:
            self.ws.send(json.dumps(message))
        except Exception as e:
            print("[PfrStream] Send failed: %s" % e)
            self._connected = False
            try:
                self.ws.close()
            except Exception:
                pass
            self.ws = None

        self.coord_buffer = []

    def flush(self):
        """Send any remaining buffered coordinates."""
        self._send_batch()

    def close(self):
        """Flush and close the WebSocket connection."""
        self.flush()
        if self.ws is not None:
            try:
                self.ws.close()
            except Exception:
                pass
            self.ws = None
            self._connected = False
            print("[PfrStream] Connection closed.")
