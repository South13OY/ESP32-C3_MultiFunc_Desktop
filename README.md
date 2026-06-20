# ESP32-C3_MultiFunc_Desktop 多功能桌面摆件

一个基于 ESP32-C3 的桌面小电子，集时钟、天气、GIF 动画、小游戏、心率监测于一体，使用 128x64 OLED 屏幕和四个按钮，带图标菜单和 WiFi 配网门户。

## 功能特性

- **时钟**  
  采用数字翻页动画效果，NTP 自动校时，显示日期和星期，WiFi 断连时显示提示符号。

- **天气**  
  使用心知天气 API，自动获取指定城市天气（温度、天气描述），每 30 分钟更新。

- **GIF 动画播放**  
  支持自定义 28 帧 128x64 单色位图动画，可作为桌面小摆件循环播放。

- **恐龙跳跃游戏**  
  致敬 Chrome 小恐龙，支持 1-5 级难度，动态加速，得分显示，可重玩。

- **健康监测**  
  连接 MAX30102 心率血氧传感器，实时显示心率和血氧饱和度，抗干扰波峰检测算法避免二倍心率。

- **WiFi 配置门户**  
  无网时自动创建 AP 热点 ESP32_Config，手机连接后自动弹出网页，输入 WiFi 密码和天气城市，保存后重启自动连接。

- **图标菜单导航**  
  32x32 单色图标水平滚动，选中项带方框，左右键切换，确认进入，返回键退出。

## 硬件需求

- 主控: ESP32-C3 超级版 (SuperMini)
- 屏幕: SSD1306 128x64 I2C OLED（地址 0x3C）
- 传感器: MAX30102 心率血氧模块（可选）
- 按钮: 4个轻触开关
- 连接:
- OLED I2C: SDA=GPIO1, SCL=GPIO0
- 按钮: GPIO2 (左), GPIO3 (右), GPIO4 (返回), GPIO5 (确认/跳跃)
- 供电: USB 5V

## 软件依赖

- Arduino IDE（或 PlatformIO）
- 需要安装以下库：
- U8g2 by olikraus
- ArduinoJson by Benoit Blanchon
- SparkFun MAX3010x Pulse and Proximity Sensor Library
- Preferences (ESP32 内置)
- HTTPClient (ESP32 内置)
- WebServer (ESP32 内置)
- DNSServer (ESP32 内置)
- Wire (内置)

## 快速开始

1. 克隆仓库
   git clone https://github.com/South13OY/ESP32-C3_MultiFunc_Desktop.git

2. 修改配置（可选）
   在代码中可直接修改默认天气城市 weatherLocation，或通过配网页面设置。

3. 填入图标和动画位图
   替换 bitmap_01 … bitmap_28 数组为你的 128x64 单色位图，以及五个菜单图标数组。可使用工具如 PCtoLCD2002 生成。

4. 编译上传
   选择 ESP32C3 Dev Module，上传速度 921600 或 115200。

5. 首次配网
   若没有已保存 WiFi，设备将自动创建热点 ESP32_Config，手机连接后浏览器访问任意网址(192.168.4.1)，填写 WiFi 密码和天气城市，保存后设备自动重启。

## 操作说明

| 按钮 | 功能 |
|------|------|
| 左/右 | 菜单选择、游戏难度调节 |
| 确认 | 进入功能、恐龙跳跃、开始游戏 |
| 返回 | 返回菜单、退出当前功能 |

## 项目结构

ESP32-C3_MultiFunc_Desktop/
├── ESP32-C3_MultiFunc_Desktop.ino        # 主程序
├── README.md
├── images/                # 效果图、GIF
└── tools/                 # 位图生成工具说明

## 许可证

本项目采用 MIT 许可证，详情见 LICENSE 文件。

## 鸣谢

- 心知天气 API
- 多位 GitHub 用户共享的恐龙游戏思路
- SparkFun MAX3010x 库
