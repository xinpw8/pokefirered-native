import requests
import time
import base64

BASE = "http://localhost:53581"

def action(n, hold_ms=35, wait_ms=500):
    requests.get(f"{BASE}/action/{n}")
    time.sleep(hold_ms / 1000.0)
    requests.get(f"{BASE}/action/0")
    time.sleep(wait_ms / 1000.0)

def hold_action(n, hold_ms):
    """Hold action for specified ms without releasing"""
    requests.get(f"{BASE}/action/{n}")
    time.sleep(hold_ms / 1000.0)
    requests.get(f"{BASE}/action/0")

def status():
    return requests.get(f"{BASE}/status").json()

def save_frame(path):
    data = requests.get(f"{BASE}/frame_b64").text
    with open(path, "wb") as f:
        f.write(base64.b64decode(data))

print("=== SURF TEST v4 - Rapid A presses ===")

# Face DOWN and trigger prompt
print("[1] Face DOWN + A to trigger prompt...")
action(2, hold_ms=50, wait_ms=400)
action(5, hold_ms=50, wait_ms=2000)
save_frame("/tmp/s4_00_prompt.png")

# Try 10 rapid A presses
for i in range(10):
    action(5, hold_ms=100, wait_ms=300)  # longer hold
    save_frame(f"/tmp/s4_{i+1:02d}.png")
    print(f"  A press #{i+1}")

s = status()
print(f"\nFinal: pos={s['pos']} map={s['map']}")
print("Done!")
