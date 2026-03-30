import requests, time, base64, sys

BASE = "http://localhost:53581"

def action(n, hold_ms=50, wait_ms=500):
    try:
        requests.get(f"{BASE}/action/{n}", timeout=5)
        time.sleep(hold_ms / 1000.0)
        requests.get(f"{BASE}/action/0", timeout=5)
        time.sleep(wait_ms / 1000.0)
    except:
        print("HTTP error - game may have crashed")
        sys.exit(1)

def status():
    return requests.get(f"{BASE}/status", timeout=5).json()

def save_frame(path):
    data = requests.get(f"{BASE}/frame_b64", timeout=5).text
    with open(path, "wb") as f:
        f.write(base64.b64decode(data))

print("=== FULL SURF TEST ===")
s = status()
print(f"Start: pos={s['pos']} map={s['map']}")

# STEP 1: Escape rope to Cerulean City
print("\n--- ESCAPE ROPE ---")
action(7, 50, 1500)   # START
action(2, 50, 600)    # DOWN 1 -> POKEMON
action(2, 50, 600)    # DOWN 2 -> BAG
action(5, 50, 1500)   # A -> enter BAG
save_frame("/tmp/sf_01_bag.png")
action(5, 50, 800)    # A -> select ESCAPE ROPE
save_frame("/tmp/sf_02_item.png")
action(5, 50, 800)    # A -> USE
save_frame("/tmp/sf_03_use.png")

# Wait for text + dismiss
time.sleep(2)
action(5, 50, 500)
action(5, 50, 500)

# Wait for warp
for i in range(15):
    time.sleep(1)
    s = status()
    if s['map'] != '1.74':
        print(f"  Warped to {s['map']} at pos={s['pos']}")
        break
    print(f"  waiting... map={s['map']}")

time.sleep(2)
s = status()
print(f"Now at: pos={s['pos']} map={s['map']}")
save_frame("/tmp/sf_04_cerulean.png")

if s['map'] == '1.74':
    print("ERROR: Escape rope failed!")
    sys.exit(1)

# STEP 2: Walk to water south of Cerulean City
print("\n--- WALKING TO WATER ---")
for _ in range(8): action(2, 50, 200)   # DOWN x8
for _ in range(4): action(3, 50, 200)   # LEFT x4
for _ in range(4): action(2, 50, 200)   # DOWN x4

time.sleep(0.5)
s = status()
print(f"At: pos={s['pos']} map={s['map']}")
save_frame("/tmp/sf_05_atwater.png")

# Face DOWN 
action(2, 50, 400)
save_frame("/tmp/sf_06_facing.png")

# STEP 3: Interact with water
print("\n--- SURF ATTEMPT ---")
print("[A] Interact with water...")
action(5, 50, 4000)   # A with 4s wait for text to fully print
save_frame("/tmp/sf_07_prompt.png")

print("[B] Select YES...")
action(5, 50, 3000)   # A to select YES
save_frame("/tmp/sf_08_after_yes.png")

print("[C] Dismiss remaining text...")
action(5, 50, 2000)
save_frame("/tmp/sf_09_dismiss1.png")
action(5, 50, 2000)
save_frame("/tmp/sf_10_dismiss2.png")

# Monitor
print("\n--- MONITORING ---")
for i in range(10):
    time.sleep(1)
    s = status()
    save_frame(f"/tmp/sf_11_{i:02d}.png")
    print(f"  t+{i+1}: pos={s['pos']} map={s['map']}")

print("\nDone!")
