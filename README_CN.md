# HerStory Bloom：T5AI AHT20 + BMP280 环境传感器语音花盆

本 demo 位于 TuyaOpen 的 `apps/tuya.ai/herstory-bloom` 目录；仓库名称为
`herstory-bloom`。它依赖 TuyaOpen 框架本身，不能脱离 TuyaOpen 根目录单独编译。

本工程适用于 T5-BOARD-3956LCD（Tuya T5AI Board + 3.5 英寸屏）以及
AHT20 + BMP280 二合一 I²C 模块。板端读取当前温度、相对湿度和气压，
AI Agent 通过本地 MCP 工具取得数据，再由扬声器用中文回答。

## 接线

接线前先断开开发板 USB 电源，并以模块 PCB 上的丝印为准：

| AHT20 + BMP280 模块 | T5AI Board |
| --- | --- |
| `VIN` / `VCC` | `3V3` |
| `GND` | `GND` |
| `SCL` | P11 排针的 `P00` / GPIO0 / I2C1 SCL |
| `SDA` | P11 排针的 `P01` / GPIO1 / I2C1 SDA |

模块必须使用 3.3V 逻辑。AHT20 默认地址为 `0x38`；程序会自动检测地址为
`0x76` 或 `0x77` 的 BMP280。程序使用红框 P11 排针引出的 I²C1，不占用
触摸屏内部的 I²C0。

## 编译与烧录

```bash
cd /Users/jchen/Documents/boshanlu_proj/TuyaOpen
. ./export.sh
cd apps/tuya.ai/herstory-bloom
tos.py build
tos.py flash
```

## 对话调用

按住板上的对话键并说话，松开后等待回答；如果当前固件使用唤醒模式，也可以先说
“你好涂鸦”再提问。以下问题都会触发板端 `read_environment_sensor` 工具：

- “现在温度是多少？”
- “现在湿度是多少？”
- “当前气压是多少？”
- “室内环境怎么样？”
- “现在热不热？”
- “房间里是不是太潮了？”

询问单项时只回答对应数据；询问整体环境时回答温度、相对湿度和气压。

## 查看日志

```bash
tos.py monitor -p /dev/cu.usbmodem5AAE1668883 -b 460800
```

传感器初始化成功时会看到：

```text
environment sensors ready: AHT20=0x38, BMP280=0x76, I2C1 SCL=P00/GPIO0 SDA=P01/GPIO1
environment MCP tool registered
```

每次语音查询后会输出类似：

```text
environment: temperature=25.31C, humidity=52.64%, pressure=100842Pa
```

关键实现位于 `src/environment_sensor.c` 和 `src/environment_mcp.c`。
