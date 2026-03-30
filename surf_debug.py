import requests, time, base64

BASE = "http://localhost:53581"

def action(n, hold_ms=50, wait_ms=500):
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

print("=== SURF DEBUG TEST ===")
s = status()
print(f"Start: pos={s['pos']} map={s['map']}")

# Walk to water (from Cerulean Cave spawn at 7,14)
# Walk DOWN to face water on the left side
print("[1] Walking to water...")
for _ in range(3): action(2, 50, 200)  # DOWN x3
for _ in range(5): action(3, 50, 200)  # LEFT x5
for _ in range(2): action(2, 50, 200)  # DOWN x2
time.sleep(0.5)
save_frame("/tmp/sd_01_pos.png")
s = status()
print(f"At water: pos={s['pos']} map={s['map']}")

# Face DOWN (toward water)
action(2, 50, 400)

# A to interact with water
print("[2] A to interact with water...")
action(5, 50, 3000)  # long wait
save_frame("/tmp/sd_02_prompt.png")

# A to select YES
print("[3] A to select YES...")
action(5, 50, 3000)
save_frame("/tmp/sd_03_after.png")

# More A presses
for i in range(5):
    action(5, 50, 1000)
    save_frame(f"/tmp/sd_04_{i}.png")
    print(f"  A #{i+2}")

s = status()
print(f"Final: pos={s['pos']} map={s['map']}")
print("Done!")
