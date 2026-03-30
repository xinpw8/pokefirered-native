import requests, time, base64

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

print("=== WALK SOUTH TO WATER ===")
s = status()
print("Start: pos=%s" % s['pos'])

# Walk DOWN as far as possible to reach south water edge
print("[1] Walking DOWN...")
for i in range(10):
    action(2, 50, 200)
    s = status()
    print("  step %d: pos=%s" % (i+1, s['pos']))

save_frame("/tmp/ss_01.png")

# Try A facing down
print("[2] A facing DOWN...")
action(5, 50, 4000)
save_frame("/tmp/ss_02.png")

# Try LEFT then A
print("[3] Walk LEFT, face DOWN, A...")
for _ in range(3): action(3, 50, 200)
s = status()
print("pos=%s" % s['pos'])
action(2, 50, 300)
action(5, 50, 4000)
save_frame("/tmp/ss_03.png")

# Try more LEFT
print("[4] More LEFT...")
for _ in range(3): action(3, 50, 200)
s = status()
print("pos=%s" % s['pos'])
save_frame("/tmp/ss_04.png")
action(2, 50, 300)
action(5, 50, 4000)
save_frame("/tmp/ss_05.png")

# Also try face LEFT + A
action(3, 50, 300)
action(5, 50, 4000)
save_frame("/tmp/ss_06.png")

print("Done!")
s = status()
print("Final: pos=%s map=%s" % (s['pos'], s['map']))
