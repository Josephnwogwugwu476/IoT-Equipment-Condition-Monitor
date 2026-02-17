IoT Equipment Condition Monitoring System
ESP32 | C/C++ | KiCad | ThingSpeak | GPRS | Multi-Protocol

Project Overview
A production-ready IoT system for real-time industrial equipment health 
monitoring. Detects vibration anomalies, temperature excursions, and 
current draw variations — uploading live data to the cloud via GPRS 
for remote predictive maintenance.

Live System Data
The system was deployed and tested with real sensor data streamed 
to ThingSpeak:

Vibration Monitoring (ADXL345 Accelerometer)
![Vibration RMS](Screenshot_2025-09-22_111331.png)
![Instantaneous Vibration](Screenshot_2025-09-22_111348.png)

Current Monitoring (ACS712 Current Sensor)
![Current RMS](Screenshot_2025-09-22_111404.png)

Temperature Monitoring (MAX6675 Thermocouple)
![Temperature & System Status](Screenshot_2026-01-17_154321.png)

---

Hardware Architecture
![PCB Schematic](ECM_PCB.kicad_sch)

Components
| Component | Function |
|-----------|----------|
| ESP32-WROOM-32D | Main MCU - WiFi/BT capable |
| ADXL345 | I2C Vibration accelerometer |
| MAX6675 | SPI Thermocouple interface |
| ACS712 | Analog current sensor |
| SIM800L | UART GPRS module for cloud upload |
| SD Card | SPI local data logging backup |
| WC1602A LCD | I2C display for local readout |

---

 Firmware Architecture

 Communication Protocols Used
- I2C → ADXL345 accelerometer
- SPI → MAX6675 thermocouple + SD card logging
- UART → SIM800L GSM/GPRS module
- Analog ADC → ACS712 current sensor

 Key Features
- FFT-based vibration analysis for fault detection
- RMS current calculations for load monitoring
- GPRS HTTP client → ThingSpeak cloud integration
- SD card fallback logging when GPRS unavailable
- SMS alert system for threshold violations
- System status reporting (0/1/2 status codes)

 Signal Processing
```c
// FFT vibration analysis - fault detection
float vibrationRMS = calculateRMS(adxlReadings, SAMPLE_SIZE);
float fftMagnitude = performFFT(adxlReadings, SAMPLE_SIZE);

// Temperature via MAX6675 SPI
float tempC = thermocouple.readCelsius();

// Current via ACS712 ADC
float currentRMS = calculateCurrentRMS(analogPin, SAMPLES);
```

---

 PCB Design
- Designed in KiCad & Fusion 360 Electronics
- Multi-layer board with star grounding for EMI reduction
- Power architecture: 5V → 4V Buck (LM2596) → 3.3V LDO
- Custom component footprints for specialized sensors

---

 Results
- ✅ 124 successful cloud data entries recorded
- ✅ Real-time vibration, and temperature monitoring
- ✅ GPRS cloud upload operational
- ✅ SD card backup logging functional
- ✅ System status reporting (0=offline, 1=connecting, 2=active)

---

 Technologies
`ESP32` `C/C++` `KiCad` `Arduino` `I2C` `SPI` `UART` `GPRS` 
`ThingSpeak` `FFT` `ADC` `SD Card` `GSM` `Embedded Systems`
```

4. Scroll down, click "Commit changes"

---

 STEP 4: PIN THE REPO TO YOUR PROFILE

1. Go to your profile page (github.com/Josephnwogwugwu476)
2. Click "Customize your pins"
3. Select `IoT-Equipment-Condition-Monitor`
4. Click Save

---

 STEP 5: UPDATE YOUR PROFILE README (Optional but impressive)

Go to your profile, if there's no README:
1. Create a repo named exactly `Josephnwogwugwu476` (same as username)
2. Add a README with a brief bio and list of projects

---

 AFTER ALL THIS:

Your portfolio link for the email becomes:
```
https://github.com/Josephnwogwugwu476
```

And you can reference the specific project as:
```
https://github.com/Josephnwogwugwu476/IoT-Equipment-Condition-Monitor

