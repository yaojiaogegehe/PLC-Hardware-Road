# ESP32-CAM 浏览器 OTA 更新设计

日期：2026-11-16

## 背景

用户后续会拆掉 MB 下载底板，并将电机信号线固定到 ESP32-CAM。串口烧录将不再方便，因此需要在固定接线前实现浏览器上传 `firmware.bin` 的 OTA 功能，并完成至少一次完整 OTA 闭环。

当前工程为 PlatformIO 单文件固件 `src/main.cpp`，使用 Arduino 内核 2.0.16、ESP-IDF 原生 `esp_http_server`、80 端口控制页面和 81 端口视频流。当前 `huge_app.csv` 没有 OTA 应用槽，必须更换分区表。

## 已确认约束

- `platform = espressif32@6.7.0` 不升级。
- 不引入第三方库，使用内核自带 `Update`。
- 引脚、电机左右归属和组合转向逻辑不变。
- 正常行车时 `/api`、`/control` 的接口格式不变。
- 不修改 81 端口视频流。
- 首次分区切换使用 MB 底板串口烧录；之后使用 WiFi OTA。
- 用户接受坏固件可能导致无法开热点、需要 USB 转 TTL 救砖的风险。

## 方案比较与选择

1. 采用内置 `Update` 和 `POST /update` 裸二进制上传。满足浏览器选择 bin、无新增依赖和现有原生 HTTP 服务器集成要求，采用此方案。
2. ArduinoOTA 网络端口依赖支持网络烧录的客户端，不满足手机浏览器上传目标，不采用。
3. ElegantOTA 等第三方组件增加依赖，超出交接文档约束，不采用。

## 文件改动

- `platformio.ini`：仅将 `board_build.partitions` 从 `huge_app.csv` 改为 `min_spiffs.csv`。
- `src/main.cpp`：加入 `Update.h`、版本宏、OTA 状态、`POST /update` handler、路由注册和网页更新区域。
- `OTA改动说明.md`：记录实现内容、用户操作步骤及逐项验收结果。
- 实施计划另行保存，不在本设计中展开任务步骤。

## 分区与容量

已核对安装包中的 `min_spiffs.csv`：

- `app0`：偏移 `0x10000`，大小 `0x1E0000`。
- `app1`：偏移 `0x1F0000`，大小 `0x1E0000`。
- 两个应用槽各 1,966,080 字节，包含 `otadata`，支持 `Update` 写入另一槽。

验收仍按交接文档采用更保守的 bin 小于 1.8MB 标准。当前固件约 0.84MiB，余量充足。

## 网页设计

页面底部增加可展开的“固件更新”区域，包含：

- 完整固件版本号。
- 仅用于选择 `.bin` 的文件输入框。
- 上传按钮。
- 中文状态：等待选择、上传中、成功正在重启、失败原因。

展开更新区域或开始上传时，前端先执行现有停车逻辑。上传使用：

```javascript
fetch('/update', {method: 'POST', body: file})
```

File 对象直接作为请求体，不进行 multipart 编码，不显示虚假进度百分比。成功响应后提示自动重启，用户重新连接热点并核对版本号。

版本通过编译期字符串拼接进入 `INDEX_HTML`，不增加额外版本请求接口。版本宏格式为：

```cpp
#define FW_VERSION "v2.1 " __DATE__ " " __TIME__
```

## 后端数据流

1. `POST /update` 收到请求后立即执行 `motorLeft(0); motorRight(0);`，并设置全局 OTA 进行中状态。
2. OTA 进行中时，`api_handler` 不执行新的行车动作，返回 `503 Service Unavailable`；正常状态下接口和参数格式保持不变。
3. 检查 `req->content_len` 非零且不大于 `0x1E0000` 的实际单槽容量；构建验收仍采用更保守的小于 1.8MB 标准。
4. 调用 `Update.begin(req->content_len)`。
5. 使用约 2KB 缓冲区循环调用 `httpd_req_recv()`，每次成功接收后调用 `Update.write()`，累计实际接收和写入字节数。
6. 任一步骤失败则终止接收并进入错误处理。
7. 接收总数必须等于 `content_len`，随后调用 `Update.end(true)`。
8. 成功后返回纯文本 `OK`，延时 500ms，调用 `ESP.restart()`。

## 错误处理

- 接收返回错误或提前结束、长度不符、`Update.begin/write/end` 失败时，调用 `Update.abort()`。
- 串口输出 `Update.getError()` 和可读错误信息；HTTP 返回 4xx 或 5xx 及中文原因/错误码。
- 更新失败时保持停车，不自动重启。前端显示后端返回的失败原因。
- 服务器栈先保持现有默认值；只有实际复现上传中断连或复位时，才将 80 端口 httpd 实例的 `config.stack_size` 调整为 8192 并复测。

## 安全与隔离

- OTA 开始和失败路径均保持电机关闭。
- 当前页面的行驶心跳不得在 OTA 期间重新启动电机；后端 OTA 状态同时防止其他客户端通过 `/api` 启动电机。
- 不修改现有电机方向映射、PWM 通道、摄像头初始化、视频流 handler 和正常画质接口。
- OTA 只供受信任的 `FPV-Car` 热点用户使用，本任务不增加账号认证或 Internet 暴露。

## 验收标准

1. `pio run` 成功，`.pio/build/esp32cam/firmware.bin` 小于 1.8MB。
2. 首次使用 MB 串口烧录双槽分区固件。
3. 上电后热点出现，网页和视频流正常，页面显示新版本号，`/api` 能控制电机。
4. 修改版本号并重新编译，通过浏览器上传新 bin。
5. 上传开始后电机保持停止；成功后自动重启。
6. 重新打开网页确认版本号改变，视频和电机控制仍正常。
7. 将逐项结果写入 `OTA改动说明.md`，未执行的项目明确标记为未验证。

## 后续使用

1. 手机或电脑连接 `FPV-Car`，打开 `http://192.168.4.1`。
2. 展开底部“固件更新”，选择 `.pio/build/esp32cam/firmware.bin`。
3. 点击上传并等待成功提示和自动重启。
4. 必要时重新连接热点，核对网页版本号确认更新生效。
