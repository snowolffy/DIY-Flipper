# แผน: ยกเครื่อง DIY Flipper Emulator + เพิ่ม target ESP32-S3

สำหรับ Claude Code (cloud) ทำใน repo `snowolffy/DIY-Flipper`
เขียนเมื่อ 2026-10-04 อ้างสถานะ repo ที่ commit `fe4f3fc` ตามเอกสาร `DIY-Flipper-Emulator-Spec.md`

## วิธีทำงานที่เจ้าของโปรเจกต์ต้องการ

- **ตัดสินใจเองทุกจุดที่แผนเปิดไว้** แล้วรายงานเหตุผลตอนจบ ห้ามหยุดถามกลางทาง เจ้าของสั่งงานแล้วไปทำอย่างอื่น
- **รายงานตอนจบเป็นภาษาไทย** ระบุ: ทำอะไรเสร็จ, ตัดสินใจอะไรเองเพราะอะไร, อะไรยังไม่ได้ทดสอบ, อะไรทำไม่ได้
- **ตรวจงานด้วยตาตัวเอง** ทุกครั้งที่แก้ UI หรือภาพบนจอเครื่อง ให้สั่ง `sim shot` / `sim shot-ui` แล้วเปิด PNG ดูก่อนถือว่าเสร็จ
- ข้อความบนจอเครื่องและ UI ของ emulator เป็น**ภาษาอังกฤษ**

## เป้าหมาย

1. emulator แสดงสิ่งที่เครื่องจริงจะทำ ระหว่างที่ตัวเครื่องยังประกอบไม่เสร็จ (ยังไม่มีบอร์ดและจอในมือ)
2. firmware ชุดเดียวกัน build ลง ESP32-S3 ได้ทันทีเมื่อของมาถึง
3. คนใช้ผ่านหน้าต่าง, Claude Code ใช้ผ่าน terminal และทั้งสองเห็นสถานะเดียวกัน

## ข้อบังคับ (ห้ามเปลี่ยน)

- เป็น **exe ตัวเดียว portable** ไม่ต้องติดตั้ง ทำงานกับ **folder จริง** ของโปรเจกต์
- รันได้บน **Linux** (cloud container) และ **Windows** (เครื่องที่ใช้จริง) ตัด macOS ออกจาก CI ได้
- firmware ยังคุยกับฮาร์ดแวร์ผ่าน `firmware/hal/hal.h` เท่านั้น โค้ด `firmware/` ชุดเดียวรันทั้งใน emulator และบนเครื่อง
- ยังคง deterministic: script เดียวกันได้ภาพและ hash เดียวกันทุกเครื่อง
- mock เปลี่ยนค่าได้ทางเดียว คือผ่านคำสั่งชุดเดียวกัน ไม่ว่ามาจาก GUI, terminal หรือ script

## ความสัมพันธ์กับแผน firmware ที่มีอยู่

มีแผน firmware อีกฉบับ (app rules + UI toolkit + module flows) ถ้าฉบับนั้นทำข้อใดในเฟส 1 ไปแล้ว ให้ข้ามข้อนั้นและใช้ของที่มี ถ้ายังไม่ได้ทำ ให้ทำในแผนนี้เท่าที่ emulator ต้องใช้ กฎที่ขัดกันให้ยึด**การตัดสินล่าสุด**ในหัวข้อ "กฎปุ่ม" ด้านล่าง ไม่ใช่ `docs/DECISIONS.md` ซึ่งล้าสมัยในเรื่องนี้

---

## เฟส 1 — ฝั่ง firmware ที่ emulator ต้องพึ่ง

### 1.1 จอสี RGB565
- `Framebuffer` เป็น 128×160 `uint16_t` RGB565 (40,960 byte)
- ตัดแนวคิด invert ออกทั้งระบบ (ตัดสินแล้วว่าไม่มี display invert) รวม setting, script check `invert` และ script 2 ตัวที่ทดสอบ invert
- asset header เป็น `uint16_t` RGB565 สีโปร่งใส `0xF81F`
- รูปแบบไฟล์ `.c16` และ theme pack `format=2` ให้ยึด**ไฟล์ที่ Studio export จริง**เป็นแหล่งอ้างอิง (เจ้าของจะ export zip เข้า repo) อย่าเดารูปแบบ ถ้ายังไม่มีไฟล์ตัวอย่างใน repo ให้ทำ loader ตามโค้ด `export.js` / `theme.js` ของ Studio และเขียนในรายงานว่ายังไม่ได้ทดสอบกับไฟล์จริง
- สีพื้นฐานของ UI: ink `#F5F5F5`, พื้น `#0B1013` หน้าตาหลักเป็นโทนเทา มีสีเฉพาะจุด

### 1.2 นโยบายการ push จอ
ปัจจุบัน `App::tick()` วาดและ push ทั้งเฟรมทุก 10 ms บนเครื่องจริง 40 KB ผ่าน SPI ใช้เวลาเกือบทั้ง tick
- push เฉพาะเมื่อภาพเปลี่ยน (dirty flag หรือเทียบเฟรม เลือกเอง)
- จำกัดอัตรา push สูงสุด (ค่าเริ่มต้น 30 fps เก็บเป็นค่าคงที่ใน board profile)

### 1.3 HAL ที่ยังขาด
เพิ่ม interface ใน `hal.h` พร้อม mock:

| Interface | ต้องทำได้ |
|---|---|
| `Buzzer` | เล่นโทน (ความถี่, ระยะเวลา), หยุด |
| `Backlight` | ตั้งระดับ 0–255 |
| `Power` | เข้า light sleep / deep sleep, เหตุที่ตื่น, มีไฟ USB อยู่ไหม, สั่งปิดเครื่อง |

- ปุ่ม Power ต้องทำงานจริงในกรอบ App (ตอนนี้ไม่ทำอะไรเลย)
- `usbPresent()` คืน "ไม่ทราบ" ได้ เมื่อบอร์ดไม่มีขาวัด (ดูตาราง pin)

### 1.4 กฎปุ่ม (ยึดชุดนี้ ตรงกับ Studio player v8)
- tap ยิงตอน**กด** ถ้าหน้านั้นไม่มี hold/repeat บนปุ่มนั้น ไม่งั้นยิงตอนปล่อย
- hold ที่ 500 ms, repeat ทุกปุ่ม: เริ่ม 200 ms คูณ 0.8 ต่ำสุด 40 ms
- combo: ปุ่มค้าง + ปุ่มอื่น (เช่น ค้าง OK + < >) ปุ่มที่ค้างไม่ยิง tap/release ของตัวเอง
- มี event `release`
- Text input: OK ค้าง = สลับตัวพิมพ์, Cancel แตะ = ลบตัวท้าย (ช่องว่าง = ย้อนกลับ), Cancel ค้าง = ยกเลิกทั้งหมด, จบด้วยช่อง ✓ ท้าย carousel
- List: Cancel ค้างบนรายการ = ลบ (ขึ้นยืนยันก่อน)

---

## เฟส 2 — แกนจำลองให้อิงฮาร์ดแวร์จริง

### 2.1 Board profile (แหล่งความจริงเดียว)
ไฟล์เดียวที่ทั้ง driver จริงและ emulator อ่าน (รูปแบบและที่อยู่เลือกเอง แต่ห้ามมีสองสำเนาที่ต้องแก้มือให้ตรงกัน) เนื้อหา:

| ส่วน | ของจริง (รหัส Cybertice) |
|---|---|
| บอร์ด | ESP32-S3-DevKitC-1 N16R8 (M1622) flash 16 MB, PSRAM 8 MB |
| จอ | ST7735 1.8" 128×160 SPI, ขา BLK แยก (L0259) |
| ปุ่ม | โมดูล 4 ปุ่ม (M1115): OK, Cancel, <, > และ micro switch 6×6 (E0016) เป็น Power |
| สวิตช์ไฟ | toggle (E0375) คั่นระหว่างแบตกับ MT3608 |
| NFC | PN532 I2C 0x24 (M0155) |
| RTC | DS3231 I2C 0x68 (M0010) |
| SD | โมดูล micro SD แบบ SPI (M0026) |
| IR | ตัวส่ง (M0063) และตัวรับ (M0054) แยกโมดูล |
| Buzzer | KY-006 passive (M1067) |
| ไฟ | LiPo 1000 mAh (P0189), TP4056 Type-C (P0270), MT3608 (P0033), divider 100 kΩ × 2 |

### 2.2 ตาราง pin (ร่าง รอเจ้าของตรวจ และต้องเทียบกับ pinout ของบอร์ดจริงเมื่อของมาถึง)

| สัญญาณ | GPIO | เหตุผล |
|---|---|---|
| SPI SCK (จอ + SD) | 12 | ขา FSPI เริ่มต้น |
| SPI MOSI (จอ + SD) | 11 | ขา FSPI เริ่มต้น |
| SPI MISO (SD) | 13 | ขา FSPI เริ่มต้น |
| TFT CS | 10 | |
| TFT DC | 14 | |
| TFT RST | 21 | |
| TFT BLK (PWM) | 47 | |
| SD CS | 16 | |
| I2C SDA (PN532 + DS3231) | 8 | ค่าเริ่มต้นของ Arduino บน S3 |
| I2C SCL | 9 | ค่าเริ่มต้นของ Arduino บน S3 |
| ปุ่ม OK | 4 | RTC GPIO ปลุกจาก sleep ได้ |
| ปุ่ม Cancel | 5 | |
| ปุ่ม < | 6 | |
| ปุ่ม > | 7 | |
| ปุ่ม Power | 15 | RTC GPIO |
| IR TX | 17 | RMT |
| IR RX | 18 | RMT |
| Buzzer | 40 | |
| แบต ADC | 1 | ADC1 (ADC2 ใช้ไม่ได้ตอนเปิด WiFi) |
| วัดไฟ USB (ตัวเลือก) | 2 | ต้องเพิ่ม divider 2 ตัว ยังไม่อยู่ในรายการของ ถ้าไม่ต่อ ตั้งเป็น -1 |

กฎที่ต้องตรวจด้วยโค้ด (unit test บน board profile):
- ห้ามใช้ GPIO 35, 36, 37 (PSRAM ของ N16R8), 0, 3, 45, 46 (strapping), 19, 20 (USB), 43, 44 (UART0), 38 และ 48 (LED บนบอร์ด แล้วแต่รุ่น)
- OK และ Power ต้องอยู่ใน GPIO 0–21
- แบต ADC ต้องอยู่ใน GPIO 1–10
- ห้าม GPIO ซ้ำกัน
- ขั้วของปุ่ม (active high/low) เป็นค่าตั้งใน profile เพราะยังไม่รู้ของโมดูล M1115

### 2.3 Mock เพิ่ม / แก้
| Mock | เพิ่ม |
|---|---|
| `MockDisplay` | เก็บ RGB565, `hash()` คิดจากพิกเซล 16-bit แบบ little-endian, เขียน PNG สี (ใช้ miniz ที่ vendored อยู่), คิดต้นทุนเวลาต่อ push = จำนวน byte × 8 ÷ ความเร็ว SPI ใน profile แล้วรายงาน fps และ ms ต่อเฟรม |
| `MockBuzzer` | ความถี่ที่กำลังดัง, ประวัติโทนล่าสุด |
| `MockBacklight` | ระดับปัจจุบัน และหรี่ภาพจอใน GUI ตามระดับ |
| `MockPower` | สถานะ awake / light sleep / deep sleep / off, สวิตช์ไฟ On/Off (Off = ไฟหาย เปิดใหม่ = cold boot), เสียบ USB หรือไม่, ตื่นด้วย OK |
| `MockNfc` | รองรับ write ลงการ์ด, การ์ด magic (เปลี่ยน UID ได้), โหมด emulate (reader มาแตะ) |
| `MockWifi` | NTP ตอบหรือไม่ตอบ |
| `MockIr` | รับสัญญาณ RAW จากไฟล์ timing |

### 2.4 Script
- script JSON เดิมต้องยังรันได้ (ยกเว้นตัวที่ทดสอบ invert ให้ลบ) ล็อก hash ใหม่ทั้งชุดหลังเปลี่ยนเป็นสี และเขียนใน commit ว่าเปลี่ยนเพราะอะไร
- เพิ่ม event และ check สำหรับ mock ใหม่ทุกตัว และ check จำนวน push ของจอ (หน้านิ่งต้องไม่ push ซ้ำ)
- `dump` เขียน PNG สี (รับ `.pbm` เดิมไม่ได้แล้ว แก้ script ที่ใช้)
- อธิบาย event/check ใหม่ใน comment บนสุดของ `simulator/core/script.h` ตามกติกาเดิม

---

## เฟส 3 — exe ตัวเดียว: server + API + คำสั่ง terminal

### โครง
- exe ชื่อ `sim` (`sim.exe`) แทนทั้ง `sim_headless` และ `sim_gui`
- ฝั่ง C++ เปิด HTTP server ผูกกับ `127.0.0.1` เท่านั้น ไลบรารีให้ vendored เป็น header เดียวแบบที่ทำกับ json/miniz (เช่น cpp-httplib) ห้ามพึ่ง runtime ภายนอก
- ไฟล์ UI (HTML/CSS/JS, ฟอนต์) **ฝังใน exe** ตอน build ใช้งานได้โดยไม่มีอินเทอร์เน็ต
- GUI และคำสั่ง terminal เรียก **API ชุดเดียวกัน** ไม่มีทางลัดเฉพาะ GUI

### คำสั่ง (ชื่อเป็นข้อเสนอ ปรับได้ แต่ต้องครบความสามารถ)
| คำสั่ง | ทำอะไร |
|---|---|
| `sim gui [--project DIR]` | เปิด session + เปิดหน้าต่าง |
| `sim open [--project DIR]` | เปิด session เบื้องหลัง ไม่มีหน้าต่าง |
| `sim close` | ปิด session |
| `sim press BTN` / `hold BTN [ms]` / `down BTN` / `up BTN` | ปุ่ม (รองรับ combo ด้วย down/up) |
| `sim wait MS` | เดินเวลาเสมือน |
| `sim state [--json]` | menu path, สถานะ mock ทุกตัว, สถานะ power, hash จอ |
| `sim shot FILE.png` | ภาพจอเครื่อง 128×160 สี (มีตัวเลือกขยาย) |
| `sim shot-ui FILE.png` | ภาพหน้าต่างแอปทั้งหน้า |
| `sim set ...` | คำสั่ง mock ทั้งหมด (battery, usb, sd, rtc, ir, nfc, wifi, ble, power switch) |
| `sim import FILE` | Import Asset |
| `sim run SCRIPT|DIR` | รัน script แบบ headless (แทน `sim_headless` ใช้สำเนา storage ใน temp เหมือนเดิม) |
| `sim restart [--cold]` | เปิดเครื่องใหม่ |

### เวลา
- มีหน้าต่างเปิดอยู่: เดินตามเวลาจริง
- ไม่มีหน้าต่าง (`sim open`): เวลาเสมือนเดินเฉพาะเมื่อสั่ง (`wait`, หรือคำสั่งปุ่มที่เดินเวลาเองตามระยะกด) ผลจึงซ้ำได้ทุกครั้ง
- `sim run` เป็นเวลาเสมือนล้วนเหมือนเดิม

### คำศัพท์เดียว
คำสั่ง terminal, บรรทัดใน command log ของ GUI และ event ใน script ต้องเป็นคำศัพท์ชุดเดียวกัน เพื่อให้ "Save as script" และ "Record" แปลง log เป็น script ได้ตรงตัว

### `shot-ui`
ใช้ browser แบบ headless ที่หาได้ในเครื่อง (Chromium/Chrome/Edge) ถ้า container ติดตั้งไม่ได้ ให้คำสั่งรายงานชัดว่าไม่มี browser และคำสั่งอื่นต้องทำงานครบ เขียนผลการลองใน cloud container ลงรายงาน

---

## เฟส 4 — หน้าต่างแอป (web UI) ตาม wireframe

**Wireframe ที่อนุมัติแล้ว:** https://claude.ai/artifact/7nqQ71Ka3WAyEuWYiMSe7F (8 artboard: หน้าต่างหลัก, terminal, และ tab IR / NFC / WiFi / Bluetooth / Files / Scripts) ให้ทำตามการจัดวางและข้อความใน wireframe ถ้าเปิดลิงก์ไม่ได้ ให้ทำตามคำบรรยายนี้

### การจัดวาง
- workspace เต็มหน้าต่าง **ไม่เลื่อนทั้งหน้า** panel ยึดขอบ มีแค่พื้นที่ตัวเครื่องที่ยืด
- **แถบบน:** ชื่อแอป, ปุ่ม Project (แสดง path folder จริง กดเพื่อเปลี่ยน), ป้าย Board (`ESP32-S3 N16R8 · ST7735 128×160 RGB565`), menu path + uptime, ปุ่ม Pause / Restart / Cold boot
- **ซ้าย (ตัวเครื่อง):** จอขยาย 3× (เลือก 1–4× หรือ Fit, nearest-neighbor), ใต้จอเป็นปุ่ม OK / Cancel / < / > เรียงแถวเดียวตามเครื่องจริง, ด้านข้างมีปุ่ม Power และสวิตช์ไฟ On/Off, ใต้สุดมี Zoom, Save screen (PNG), Edit keys, hash ของจอ
- **ขวา (dock แบบ tab):** Hardware, IR, NFC, WiFi, Bluetooth, Files, Scripts
- **ล่าง:** command log (เวลา + คำสั่ง) และปุ่ม Save as script

### ปุ่มกับคีย์บอร์ด
- ค่าเริ่มต้น: Z = OK, X = Cancel, ← = <, → = >, P = Power (ตรงกับ Studio player) เปลี่ยนได้ที่ Edit keys และจำไว้ต่อโปรเจกต์
- ทุกปุ่มบนจอแสดงป้ายคีย์ของตัวเอง
- กดคีย์ค้างหลายคีย์พร้อมกันได้ (combo)
- เมาส์: กดค้างได้ และ**คลิกขวาที่ปุ่ม = ล็อกค้าง** จนคลิกขวาอีกครั้ง ปุ่มที่ค้างต้องมีสถานะให้เห็นชัด พร้อมข้อความบอกว่ากำลังค้างปุ่มไหน
- ขณะพิมพ์ในช่องกรอก คีย์ต้องไม่ไปถึงเครื่อง

### Tab
| Tab | สั่งได้ | แสดงจาก firmware |
|---|---|---|
| Hardware | แบต % / No reading, USB plugged, SD inserted / Fail every write, นาฬิกา (Missing, Set to PC time) | backlight, buzzer, IR, NFC, WiFi, Bluetooth, อัตรา push จอ, power state แต่ละแถวบอกชื่อชิ้นส่วนและ pin จาก board profile |
| IR | protocol / address / command → Press remote button, โหลด RAW timings | กำลังฟังหรือไม่, รายการที่เครื่องส่ง |
| NFC | UID / type / blocks หรือใช้ dump, Place / Remove, Magic card, Tap with a reader | polling, สถานะ emulate, สิ่งที่เครื่องเขียน |
| WiFi | รายการ network (ชื่อ, dBm, Locked, เพิ่ม), connect ครั้งหน้าสำเร็จไหม, NTP ตอบไหม, delay | สถานะ WiFi |
| Bluetooth | ชื่อ host, connect / disconnect, Pair list is full | รหัสจับคู่, host ที่ pair แล้ว, คีย์ที่ host ได้รับ |
| Files | path จริง, Open in file manager, Reset from seed, Import asset (ลากวางหรือเลือกไฟล์) | โครงสร้าง `flash/` และ `sd/` |
| Scripts | เลือก, Run here, Run all, Stop, Restart the device first, Start recording | ผลต่อ script |

### สไตล์
- สี: พื้น `#0B1013`, panel `#141B20`, เส้น `#26313A`, ตัวอักษร `#E6EBEE`, ตัวอักษรรอง `#9AA9B3`, สีเน้นสีเดียว `#F0B24A`, พื้น log `#05080A`
- ฟอนต์: IBM Plex Sans (UI), IBM Plex Mono (ค่า, path, log) ฝังในแอป ถ้าฝังไม่ได้ให้ใช้ฟอนต์ระบบและรายงาน
- สถานะว่างต้องมีข้อความบอกว่าจะเห็นอะไรที่นี่ (ดูใน wireframe)

### หน้าต่าง
- Windows: เปิดด้วย Edge หรือ Chrome โหมดแอป (ไม่มีแถบ browser) ถ้าไม่มี เปิด browser เริ่มต้น
- Linux: ทางเดียวกันกับ Chromium/Chrome ถ้ามี
- ปิดหน้าต่างแล้ว session ต้องปิดตาม (ยกเว้นเปิดด้วย `sim open`)

---

## เฟส 5 — target ESP32-S3 (compile ได้ ยังไม่มีเครื่องให้ทดสอบ)

- โปรเจกต์ PlatformIO สำหรับ `esp32-s3-devkitc-1` ตั้งค่า N16R8 (flash 16 MB, PSRAM แบบ octal, partition มี LittleFS)
- driver จริงที่ implement `hal.h` ครบทุก interface อ่าน pin จาก board profile:
  - จอ ST7735 (SPI), backlight (PWM), ปุ่ม 5 ตัว, flash (LittleFS), SD (SPI, FAT32)
  - RTC DS3231, NFC PN532 (I2C), IR ส่ง/รับด้วย RMT
  - แบต (ADC + ตารางเดิม), buzzer, power (light/deep sleep ตื่นด้วย OK), WiFi, BLE HID
- เลือกไลบรารีเอง ให้เหตุผลในรายงาน
- **CI:** เพิ่ม job ที่ compile target นี้ทุก commit และพิมพ์ขนาด flash/RAM ที่ใช้
- ทุก driver ต้องระบุใน `docs/STATUS.md` ว่า **"compile ผ่าน ยังไม่เคยรันบนฮาร์ดแวร์"**
- เขียน `docs/BRINGUP.md`: ขั้นตอนทดสอบทีละชิ้นเมื่อของมาถึง เรียงตามนี้ จอ + ปุ่ม → flash → SD → RTC → แบต → buzzer → IR → NFC → WiFi / BLE → sleep แต่ละขั้นบอกว่าต่ออะไร แฟลชอะไร และควรเห็นอะไร

---

## เฟส 6 — เก็บงาน

- ลบ `simulator/gui` (Dear ImGui + SDL2) และ `sim_headless` หลัง `sim` ทำได้ครบ
- CI: เหลือ Linux + Windows, job Windows อัปโหลด `sim.exe` ตัวเดียว
- อัปเดต `README.md`, `CLAUDE.md`, `docs/STATUS.md`, `docs/DECISIONS.md` (แก้กฎปุ่มและเรื่องสีให้ตรงกับแผนนี้), `docs/ARCHITECTURE.md`
- ใน `CLAUDE.md` เขียนวิธีที่ Claude Code รอบถัดไปใช้ `sim` ดูผลงานตัวเอง

## เกณฑ์ว่าเสร็จ

1. `ctest` ผ่านบน Linux และ Windows: unit test, script ทุกตัว, test กฎ pin
2. จาก terminal ล้วน: `sim open` → กดปุ่มไปหน้า Settings → `sim state` รายงาน path ถูก → `sim shot` ได้ PNG สีที่เปิดดูได้
3. `sim gui` บน Windows เปิดหน้าต่างตาม wireframe, กด combo ค้าง Z + → ได้, ทุกการคลิกขึ้นใน command log
4. หน้านิ่ง 1 วินาที จอไม่ถูก push ซ้ำ (มี script ตรวจ)
5. theme pack `format=2` และ `.c16` จาก Studio import แล้วแสดงสีถูก (หรือรายงานว่ายังไม่มีไฟล์จริงให้ทดสอบ)
6. job CI ของ ESP32-S3 compile ผ่าน
7. แนบภาพ `shot` และ `shot-ui` ที่ถ่ายเองในรายงาน พร้อมคำบรรยายว่าตรงกับ wireframe ตรงไหนและต่างตรงไหน

## นอกขอบเขต

- การเชื่อม emulator กับเครื่องจริงผ่าน serial
- การจำลองระดับชิป (Wokwi/QEMU) และการ build เป็น WebAssembly
- การรวม emulator เข้า Flipper UI Studio
- ฟีเจอร์ firmware ใหม่ที่อยู่ในแผน firmware อีกฉบับ (module flows, lock screen, games ฯลฯ) ทำเฉพาะส่วนที่แผนนี้ระบุ
