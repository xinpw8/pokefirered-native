import requests
import time
import base64

BASE = "http://localhost:53581"

def status():
    return requests.get(f"{BASE}/status").json()

def save_frame(path):
    data = requests.get(f"{BASE}/frame_b64").text
    with open(path, "wb") as f:
        f.write(base64.b64decode(data))

print("=== SURF TEST v6 - Long A hold ===")

# Face water
requests.get(f"{BASE}/action/2")  # DOWN
time.sleep(0.2)
requests.get(f"{BASE}/action/0")
time.sleep(0.5)

save_frame("/tmp/s6_00_before.png")

# Hold A for 5 full seconds — should advance through ALL text and select YES
print("[1] Holding A for 5 seconds...")
requests.get(f"{BASE}/action/5")  # A pressed
for i in range(25):
    time.sleep(0.2)
    save_frame(f"/tmp/s6_hold_{i:02d}.png")
    s = status()
    print(f"  t+{(i+1)*0.2:.1f}s: pos={s['pos']} map={s['map']}")

# Release A
requests.get(f"{BASE}/action/0")
time.sleep(0.5)
save_frame("/tmp/s6_released.png")

# Monitor
print("[2] Monitoring...")
for i in range(10):
    time.sleep(1.0)
    s = status()
    save_frame(f"/tmp/s6_mon_{i:02d}.png")
    print(f"  t+{i+1}s: pos={s['pos']} map={s['map']}")

print("\nDone!")
