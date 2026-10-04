# แผนงาน: UI / OS บนตัวเครื่อง DIY Flipper (เฟิร์มแวร์)

## 0. วิธีทำงาน

- งานนี้เขียนใน repo เฟิร์มแวร์ (C++ / ESP32-S3) อย่างเดียว **ห้ามแก้ artifact "Flipper UI Studio"**
- ทำงานบน branch ใหม่ commit เป็นช่วงๆ ตาม phase (ข้อ 6) เพื่อให้ตรวจย้อนได้
- เจอเรื่องที่แผนนี้ไม่ได้ระบุ หรือขัดกันเอง **ให้ตัดสินใจเอง อย่าหยุดถาม** แล้วจดเหตุผลไว้ในรายงาน
- ข้อที่ติดป้าย **[ล็อกแล้ว]** ห้ามเปลี่ยน ข้อที่ติด **[ร่าง]** ปรับได้ถ้ามีเหตุผลทางเทคนิค แต่ต้องรายงาน
- จบงานให้เขียน **รายงานภาษาไทย** ตามหัวข้อ 9

ลำดับความสำคัญเมื่อแหล่งข้อมูลขัดกัน: **แผนนี้ > flow JSON > ภาพหน้าจอ PNG > wireframe**

---

## 1. ไฟล์ต้นทาง (อยู่ใน repo แล้ว)

งานนี้รันใน **Claude Code บน cloud container** ซึ่งเห็นแค่ repo นี้ เปิดลิงก์ claude.ai (Studio, wireframe) ไม่ได้ ผู้ใช้อัปโหลด **`docs/ui/inbox/flipper-ui-inbox.zip`** ไว้ให้ ซึ่ง export ด้วยโค้ด export ของ Studio เอง ให้เริ่มด้วย:

1. แตก zip ที่ **root ของ repo** (path ข้างใน zip ตรงกับ repo แล้ว) จะได้
   - `docs/ui/flows/`: `flow-*.json` (state, transition, trigger, input mode, start/end; `schema` อธิบายรูปแบบอยู่ในไฟล์), `flow-*.md`, `flows-all.json`, `templates.json` (โซนของ template ที่หน้าในผังใช้), `screens/*.png` (ภาพจริงทุกหน้า 1:1 สี RGB565 โปร่งใส = alpha)
   - `firmware/assets/generated/`: `ui_assets.h`, `ui_templates.h`, `templates.json` (template ทั้งหมด) และ header ของฟอนต์ 2 ตัว + ไอคอน/anim 18 ตัว (เปลี่ยนนามสกุลเป็น `.h` แล้ว)
   - `docs/ui/REGISTRY.txt`: บรรทัด `#include` และ entry สำเร็จรูปสำหรับ `PIC_LIBRARY` / `GIF_LIBRARY` / `FONT_LIBRARY`
2. ถ้ามีไฟล์ชื่อเดียวกันอยู่แล้วใน `firmware/assets/generated/` ให้ดู diff ก่อนเขียนทับ (ของใหม่ถือว่าล่าสุด)
3. ลงทะเบียนใน `firmware/assets/assets.cpp` ตาม `REGISTRY.txt` (ถ้าโครงตารางใน repo ต่างไป ให้ปรับตาม repo)
4. ลบ `docs/ui/inbox/` เมื่อจัดเสร็จ

รูปแบบ header: รูป/anim เป็น RGB565 หนึ่ง `uint16_t` ต่อพิกเซล `idx = y*w + x` สีโปร่งใสเป็น key `0xF81F` (`UI_TRANSPARENT`) · ฟอนต์เป็น 1bpp mask (LSB-first, แต่ละ glyph เริ่ม byte ใหม่) ให้เฟิร์มแวร์เลือกสีตอนวาด · ไอคอนรุ่นเก่า (battery, wifi, bluetooth, IR, NFC, Games, WiFi Setup, BT Remote, Settings, arrow1) มีพื้นดำทึบ ไม่ได้โปร่งใส

**[ร่าง]** ในชุดนี้มีเฉพาะไอคอนสีขาว เวอร์ชันสีเทา (รายการที่ไม่ได้เลือก) ให้เฟิร์มแวร์ **เปลี่ยนสีตอนวาด** (พิกเซลขาว → `0x8410` หรือ → เทาเข้มเมื่อจาง) แทนการเก็บ asset ซ้ำ ส่วน StatusBar ไม่ใช่ asset ให้วาดจาก toolkit (ข้อความ + ไอคอนแบต/BT/WiFi)

ถ้า zip ไม่อยู่: ทำ phase ที่ไม่ต้องใช้ไปก่อน (toolkit, input, app host) ใช้ placeholder แทน asset แล้วระบุในรายงานว่าขาดไฟล์อะไร

ผังในชุดนี้มีเฉพาะชุด **`-new`**: Main-new, IR-new, NFC-new, WiFi-new, Bluetooth-new, Games-new, Settings-new

ชื่อ mockup เป็นแบบ `<โมดูล> <รหัส> <ชื่อ>-new` เช่น `IR S2 Send Signals-new` **ให้ใช้รหัส (M1, S2, Y4 …) เป็นชื่อ state ในโค้ด** เช่น `enum class ScreenId { M1_BootStatus, … }` เพื่อให้ไล่เทียบกับผังได้


---

## 2. ข้อตกลงของระบบ (อ้างอิงตอนเขียนทุกหน้า)

### 2.1 จอ สี ฟอนต์
- **[ล็อกแล้ว]** ST7735 128×160 แนวตั้ง RGB565 ใช้โทนเทาเป็นหลัก ยังไม่มีสีเน้น
  - ขาว `0xFFFF` = ตัวที่เลือก/ข้อความหลัก
  - เทา `0x8410` = ไม่ได้เลือก/ข้อความรอง
  - เทาเข้ม (ประมาณ `0x4A49`) = ใช้ไม่ได้/จาง
- **[ล็อกแล้ว]** ข้อความบนเครื่องเป็น **อังกฤษล้วน** ฟอนต์ 6×8 (เนื้อหา) และ 8×8 (โลโก้/ตัวใหญ่)
- **[ร่าง]** นาฬิกา Lock screen ตอนนี้คือ 8×8 ขยาย 3 เท่า ถ้าจะทำฟอนต์ตัวเลขใหญ่แยกก็ทำได้

### 2.2 โครงหน้า (พิกัดจริง ตรงกับ `templates.json`)
| ส่วน | พิกัด |
|---|---|
| Status bar | y0–9 + เส้นขาวหนา 2px ที่ y10–11 · เวลา x1 · ไอคอน BT x75 · WiFi x83 · "100%" x91 · ไอคอนแบต x117 |
| Title bar | y12–21 ข้อความกึ่งกลาง + เส้นที่ y22 |
| เนื้อหา | y23–148 (126px) |
| Bottom bar | เส้นที่ y149 + ข้อความที่ y151 |

- **[ล็อกแล้ว]** ไอคอน BT/WiFi ใน status bar โผล่ **เฉพาะตอนเชื่อมต่ออยู่**
- **[ร่าง]** Bottom bar: Launcher แสดงชื่อรายการที่เลือก · list แคบแสดง `n/N` · หน้าอื่นแสดงคำบอกปุ่ม (`OK=YES  CANCEL=NO`) หรือว่าง

### 2.3 Launcher (การ์ด) [ล็อกแล้ว]
- การ์ด 126×26 ระยะห่าง 29px เริ่ม y25 · ไอคอน 12px ที่ (x8, top+7) · ข้อความที่ (x25, top+9)
- ตัวที่เลือก: กรอบขาว + ลูกศร 16px ท้ายการ์ด · ที่ไม่ได้เลือก: กรอบ/ข้อความ/ไอคอนสีเทา
- เห็น 4 ใบเต็ม + ใบที่ 5 โผล่ครึ่งใบ · scrollbar 2px ที่ x126 · ชื่อยาวใช้ marquee เฉพาะตัวที่เลือก
- รายการ: IR, NFC, Games, WiFi Setup, Bluetooth Remote, Settings

### 2.4 List แคบ (แคตตาล็อกของทุกโมดูล) [ล็อกแล้ว]
- แถวสูง 14px → 9 แถวพอดีใน 126px · เลือก = กรอบขาว · ข้อความเริ่ม x5 (ข้อความอยู่ที่ y+3 ของแถว)
- ไอคอนซ้าย ใส่เฉพาะ list ที่ชนิดผสม (โฟลเดอร์กับ "New category")
- ท้ายแถว [ร่าง]: ✓ = จำไว้/ต่ออยู่/emulate ได้ · กุญแจ = มีรหัส · แท่งสัญญาณ 4 ระดับ · จุด = ต่ออยู่ · ข้อความเทา (NEC, RAW, ON, 80%)
- marquee เฉพาะแถวที่เลือก · list ว่าง = ข้อความสีเทากลางจอ ("No remotes", "No dumps" …)

### 2.5 ปุ่มและการกด [ล็อกแล้ว]
- ปุ่มมี 5 ตัว: OK, Cancel, `<`, `>` และ Power แยก (ไม่มี D-pad)
- ค่าคงที่ (ตรงกับ Studio): `HOLD_MS = 500`, repeat เริ่ม 200ms, เร่งทีละ ×0.8, เร็วสุด 40ms, debounce 20–30ms
- **ทุกหน้า:** Cancel กดสั้น = ย้อนกลับ 1 ชั้น (screen stack push/pop)
- **Home Screen:** รับเฉพาะ Cancel (เปิด Launcher) ปุ่มอื่นไม่ทำอะไร
- **List:** `<` `>` = ขึ้น/ลงอัตโนมัติ · OK = เลือก · Cancel ค้าง = จัดการแถว (ยืนยันลบ หรือเมนู Rename/Delete สำหรับหมวด IR)
- **แถวค่าใน Settings [ร่าง]:**
  - Toggle: OK สลับทันที
  - ตัวเลือก: OK เปิดรายการย่อย (✓ ตัวที่เลือก)
  - ตัวเลข: OK เข้าโหมดแก้ในแถว, `<` `>` ปรับ (กดค้างเร่ง), OK บันทึก, Cancel คืนค่าเดิม
- **Text-input (carousel แถวเดียว):** `<` `>` หมุน · OK เพิ่มตัวอักษร · Cancel ลบตัวท้าย (ช่องว่างอยู่แล้ว = ย้อนกลับ) · Cancel ค้าง ยกเลิกทั้งหมด · OK ค้าง สลับตัวพิมพ์ · ท้ายวงมีช่องพิเศษ **⌫** (ลบ) และ **✓** (จบ)
  - ชุดตัวอักษรชื่อไฟล์: `A-Z 0-9 _ - .`
  - ชุดรหัส WiFi: `A-Z a-z 0-9 _ - . ! @ # $ % ^ & * ( )`
- **PIN:** 6 หลักคงที่ · carousel เลข 0–9 · ครบหลักที่ 6 ตรวจอัตโนมัติ (ไม่มี ✓) · Cancel สั้นลบหลักล่าสุด · Cancel ค้างออก
- **รหัสฉุกเฉิน:** 8 หลัก รูปแบบเดียวกับ PIN

### 2.6 Lock / PIN / ฉุกเฉิน [ล็อกแล้ว]
- Lock screen ใช้ **PIN อย่างเดียว** (ตัด Pattern ออกแล้ว) · ตั้ง PIN เป็นทางเลือก เครื่องมาแบบไม่ล็อก
- Lock screen แสดง: นาฬิกากลางจอค่อนบน, แบต % ตำแหน่งเดียวกับ status bar, ข้อความ custom ตัวเล็กใต้นาฬิกา, ไอคอนกุญแจล่างจอเมื่อมี PIN
- ผิดเกิน **3 ครั้ง** → ล็อกชั่วคราว และเวลาสะสมเมื่อผิดซ้ำ
  - **[ร่าง]** ตัวเลขยังไม่กำหนด ตัดสินใจเอง (เช่น 30 วิ แล้วเพิ่มเป็นสองเท่า เพดาน 10 นาที)
  - ต้องเก็บตัวนับลง flash ไม่ให้ reboot ล้างได้
- ตัวนับผิดใช้ร่วมกันทุกที่ที่กรอก PIN หรือรหัสฉุกเฉิน (Lock, เปลี่ยน/ลบ PIN, เมนูฉุกเฉิน, Factory Reset)
- เมนูฉุกเฉินเข้าได้ **เฉพาะตอนหน้า Boot status** ด้วย Power ค้าง (combo พิเศษ **[ร่าง]** ตัดสินใจเองหรือไม่ทำก็ได้) → Reset PIN → กรอกรหัสฉุกเฉิน → ลบ PIN แล้ว boot ต่อ
- รหัสฉุกเฉินเป็นค่าคงที่ 8 หลัก แยกเป็นค่าคงที่กระจายในไฟล์หลัก ตั้งชื่อตัวแปรให้กลมกลืน ประกอบเฉพาะในฟังก์ชันตรวจเดียว (กันการเปิดอ่านซอร์สผ่านๆ ไม่ได้กัน reverse-engineer)
- Factory Reset: 3 ขอบเขต (Settings only / Settings + WiFi/BT / Everything รวม PIN) · **กรอกรหัสฉุกเฉินทุกครั้งทุกขอบเขต** ก่อน dialog ยืนยัน · ไม่ลบไฟล์บน SD

### 2.7 การเชื่อมต่อ [ล็อกแล้ว]
- **WiFi:** manual ล้วน ไม่ต่อเองตอน boot และไม่ตัดเองตอนออกจากหน้า (state อยู่ใน WiFiManager) · จำรหัสเครือข่ายที่ต่อสำเร็จ (plain text ใน flash) แต่ต้องกดเลือกเองทุกครั้ง · ต่อสำเร็จแล้ว NTP sync และเขียนเวลาลง DS3231
- **Bluetooth:** ไม่เปิดตอน boot · advertise เมื่อเข้าเมนู Bluetooth · ไม่ตัดเองตอนออกจากหน้า (BLEManager) · host ที่เคยจับคู่ต่อเองไม่ถาม · host ใหม่ขึ้น dialog ยืนยัน · ที่เก็บเต็มลบตัวเก่าสุด (LRU)
  - **[ร่าง]** ออกจากเมนูตอนยังไม่ต่อ ให้หยุด advertise
- **Remote Control:** 2 ชั้น: กลุ่ม (Media / Presentation / Keys) → ปุ่มในกลุ่ม · OK ส่ง · OK ค้าง (repeat) ส่งซ้ำ · ขอบเขต HID: Consumer Control + Keyboard (ลูกศร, PgUp/PgDn, Esc, Enter) เท่านั้น

---

## 3. กฎของแอป (Games / โปรแกรมกำหนดเอง) [ล็อกแล้ว ยกเว้นที่ระบุ]

### 3.1 ระดับ bypass
| ระดับ | ใช้กับ | Host ทำอะไร |
|---|---|---|
| 0 Default | เกมในตัว และ pack บน SD ทุกตัว | ดัก Cancel ค้าง 500ms แล้ว **host วาดเมนูพักเอง** (Resume / Restart / Exit) · Cancel สั้นส่งให้แอปตอนปล่อยปุ่ม |
| 1 Custom pause | แอป standalone ใน bypass list | ดัก Cancel ค้างเหมือนเดิม แต่เรียก `onPauseRequest()` ให้แอปวาดหน้าหยุดเอง และแอปเรียก `host.exit()` เอง |
| 2 Raw input | แอป standalone ใน bypass list ที่ขอระดับนี้ | ไม่ดักอะไร ส่งเหตุการณ์ปุ่มดิบ (down/up + เวลา) ทั้ง 4 ปุ่มผ่าน `onRawInput()` ทันที |

### 3.2 ทำงานเสมอทุกระดับ (แอปปิดไม่ได้)
- **Failsafe:** Cancel ค้างต่อเนื่อง ≈3 วินาที → บังคับออก (`onExit` มีเวลาจำกัด เกินแล้ว host ตัด) แล้วกลับ Games menu
  - **[ร่าง]** แถบความคืบหน้าบางๆ ที่ขอบล่างตั้งแต่ ≈1.5 วิ ตัวเลขปรับได้
- ปุ่ม Power (sleep + ล็อก)
- แบต: ต่ำกว่า 5% ส่ง `onSaveRequest` แล้วขึ้นหน้าเตือนเต็มจอ · 2–3% ปิดเครื่อง
- ปิด idle-dim / idle-sleep ระหว่างอยู่ในแอป
- แอปเข้าถึง peripheral ผ่าน core service เท่านั้น

### 3.3 การบังคับใช้ในโค้ด
- `app_rules.h` (หรือชื่อใกล้เคียง) เก็บค่าคงที่: เวลา hold/failsafe, ปุ่มที่สงวน, งบทรัพยากร และตาราง bypass `{appId, level}` (ไม่อยู่ในตาราง = ระดับ 0)
- bypass ใช้ได้เฉพาะแอป `type: canvas` ที่ compile รวมในเฟิร์มแวร์ · pack บน SD ขอระดับ 1/2 ไม่ได้
- `static_assert`: ระดับ 1 ต้องมี `onPauseRequest()` ระดับ 2 ต้องมี `onRawInput()`
- manifest ของแอปมี `type: canvas | screens`
  - แอป `screens` (menu_flow) ใช้กฎ list ปกติ ไม่มีเมนูพัก (Cancel ค้าง = จัดการแถว)
- engine ตรวจ pack ตอนโหลด (`validatePack()`): sprite2d = ภาพ RGB565 ≤128×160, จำนวน/ขนาด asset ไม่เกินงบ PSRAM, ผูกปุ่ม Cancel ค้างไม่ได้ · menu_flow = ใช้ได้เฉพาะแบบหน้ามาตรฐาน ไม่ผ่าน → หน้า "Failed to load" พร้อมเหตุผลสั้น

---

## 4. UI toolkit ที่ต้องสร้าง (C++)

เขียนเป็นชิ้นส่วนที่ใช้ซ้ำ ไม่วาดซ้ำในแต่ละหน้า พิกัดตามข้อ 2 และ `templates.json`

| ชิ้นส่วน | ใช้ที่ |
|---|---|
| StatusBar (มี/ไม่มีไอคอน BT/WiFi ตามสถานะจริง) | ทุกหน้าใน OS ยกเว้น Boot, Lock, PIN, เกม |
| TitleBar, BottomBar | หน้าแบบ list / detail / message |
| CatalogList (แถว 14px, ไอคอนซ้าย/ท้าย, marquee, empty state, scrollbar, Cancel ค้าง) | ทุกโมดูล |
| LauncherCards | M6 |
| ValueRow (toggle / option / number + โหมดแก้ไข) + OptionPicker (✓) | Settings |
| KeyValue | R3, D2, P2, Y2, Y6, L2 |
| Progress (ชื่อ + แถบ + ตัวนับ) | R2, W4 |
| Busy (ไอคอน + ข้อความ + loading gif) | L1, R1, W3, E2, C1, C5, G2 |
| Message / Result (ไอคอนใหญ่ + ข้อความ) | L5, C6a, ข้อผิดพลาดต่างๆ |
| TextInput carousel | ชื่อสัญญาณ/หมวด/dump, รหัส WiFi, Lock message |
| DigitEntry (6 หรือ 8 หลัก) | PIN, รหัสฉุกเฉิน |
| Dialog ยืนยัน, Popup เมนูสั้น, Toast | ลบ, จับคู่ BT, Rename/Delete หมวด, "Sent", "Not connected" |
| FailsafeBar | host ตอน Cancel ค้างในแอป |

**ระบบ input:** แปลงปุ่มเป็นเหตุการณ์ tap / hold / repeat / release และ combo (ปุ่มหนึ่งถูกกดค้างก่อน) ตาม semantics ใน `schema` ของ flow JSON (โดยเฉพาะ: ถ้าปุ่มเดียวกันมีทั้ง tap และ hold ในหน้านั้น tap ยิงตอนปล่อย) ใช้ input mode ของแต่ละหน้า (`normal` / `list` / `text`) ตามที่ flow ระบุ

**Transition:** รองรับ `cut`, `slide`, `push`, `fade` ตามที่ edge ระบุ (เช่น Home ↔ Launcher = slide ขึ้น/ลง 250ms, ออกจาก Boot logo = fade 400ms) ถ้าจอ/SPI ช้าเกินจะ fade เต็มจอได้ลื่น ให้ลดรูปแล้วรายงาน

---

## 5. Flow รายโมดูล (ทำตาม `flow-*-new.json`)

ไฟล์ JSON มีครบ: state, transition, trigger, ทิศ animation, input mode, start/end, และหน้าจอที่ลิงก์ ด้านล่างนี้เป็นสรุปและข้อควรรู้เพิ่มเติม

- **Node แบบ `internal`** = จุดที่เฟิร์มแวร์ตัดสินเอง (เช่น "PIN set?", "Protocol decoded?") ทางออกแต่ละเส้นมีชื่อบอกเงื่อนไข ไม่มี trigger
- **Node "Launcher (back)"** ในทุกโมดูล = pop กลับ Launcher · **node "Open selected module"** ใน Main = เปิดโมดูลที่เลือก (ข้ามไปผังของโมดูลนั้น)
- **System event** ที่ใช้ในผัง ต้องมีจุดยิงจริงในโค้ด:
  - เดิมใน Studio: `nfc_card_found`, `ir_received`, `wifi_connected`, `ble_connected`, `ble_disconnected`
  - เพิ่มใหม่: `boot_done`, `lockout_over`, `ir_learn_timeout`, `nfc_read_done`, `nfc_card_lost`, `nfc_write_done`, `wifi_scan_done`, `wifi_connect_failed`, `ble_pair_request`, `pack_loaded`, `pack_failed`, `app_failsafe`

| ผัง | ข้อควรรู้ |
|---|---|
| Main-new | Boot status (ไม่รับปุ่ม ยกเว้นทางฉุกเฉิน) → `boot_done` → Boot logo (รอจนพร้อมแต่ไม่ต่ำกว่า ~3 วิ, fade) → "PIN set?" → Lock → Home → Launcher · ตื่นจาก deep sleep ข้าม splash ไปหน้าเดิม (ตามแผนเดิม) แต่ต้องผ่าน Lock ถ้ามี PIN |
| IR-new | 2 mode: Send (หมวด → สัญญาณ → OK ส่งทันที + toast) และ Learn (รับทีละสัญญาณ → ถอดรหัสหรือ RAW → ตั้งชื่อ → เลือก/สร้างหมวด → บันทึก แล้วกลับไปรอสัญญาณถัดไป) · หมวด Rename/Delete ได้บนเครื่อง (ลบหมวด = ลบสัญญาณข้างในทั้งหมด, ข้อความยืนยันบอกจำนวน) · RX เปิดเฉพาะตอนอยู่หน้า Learn · timeout ~15 วิ **[ร่าง]** |
| NFC-new | Read / Write / Emulate / Saved Dumps · PN532 เริ่มเมื่อเข้าหน้า NFC (ไม่ตอบ = "NFC module not found") · Write ตรวจชนิดบัตรก่อน และเตือนว่า UID ไม่เปลี่ยนบนบัตรปกติ · Emulate เลือกได้เฉพาะ dump ที่ emulatable (ไม่ได้ = แสดง emulation_note) และปิด idle-sleep ระหว่าง emulate **[ร่าง]** · บัตรที่ไม่ใช่ MIFARE Classic บันทึกแค่ UID **[ร่าง]** |
| WiFi-new | Connect (สแกน → เลือก → จำไว้/เปิดโล่งต่อเลย ไม่งั้นใส่รหัส) · Saved Networks (OK ต่อ, Cancel ค้างลืม — ลืมแล้วไม่ตัดการเชื่อมต่อปัจจุบัน) · Disconnect ตัดทันที ไม่มี dialog **[ร่าง]** · บันทึกรหัสเฉพาะตอนต่อสำเร็จ · timeout ~15 วิ **[ร่าง]** |
| Bluetooth-new | เมนูมีแถวสถานะที่เลือกไม่ได้ · Remote 2 ชั้น · Paired Devices (ลืม host ที่ต่ออยู่ = ตัดทันที) · หลุดระหว่างใช้ = รายการจางลง + toast "Not connected" · กลุ่ม Keys ไม่มี node แยกในผัง (หน้าตาเหมือนกลุ่ม Media) |
| Games-new | เกมในตัวขึ้นก่อน ชุด SD ตามหลัง · ไม่มี SD = toast · หน้าเกมเต็มจอไม่มี status bar · เมนูพัก/failsafe ตามข้อ 3 · Exit แล้วเกมเซฟใน onExit เอง จำแถวที่เลือกไว้ · หน้าภายในแต่ละเกมเป็นเรื่องของแต่ละเกม |
| Settings-new | Display (ความสว่าง PWM แสงเปลี่ยนสด, Dim after) · Power (Deep/Light/Off/Never; Sleep after ซ่อนเมื่อ Never; Low battery %) · Sound (สอง toggle + Volume ที่มีไว้เฉยๆ) · Lock Screen (Set / Change / Remove PIN ตรวจ PIN เดิมก่อน, Lock message) · Date & Time (แก้ทีละช่อง บันทึกลง DS3231, 24h เป็นค่าเริ่มต้น) · System (Storage แยก Flash/SD, Factory Reset ตามข้อ 2.6, Firmware) · ทุกค่าเปลี่ยนแล้วบันทึกลง flash ทันทีเมื่อกด OK **[ร่าง]** · ไม่มีหมวด Connectivity (Saved Networks อยู่ใน WiFi, Paired Devices อยู่ใน Bluetooth) |

---

## 6. ลำดับงาน (phase)

1. **ย้าย asset เป็น RGB565** (เคยเลื่อนไว้จนกว่าเครื่องมือออกแบบจะเสร็จ ซึ่งเสร็จแล้ว): ใช้ header ที่ export จาก Studio, ปรับ driver/renderer ให้วาด RGB565 + สีโปร่งใส key และฟอนต์ 1bpp ที่เลือกสีตอนวาด ถ้า repo ทำไปแล้วบางส่วน ให้ต่อจากของเดิม
2. **ระบบ input + UI toolkit** (ข้อ 2.5, 4) พร้อม transition
3. **OS shell**: Boot status, Boot logo, Lock/PIN/ล็อกชั่วคราว, Home, Launcher, เมนูฉุกเฉิน, StatusBar ที่อ่านสถานะจริง
4. **App host + กฎของแอป** (ข้อ 3) รวม failsafe, bypass, `static_assert`, `validatePack()`
5. **หน้าของแต่ละโมดูล** ต่อเข้ากับ core service ที่มีอยู่ เรียงตามที่เห็นสมควร (แนะนำ Settings → WiFi → Bluetooth → IR → NFC → Games)
6. **ทดสอบ**: ถ้า repo มี core แบบ headless / mock HAL ของ dev tool อยู่แล้ว ให้เขียน test เดินตามผัง (ยิงปุ่ม/event ตาม transition แล้วตรวจว่าไปถึง state ที่ถูก) ถ้ายังไม่มี ให้ทำ test ชุดเล็กที่รันบนเครื่องได้ หรือระบุในรายงานว่ายังไม่ได้ทดสอบอะไร

---

## 7. เรื่องที่ยังไม่ตัดสินใจ (ตัดสินใจเองได้ แล้วรายงาน)

- เวลาล็อกชั่วคราวและการสะสม
- เพดานตัวอักษรของ Lock message (เสนอ 20 ตัว = บรรทัดเดียวด้วยฟอนต์ 6×8)
- ระยะเวลา toast / หน้าผลลัพธ์ (ผังใช้ 1–1.5 วิ) และ timeout ของ IR learn / WiFi connect
- combo ปุ่มเข้าเมนูฉุกเฉิน (นอกจาก Power ค้าง)
- โซนเวลา: ถือว่า UTC+7 คงที่ (ยังไม่มีแถวตั้งค่า)
- มุกบนหน้า Firmware (เว้นช่องไว้ได้)
- สีเน้น: ยังไม่ใช้ เว้นจุดให้ใส่ทีหลังได้ง่าย (รวมสีไว้ที่เดียว)

---

## 8. เกณฑ์ว่างานเสร็จ

- ทุก state ในผัง `-new` มีหน้าจริงในเฟิร์มแวร์ และทุก transition (ปุ่ม/timer/event/ตัดสินใจ) ทำงานตามผัง
- หน้าตาตรงกับ `screens/*.png` ในเรื่องตำแหน่งและขนาด (ข้อความตัวอย่างแทนด้วยข้อมูลจริงได้)
- กฎของแอปบังคับด้วยโค้ดจริง: แอประดับ 1/2 ที่ขาด callback ต้อง build ไม่ผ่าน
- ตัวนับ PIN ผิดอยู่รอด reboot · Factory Reset ทุกขอบเขตต้องกรอกรหัสฉุกเฉิน
- ไอคอน BT/WiFi ใน status bar สะท้อนสถานะจริง

## 9. รายงาน (ภาษาไทย)

1. สรุปสิ่งที่ทำ แยกตาม phase
2. **เรื่องที่ตัดสินใจเองพร้อมเหตุผล** (โดยเฉพาะข้อ [ร่าง] และข้อ 7)
3. จุดที่ทำไม่ได้ / ลดรูป / ต่างจากผังหรือภาพ พร้อมเหตุผล
4. วิธีทดสอบที่ใช้ และสิ่งที่ยังไม่ได้ทดสอบ
5. สิ่งที่ต้องให้ผู้ใช้ทำต่อ (เช่น export asset เพิ่ม, ต่อสาย, ตั้งค่า)
