import requests, time, base64, sys

BASE = "http://localhost:53581"

def action(n, hold_ms=50, wait_ms=500):
    requests.get(BASE + "/action/" + str(n), timeout=5)
    time.sleep(hold_ms / 1000.0)
    requests.get(BASE + "/action/0", timeout=5)
    time.sleep(wait_ms / 1000.0)

def status():
    return requests.get(BASE + "/status", timeout=5).json()

def save_frame(path):
    data = requests.get(BASE + "/frame_b64", timeout=5).text
    with open(path, "wb") as f:
        f.write(base64.b64decode(data))

print("=== SURF IN CAVE ===")
s = status()
print("Start: pos=%s map=%s" % (s['pos'], s['map']))

# Player at (7,14). Walk LEFT toward water.
print("[1] Walking LEFT...")
for _ in range(5): action(3, 50, 200)
time.sleep(0.3)
save_frame("/tmp/sc2_01.png")
s = status()
print("pos=%s" % s['pos'])

# Face LEFT and press A
print("[2] Face LEFT + A...")
action(3, 50, 300)
action(5, 50, 4000)
save_frame("/tmp/sc2_02.png")

# A again
action(5, 50, 3000)
save_frame("/tmp/sc2_03.png")

# Walk DOWN to try different water edge
print("[3] Walk DOWN...")
for _ in range(4): action(2, 50, 200)
save_frame("/tmp/sc2_04.png")
s = status()
print("pos=%s" % s['pos'])

# Face LEFT + A
action(3, 50, 300)
action(5, 50, 4000)
save_frame("/tmp/sc2_05.png")

# A
action(5, 50, 3000)
save_frame("/tmp/sc2_06.png")

print("Done! Check /tmp/sc2_*.png")
s = status()
print("Final: pos=%s map=%s" % (s['pos'], s['map']))
