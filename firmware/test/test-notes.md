# SmartFlag® Firmware Test Log
**Firmware Version:** v0.2-test-ready  
**Device:** BSoM  F1H002242
**Date:** 2025-06-02
**Tester:** [Bill Wight]  

---

## ✅ Test Summary

| Test ID | Description                         | Status | Notes                          |
|---------|-------------------------------------|--------|--------------------------------|
| T001    | Power-on boot, motor idle          | ☐ Pass / ☐ Fail |                                |
| T002    | Move CW with timeout               | ☐ Pass / ☐ Fail |                                |
| T003    | Move CCW with timeout              | ☐ Pass / ☐ Fail |                                |
| T004    | CW stops on halyard sensor (HALF)  | ☐ Pass / ☐ Fail |                                |
| T005    | CCW stops on halyard sensor (FULL) | ☐ Pass / ☐ Fail |                                |
| T006    | Stall trigger when >1.8A           | ☐ Pass / ☐ Fail |                                |
| T007    | Lid open disables motion           | ☐ Pass / ☐ Fail |                                |
| T008    | Lid close triggers CALIBRATE       | ☐ Pass / ☐ Fail |                                |
| T009    | Gradual PWM ramp-up works          | ☐ Pass / ☐ Fail |                                |
| T010    | ENABLE_MOTOR toggles correctly     | ☐ Pass / ☐ Fail |                                |

---

## 🛠 Observations & Notes

- What worked:
  - ...
- What failed:
  - ...
- Unexpected behavior:
  - Low values for ramp speed (around 30-40 or less) result in immediate stoppage

---

## 📦 Recommendations for Next Version

- [ ] ...
- [ ] ...

---

