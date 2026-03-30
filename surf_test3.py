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

print("=== SURF TEST v3 - Careful YES selection ===")
s = status()
print(f"Status: pos={s['pos']} map={s['map']}")

# Face DOWN toward water
print("[1] Face DOWN...")
action(2, hold_ms=50, wait_ms=400)

# Press A on water
print("[2] A on water (trigger prompt)...")
action(5, hold_ms=50, wait_ms=2000)  # long wait for prompt to fully appear
save_frame("/tmp/s3_01_prompt.png")

# Make sure UP is pressed to select YES
print("[3] UP to ensure YES is selected...")
action(1, hold_ms=50, wait_ms=600)
save_frame("/tmp/s3_02_yes.png")

# Press A on YES
print("[4] A to confirm YES...")
action(5, hold_ms=50, wait_ms=1500)
save_frame("/tmp/s3_03_after_yes.png")

# Press A again to dismiss any text
print("[5] A to dismiss text if any...")
action(5, hold_ms=50, wait_ms=1500)
save_frame("/tmp/s3_04_after_a.png")

# Press A again
print("[6] A again...")
action(5, hold_ms=50, wait_ms=1500)
save_frame("/tmp/s3_05_after_a2.png")

# Monitor
print("[7] Monitoring...")
for i in range(10):
    time.sleep(1.0)
    s = status()
    save_frame(f"/tmp/s3_06_{i:02d}.png")
    print(f"  t+{i+1}s: pos={s['pos']} map={s['map']}")

print("\nDone!")
