# ESP32 Closed-Loop CO2 Monitoring System (with Drift Compensation)

[中文文档](README.md)

An ESP32-based closed-loop pump-driven CO2 monitoring system for sealed chambers (e.g., microbial culture vessels), featuring a multi-anchor drift compensation algorithm to address data drift caused by inadequate gas-tightness in low-cost setups.

## Demo

| Raw Data (Continuous drop due to leakage) | Compensated Data (Restored true fluctuations) |
| ----------------------------------------- | --------------------------------------------- |
| ![Raw](docs/images/before_compensation.png) | ![Compensated](docs/images/after_compensation.png) |

## Core Features

### Multi-Anchor Drift Compensation Algorithm

**Problem**: Sealed gas circuits inevitably leak over time, causing a systematic downward drift in CO2 readings that masks the true biological activity signal.

**Solution**: A non-linear drift compensation algorithm implemented in the frontend:

1. **Anchor Selection (Cycle-Closure Method)**: Select start and end points of a complete light-dark cycle as anchors. Theoretically, after one complete cycle, CO2 should return to a similar level; the measured difference represents cumulative drift within that cycle. Essentially, gas leakage superimposes a downward trend onto the periodic biological fluctuations; the compensation algorithm levels this tilted baseline to restore the true biological signal
   > *Note: The cycle-closure method has considerable error margin. It is only suitable for scenarios where drift trends are clearly discernible, aimed at restoring approximate periodic waveforms rather than precise quantitative analysis

2. **Slope Calculation**: Linear regression on each anchor period to extract drift rate (ppm/h)
3. **Interpolated Transition**: Linear interpolation between anchors for smooth drift rate transitions
4. **Cumulative Correction**: Integrate the drift rate over time, subtract accumulated drift from raw readings

**Mathematical Expression**:

- Anchor drift rate (linear regression slope):
 
 $$slope = \frac{n \sum x_i y_i - \sum x_i \sum y_i}{n \sum x_i^2 - (\sum x_i)^2}$$

- Corrected value (cumulative integral):
 
 $$CO_{2,corrected} = CO_{2,raw} - \int_0^t slope(\tau) \, d\tau$$

### Timestamp Alignment & NTP Synchronization

This mechanism trades off exact timestamp-to-sample correspondence for well-aligned timestamp sequences and reduced NTP query frequency.

The system ensures accurate and uniform timestamps through a dual mechanism:

1. **NTP Time Sync**: After each measurement (as well as on power-on or recovery from power loss), the system syncs with an NTP server and calculates the next 10-minute mark as the next sampling time, correcting accumulated RTC drift (embedded crystal oscillators can drift by several seconds to tens of seconds per day)
2. **Grid Alignment**: Timestamps are aligned to 10-minute boundaries (`00:00:00`, `00:10:00`, `00:20:00`...). A single measurement takes ~45 seconds, but the uploaded timestamp reflects the **scheduled time**, not the completion time

**Significance of Timestamp Alignment**:

| Potential Issue | Impact Without Alignment | Role of Alignment Mechanism |
| --------------- | ------------------------ | --------------------------- |
| RTC Cumulative Drift | After a week, timestamps may drift by several minutes, causing temporal misalignment with real-world events (e.g., sunrise/sunset) | NTP calibration after each measurement ensures long-term time accuracy |
| Multi-device Data Fusion | Inconsistent time bases across devices preclude cross-comparison analysis | Unified time reference enables direct overlay and comparison of multi-source data |
| Data Indexing Efficiency | Scattered timestamps (`14:07:23`, `14:17:41`...) reduce retrieval and archival efficiency | Regularized timestamps (`14:00:00`, `14:10:00`...) facilitate indexing and visualization |

### Closed-Loop Pump Sampling

PWM-controlled pump for closed-loop gas circuit sampling:

1. Pump circulation (adjustable duration and intensity)
2. Stop pump, wait for pressure stabilization
3. Read sensor data
4. Real-time upload to server

### Automatic Startup

- Power-on auto-start, no manual intervention required
- Multi-WiFi hotspot support with automatic switching
- Auto-resume after power recovery
- Automatic timestamp alignment to fixed intervals

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
| Pump Driver | MOS Driver Module (4-pin) | PWM signal amplification |
| Temp/Humidity | DHT22/SHT30 etc. | **Reserved interface, not yet used** |

### Pin Connections

| ESP32 Pin | Connected Device |
| --------- | ---------------- |
| GPIO 18 | MOS Driver Module PWM (Pump Control) |
| GPIO 16 | ZG09SR RX |
| GPIO 17 | ZG09SR TX |

See [Hardware Documentation](docs/HARDWARE.md) for complete wiring instructions.

## Quick Start

### 1. Firmware Configuration

```bash
cd firmware
cp src/config.h.example src/config.h
# Edit config.h with your personal configuration
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

Import sample data (includes table structure and test data):

```bash
mysql -u your_user -p co2data < data/sample_data.sql
```

Or manually create an empty table:

```sql
CREATE TABLE co2_measurements (
  id INT AUTO_INCREMENT PRIMARY KEY,
  timestamp DATETIME NOT NULL,
  co2_ppm INT NOT NULL,
  temperature FLOAT DEFAULT NULL,  -- Reserved, not yet used
  humidity FLOAT DEFAULT NULL,     -- Reserved, not yet used
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_timestamp (timestamp)
);
```

### 3. Using Drift Compensation

1. Open `index.html` in your browser
2. Select a time range to query data
3. In the "Drift Compensation" panel, add anchor periods
   - Select periods where CO2 **rate of change is stable** (e.g., nighttime respiration-only period, photosynthesis equilibrium)
   - The system calculates the measured slope; the difference from expected slope is the drift rate
4. Click "Apply Drift Compensation" to see the result

## Serial Commands

| Command | Function |
| ------- | -------- |
| auto | Resume automatic monitoring mode |
| stop | Pause automatic mode, enter manual mode |
| single | Single measurement (manual mode, no upload) |
| vent | Start ventilation function |
| status | View current status |

## Configuration Parameters

Adjustable in `config.h`:

| Parameter | Default | Description |
| --------- | ------- | ----------- |
| PUMP_DUTY_MEASURE | 120 | Sampling pump PWM duty cycle (0-255) |
| PUMP_DUTY_VENT | 150 | Ventilation pump PWM duty cycle |
| PUMP_TIME_MEASURE | 25 | Sampling pump run time (seconds) |
| PUMP_TIME_VENT | 50 | Ventilation pump run time (seconds) |
| SENSOR_STABILIZE_TIME | 20 | Pressure stabilization wait time (seconds) |
| INTERVAL_SECONDS | 600 | Measurement interval (fixed 10 min, grid-aligned)* |

> *Note: Modifying `INTERVAL_SECONDS` requires corresponding changes to the alignment logic. See `getNextAlignedEpoch()` function in source code. A single complete measurement duration should be significantly shorter than the measurement interval to prevent cycle overlap.

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
│   ├── sensor_data.php    # Data reception API
│   └── config.php.example
├── data/
│   └── sample_data.sql    # Sample data (test data)
├── docs/
│   ├── HARDWARE.md        # Hardware wiring guide
│   └── images/
└── README.md
```

## Application Scenarios

This project was originally developed to monitor algae respiration in sealed chambers, but the concepts are applicable to other scenarios involving:

- Poor gas circuit sealing
- Long-term continuous CO2 or other sensor data monitoring
- Sensor long-term drift issues

## Tech Stack

- Firmware: PlatformIO + Arduino Framework
- Sensor: ZG09SR (Modbus RTU)
- Frontend: ECharts + Vanilla JS
- Backend: PHP + MySQL
- Hardware: ESP32-S3 + PWM Pump Control

## License

MIT License - See [LICENSE](LICENSE)
