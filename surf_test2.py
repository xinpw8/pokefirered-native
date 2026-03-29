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

print("=== SURF TEST v2 ===")
s = status()
print(f"Status: pos={s['pos']} map={s['map']}")

# Player is near water. Face DOWN toward water, then press A.
print("\n[1] Face DOWN...")
action(2, hold_ms=50, wait_ms=300)
save_frame("/tmp/s2_01.png")

print("[2] Press A on water...")
action(5, hold_ms=50, wait_ms=2000)
save_frame("/tmp/s2_02_prompt.png")

# YES should be highlighted. Press A.
print("[3] A on YES...")
action(5, hold_ms=50, wait_ms=500)
save_frame("/tmp/s2_03_yes.png")

# Monitor rapidly for 30 seconds
print("[4] Monitoring...")
for i in range(30):
    time.sleep(0.5)
    s = status()
    if i % 4 == 0:
        save_frame(f"/tmp/s2_04_{i:02d}.png")
    print(f"  t+{(i+1)*0.5:.1f}s: step={s['step']} pos={s['pos']} map={s['map']}")

print("\nDone!")
