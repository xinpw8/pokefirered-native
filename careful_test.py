import requests, time, base64

BASE = "http://localhost:53581"

def act(n, hold=50, wait=500):
    requests.get(BASE + "/action/" + str(n), timeout=5)
    time.sleep(hold / 1000.0)
    requests.get(BASE + "/action/0", timeout=5)
    time.sleep(wait / 1000.0)

def st():
    return requests.get(BASE + "/status", timeout=5).json()

def frame(path):
    d = requests.get(BASE + "/frame_b64", timeout=5).text
    with open(path, "wb") as f:
        f.write(base64.b64decode(d))

# PHASE 1: Open menu, visually find BAG
print("=== PHASE 1: MENU ===")
act(7, 50, 1500)  # START
frame("/tmp/ct_menu.png")

# Navigate: press DOWN until BAG is reached
# Menu: POKEDEX(0), POKEMON(1), BAG(2), AAAAAAA(3), SAVE(4), OPTION(5), EXIT(6)
# First go UP 6x to ensure we're at POKEDEX
print("Going UP to POKEDEX...")
for _ in range(6): act(1, 50, 300)
frame("/tmp/ct_at_top.png")

print("DOWN to BAG (2 presses)...")
act(2, 50, 500)
act(2, 50, 500)
frame("/tmp/ct_at_bag.png")

print("A to enter BAG...")
act(5, 50, 1500)
frame("/tmp/ct_bag_screen.png")

print("A to select ESCAPE ROPE...")
act(5, 50, 800)
frame("/tmp/ct_item_sel.png")

print("A to USE...")
act(5, 50, 1000)
frame("/tmp/ct_use.png")

# Wait for text
time.sleep(2)
frame("/tmp/ct_text.png")

# Dismiss text
print("A to dismiss...")
act(5, 50, 500)
act(5, 50, 500)
act(5, 50, 500)

# Wait for warp
print("Waiting for warp...")
for i in range(15):
    time.sleep(1)
    s = st()
    m = s['map']
    if m != '1.74':
        print("WARPED! map=%s pos=%s" % (m, s['pos']))
        break
    print("  t+%d map=%s" % (i+1, m))

time.sleep(2)
s = st()
print("After warp: pos=%s map=%s" % (s['pos'], s['map']))
frame("/tmp/ct_warped.png")

if s['map'] == '1.74':
    print("ESCAPE ROPE FAILED")
    exit(1)

# PHASE 2: Walk to water in Cerulean City
print("\n=== PHASE 2: TO WATER ===")
for _ in range(8): act(2, 50, 200)  # DOWN
for _ in range(4): act(3, 50, 200)  # LEFT
for _ in range(4): act(2, 50, 200)  # DOWN
time.sleep(0.5)
s = st()
print("At: pos=%s" % s['pos'])
frame("/tmp/ct_water_pos.png")

# Face DOWN, A
act(2, 50, 400)
frame("/tmp/ct_face_water.png")
print("A on water...")
act(5, 50, 4000)
frame("/tmp/ct_surf_prompt.png")

# A to select YES
print("A for YES...")
act(5, 50, 3000)
frame("/tmp/ct_after_yes.png")

# More A
act(5, 50, 2000)
frame("/tmp/ct_after_a2.png")

# Monitor
print("Monitoring...")
for i in range(8):
    time.sleep(1)
    s = st()
    print("  t+%d: pos=%s map=%s" % (i+1, s['pos'], s['map']))

print("DONE")
