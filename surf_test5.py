import requests
import time
import base64

BASE = "http://localhost:53581"

def action(n, hold_ms=35, wait_ms=500):
    requests.get(f"{BASE}/action/{n}")
    time.sleep(hold_ms / 1000.0)
    requests.get(f"{BASE}/action/0")
    time.sleep(wait_ms / 1000.0)

def status():
    return requests.get(f"{BASE}/status").json()

def save_frame(path):
    data = requests.get(f"{BASE}/frame_b64").text
    with open(path, "wb") as f:
        f.write(base64.b64decode(data))

print("=== SURF TEST v5 - Patient approach ===")

# Step 1: Face water and press A
print("[1] Face DOWN + A...")
action(2, hold_ms=50, wait_ms=400)
action(5, hold_ms=50, wait_ms=500)

# Step 2: Wait LONG for text to fully scroll and YES/NO to appear
print("[2] Waiting 5 seconds for text to fully appear...")
time.sleep(5.0)
save_frame("/tmp/s5_01_waited.png")

# Step 3: Single A press to select YES
print("[3] Single A to select YES...")
action(5, hold_ms=50, wait_ms=3000)  # very long wait after
save_frame("/tmp/s5_02_after_yes.png")

# Step 4: If "used SURF!" text appeared, press A to dismiss
print("[4] A to dismiss any text...")
action(5, hold_ms=50, wait_ms=3000)
save_frame("/tmp/s5_03_after_dismiss.png")

# Monitor
print("[5] Monitoring...")
for i in range(10):
    time.sleep(1.0)
    s = status()
    save_frame(f"/tmp/s5_04_{i:02d}.png")
    print(f"  t+{i+1}s: pos={s['pos']} map={s['map']}")

print("\nDone!")
