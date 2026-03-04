# ESP32 闭环式 CO2 监测系统（带漂移补偿）

[English Documentation](README_EN.md)

基于 ESP32 的闭环气泵式 CO2 监测系统，适用于密封腔体（如微生物培养瓶），内置多锚点漂移补偿算法弥补低成本装置气密性不足导致的数据漂移。

## 效果演示

| 原始数据（泄漏导致持续下降） | 补偿后数据（还原真实波动） |
| ---------------------------- | -------------------------- |
| ![原始](docs/images/before_compensation.png) | ![补偿后](docs/images/after_compensation.png) |

## 核心特性

### 多锚点漂移补偿算法

**问题**：密封气路不可避免会随时间泄漏，导致 CO2 读数出现系统性下降趋势，掩盖真实的生物活动信号。

**解决方案**：前端实现的非线性漂移补偿算法：

1. **锚点选择（周期闭环法）**：选择完整光暗周期的起止点作为锚点。理论上经过一个完整周期后，CO2 应回归相近水平；实测差值即为该周期内的累积漂移。本质上，漏气使原本的周期性波动叠加了一个下降趋势，补偿算法的作用就是将倾斜的基线抬平，还原真实的生物活动信号
   > *注意：周期闭环法误差较大，仅适用于漂移趋势明显可辨的场景，用于还原大致的周期性波动图像，不建议用于精密定量分析

2. **斜率计算**：对每个锚点时段进行线性回归，提取漂移速率（ppm/h）
3. **插值过渡**：锚点之间线性插值，实现漂移速率平滑过渡
4. **累积修正**：对漂移速率积分，从原始读数中减去累积漂移量

**数学表达**：

- 锚点漂移速率（线性回归斜率）：
 
 $$slope = \frac{n \sum x_i y_i - \sum x_i \sum y_i}{n \sum x_i^2 - (\sum x_i)^2}$$

- 修正值（累积积分）：
 
 $$CO_{2,corrected} = CO_{2,raw} - \int_0^t slope(\tau) \, d\tau$$

### 时间戳对齐与NTP同步

该机制牺牲了时间戳与实际采样时刻的绝对匹配，但换来了整齐的时间戳序列和更低的NTP访问频率。

系统通过双重机制确保数据时间戳的准确性与均匀性：

1. **NTP时间同步**：每次测量完成后（以及开机、断电恢复时），系统与NTP服务器校准时间并计算下一个10分钟整点作为下次采集时间，纠正内部RTC的累积漂移（嵌入式晶振误差可能导致每天数秒至数十秒的偏差）
2. **整点对齐**：时间戳对齐到10分钟整点（`00:00:00`, `00:10:00`, `00:20:00`...）。单次测量耗时约45秒，但上传的时间戳是**计划采集时间**而非完成时间

**时间戳对齐的意义**：

| 潜在问题 | 未对齐时的影响 | 对齐机制的作用 |
| -------- | -------------- | -------------- |
| RTC累积漂移 | 运行一周后时间戳可能偏差数分钟，数据与真实事件（如日出日落）产生错位 | 每次采集后NTP校准，确保长期运行的时间精度 |
| 多设备数据融合 | 不同设备时间基准不一致，无法进行横向对比分析 | 统一时间基准，支持多源数据的直接叠加与对比 |
| 数据索引效率 | 时间戳分散（`14:07:23`, `14:17:41`...），检索与归档效率低 | 规整时间戳（`14:00:00`, `14:10:00`...），便于索引与可视化 |

### 气泵闭环采样

PWM 控制气泵进行闭环气路采样：

1. 气泵循环（可调时长和强度）
2. 停泵，等待气压稳定
3. 读取传感器数据
4. 实时上传服务器

### 自动启动运行

- 上电自动启动，无需人工干预
- 多 WiFi 热点支持，自动切换
- 断电恢复后自动继续
- 时间戳自动对齐到整点

### 完整物联网数据流

```
ESP32 采样 → HTTP 上传 → PHP 存储 → ECharts 可视化
```

## 硬件需求

| 组件 | 型号 | 说明 |
| ---- | ---- | ---- |
| 主控 | ESP32-S3-DevKitC-1 | 主控制器 |
| CO2 传感器 | ZG09SR | NDIR 红外，Modbus RTU |
| 气泵 | 5V 微型气泵 | PWM 控制，闭环采样 |
| 气泵驱动 | MOS 驱动模块（四脚） | PWM 信号放大 |
| 温湿度 | DHT22/SHT30 等 | **预留接口，暂未使用** |

### 引脚连接

| ESP32 引脚 | 连接设备 |
| ---------- | -------- |
| GPIO 18 | MOS 驱动模块 PWM（气泵控制） |
| GPIO 16 | ZG09SR RX |
| GPIO 17 | ZG09SR TX |

详见 [硬件文档](docs/HARDWARE.md) 获取完整接线说明。

## 快速开始

### 1. 固件配置

```bash
cd firmware
cp src/config.h.example src/config.h
# 编辑 config.h，填入个人的需求配置
```

使用 PlatformIO 编译上传：

```bash
pio run -t upload
```

### 2. 服务器部署

```bash
cd web
cp config.php.example config.php
# 编辑 config.php，填入数据库信息
```

导入示例数据（包含表结构和测试数据）：

```bash
mysql -u your_user -p co2data < data/sample_data.sql
```

或手动创建空表：

```sql
CREATE TABLE co2_measurements (
  id INT AUTO_INCREMENT PRIMARY KEY,
  timestamp DATETIME NOT NULL,
  co2_ppm INT NOT NULL,
  temperature FLOAT DEFAULT NULL,  -- 预留，暂未使用
  humidity FLOAT DEFAULT NULL,     -- 预留，暂未使用
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_timestamp (timestamp)
);
```

### 3. 使用漂移补偿

1. 浏览器打开 `index.html`
2. 选择时间范围查询数据
3. 在"漂移补偿"面板添加锚点时段
   - 选择 CO2 **变化率稳定**的时段（如夜间纯呼吸期、光合平衡期）
   - 系统会计算该时段的实测斜率，与预期斜率的差值即为漂移速率
4. 点击"应用漂移补偿"查看效果

## 串口命令

| 命令 | 功能 |
| ---- | ---- |
| auto | 恢复自动监测模式 |
| stop | 暂停自动，进入手动模式 |
| single | 单次测量（手动模式不上传） |
| vent | 启动通风功能 |
| status | 查看当前状态 |

## 配置参数

在 `config.h` 中可调整：

| 参数 | 默认值 | 说明 |
| ---- | ------ | ---- |
| PUMP_DUTY_MEASURE | 120 | 采样气泵 PWM 占空比 (0-255) |
| PUMP_DUTY_VENT | 150 | 通风气泵 PWM 占空比 |
| PUMP_TIME_MEASURE | 25 | 采样气泵运行时间（秒） |
| PUMP_TIME_VENT | 50 | 通风气泵运行时间（秒） |
| SENSOR_STABILIZE_TIME | 20 | 气压稳定等待时间（秒） |
| INTERVAL_SECONDS | 600 | 测量间隔（固定10分钟，对齐整点）* |

> *注意：修改 `INTERVAL_SECONDS` 需同时修改对齐逻辑，参见源码中 `getNextAlignedEpoch()` 函数。单次完整测量耗时应远小于测量间隔，否则可能导致周期重叠。

## 项目结构

```
esp32-co2-drift-compensator/
├── firmware/              # ESP32 固件
│   ├── src/
│   │   ├── main.cpp
│   │   └── config.h.example
│   └── platformio.ini
├── web/                   # Web 前端 + PHP 后端
│   ├── index.html         # 数据可视化页面
│   ├── get_data.php       # 数据查询 API
│   ├── sensor_data.php    # 数据接收 API
│   └── config.php.example
├── data/
│   └── sample_data.sql    # 示例数据（测试数据）
├── docs/
│   ├── HARDWARE.md        # 硬件接线指南
│   └── images/
└── README.md
```

## 应用场景

本项目最初为监测密封腔体内藻类呼吸而开发，但该项目的一些思路适用于其他存在以下情况的场景：

- 气路密封性较差
- 需要长期连续监测 CO2 或其他传感数据
- 传感器长期漂移问题

## 技术栈

- 固件：PlatformIO + Arduino 框架
- 传感器：ZG09SR（Modbus RTU）
- 前端：ECharts + 原生 JS
- 后端：PHP + MySQL
- 硬件：ESP32-S3 + PWM 气泵控制

## 许可证

MIT License - 详见 [LICENSE](LICENSE)
