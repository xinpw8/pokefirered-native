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

print("=== ESCAPE ROPE TEST v2 ===")
print(f"Status: {status()}")

# Step 1: Open START menu with longer wait for animation
print("\n[1] START menu...")
action(7, hold_ms=35, wait_ms=1500)  # longer wait for menu animation
save_frame("/tmp/er2_01_menu.png")

# Step 2: DOWN x2 with longer waits  
print("[2] DOWN #1...")
action(2, hold_ms=50, wait_ms=600)
save_frame("/tmp/er2_02_down1.png")

print("[3] DOWN #2...")
action(2, hold_ms=50, wait_ms=600)
save_frame("/tmp/er2_03_down2.png")

# Step 3: A to enter BAG
print("[4] A -> BAG...")
action(5, hold_ms=50, wait_ms=1500)  # bag screen loads
save_frame("/tmp/er2_04_bag.png")

# Step 4: A to select first item (ESCAPE ROPE)
print("[5] A -> select item...")
action(5, hold_ms=50, wait_ms=800)
save_frame("/tmp/er2_05_item.png")

# Step 5: A on USE
print("[6] A -> USE...")
action(5, hold_ms=50, wait_ms=1500)
save_frame("/tmp/er2_06_use.png")

# Step 6: Wait for text + A to dismiss
print("[7] Wait + A to dismiss text...")
time.sleep(1.5)
save_frame("/tmp/er2_07_preA.png")
action(5, hold_ms=50, wait_ms=500)
# Press A a couple more times in case there are multiple text boxes
action(5, hold_ms=50, wait_ms=500)
action(5, hold_ms=50, wait_ms=500)
save_frame("/tmp/er2_08_postA.png")

# Step 7: Monitor
print("[8] Monitoring...")
for i in range(15):
    time.sleep(1.0)
    s = status()
    save_frame(f"/tmp/er2_09_{i:02d}.png")
    print(f"  t+{i+1}s: step={s['step']} pos={s['pos']} map={s['map']}")
    if s['map'] != '1.74':
        print("  *** MAP CHANGED! Warp succeeded! ***")
        break

print("\nDone!")
