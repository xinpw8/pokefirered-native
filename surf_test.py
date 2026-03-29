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

print("=== SURF TEST ===")
s = status()
print(f"Status: {s}")
save_frame("/tmp/surf_00_start.png")

# We're in Cerulean City. Let's walk down to the water.
# First, let me get to the water south of Cerulean City.
# Walk DOWN towards water
print("\n[1] Walking DOWN towards water...")
for i in range(8):
    action(2, hold_ms=50, wait_ms=200)  # DOWN
    
save_frame("/tmp/surf_01_walked.png")
s = status()
print(f"After walk: {s}")

# Walk LEFT towards the water
print("[2] Walking LEFT towards water...")
for i in range(4):
    action(3, hold_ms=50, wait_ms=200)  # LEFT

save_frame("/tmp/surf_02_left.png")
s = status()
print(f"After left: {s}")

# Walk DOWN more
print("[3] Walking DOWN more...")
for i in range(4):
    action(2, hold_ms=50, wait_ms=200)

save_frame("/tmp/surf_03_more.png")
s = status()
print(f"After more down: {s}")

# Now face DOWN towards water and press A to trigger surf
print("[4] Face DOWN and press A to surf...")
action(2, hold_ms=50, wait_ms=300)  # face DOWN
action(5, hold_ms=50, wait_ms=1500)  # A
save_frame("/tmp/surf_04_prompt.png")

# Check if surf prompt appeared (YES/NO)
print("[5] Press A on YES...")
action(5, hold_ms=50, wait_ms=1500)
save_frame("/tmp/surf_05_after.png")

s = status()
print(f"After surf attempt: {s}")

# Monitor for a bit
print("[6] Monitoring...")
for i in range(5):
    time.sleep(1.0)
    s = status()
    save_frame(f"/tmp/surf_06_{i:02d}.png")
    print(f"  t+{i+1}s: step={s['step']} pos={s['pos']} map={s['map']}")

print("\nDone!")
