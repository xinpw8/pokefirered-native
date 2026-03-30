import requests, time, base64, sys

BASE = "http://localhost:53581"

def action(n, hold_ms=50, wait_ms=500):
    try:
        requests.get(f"{BASE}/action/{n}", timeout=5)
        time.sleep(hold_ms / 1000.0)
        requests.get(f"{BASE}/action/0}", timeout=5)
        time.sleep(wait_ms / 1000.0)
    except Exception as e:
        print(f"HTTP error: {e}")
        sys.exit(1)

def status():
    return requests.get(f"{BASE}/status", timeout=5).json()

def save_frame(path):
    data = requests.get(f"{BASE}/frame_b64", timeout=5).text
    with open(path, "wb") as f:
        f.write(base64.b64decode(data))

print("=== SURF IN CAVE TEST ===")
s = status()
print(f"Start: pos={s['pos']} map={s['map']}")
save_frame("/tmp/sc_00.png")

# Player at (7,14) in Cerulean Cave. Water is to the LEFT.
# Walk LEFT to get adjacent to water
print("[1] Walking LEFT to water edge...")
for _ in range(4): action(3, 50, 200)  # LEFT x4
time.sleep(0.3)
save_frame("/tmp/sc_01.png")
s = status()
print(f"After walk: pos={s['pos']}")

# Try facing LEFT (toward water) and pressing A
print("[2] Face LEFT + A...")
action(3, 50, 300)  # LEFT to face water
action(5, 50, 4000)  # A - interact
save_frame("/tmp/sc_02.png")

# Check if surf prompt appeared
# If not, try DOWN (might need to be at a different edge)
print("[3] Try more positions...")
# Walk DOWN to find water edge
for _ in range(3): action(2, 50, 200)  # DOWN x3
time.sleep(0.3)
save_frame("/tmp/sc_03.png")
s = status()
print(f"After down: pos={s['pos']}")

# Face LEFT toward water and press A
action(3, 50, 300)
action(5, 50, 4000)
save_frame("/tmp/sc_04.png")

# Try DOWN into water
action(2, 50, 300)
action(5, 50, 4000)
save_frame("/tmp/sc_05.png")

s = status()
print(f"Final: pos={s['pos']} map={s['map']}")
print("Done - check frames!")
