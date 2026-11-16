# OTA 无线刷机改造 · 任务交接文档

> 接手前请通读本文件。本工程属于一位零基础用户，所有"禁区"都是血泪定稿，违反任何一条都会导致整车返工。

## 0. 背景（一句话）

ESP32-CAM 四驱 FPV 小车固件，功能已定型并实车验证。用户即将把杜邦线**焊死**在 ESP32-CAM 的邮票孔上，之后串口烧录口将永久失效，因此必须在焊死前给固件加上 **OTA（浏览器上传 .bin 无线刷机）** 功能。

## 1. 工程现状（动手前必读）

| 项 | 现状 |
|---|---|
| 工程 | `D:\PLC\小车\`，PlatformIO，唯一 env：`esp32cam`，固件为单文件 `src/main.cpp` |
| 平台 | `platform = espressif32@6.7.0`（= Arduino 内核 2.0.x，**锁死，勿升 3.x**，固件用了旧版 ledc API） |
| 分区表 | 当前 `huge_app.csv`（单 3MB 槽，**无 OTA 双槽**）→ **必须改为 `min_spiffs.csv`**，否则 Update 库无槽可写 |
| HTTP 服务 | **ESP-IDF 原生 `esp_http_server`**（不是 Arduino WebServer），80 端口控制/网页，81 端口视频流（两个 httpd 实例） |
| 现有路由 | 80：`GET /`（网页）、`GET /api?dir=&spd=`（行车指令）、`GET /control?var=&val=`（画质等）、`GET /capture`；81：`GET /stream` |
| 网络 | AP 模式：SSID `FPV-Car`，密码 `12345678`，固定 `192.168.4.1` |
| 引脚铁律（一行不许动） | `IO12→ENA · IO13→IN1 · IO15→IN2 · IO14→ENB · IO2→IN3 · IO4→IN4`；ENA+IN1+IN2=左侧电机组，ENB+IN3+IN4=右侧；**GPIO16 被 PSRAM 占用不可用** |
| 网页前端 | 内嵌在 main.cpp 中的 HTML/JS（约 100~260 行区域），中文界面，方向键+速度档+流畅/清晰切换 |

## 2. 任务目标

网页底部新增"固件更新"区：选本地 `firmware.bin` → 上传 → 写入 flash → 自动重启 → 新版本生效。全程不拆线、不走串口。

## 3. 实现要求

1. **platformio.ini**：`board_build.partitions` 改为 `min_spiffs.csv`，其余行不动
2. **main.cpp**：
   - `#include <Update.h>`（内置库，不要引入任何第三方库）
   - 新增版本宏，如 `#define FW_VERSION "v2.1 "__DATE__" "__TIME__`，并**在网页上显示**——用户要肉眼确认 OTA 是否生效
   - 新增 `POST /update` handler：按 `req->content_len` 用 `httpd_req_recv()` 循环接收（缓冲区 1~2KB），`Update.begin(len)` + `Update.write()`，完成后 `Update.end(true)`；失败时把错误码输出到串口和 HTTP 响应
   - **收到 /update 请求的第一件事：停车**（置电机 S，调用现有停止逻辑），防止刷机过程中车乱窜
   - 成功后响应 "OK"，`delay(500)` 后 `ESP.restart()`
   - 前端用 `fetch('/update', {method:'POST', body: file})` **裸二进制上传**（浏览器 File 对象直接 POST，**不要做 multipart 解析**，最简最稳）；按钮放页面底部、加中文状态提示（上传中 / 成功，正在重启 / 失败原因）
3. 若上传中途断连或复位：尝试把该 httpd 实例 `config.stack_size` 提到 8192 再测

## 4. 禁区（违反=返工）

- 不改引脚定义、不改电机驱动逻辑（左右侧归属已对实车验证）
- 不改 `/api`、`/control` 的接口格式（手机端遥控网页已定型在用）
- 不升 platform / Arduino 内核版本
- 不动 81 端口视频流
- 不引入第三方库；不改动工程外其他目录

## 5. 验收标准（逐条过）

1. `pio run` 编译通过；确认 `firmware.bin` 体积 **< 1.8MB**（min_spiffs 单槽上限约 1.9MB）
2. 串口烧录后实机验证：AP 出现 → 网页正常 → 视频流正常 → `/api` 能让电机转 → 页面显示新版本号
3. **OTA 闭环**：改一下版本号重新 `pio run`，浏览器上传新 bin → 自动重启 → 页面版本号变化 → 电机仍受控
4. 上传过程中电机保持停止

## 6. 交付物

- 修改后的 `main.cpp` + `platformio.ini`（就在本工程内改）
- 本目录下新增 `OTA改动说明.md`：改了哪些点、验收结果（逐条对应第 5 节）
- 待上传的 bin 路径：`.pio/build/esp32cam/firmware.bin`

## 7. 用户使用流程（写进改动说明，照抄即可）

1. 手机/电脑连 WiFi `FPV-Car` → 浏览器开 `http://192.168.4.1`
2. 页面底部"固件更新"→ 选择 `firmware.bin` → 点上传
3. 等 10~20 秒提示成功，车自动重启，**核对页面版本号**确认更新生效

## 8. 备注

- 分区表从 huge_app 切到 min_spiffs 后，**首次烧录必须走串口**（用户手头的 MB 底板）——这正是本次任务存在的原因；之后的每次更新才走 OTA
- 若 OTA 刷入损坏固件导致无法连 WiFi（变砖），属预期内风险，救砖手段是 ¥5 的 USB 转 TTL 模块，不在本任务范围
