#!/usr/bin/env python3
"""gen_flow_scripts.py - writes sim/scripts/flow-*.json: one script per UI flow (docs/ui/flows/flow-*-new.json)
that walks the flow's transitions and asserts the screen code after each step. Run it after changing a walk:

    python3 tools/gen_flow_scripts.py

Step tokens (space separated):
  O C L R P          tap OK / Cancel / < / > / Power (80 ms press, 150 ms per step)
  hO hC ...          hold 700 ms;  hC:3200  hold for a given time
  w500               wait 500 ms
  =S2                assert the top screen's code is S2
  ev:nfc_read_done   assert a system event fired (among the last 16)
  st:wifi.state=connected     assert a state field (value parsed as JSON when it can be)
  file:sd:/ir/TV.ir  / nofile:...  / has:path=text   storage checks
  t:TEXT             type TEXT on a text carousel (file-name charset) and pick the done slot
  tp:TEXT            same with the password charset (OK hold switches case for lower-case letters)
  d:123456           enter digits on a digit carousel starting at 0;  dk:... continues from the last digit
  card / ntag / nocard        place a MIFARE Classic 1K / NTAG215 card, or take it away
  {json}             any command object (no spaces inside), e.g. {"type":"ble_host","name":"Laptop"}
"""
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "sim", "scripts")
# SHOTS=<folder>: also write a PNG of every screen the first time a walk reaches it (scripts go to <folder> too)
SHOTS = os.environ.get("SHOTS", "")
if SHOTS:
    OUT = SHOTS
FILE = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-."
PASS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.!@#$%^&*()"
BTN = {"O": "OK", "C": "CANCEL", "L": "LEFT", "R": "RIGHT", "P": "POWER"}
CLASSIC = {"type": "nfc_card", "uid": "04A23B1C", "card_type": "MIFARE Classic 1K",
           "blocks": ["%02X" % i + "00" * 15 for i in range(64)]}
NTAG = {"type": "nfc_card", "uid": "04A2245A6B1C80", "card_type": "NTAG215"}
UNLOCKED_AT = 5300
# frame hashes locked after a reviewed run (sim shot of that moment looked right)
HASHES = {"HASH_SETTINGS": "591d81816f1b8755", "HASH_SPLASH": "fcab818f92fa02f4"}  # boot status + logo + fade: the lock screen is up by then


class Script:
    def __init__(self, t0=UNLOCKED_AT):
        self.t, self.ev, self.digit = t0, [], 0
        self.shot = set()

    def add(self, e):
        e["t_ms"] = self.t
        self.ev.append(e)

    def tap(self, b):
        self.add({"type": "button", "button": b, "action": "press"})
        self.t += 150

    def run(self, steps):
        for s in steps.split():
            if s in BTN:
                self.tap(BTN[s])
            elif s[0] == "h" and s[1:].split(":")[0] in BTN:
                b, _, d = s[1:].partition(":")
                d = int(d or 700)
                self.add({"type": "button", "button": BTN[b], "action": "hold", "duration_ms": d})
                self.t += d + 150
            elif s[0] == "w":
                self.t += int(s[1:])
            elif s[0] == "=":
                self.add({"type": "assert", "check": "screen_equals", "value": s[1:]})
                if SHOTS and s[1:] not in self.shot:  # first time on this screen: a picture for the gallery
                    self.shot.add(s[1:])
                    self.add({"type": "dump", "path": os.path.join(SHOTS, s[1:] + ".png")})
            elif s.startswith("ev:"):
                self.add({"type": "assert", "check": "event_fired", "value": s[3:]})
            elif s.startswith("st:"):
                f, _, v = s[3:].partition("=")
                try:
                    v = json.loads(v)
                except ValueError:
                    pass
                self.add({"type": "assert", "check": "state_equals", "field": f, "value": v})
            elif s.startswith("file:"):
                self.add({"type": "assert", "check": "storage_file_exists", "path": s[5:]})
            elif s.startswith("nofile:"):
                self.add({"type": "assert", "check": "storage_file_missing", "path": s[7:]})
            elif s.startswith("has:"):
                p, _, txt = s[4:].partition("=")
                self.add({"type": "assert", "check": "storage_file_contains", "path": p, "text": txt})
            elif s.startswith("t:") or s.startswith("tp:"):
                cs = FILE if s.startswith("t:") else PASS
                self.typ(s.split(":", 1)[1], cs)
            elif s.startswith("d:") or s.startswith("dk:"):
                if s.startswith("d:"):
                    self.digit = 0
                for ch in s.split(":", 1)[1]:
                    self.move(int(ch) - self.digit, 10)
                    self.digit = int(ch)
                    self.tap("OK")
            elif s == "card":
                self.add(dict(CLASSIC)); self.t += 50
            elif s == "ntag":
                self.add(dict(NTAG)); self.t += 50
            elif s == "nocard":
                self.add({"type": "nfc_card", "action": "remove"}); self.t += 50
            elif s.startswith("HASH_"):
                self.add({"type": "assert", "check": "framebuffer_hash_equals", "value": HASHES.get(s, "unset")})
            elif s[0] == "{":
                self.add(json.loads(s)); self.t += 50
            else:
                raise SystemExit("unknown step " + s)

    def move(self, d, n):
        d %= n
        for _ in range(d if d <= n - d else n - d):
            self.tap("RIGHT" if d <= n - d else "LEFT")

    def typ(self, text, cs):
        n, pos, lower = len(cs) + 2, 0, False
        for ch in text:
            want_lower = ch.islower()
            if want_lower != lower:
                self.add({"type": "button", "button": "OK", "action": "hold", "duration_ms": 700})
                self.t += 850
                lower = want_lower
            tgt = cs.index(ch.upper())
            self.move(tgt - pos, n)
            pos = tgt
            self.tap("OK")
        self.move(n - 1 - pos, n)  # the done slot
        self.tap("OK")


def write(name, desc, steps, init=None, t0=UNLOCKED_AT):
    s = Script(t0)
    s.run(steps)
    st = {"battery_percent": 100, "rtc": "2026-10-03T12:34:00"}
    st.update(init or {})
    doc = {"name": name, "about": desc, "initial_state": st, "events": s.ev}
    with open(os.path.join(OUT, name + ".json"), "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


DEMO = {"storage_seed": "seeds/demo"}
TO_MODULE = {"ir": "O C O", "nfc": "O C R O", "games": "O C R R O", "wifi": "O C R R R O",
             "bt": "O C R R R R O", "settings": "O C R R R R R O"}

write("flow-main", "Main-new: boot, lock screens, PIN, lockout across a reboot, home, launcher, emergency menu",
      "w5300 =M3a ev:boot_done O w200 =M5 C w300 =M6a R R R R R =M6b C w300 =M5 C w300 =M6b R O =I0 C =M6a C w300 =M5 "
      # set a PIN through Settings, then lock with Power and unlock
      "C w300 =M6a L O =T0 R R R O =K1a O d:123456 =K3b d:123456 =K1b C C C w300 P w300 O w100 =M3b "
      "O =M4a d:111111 =M4b w1600 =M4a d:222222 =M4b w1600 d:333333 =M4c st:screen=\"M4c\" "
      # the lockout survives a power cycle
      '{"type":"restart","cold_boot":true} w4800 w600 =M3b O =M4c w30000 ev:lockout_over =M3b O d:123456 w300 =M5 '
      # emergency menu: hold Power on the boot status page, wrong code, then the right one removes the PIN
      '{"type":"restart","cold_boot":true} w200 =M1 hP:900 =M7a O =M7b d:12345678 =M7b hC =M7a O d:40917263 w100 =M2 '
      "w3600 =M3a O w200 =M5", t0=0)

write("flow-settings", "Settings-new: every page, value editing, PIN set/change/remove, lock message, date & time, reset",
      TO_MODULE["settings"] + " =T0 O =T1 O =T1e R R O =T1 st:backlight=255 has:flash:/settings.ini=brightness=100 "
      "O L C =T1 has:flash:/settings.ini=brightness=100 R O =T1e C =T1 C =T0 "
      "R O =T2 O =T2p R R R O =T2 has:flash:/settings.ini=sleep_mode=Never C =T0 R O =T3 O has:flash:/settings.ini=button_sound=0 "
      "O R R O =T3 L O C =T0 "
      "R O =K1a O =K3a d:123456 =K3b d:654321 =K3c O =K3a d:123456 =K3b d:123456 =K1b "
      "O =Kold d:123456 =K3a hC =K1b R O =Krold d:123456 w200 =K5 C =K1b O d:123456 w200 =K5 O =K1a "
      "O =K6 hC =K1a O =K6 t:HI =K1a has:flash:/settings.ini=lock_message=HI C =T0 "
      "R O =D0 O =D0e R O R O =D0 R R O =D0 has:flash:/settings.ini=clock_24h=0 C =T0 "
      "R O =Y1 O =Y2 C R R O =Y6 C L O =Y3 O =Y4 hC =Y3 R R O =Y4 d:40917263 w200 =Y5 C =Y3 O d:40917263 w200 =Y5 O w100 "
      "=M1 nofile:flash:/settings.ini", init=DEMO)

write("flow-wifi", "WiFi-new: scan, open and secured networks, wrong password, connect + NTP, saved networks, forget, disconnect",
      TO_MODULE["wifi"] + " =F0 O =C1 st:wifi.state=scanning w900 ev:wifi_scan_done =C2 R R R O =C1 w900 =C2 "
      '{"type":"wifi","next_connect_succeeds":false} O =C4 tp:Hello12 =C5 w900 ev:wifi_connect_failed =C6b O =C4 hC =C2 '
      '{"type":"wifi","next_connect_succeeds":true} O =C4 tp:Hello12 =C5 w900 ev:wifi_connected =C6a st:wifi.password="Hello12" '
      "has:flash:/wifi.ini=HomeNet w1600 =F0 st:wifi.state=connected "
      "R O =N1 O =C5 w900 =C6a w1600 =F0 O hC =N1d O =N1 C =F0 R O =F0 st:wifi.state=off "
      "L O w900 =C2 R O =C5 w900 =C6a w1600 C w300 =M6a st:wifi.state=connected")

write("flow-bluetooth", "Bluetooth-new: advertise, known and new hosts, pair request, remote pages, not connected, paired devices, forget",
      TO_MODULE["bt"] + ' =B-M0 st:ble.state=advertising {"type":"ble_host","name":"Laptop"} w100 ev:ble_connected =B-M1 '
      '{"type":"ble_host","action":"disconnect"} w100 ev:ble_disconnected =B-M0 {"type":"ble_host","name":"New_Phone"} w100 '
      "ev:ble_pair_request =B-P C =B-M0 st:ble.state=advertising "
      '{"type":"ble_host","name":"New_Phone"} w100 =B-P O w100 =B-M1 st:ble.host="New_Phone" O =B0 O =B1 O hO:800 '
      'st:ble.keys_sent=4 {"type":"ble_host","action":"disconnect"} w100 =B1x O st:ble.keys_sent=4 '
      '{"type":"ble_host","name":"New_Phone"} w100 =B1 C =B0 R O =B1p O st:ble.keys_sent=5 C R O =B1k C C =B-M1 '
      "R O =P1 O =P2 C =P1 hC =P1d O =P1 st:ble.state=advertising C =B-M0 C w300 =M6a st:ble.state=off")

write("flow-ir", "IR-new: send (categories, signals, toast, rename/delete), learn (decoded, RAW, timeout, new category)",
      TO_MODULE["ir"] + " =I0 O =S1 O =S2 O =S2t st:ir.sent_count=1 w1100 =S2 hC =S3 C =S2 hC =S3 O =S2 C =S1 "
      "hC w200 =S1a O =S1b C C C C C C t:TV =S1b hC =S1 hC O =S1b C C C C C C t:AC =S1 file:sd:/ir/AC.ir R R hC R O =S1c O =S1 nofile:sd:/ir/TV.ir "
      "C =I0 R O =L1 st:ir.listening=true "
      '{"type":"ir_signal","protocol":"NEC","address":4,"command":8} w100 ev:ir_received =L2a st:ir.listening=false O =L3 '
      "t:VOL_UP =L4 R R O =L4n t:AUDIO w100 =L5 file:sd:/ir/AUDIO.ir has:sd:/ir/AUDIO.ir=VOL_UP w1300 =L1 "
      '{"type":"ir_signal","protocol":"RAW","raw":[9000,4500,560,560,560,1690]} w100 =L2b C =L1 w15100 ev:ir_learn_timeout =L1b '
      "O =L1 C =I0 st:ir.listening=false C w300 =M6a "
      # deep sleep (Power) and wake with OK: back in the module that was open
      'O =I0 P w100 st:power.state="deep_sleep" O w300 st:power.state="awake" =I0 O =S1', init=DEMO)

write("flow-ir-empty", "IR-new: S1-0 and S2-0 empty states",
      TO_MODULE["ir"] + " O =S1-0 C R O {\"type\":\"ir_signal\",\"protocol\":\"NEC\",\"address\":1,\"command\":2} w100 O t:X "
      "R O t:EMPTY w100 =L5 w1300 C L O =S1 O =S2 hC O =S2-0 C =S1")

write("flow-nfc", "NFC-new: read (card found/lost/done), save, write (match and mismatch), emulate, saved dumps",
      TO_MODULE["nfc"] + " =N0 O =R1 st:nfc.polling=true card w50 ev:nfc_card_found =R2 nocard w100 ev:nfc_card_lost =R1 "
      "card w50 =R2 w1100 ev:nfc_read_done =R3 C =R1 nocard card w1200 =R3 O =R4 t:DESK w100 =R5 file:sd:/nfc/DESK.nfc "
      "nocard w1300 =R1 C =N0 st:nfc.polling=false "
      "R O =W1 R R O w200 =W2 C =W1 O w200 =W2 O =W3 ntag w100 =W5b O =W1 O w200 O =W3 nocard card w100 =W4 w800 "
      "ev:nfc_write_done =W5a O =W1 C =N0 "
      "R O =E1 O w200 =E2 st:nfc.emulating=true {\"type\":\"nfc_reader\"} st:nfc.reader_taps=1 C =E1 st:nfc.emulating=false "
      "R R R O w200 =E1p O =E1 C =N0 R O =D1 O =D2 C hC w200 =D1d O =D1 C =N0 C w300 =M6a "
      '{"type":"nfc_module","present":false} O st:screen="N0"', init=DEMO)

write("flow-games", "Games-new: built-in games at each bypass level, pause menu, failsafe, SD packs (loaded, failed, menu_flow)",
      TO_MODULE["games"] + " =G1 O =G3 hC w200 =G4 O =G3 hC R O =G3 hC R R O =G1 O hC:3200 w-150 =G5 ev:app_failsafe w700 =G1 "
      "R O =G3 hC =G3 C =G1 R O =G3 w3500 O w100 C =G1 "
      "O hC:3200 w700 =G1 R O =G2 w700 ev:pack_failed =G2f O =G1 R O w700 =G3 C =G1 R O w700 ev:pack_loaded =G3 "
      "hC:3200 w700 =G1 C w300 =M6a "
      '{"type":"sd","present":false} O =G1', init=DEMO)

write("display-static", "a still screen is not pushed again; a clock tick pushes only the status bar",
      "O w200 =M5 w1500 w0 " + '{"type":"assert","check":"display_static_ms","value":1000}', t0=UNLOCKED_AT)
print("wrote", len([f for f in os.listdir(OUT) if f.startswith("flow-")]), "flow scripts")

# ---- feature scripts (the older script set, rewritten for the colour UI) ----
write("boot-to-settings", "cold boot to the Settings menu; the frame hash locks the colour rendering",
      "w5300 =M3a O w200 =M5 C w300 R R R R R O =T0 st:menu_path=\"Home/Menu/Settings\" HASH_SETTINGS C =M6b", t0=0)

write("boot-buttons", "the boot status page ignores buttons; only Power hold (or Cancel held + OK) opens the emergency menu",
      "w100 O R L C =M1 w1500 =M2 w3700 =M3a "
      '{"type":"restart","cold_boot":true} w100 hC:300 =M1 {"type":"button","button":"CANCEL","action":"down"} w100 O '
      '{"type":"button","button":"CANCEL","action":"up"} w100 =M7a C w100 =M2', t0=0)

write("hardware-faults", "no battery reading, no RTC, no SD card, failing flash writes: the UI keeps going",
      '{"type":"battery","percent":"unknown"} {"type":"rtc","missing":true} {"type":"sd","present":false} '
      "w1100 st:battery.firmware_percent=-1 st:rtc=null O w200 =M5 C w300 R R R R R O R R R R R O =Y1 O =Y2 C "
      'C =T0 {"type":"storage","fail_writes":true} R O =T1 O R O =T1 nofile:flash:/settings.ini C C w300 L L L O =G1 C', t0=UNLOCKED_AT)

write("ir-learn-then-send", "learn a NEC code into a new category, then send it from the Send list",
      TO_MODULE["ir"] + ' R O {"type":"ir_signal","protocol":"NEC","address":7,"command":2} w100 =L2a O t:POWER R O t:TV w100 '
      "=L5 has:sd:/ir/TV.ir=POWER w1300 =L1 C L O =S1 O =S2 O =S2t st:ir.sent_count=1")

write("nfc-read-save", "read a card with no SD card (save fails), insert the card, save again",
      TO_MODULE["nfc"] + ' {"type":"sd","present":false} O card w1200 =R3 O t:KEY =R4 nofile:sd:/nfc/KEY.nfc '
      '{"type":"sd","present":true} O '
      "w100 =R5 has:sd:/nfc/KEY.nfc=uid=04A23B1C")

write("wifi-connect", "connect to a secured network and get the time by NTP",
      '{"type":"rtc","time":"2026-01-01T00:00:00"} ' + TO_MODULE["wifi"] + " O w900 O tp:pass w900 =C6a st:wifi.ssid=\"HomeNet\" "
      "st:wifi.password=\"pass\" w1600 =F0")

write("ble-remote", "pair a phone and send media keys; leaving the menu keeps the connection",
      TO_MODULE["bt"] + ' {"type":"ble_host","name":"Phone"} w100 =B-P O w100 =B-M1 O O O =B1 st:ble.keys_sent=1 '
      "C C C w300 =M6a st:ble.state=connected st:ble.host=\"Phone\"")

write("theme-import", "import a 1-bit (format 1) and a colour (format 2) theme pack, pick the colour one, it survives a reboot",
      '{"type":"import","file":"fixtures/night-theme.zip"} {"type":"import","file":"fixtures/sunset-theme.zip"} '
      "file:sd:/system/theme/sunset/splash.c16 " + TO_MODULE["settings"] + " L O R R R O =Y7 R R O st:theme=\"sunset\" "
      'has:flash:/settings.ini=theme=sunset {"type":"restart","cold_boot":true} w2500 =M2 st:theme="sunset" HASH_SPLASH', init={})
write("system-events", "the flows' shared event catalog: SD out/in, battery low/critical, WiFi lost",
      'O w200 {"type":"sd","present":false} w50 ev:sd_removed {"type":"sd","present":true} w50 ev:sd_inserted '
      '{"type":"battery","percent":12} w9000 ev:battery_low '
      + TO_MODULE["wifi"][2:] + ' O w900 O tp:pw w900 w1600 st:wifi.state=connected {"type":"wifi","drop":true} w50 ev:wifi_lost '
      '{"type":"battery","percent":3} w9000 ev:battery_critical st:power.state="off"')
print("wrote feature scripts")
