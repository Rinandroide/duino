# DU-INO Eurorack Custom Modules

Custom open-source functions for the **[Detroit Underground DU-INO](https://github.com/logickworkshop/du-ino)** Arduino-based Eurorack module by Logick Workshop.

---

## 🎛️ Included Modules

### 1. r-EUCLID (`Euclid/`)
A dual-channel Euclidean rhythm and random stepped-CV generator.
* **GT1 (Out):** Channel 1 Trigger Out (Euclidean beat).
* **GT2 (Out):** Channel 2 Trigger Out (Euclidean beat).
* **GT3 (In):** External Clock In (Supports interrupt sync).
* **GT4 (In):** Reset In.
* **CO1 (Out):** Channel 1 Random Stepped CV (0–5V).
* **CO2 (Out):** Channel 2 Random Stepped CV (0–5V).

#### Switch Configuration
```text
SG2    [^][^]    SG1
SG4    [_][_]    SG3
SC2    [^][^]    SC1
SC4    [_][_]    SC3
