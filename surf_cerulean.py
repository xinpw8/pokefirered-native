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

print("=== ESCAPE ROPE + SURF TEST ===")

# Use escape rope to get to Cerulean City
print("[1] Opening menu...")
action(7, 50, 1500)  # START
action(2, 50, 600)   # DOWN
action(2, 50, 600)   # DOWN - cursor on BAG
action(5, 50, 1500)  # A - enter BAG
action(5, 50, 800)   # A - select ESCAPE ROPE
action(5, 50, 800)   # A - USE
time.sleep(2)
action(5, 50, 500)   # A dismiss text
action(5, 50, 500)   # A 
action(5, 50, 500)   # A

print("[2] Waiting for warp...")
for i in range(10):
    time.sleep(1)
    s = status()
    print(f"  t+{i+1}: pos={s['pos']} map={s['map']}")
    if s['map'] != '1.74':
        print("  WARPED!")
        break

time.sleep(2)
s = status()
print(f"\n[3] Now at: pos={s['pos']} map={s['map']}")
save_frame("/tmp/sc_01_cerulean.png")

# Navigate to water (south of Cerulean City)
print("[4] Walking to water...")
for _ in range(6): action(2, 50, 200)  # DOWN x6
for _ in range(3): action(3, 50, 200)  # LEFT x3
for _ in range(4): action(2, 50, 200)  # DOWN x4
time.sleep(0.5)
s = status()
print(f"After walk: pos={s['pos']} map={s['map']}")
save_frame("/tmp/sc_02_atwater.png")

# Face DOWN toward water
action(2, 50, 400)

# A to interact - trigger surf prompt
print("[5] A to interact with water...")
action(5, 50, 4000)  # very long wait for text
save_frame("/tmp/sc_03_prompt.png")

# A to select YES (default)
print("[6] A to select YES...")  
action(5, 50, 3000)
save_frame("/tmp/sc_04_yes.png")

# More A for any remaining text
print("[7] More A presses...")
action(5, 50, 2000)
save_frame("/tmp/sc_05_a2.png")
action(5, 50, 2000)
save_frame("/tmp/sc_06_a3.png")

# Monitor
print("[8] Monitoring...")
for i in range(8):
    time.sleep(1)
    s = status()
    print(f"  t+{i+1}: pos={s['pos']} map={s['map']}")

save_frame("/tmp/sc_07_final.png")
print("Done!")
