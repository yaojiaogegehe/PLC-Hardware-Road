# OTA 改动说明

## 改动点

- 分区表由 `huge_app.csv` 改为 `min_spiffs.csv`，启用 app0/app1 双槽。
- 使用内置 `Update`，新增 `POST /update` 裸二进制上传，无第三方依赖。
- 网页底部增加版本显示、bin 选择、上传按钮和中文状态。
- OTA 开始立即停车，OTA 期间 `/api` 返回 503；成功后延时 500ms 自动重启。
- 80 端口 httpd 任务栈由默认 4096 调为 8192（`config.stack_size = 8192`）：修复 POST /update 在进 handler 前断连，已经 1 字节 POST 与完整 OTA 实测验证。
- 引脚、电机映射、正常控制接口和 81 端口视频流未改。

## 用户无线更新流程

1. 手机/电脑连接 WiFi `FPV-Car`，打开 `http://192.168.4.1`。
2. 展开页面底部“固件更新”，选择 `.pio/build/esp32cam/firmware.bin`。
3. 点击“上传固件”，等待成功提示和自动重启。
4. 必要时重新连接热点，核对页面版本号确认更新生效。
5. 注意：同一时间只从一台设备上传固件，不要多设备同时更新。

## 风险

首次双槽固件必须串口烧录。刷入坏固件可能导致热点无法启动，需要 USB 转 TTL 救砖。更新中不能断电。

## 验收记录

| 项目 | 结果 |
| --- | --- |
| 编译 | 通过（`platformio run` SUCCESS，firmware.bin 约 879 KB / 879088 字节） |
| 首次串口烧录（MB 底板，COM3） | 通过（`Hash of data verified`，SUCCESS，写入 879088 字节） |
| 热点 `FPV-Car` 可见 | 通过（用户确认已连上热点并打开网页） |
| 网页打开与版本显示 | 通过（用户确认页面显示完整 FW_VERSION） |
| 实时视频、三档速度、画质、停止 | 通过（用户确认视频与控制正常） |
| 电机前进/后退/组合转向 | 未验证（电机实测未执行） |
| 第二版固件编译（FW_VERSION 改为 v2.1-ota2） | 通过（`platformio run` SUCCESS，bin 879088 字节，Flash 占用 44.4%） |
| OTA 上传、成功提示、自动重启 | 通过（用户报告“这次成功了”；串口全程打点：content_len=879392 → Update.begin 成功 → 进度 131648~786464 → 接收完成 879392 → OTA 成功 → rst:0xc SW_CPU_RESET 自动重启）。首次尝试曾失败（Failed to fetch、热点消失），根因定位为 httpd 栈不足，8192 后修复 |
| 串口诊断固件（v2.1-ota2 + OTA 打点） | 通过（烧录 879328 字节，Hash of data verified，SUCCESS） |
| 第三版 OTA 候选固件（v2.1-ota3 + OTA 打点）编译 | 通过（`platformio run` SUCCESS，bin 879328 字节 < 1,800,000） |
| 路由注册诊断固件（v2.1-ota3 + 注册打点）串口烧录 | 通过（写入 879392 字节，Hash of data verified，SUCCESS 18.67 秒） |
| 第四版 OTA 候选固件（v2.1-ota4）编译 | 通过（`platformio run` SUCCESS 8.65 秒，bin 879392 字节 < 1,800,000） |
| 栈单变量诊断固件（v2.1-ota4 + httpd 栈 8192）编译与烧录 | 编译/烧录通过（SUCCESS 8.29 秒；写入 879392 字节，Hash verified，18.66 秒） |
| 栈修复验证（1 字节 POST /update） | 通过：HTTP 500 + Update 错误码 8（预期），串口见 handler 入口打点与 Update.begin 成功，栈假设成立 |
| 第五版 OTA 候选固件（v2.1-ota5 + 栈 8192）编译 | 通过（`platformio run` SUCCESS 8.32 秒，bin 879392 字节 < 1,800,000） |
| OTA 后重启与网络恢复 | 通过（串口：重启后摄像头 OK、热点启动、路由注册结果=0、就绪；上传期间的 cam_hal: EV-VSYNC-OVF 未中断写入或重启流程） |
| OTA 后版本变化与视频正常 | 通过（用户报告成功；控制器核对串口启动日志正常） |
| OTA 后电机控制 | 未验证（电机实测未执行） |
| 上传期间停车（OTA 期间电机不转） | 未验证（电机实测未执行） |

## 测试序列（OTA 闭环已完成，电机项未验证）

1. 诊断固件 v2.1-ota2（含打点）已串口烧录到板子：`update_handler` 首动作仍为停车并置 `otaInProgress`，随后打印 content_len、`Update.begin` 成功、每 128KiB 接收进度、接收完成；`ota_fail` 原有错误日志不变。
2. 候选固件 v2.1-ota3 已编译，位于 `.pio/build/esp32cam/firmware.bin`。
3. 受控实测：GET / 正常（v2.1-ota2）；完整固件与 1 字节原始 POST /update 均在进入 handler 前断开（10054/直接关闭），无 handler 入口日志 → 排除固件体积与浏览器因素。
4. 路由注册打点（开机打印 `OTA /update 路由注册结果=`，`src/main.cpp:492-493`）的 v2.1-ota3 诊断固件已串口烧录成功：用户拔插 USB 释放 COM3 后一次上传通过，写入 879392 字节，Hash of data verified，SUCCESS（18.67 秒）。无栈证据前不调整栈大小。
5. v2.1-ota4 候选已编译：`platformio run` SUCCESS（8.65 秒），firmware.bin 879392 字节（< 1,800,000）。
6. boot 日志已确认 `OTA /update 路由注册结果=0`（路由注册成功）；完整与 1 字节 POST /update 仍在进 handler 前断开。
7. 单变量栈测试：假设 httpd 默认栈 4096 不足以承载含 2048 字节栈缓冲的 /update 请求路径，仅将 `startCameraServer()` 80 端口启动前配置改为 `config.stack_size = 8192;`（版本仍为 v2.1-ota4，其余未动）。编译 SUCCESS（8.29 秒，bin 879392 字节），串口烧录一次成功（879392 字节，Hash verified，18.66 秒）。
8. 栈假设**验证通过**：控制器 1 字节 POST /update 返回 HTTP 500（Update 错误码 8，无效固件符合预期），串口顺序为 `OTA 请求到达，content_len=1` → `Update.begin 成功` → 预期写入/格式失败，handler 可进入并回应。保留 stack_size = 8192。
9. v2.1-ota5 OTA 候选已编译：`platformio run` SUCCESS（8.32 秒），`.pio/build/esp32cam/firmware.bin` = 879392 字节（< 1,800,000）。未串口烧录。
10. **完整 OTA 闭环实测通过**（用户网页上传 v2.1-ota5，报告“这次成功了”；控制器核对串口）：`OTA 请求到达，content_len=879392` → `Update.begin 成功` → 接收进度 131648 至 786464 字节 → `接收完成 879392` → `OTA 成功，准备重启` → `rst:0xc (SW_CPU_RESET)` → 摄像头 OK、热点启动、`路由注册结果=0`、就绪。上传期间出现 `cam_hal: EV-VSYNC-OVF`，未中断写入或重启流程。
11. 收尾编译核对：`platformio run` SUCCESS（3.69 秒），RAM 17.2%（56244 字节），Flash 44.4%（872817 / 1966080 字节），firmware.bin = 879392 字节（< 1,800,000）。
12. 故障排查结论：POST /update 断连根因为 httpd 默认栈 4096 不足（handler 含 2048 字节栈缓冲），`config.stack_size = 8192` 修复并经 1 字节 POST 与完整 OTA 双重验证，保留该设置。电机相关各项（前进/后退/转向、上传期间停车）仍未实测。
