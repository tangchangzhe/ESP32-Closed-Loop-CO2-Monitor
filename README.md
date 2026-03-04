# ESP32 CO2 Drift Compensator

[中文文档](README_CN.md)

A pump-based closed-loop CO2 monitoring system with built-in software drift compensation algorithm for low-cost sealed chamber applications.

## Demo

| Raw Data (Leakage causes continuous decline) | Compensated Data (Restored true fluctuation) |
| -------------------------------------------- | -------------------------------------------- |
| ![Raw](docs/images/before_compensation.png)  | ![Compensated](docs/images/after_compensation.png) |

## Features

### Pump-Based Closed-Loop Sampling

PWM-controlled air pump for closed-loop gas circuit sampling:

1. Pump circulation (adjustable duration and intensity)
2. Pump off, wait for pressure stabilization
3. Read sensor data
4. Real-time upload to server

### Auto-Start Operation

- Automatic startup on power-on, no manual intervention required
- Multi-WiFi hotspot support with automatic switching
- Auto-recovery after power loss
- Timestamp alignment to 10-minute intervals

### Timestamp Alignment Mechanism

The system ensures clean, aligned timestamps for data consistency:

- All data timestamps are aligned to exact 10-minute marks (`00:00`, `00:10`, `00:20`...)
- Even though measurement takes ~45 seconds, the uploaded timestamp reflects the **scheduled time**, not completion time
- After power recovery, the system calculates the next nearest aligned time
- If less than 10 seconds remain until the next interval, it skips to the following one

### Multi-Point Drift Compensation Algorithm

**Problem**: Sealed gas circuits inevitably leak over time, causing CO2 readings to drift downward continuously.

**Solution**: A non-linear drift compensation algorithm implemented in the frontend:

1. **Anchor Selection**: User selects multiple "stable periods" where CO2 should be constant (e.g., nighttime when algae respiration is steady)
2. **Slope Calculation**: Linear regression on each anchor period yields drift rate (ppm/h)
3. **Interpolation**: Linear interpolation between anchors for smooth drift rate transition
4. **Cumulative Correction**: Integrate drift rate over time and subtract from raw readings

**Mathematical Expression**:

- Anchor drift rate (linear regression slope):
  
  $$slope = \frac{n \sum x_i y_i - \sum x_i \sum y_i}{n \sum x_i^2 - (\sum x_i)^2}$$

- Corrected value (cumulative integration):
  
  $$CO_{2,corrected} = CO_{2,raw} - \int_0^t slope(\tau) \, d\tau$$

### Complete IoT Data Pipeline

```
ESP32 Sampling → HTTP Upload → PHP Storage → ECharts Visualization
```

## Hardware Requirements

| Component | Model | Description |
| --------- | ----- | ----------- |
| MCU | ESP32-S3-DevKitC-1 | Main controller |
| CO2 Sensor | ZG09SR | NDIR infrared, Modbus RTU |
| Air Pump | 5V Micro Pump | PWM controlled, closed-loop sampling |
| Pump Driver | MOS Module (4-pin) | PWM signal amplification |
| Temp/Humidity | DHT22/SHT30 etc. | **Reserved interface, not used** |

### Pin Connections

| ESP32 Pin | Connected Device |
| --------- | ---------------- |
| GPIO 18 | MOS Module PWM (Pump control) |
| GPIO 16 | ZG09SR RX |
| GPIO 17 | ZG09SR TX |

See [Hardware Documentation](docs/HARDWARE.md) for detailed wiring.

## Quick Start

### 1. Firmware Configuration

```bash
cd firmware
cp src/config.h.example src/config.h
# Edit config.h with your WiFi and server information
```

Compile and upload using PlatformIO:

```bash
pio run -t upload
```

### 2. Server Deployment

```bash
cd web
cp config.php.example config.php
# Edit config.php with your database information
```

Import sample data (includes table structure and Jan 3-9 test data):

```bash
mysql -u your_user -p co2data < data/sample_data.sql
```

Or manually create an empty table:

```sql
CREATE TABLE co2_measurements (
  id INT AUTO_INCREMENT PRIMARY KEY,
  timestamp DATETIME NOT NULL,
  co2_ppm INT NOT NULL,
  temperature FLOAT DEFAULT NULL,  -- Reserved, not used
  humidity FLOAT DEFAULT NULL,     -- Reserved, not used
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_timestamp (timestamp)
);
```

### 3. Using Drift Compensation

1. Open `index.html` in browser
2. Select time range to query data
3. In "Drift Compensation" panel, add anchor time periods (select periods when CO2 should be stable)
4. Click "Apply Drift Compensation" to see results

## Serial Commands

| Command | Function |
| ------- | -------- |
| auto | Resume automatic monitoring mode |
| stop | Pause automatic, enter manual mode |
| single | Single measurement (no upload in manual mode) |
| vent | Start ventilation function |
| status | View current status |

## Configuration Parameters

Adjustable in `config.h`:

| Parameter | Default | Description |
| --------- | ------- | ----------- |
| PUMP_DUTY_MEASURE | 120 | Sampling pump PWM duty (0-255) |
| PUMP_DUTY_VENT | 150 | Ventilation pump PWM duty |
| PUMP_TIME_MEASURE | 25 | Sampling pump run time (seconds) |
| PUMP_TIME_VENT | 50 | Ventilation pump run time (seconds) |
| SENSOR_STABILIZE_TIME | 20 | Pressure stabilization wait time (seconds) |
| INTERVAL_SECONDS | 600 | Measurement interval (fixed 10min, aligned)* |

> *Note: Modifying `INTERVAL_SECONDS` requires also modifying the alignment logic. See `getNextAlignedEpoch()` function in source code.

## Project Structure

```
esp32-co2-drift-compensator/
├── firmware/              # ESP32 Firmware
│   ├── src/
│   │   ├── main.cpp
│   │   └── config.h.example
│   └── platformio.ini
├── web/                   # Web Frontend + PHP Backend
│   ├── index.html         # Data visualization page
│   ├── get_data.php       # Data query API
│   ├── sensor_data.php    # Data receiving API
│   └── config.php.example
├── data/
│   └── sample_data.sql    # Sample data (Jan 3-9 test)
├── docs/
│   ├── HARDWARE.md        # Hardware wiring guide
│   └── images/
└── README.md
```

## Use Cases

This project was originally developed for monitoring algae respiration in sealed chambers, but some ideas of this project is applicable to any scenario with:

- Poor gas circuit sealing
- Long-term continuous CO2 monitoring needs
- Sensor long-term drift issues

## Tech Stack

- Firmware: PlatformIO + Arduino Framework
- Sensor: ZG09SR (Modbus RTU)
- Frontend: ECharts + Vanilla JS
- Backend: PHP + MySQL
- Hardware: ESP32-S3 + PWM Pump Control

## License

MIT License - See [LICENSE](LICENSE)
