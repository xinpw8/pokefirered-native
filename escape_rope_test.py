import requests
import time
import base64
import sys

BASE = "http://localhost:53581"

def action(n, hold_ms=35, wait_ms=500):
    """Send action with calibrated timing"""
    requests.get(f"{BASE}/action/{n}")
    time.sleep(hold_ms / 1000.0)
    requests.get(f"{BASE}/action/0")  # release
    time.sleep(wait_ms / 1000.0)

def status():
    return requests.get(f"{BASE}/status").json()

def save_frame(path):
    data = requests.get(f"{BASE}/frame_b64").text
    with open(path, "wb") as f:
        f.write(base64.b64decode(data))

# Actions: 0=noop, 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT, 5=A, 6=B, 7=START

print("=== ESCAPE ROPE DEBUG TEST ===")
print(f"Initial status: {status()}")

# Step 1: Open START menu
print("\n[1] Opening START menu...")
action(7, hold_ms=35, wait_ms=800)
save_frame("/tmp/erope_01_menu.png")

# Step 2: Navigate DOWN to BAG (3rd item)
# Menu: POKEDEX, POKEMON, BAG, PLAYER, SAVE, OPTION, EXIT
# Fresh game should start at POKEDEX, so DOWN 2x to reach BAG
print("[2] DOWN to POKEMON...")
action(2, hold_ms=35, wait_ms=500)
save_frame("/tmp/erope_02_pokemon.png")

print("[3] DOWN to BAG...")
action(2, hold_ms=35, wait_ms=500)
save_frame("/tmp/erope_03_bag.png")

# Step 3: Press A to enter BAG
print("[4] A to enter BAG...")
action(5, hold_ms=35, wait_ms=1000)
save_frame("/tmp/erope_04_bagscreen.png")

# Step 4: In the bag, should be on Items pocket with ESCAPE ROPE as first item
# Press A on ESCAPE ROPE
print("[5] A to select ESCAPE ROPE...")
action(5, hold_ms=35, wait_ms=800)
save_frame("/tmp/erope_05_selected.png")

# Step 5: USE submenu should appear - press A on USE
print("[6] A to USE...")
action(5, hold_ms=35, wait_ms=800)
save_frame("/tmp/erope_06_use.png")

# Step 6: Text should appear "AAAAAAA used the ESCAPE ROPE."
# Wait for it, then press A to dismiss
print("[7] Waiting for text + A to dismiss...")
time.sleep(1.0)
save_frame("/tmp/erope_07_text.png")
action(5, hold_ms=35, wait_ms=500)
save_frame("/tmp/erope_08_aftertext.png")

# Step 7: Now the escape rope effect should start
# Wait and capture frames to see what happens
print("[8] Monitoring escape rope effect...")
for i in range(10):
    time.sleep(1.0)
    s = status()
    save_frame(f"/tmp/erope_09_{i:02d}.png")
    print(f"  t+{i+1}s: step={s['step']} pos={s['pos']} map={s['map']}")

print("\nDone! Check /tmp/erope_*.png and play15.log for debug output")
