# ESP32-CAM 浏览器 OTA 更新 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 ESP32-CAM 现有遥控网页中加入浏览器上传 `firmware.bin` 的 OTA 功能，使拆掉 MB 底板后仍能无线更新固件。

**Architecture:** 保留现有原生 `esp_http_server`，在 80 端口实例增加 `POST /update`，使用 Arduino 内核内置 `Update` 写入另一 OTA 应用槽。前端直接 POST File 对象，后端在接收期间强制停车并拒绝行车命令，成功后自动重启。

**Tech Stack:** PlatformIO、espressif32@6.7.0、Arduino 2.0.16、ESP-IDF `esp_http_server`、内置 `Update`、HTML/CSS/JavaScript。

**Spec:** `docs/superpowers/specs/2026-11-16-ota-update-design.md`

## Global Constraints

- 平台固定为 `espressif32@6.7.0`，不升级 Arduino 内核。
- 不引入第三方库，不新增测试框架或测试文件；使用临时内联检查和实机验收。
- 引脚、电机左右归属、PWM、正常 `/api` 和 `/control` 格式、81 端口视频流不变。
- 分区仅改为 `min_spiffs.csv`，首次必须经 MB 串口烧录。
- OTA 期间保持停车，失败时不重启，成功响应后延时 500ms 重启。
- 当前目录不是 Git 仓库；不初始化仓库，不执行 commit。
- 实机验收需在空旷处架空车轮，先用休闲档；任何异常发热、异味或冒烟立即断电。

---

### Task 1: 双槽分区与可见固件版本

**Files:**
- Modify: `platformio.ini:9`
- Modify: `src/main.cpp` 的 include 区域和 `INDEX_HTML`

**Interfaces:**
- Produces: 编译期字符串 `FW_VERSION`；网页元素 `updatePanel`、`fwStatus`；双槽分区 `min_spiffs.csv`。

- [ ] **Step 1: 写失败的临时内联检查**

```bash
node -e 'const fs=require("fs"),assert=require("assert"); const ini=fs.readFileSync("platformio.ini","utf8"); const src=fs.readFileSync("src/main.cpp","utf8"); assert(ini.includes("board_build.partitions = min_spiffs.csv"),"min_spiffs 未启用"); assert(src.includes("#define FW_VERSION"),"FW_VERSION 未定义"); assert(src.includes("id=\"updatePanel\""),"更新区域缺失"); assert(src.includes("id=\"fwStatus\""),"状态元素缺失");'
```

- [ ] **Step 2: 运行检查并确认因功能缺失而失败**

Run：执行 Step 1。
Expected：`AssertionError`，第一项失败信息为 `min_spiffs 未启用`。

- [ ] **Step 3: 修改分区、版本宏和更新区域骨架**

`platformio.ini` 仅修改一行：

```ini
board_build.partitions = min_spiffs.csv
```

在 `src/main.cpp` include 区域后加入：

```cpp
#define FW_VERSION "v2.1 " __DATE__ " " __TIME__
```

在 `INDEX_HTML` 的 CSS 中加入：

```css
.update-panel{bottom:76px;left:50%;transform:translateX(-50%);z-index:4}
.update-panel summary{padding:9px 16px;border:1px solid rgba(255,255,255,.2);border-radius:999px;background:var(--glass);cursor:pointer;list-style:none;font-size:13px}
.update-panel summary::-webkit-details-marker{display:none}
.update-body{position:absolute;bottom:calc(100% + 8px);left:50%;transform:translateX(-50%);width:min(360px,90vw);padding:16px;border:1px solid rgba(255,255,255,.2);border-radius:16px;background:rgba(8,14,22,.96);text-align:left;font-size:13px}
#fwStatus{margin-top:10px;overflow-wrap:anywhere;color:rgba(255,255,255,.78)}
@media (orientation:portrait) and (max-width:700px){.update-panel{bottom:245px}}
```

把现有 `.key-hint` 从底部移到顶部，避免与更新区域重叠：

```css
.key-hint{top:48px;left:50%;transform:translateX(-50%);font-size:12px;color:rgba(255,255,255,.62);pointer-events:none;white-space:nowrap;text-shadow:0 1px 4px #000}
```

在 `quality` 按钮后加入以下 HTML。这里的关闭/重开 raw literal 是有意的编译期字符串拼接，不能删掉版本两侧引号：

```html
<details id="updatePanel" class="control update-panel">
 <summary>固件更新</summary>
 <div class="update-body">
  <div>固件版本：)rawliteral" FW_VERSION R"rawliteral(</div>
  <div id="fwStatus">请选择 firmware.bin</div>
 </div>
</details>
```

- [ ] **Step 4: 运行检查并编译**

Run：再次执行 Step 1，然后执行：

```bash
C:/Users/haoli/.platformio/penv/Scripts/platformio.exe run
```

Expected：内联检查无输出且退出码 0；PlatformIO 最后为 `SUCCESS`。

- [ ] **Step 5: 检查 bin 体积**

```bash
C:/Users/haoli/.platformio/penv/Scripts/python.exe -c 'import os; p=".pio/build/esp32cam/firmware.bin"; n=os.path.getsize(p); print(p,n,"bytes"); assert n < 1800000'
```

Expected：退出码 0，体积小于 1,800,000 字节。

---

### Task 2: 网页选择 bin、裸 POST 与停车状态

**Files:**
- Modify: `src/main.cpp` 的 `INDEX_HTML`

**Interfaces:**
- Consumes: Task 1 的 `updatePanel`、`fwStatus`、`FW_VERSION` 和现有 `stopAll()`。
- Produces: `updateFirmware()`；元素 `fwFile`、`fwUpload`；上传请求 `POST /update`，请求体为 File 本身。

- [ ] **Step 1: 写前端行为的失败检查**

以下检查直接提取生产代码中的 `updateFirmware()`，通过最小 DOM/fetch 替身执行真实函数：

```bash
node -e '
const fs=require("fs"),vm=require("vm"),assert=require("assert");
const src=fs.readFileSync("src/main.cpp","utf8");
const start=src.indexOf("function updateFirmware()");
assert(start>=0,"updateFirmware 缺失");
const end=src.indexOf("\n}\n",start);
const code=src.slice(start,end+2);
(async()=>{
 const file={name:"firmware.bin"}; let request=null; let stopped=false;
 const elements={fwFile:{files:[file]},fwUpload:{disabled:false},fwStatus:{textContent:""}};
 const context={document:{getElementById:id=>elements[id]},stopAll:()=>{stopped=true},fetch:(url,options)=>{request={url,options};return Promise.resolve({ok:true,text:()=>Promise.resolve("OK")});},Error,Promise};
 vm.runInNewContext(code+";updateFirmware",context);
 await context.updateFirmware();
 assert(stopped,"上传前未停车");
 assert.strictEqual(request.url,"/update");
 assert.strictEqual(request.options.method,"POST");
 assert.strictEqual(request.options.body,file);
 assert.strictEqual(elements.fwUpload.disabled,false);
 assert(elements.fwStatus.textContent.includes("成功"),"缺少成功提示");
 console.log("raw POST and stop-before-upload passed");
})().catch(e=>{console.error(e);process.exit(1);});
'
```

- [ ] **Step 2: 运行检查并确认失败**

Run：执行 Step 1。
Expected：`AssertionError: updateFirmware 缺失`。

- [ ] **Step 3: 添加文件控件和上传函数**

在更新区域的版本行之后、`fwStatus` 之前加入：

```html
<input id="fwFile" type="file" accept=".bin,application/octet-stream">
<button id="fwUpload" type="button">上传固件</button>
```

在 CSS 中加入：

```css
#fwFile{width:100%;margin-top:12px;font-size:12px}
#fwUpload{width:100%;margin-top:10px;padding:10px;border-radius:10px;background:#17657c}
#fwUpload:disabled{opacity:.5;cursor:wait}
```

在现有 `setfs()` 函数之后加入：

```javascript
function updateFirmware(){
  var file = document.getElementById('fwFile').files[0];
  var button = document.getElementById('fwUpload');
  var status = document.getElementById('fwStatus');
  if (!file) {
    status.textContent = '请先选择 firmware.bin';
    return;
  }
  stopAll();
  button.disabled = true;
  status.textContent = '上传中，请勿断电或关闭页面';
  return fetch('/update', {method:'POST', body:file}).then(function(response){
    return response.text().then(function(text){
      if (!response.ok) throw new Error(text || ('HTTP ' + response.status));
      status.textContent = '成功，正在重启。请稍后重新连接 FPV-Car 并核对版本号';
      button.disabled = false;
    });
  }).catch(function(error){
    stopAll();
    status.textContent = '失败：' + error.message;
    button.disabled = false;
  });
}
```

在网页脚本末尾、现有 `updateDrive(true);` 之前加入：

```javascript
document.getElementById('updatePanel').addEventListener('toggle', function(){
  if (this.open) stopAll();
});
document.getElementById('fwUpload').addEventListener('click', updateFirmware);
```

- [ ] **Step 4: 运行前端行为检查**

Run：再次执行 Step 1。
Expected：`raw POST and stop-before-upload passed`。

- [ ] **Step 5: 检查完整脚本语法**

```bash
node -e 'const fs=require("fs"),vm=require("vm"),assert=require("assert"); const src=fs.readFileSync("src/main.cpp","utf8"); const script=src.match(/<script>([\s\S]*?)<\/script>/); assert(script,"script 缺失"); new vm.Script(script[1]); console.log("JavaScript syntax passed");'
```

Expected：`JavaScript syntax passed`。

---

### Task 3: 后端 OTA 写入、错误响应与行车锁

**Files:**
- Modify: `src/main.cpp` 的 include、电机全局状态、`api_handler`、HTTP handler 区域和 `startCameraServer()`

**Interfaces:**
- Consumes: Task 2 的裸二进制 `POST /update`。
- Produces: `volatile bool otaInProgress`、`ota_fail()`、`update_handler()`；OTA 期间 `/api` 返回 HTTP 503，正常时行为不变。

- [ ] **Step 1: 写后端结构的失败检查**

```bash
node -e 'const fs=require("fs"),assert=require("assert"); const s=fs.readFileSync("src/main.cpp","utf8"); for(const [needle,name] of [["#include <Update.h>","Update include"],["volatile bool otaInProgress","OTA 状态"],["static esp_err_t update_handler","更新 handler"],["httpd_req_recv(req","接收循环"],["Update.end(true)","完成写入"],[".method = HTTP_POST","POST 路由"]]) assert(s.includes(needle),name+" 缺失");'
```

- [ ] **Step 2: 确认结构检查失败**

Run：执行 Step 1。
Expected：至少一项以 `缺失` 结束的 AssertionError。

- [ ] **Step 3: 添加 OTA 全局状态**

在 include 区域加入：

```cpp
#include <Update.h>
```

在 `lastCmdTime` 后加入：

```cpp
volatile bool otaInProgress = false;
```

- [ ] **Step 4: 阻止 OTA 期间的行车命令**

在 `api_handler()` 函数体第一行加入：

```cpp
  if (otaInProgress) {
    motorLeft(0); motorRight(0);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, "OTA 进行中，行车控制已暂停", HTTPD_RESP_USE_STRLEN);
  }
```

- [ ] **Step 5: 实现 OTA 错误处理和接收 handler**

在 `capture_handler()` 前加入以下完整函数。错误码必须在 `abort()` 前读取，避免被 abort 错误覆盖：

```cpp
static esp_err_t ota_fail(httpd_req_t *req, const char *reason) {
  uint8_t error = Update.getError();
  if (Update.isRunning()) Update.abort();
  otaInProgress = false;
  motorLeft(0); motorRight(0);
  Serial.printf("OTA 失败：%s，Update 错误码=%u\n", reason, (unsigned)error);
  char message[192];
  snprintf(message, sizeof(message), "失败：%s；Update 错误码=%u", reason, (unsigned)error);
  httpd_resp_set_status(req, "500 Internal Server Error");
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t update_handler(httpd_req_t *req) {
  motorLeft(0); motorRight(0);
  otaInProgress = true;
  size_t total = req->content_len;
  if (total == 0 || total > 0x1E0000) {
    return ota_fail(req, "固件为空或超过 OTA 单槽容量");
  }
  if (!Update.begin(total, U_FLASH)) {
    return ota_fail(req, "无法开始 Flash 更新");
  }
  uint8_t buffer[2048];
  size_t received = 0;
  while (received < total) {
    size_t chunk = total - received;
    if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
    int count = httpd_req_recv(req, (char *)buffer, chunk);
    if (count <= 0) return ota_fail(req, "接收中断或连接失败");
    if (Update.write(buffer, count) != (size_t)count) {
      return ota_fail(req, "Flash 写入失败");
    }
    received += count;
  }
  if (received != total || !Update.end(true)) {
    return ota_fail(req, "固件长度或最终校验失败");
  }
  Serial.printf("OTA 成功，共接收 %u 字节，准备重启\n", (unsigned)received);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  esp_err_t response = httpd_resp_send(req, "OK", 2);
  delay(500);
  ESP.restart();
  return response;
}
```

- [ ] **Step 6: 注册 POST 路由**

在 `startCameraServer()` 的其他 URI 定义后加入：

```cpp
  httpd_uri_t update_uri = { .uri = "/update", .method = HTTP_POST, .handler = update_handler, .user_ctx = NULL };
```

在 80 端口实例的 `httpd_register_uri_handler()` 区域加入：

```cpp
    httpd_register_uri_handler(camera_httpd, &update_uri);
```

不修改随后用于 81 端口的配置和 `stream_uri`。

- [ ] **Step 7: 运行结构检查和完整编译**

Run：再次执行 Step 1，然后执行：

```bash
C:/Users/haoli/.platformio/penv/Scripts/platformio.exe run
C:/Users/haoli/.platformio/penv/Scripts/python.exe -c 'import os; n=os.path.getsize(".pio/build/esp32cam/firmware.bin"); print(n,"bytes"); assert n < 1800000'
```

Expected：结构检查退出码 0，编译 `SUCCESS`，bin 小于 1,800,000 字节。既有摄像头弃用 warning 可保留，不在本任务顺手修改。

---

### Task 4: 首次串口烧录、OTA 闭环与改动说明

**Files:**
- Modify: `src/main.cpp` 的 `FW_VERSION` 用于第二轮测试
- Create: `OTA改动说明.md`
- Read: `.pio/build/esp32cam/firmware.bin`

**Interfaces:**
- Consumes: 前三个任务完成的网页、`POST /update` 和双槽分区。
- Produces: 已验证的无线更新流程、逐项验收记录和用户操作说明。

- [ ] **Step 1: 首次通过 MB 串口烧录**

保持 MB 底板、USB 直连，关闭 Monitor 和其他串口程序，执行：

```bash
C:/Users/haoli/.platformio/penv/Scripts/platformio.exe run -t upload
```

Expected：所有写入段 `Hash of data verified`，最后 `SUCCESS`。若 COM3 被占用，先关闭占用程序并拔插 USB，不修改代码解决串口占用。

- [ ] **Step 2: 验证首次固件**

上电后连接 `FPV-Car`，打开 `http://192.168.4.1`，确认：

- 页面显示 `FW_VERSION` 的完整版本；
- 实时视频正常，三档速度、画质和停止可用；
- 电机接线已完成时，架空车轮验证前进、后退和组合转向；
- 电机未接线时，在 `OTA改动说明.md` 明确写“电机实测未执行”，不能标为通过。

- [ ] **Step 3: 编译可辨识的第二版本**

将版本宏临时改为：

```cpp
#define FW_VERSION "v2.1-ota2 " __DATE__ " " __TIME__
```

执行：

```bash
C:/Users/haoli/.platformio/penv/Scripts/platformio.exe run
```

Expected：`SUCCESS`；待上传文件为 `.pio/build/esp32cam/firmware.bin`。

- [ ] **Step 4: 完成真实 OTA 闭环**

保持车轮架空，在网页展开“固件更新”，选择上述 bin 并上传。

Expected：页面显示上传中和成功提示；板子自动重启；必要时重新连接热点；页面版本变为 `v2.1-ota2`，视频和电机控制仍正常。若电机已接线，在上传开始前让车轮转动，确认收到更新请求后立即停止且整个上传过程不转。

- [ ] **Step 5: 仅在出现实际栈相关故障时调整栈**

若上传中途稳定复现断连或复位，将 80 端口实例启动前的配置设为：

```cpp
  config.stack_size = 8192;
```

然后重新编译、串口烧录并重复 OTA 闭环。没有该故障时不做此改动。

- [ ] **Step 6: 写 `OTA改动说明.md`**

文档必须包含以下已确定内容，并按刚才的真实结果填写验收项：

```markdown
# OTA 改动说明

## 改动点

- 分区表由 `huge_app.csv` 改为 `min_spiffs.csv`，启用 app0/app1 双槽。
- 使用内置 `Update`，新增 `POST /update` 裸二进制上传，无第三方依赖。
- 网页底部增加版本显示、bin 选择、上传按钮和中文状态。
- OTA 开始立即停车，OTA 期间 `/api` 返回 503；成功后延时 500ms 自动重启。
- 引脚、电机映射、正常控制接口和 81 端口视频流未改。

## 用户无线更新流程

1. 手机/电脑连接 WiFi `FPV-Car`，打开 `http://192.168.4.1`。
2. 展开页面底部“固件更新”，选择 `.pio/build/esp32cam/firmware.bin`。
3. 点击“上传固件”，等待成功提示和自动重启。
4. 必要时重新连接热点，核对页面版本号确认更新生效。

## 风险

首次双槽固件必须串口烧录。刷入坏固件可能导致热点无法启动，需要 USB 转 TTL 救砖。更新中不能断电。
```

验收部分逐项记录：编译和 bin 体积、首次串口烧录、热点、网页、视频、版本、OTA 自动重启、版本变化、电机控制、上传期间停车。每项只写实际观察到的结果；未执行项明确写“未验证”。

- [ ] **Step 7: 最终核对交付物**

Run：

```bash
C:/Users/haoli/.platformio/penv/Scripts/platformio.exe run
```

Expected：最终源码编译 `SUCCESS`。确认 `src/main.cpp`、`platformio.ini`、`OTA改动说明.md` 与设计一致，待上传 bin 路径仍为 `.pio/build/esp32cam/firmware.bin`。
