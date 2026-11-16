// ============================================================
// FPV 四驱小车 · 完整固件（单文件版，PlatformIO/Arduino 通用）
// 硬件：ESP32-CAM (AI-Thinker) + L298N + 4×520 减速电机 + 7.4V 锂电
// 用法：烧录后开机自建热点 FPV-Car（密码 12345678），浏览器打开 http://192.168.4.1
//       中文网页：实时画面 + 组合方向控制 + 三档速度 + 画质切换
// 引脚表（接线唯一标准）：
//   GPIO12→ENA  GPIO13→IN1  GPIO15→IN2  GPIO14→ENB  GPIO2→IN3  GPIO4→IN4
//   电源：7.4V 电池 → L298N +12V/GND；L298N +5V → ESP32-CAM 5V；GND 全程共地
// 环境：Arduino 内核 2.0.x（ledcSetup/ledcAttachPin 旧 API）
// ============================================================

#include <Arduino.h>
#include <Update.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"

// ===================== 固件版本（网页显示）=====================
#define FW_VERSION "v2.1-ota5 " __DATE__ " " __TIME__

// ===================== WiFi 热点设置 =====================
const char* AP_SSID = "FPV-Car";
const char* AP_PASSWORD = "12345678";   // 至少 8 位

// ===================== L298N 引脚 =====================
// 注意：GPIO16 在 AI-Thinker 板上被 PSRAM 占用、未引出，不能用！IN4 用 GPIO4。
#define PIN_ENA 12   // 左侧电机调速(PWM) → ENA（跳线帽已拔）
#define PIN_IN1 13   // 左侧电机方向 → IN1
#define PIN_IN2 15   // 左侧电机方向 → IN2
#define PIN_ENB 14   // 右侧电机调速(PWM) → ENB（跳线帽已拔）
#define PIN_IN3 2    // 右侧电机方向 → IN3
#define PIN_IN4 4    // 右侧电机方向 → IN4

#define PWM_CH_LEFT  2   // 摄像头 XCLK 占用通道 0，电机用 2/3
#define PWM_CH_RIGHT 3
#define PWM_FREQ     2000
#define PWM_RES      8   // 0~255

// ===================== 摄像头引脚（AI-Thinker 固定定义）=====================
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// ===================== 电机控制 =====================
unsigned long lastCmdTime = 0;   // 断线看门狗：800ms 无指令自动停车
volatile bool otaInProgress = false;

void motorLeft(int spd) {        // -255 ~ +255
  if (spd > 0)      { digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW);  }
  else if (spd < 0) { digitalWrite(PIN_IN1, LOW);  digitalWrite(PIN_IN2, HIGH); }
  else              { digitalWrite(PIN_IN1, LOW);  digitalWrite(PIN_IN2, LOW);  }
  ledcWrite(PWM_CH_LEFT, constrain(abs(spd), 0, 255));
}
void motorRight(int spd) {
  if (spd > 0)      { digitalWrite(PIN_IN3, HIGH); digitalWrite(PIN_IN4, LOW);  }
  else if (spd < 0) { digitalWrite(PIN_IN3, LOW);  digitalWrite(PIN_IN4, HIGH); }
  else              { digitalWrite(PIN_IN3, LOW);  digitalWrite(PIN_IN4, LOW);  }
  ledcWrite(PWM_CH_RIGHT, constrain(abs(spd), 0, 255));
}

// ===================== 中文遥控网页 =====================
static const char INDEX_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no,viewport-fit=cover">
<title>FPV 小车遥控</title>
<style>
:root{color-scheme:dark;--accent:#61dafb;--glass:rgba(12,20,29,.78)}
*{box-sizing:border-box}
html,body{width:100%;height:100%;margin:0;overflow:hidden;background:#05080d;color:#f4f7fb;font-family:system-ui,-apple-system,"Segoe UI",sans-serif}
button{border:1px solid rgba(255,255,255,.2);color:inherit;background:var(--glass);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);touch-action:none;user-select:none;-webkit-user-select:none;-webkit-tap-highlight-color:transparent;cursor:pointer}
button:focus-visible{outline:2px solid var(--accent);outline-offset:3px}
#cockpit{position:relative;width:100%;height:100%;height:100dvh;overflow:hidden;display:flex;align-items:center;justify-content:center;background:#05080d}
#cam{display:block;width:100%;height:100%;object-fit:contain;background:#05080d}
.control{position:absolute;z-index:2}
.brand{top:max(14px,env(safe-area-inset-top));left:50%;transform:translateX(-50%);font-size:13px;font-weight:700;letter-spacing:3px;color:rgba(255,255,255,.72);pointer-events:none;white-space:nowrap}
.stop{top:max(16px,env(safe-area-inset-top));left:max(16px,env(safe-area-inset-left));padding:12px 22px;border-radius:14px;background:rgba(202,50,58,.88);border-color:rgba(255,255,255,.3);font-size:17px;font-weight:700}
.stop:active{background:#f04b55;transform:scale(.96)}
.gear-panel{top:max(14px,env(safe-area-inset-top));right:max(16px,env(safe-area-inset-right));text-align:right}
.gear-title{display:block;margin:0 4px 7px;font-size:11px;letter-spacing:2px;color:rgba(255,255,255,.7)}
.gears{display:flex;padding:4px;border-radius:15px;background:rgba(5,10,16,.68);border:1px solid rgba(255,255,255,.14)}
.gear-button{padding:10px 13px;border:0;border-radius:11px;background:transparent;font-size:14px;white-space:nowrap}
.gear-button.active{background:var(--accent);color:#08202a;font-weight:800;box-shadow:0 0 18px rgba(97,218,251,.25)}
.motion{bottom:max(20px,env(safe-area-inset-bottom));display:flex;gap:12px}
.longitudinal{left:max(18px,env(safe-area-inset-left));flex-direction:column}
.steering{right:max(18px,env(safe-area-inset-right))}
.drive-button{width:clamp(72px,9vw,98px);height:clamp(60px,13vh,86px);border-radius:22px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px;box-shadow:0 8px 24px rgba(0,0,0,.25)}
.drive-button .arrow{font-size:25px;line-height:1;color:var(--accent)}
.drive-button .label{font-size:13px;font-weight:650}
.drive-button.pressed{background:rgba(36,132,160,.9);border-color:var(--accent);transform:scale(.95);box-shadow:0 0 22px rgba(97,218,251,.28)}
.quality{bottom:max(22px,env(safe-area-inset-bottom));left:50%;transform:translateX(-50%);padding:12px 20px;border-radius:999px;font-size:14px;font-weight:700;white-space:nowrap}
.quality:active{background:rgba(36,132,160,.9)}
.key-hint{top:48px;left:50%;transform:translateX(-50%);font-size:12px;color:rgba(255,255,255,.62);pointer-events:none;white-space:nowrap;text-shadow:0 1px 4px #000}
.update-panel{bottom:76px;left:50%;transform:translateX(-50%);z-index:4}
.update-panel summary{padding:9px 16px;border:1px solid rgba(255,255,255,.2);border-radius:999px;background:var(--glass);cursor:pointer;list-style:none;font-size:13px}
.update-panel summary::-webkit-details-marker{display:none}
.update-body{position:absolute;bottom:calc(100% + 8px);left:50%;transform:translateX(-50%);width:min(360px,90vw);padding:16px;border:1px solid rgba(255,255,255,.2);border-radius:16px;background:rgba(8,14,22,.96);text-align:left;font-size:13px}
#fwFile{width:100%;margin-top:12px;font-size:12px}
#fwUpload{width:100%;margin-top:10px;padding:10px;border-radius:10px;background:#17657c}
#fwUpload:disabled{opacity:.5;cursor:wait}
#fwStatus{margin-top:10px;overflow-wrap:anywhere;color:rgba(255,255,255,.78)}
@media (orientation:portrait) and (max-width:700px){.update-panel{bottom:245px}}
.rotate{display:none}
@media (pointer:coarse){.key-hint{display:none}}
@media (orientation:portrait) and (max-width:700px){
 .brand{display:none}
 .rotate{display:block;top:22px;left:50%;transform:translateX(-50%);font-size:12px;color:rgba(255,255,255,.7);white-space:nowrap;pointer-events:none}
 .gear-panel{top:68px;right:12px}
 .gear-button{padding:9px 10px;font-size:13px}
 .drive-button{width:74px;height:66px;border-radius:18px}
 .quality{bottom:178px}
}
@media (max-height:420px){
 .drive-button{height:60px;border-radius:17px}
 .motion{bottom:max(12px,env(safe-area-inset-bottom))}
 .quality{bottom:max(14px,env(safe-area-inset-bottom))}
 .gear-button{padding:8px 11px}
 .stop{padding:10px 18px}
}
</style>
</head>
<body>
<main id="cockpit">
<img id="cam" alt="FPV 实时画面">
<div class="control brand">FPV · CAR</div>
<button id="stop" class="control stop" type="button">停止</button>
<div class="control gear-panel">
 <span class="gear-title">速度档位</span>
 <div class="gears" role="group" aria-label="速度档位">
  <button class="gear-button active" data-gear="0" type="button">休闲</button>
  <button class="gear-button" data-gear="1" type="button">兜风</button>
  <button class="gear-button" data-gear="2" type="button">疯狂</button>
 </div>
</div>
<div class="control motion longitudinal">
 <button class="drive-button" data-motion="forward" type="button"><span class="arrow">▲</span><span class="label">前进</span></button>
 <button class="drive-button" data-motion="backward" type="button"><span class="arrow">▼</span><span class="label">后退</span></button>
</div>
<div class="control motion steering">
 <button class="drive-button" data-motion="left" type="button"><span class="arrow">◀</span><span class="label">左转</span></button>
 <button class="drive-button" data-motion="right" type="button"><span class="arrow">▶</span><span class="label">右转</span></button>
</div>
<div class="control key-hint">W/S 前进后退 · A/D 组合转向 · Q 换挡 · E 画质</div>
<button id="quality" class="control quality" type="button">画面：流畅</button>
<details id="updatePanel" class="control update-panel">
 <summary>固件更新</summary>
 <div class="update-body">
  <div>固件版本：)rawliteral" FW_VERSION R"rawliteral(</div>
  <input id="fwFile" type="file" accept=".bin,application/octet-stream">
  <button id="fwUpload" type="button">上传固件</button>
  <div id="fwStatus">请选择 firmware.bin</div>
 </div>
</details>
<div class="control rotate">旋转手机至横屏，获得完整驾驶视野</div>
</main>
<script>
document.getElementById('cam').src = 'http://' + location.hostname + ':81/stream';
var speeds = [100, 170, 255];
var gear = 0;
var framesize = 4;
var state = {forward:false, backward:false, left:false, right:false};
var lastDrive = '';
function driveFromKeys(state){
  var forward = state.forward && !state.backward;
  var backward = state.backward && !state.forward;
  var left = state.left && !state.right;
  var right = state.right && !state.left;
  if (!forward && !backward) return 'S';
  if (forward) {
    if (left) return 'FL';
    if (right) return 'FR';
    return 'F';
  }
  if (left) return 'BL';
  if (right) return 'BR';
  return 'B';
}
function nextGear(index){
  return (index + 1) % speeds.length;
}
function snd(d){
  fetch('/api?dir=' + d + '&spd=' + speeds[gear]).catch(function(){});
}
function updateDrive(force){
  var d = driveFromKeys(state);
  document.querySelectorAll('[data-motion]').forEach(function(b){
    b.classList.toggle('pressed', state[b.dataset.motion]);
  });
  if (force || d !== lastDrive) {
    lastDrive = d;
    snd(d);
  }
}
function stopAll(){
  Object.keys(state).forEach(function(key){ state[key] = false; });
  updateDrive(true);
}
function setGear(index){
  gear = index;
  document.querySelectorAll('[data-gear]').forEach(function(b){
    b.classList.toggle('active', Number(b.dataset.gear) === gear);
  });
  if (lastDrive !== 'S') updateDrive(true);
}
function setfs(n){
  framesize = n;
  document.getElementById('quality').textContent = n === 4 ? '画面：流畅' : '画面：清晰';
  fetch('/control?var=framesize&val=' + n).catch(function(){});
}
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
document.querySelectorAll('[data-motion]').forEach(function(b){
  var key = b.dataset.motion;
  b.addEventListener('pointerdown', function(e){
    e.preventDefault();
    b.setPointerCapture(e.pointerId);
    state[key] = true;
    updateDrive(false);
  });
  function release(){
    if (!state[key]) return;
    state[key] = false;
    updateDrive(false);
  }
  b.addEventListener('pointerup', release);
  b.addEventListener('pointercancel', release);
  b.addEventListener('lostpointercapture', release);
  b.addEventListener('contextmenu', function(e){ e.preventDefault(); });
});
document.querySelectorAll('[data-gear]').forEach(function(b){
  b.addEventListener('click', function(){ setGear(Number(b.dataset.gear)); });
});
document.getElementById('stop').addEventListener('pointerdown', function(e){
  e.preventDefault();
  stopAll();
});
document.getElementById('quality').addEventListener('click', function(){
  setfs(framesize === 4 ? 5 : 4);
});
var keyMap = {w:'forward', s:'backward', a:'left', d:'right'};
document.addEventListener('keydown', function(e){
  var key = e.key.toLowerCase();
  if (keyMap[key]) {
    e.preventDefault();
    state[keyMap[key]] = true;
    updateDrive(false);
  } else if (key === 'q' && !e.repeat) {
    e.preventDefault();
    setGear(nextGear(gear));
  } else if (key === 'e' && !e.repeat) {
    e.preventDefault();
    setfs(framesize === 4 ? 5 : 4);
  }
});
document.addEventListener('keyup', function(e){
  var key = e.key.toLowerCase();
  if (keyMap[key]) {
    e.preventDefault();
    state[keyMap[key]] = false;
    updateDrive(false);
  }
});
window.addEventListener('blur', stopAll);
document.addEventListener('visibilitychange', function(){ if (document.hidden) stopAll(); });
// 行驶时定时刷新指令，避免触发 800ms 无指令停车看门狗。
setInterval(function(){ if (lastDrive && lastDrive !== 'S') snd(lastDrive); }, 250);
document.getElementById('updatePanel').addEventListener('toggle', function(){
  if (this.open) stopAll();
});
document.getElementById('fwUpload').addEventListener('click', updateFirmware);
updateDrive(true);
</script>
</body>
</html>)rawliteral";

// ===================== HTTP 服务器 =====================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t api_handler(httpd_req_t *req) {   // /api?dir=F&spd=130
  if (otaInProgress) {
    motorLeft(0); motorRight(0);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, "OTA 进行中，行车控制已暂停", HTTPD_RESP_USE_STRLEN);
  }
  char buf[64] = {0};
  char dir[8]  = {0};
  char spdStr[8] = {0};
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK ||
      httpd_query_key_value(buf, "dir", dir, sizeof(dir)) != ESP_OK) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  httpd_query_key_value(buf, "spd", spdStr, sizeof(spdStr));
  int spd = atoi(spdStr);
  if (spd <= 0)  spd = 130;
  if (spd > 255) spd = 255;

  int turn = spd * 2 / 3;
  if      (!strcmp(dir, "F"))  { motorLeft(spd);   motorRight(spd);   }
  else if (!strcmp(dir, "B"))  { motorLeft(-spd);  motorRight(-spd);  }
  else if (!strcmp(dir, "FL")) { motorLeft(turn);  motorRight(spd);   }
  else if (!strcmp(dir, "FR")) { motorLeft(spd);   motorRight(turn);  }
  else if (!strcmp(dir, "BL")) { motorLeft(-turn); motorRight(-spd);  }
  else if (!strcmp(dir, "BR")) { motorLeft(-spd);  motorRight(-turn); }
  else                         { motorLeft(0);     motorRight(0);     }

  lastCmdTime = millis();
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

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
  Serial.printf("OTA 请求到达，content_len=%u\n", (unsigned)total);
  if (total == 0 || total > 0x1E0000) {
    return ota_fail(req, "固件为空或超过 OTA 单槽容量");
  }
  if (!Update.begin(total, U_FLASH)) {
    return ota_fail(req, "无法开始 Flash 更新");
  }
  Serial.printf("OTA Update.begin 成功，目标 %u 字节\n", (unsigned)total);
  uint8_t buffer[2048];
  size_t received = 0;
  size_t nextLog = 131072;
  while (received < total) {
    size_t chunk = total - received;
    if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
    int count = httpd_req_recv(req, (char *)buffer, chunk);
    if (count <= 0) return ota_fail(req, "接收中断或连接失败");
    if (Update.write(buffer, count) != (size_t)count) {
      return ota_fail(req, "Flash 写入失败");
    }
    received += count;
    if (received >= nextLog) {
      Serial.printf("OTA 接收进度 %u/%u 字节\n", (unsigned)received, (unsigned)total);
      nextLog += 131072;
    }
  }
  Serial.printf("OTA 接收完成，共 %u 字节，开始最终校验\n", (unsigned)received);
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

static esp_err_t capture_handler(httpd_req_t *req) {   // /capture 拍一张照
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t cmd_handler(httpd_req_t *req) {   // /control?var=framesize&val=4
  char buf[64] = {0};
  char variable[16] = {0};
  char value[16]    = {0};
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK ||
      httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK ||
      httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  int val = atoi(value);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;
  if      (!strcmp(variable, "framesize")) res = s->set_framesize(s, (framesize_t)val);
  else if (!strcmp(variable, "quality"))   res = s->set_quality(s, val);
  else if (!strcmp(variable, "hmirror"))   res = s->set_hmirror(s, val);
  else if (!strcmp(variable, "vflip"))     res = s->set_vflip(s, val);
  else res = -1;
  if (res) return httpd_resp_send_500(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t stream_handler(httpd_req_t *req) {   // /stream（端口 81，MJPEG）
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];
  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) { res = ESP_FAIL; }
    else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool ok = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb); fb = NULL;
        if (!ok) res = ESP_FAIL;
      } else {
        _jpg_buf_len = fb->len; _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (fb) { esp_camera_fb_return(fb); fb = NULL; _jpg_buf = NULL; }
    else if (_jpg_buf) { free(_jpg_buf); _jpg_buf = NULL; }
    if (res != ESP_OK) break;
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // OTA 接收使用 2KB 栈缓冲区，HTTP 任务栈保留 8KB。
  config.stack_size = 8192;
  httpd_uri_t index_uri   = { .uri = "/",        .method = HTTP_GET, .handler = index_handler,   .user_ctx = NULL };
  httpd_uri_t api_uri     = { .uri = "/api",     .method = HTTP_GET, .handler = api_handler,     .user_ctx = NULL };
  httpd_uri_t cmd_uri     = { .uri = "/control", .method = HTTP_GET, .handler = cmd_handler,     .user_ctx = NULL };
  httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL };
  httpd_uri_t update_uri = { .uri = "/update", .method = HTTP_POST, .handler = update_handler, .user_ctx = NULL };
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &api_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    esp_err_t update_reg = httpd_register_uri_handler(camera_httpd, &update_uri);
    Serial.printf("OTA /update 路由注册结果=%d（0 为成功）\n", (int)update_reg);
  }
  config.server_port += 1;   // 视频流在 81 端口
  config.ctrl_port += 1;
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// ===================== 主程序 =====================
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("===== FPV 四驱小车启动 =====");

  pinMode(PIN_ENA, OUTPUT); pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_ENB, OUTPUT); pinMode(PIN_IN3, OUTPUT); pinMode(PIN_IN4, OUTPUT);
  ledcSetup(PWM_CH_LEFT,  PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_ENA, PWM_CH_LEFT);
  ledcSetup(PWM_CH_RIGHT, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_ENB, PWM_CH_RIGHT);
  motorLeft(0); motorRight(0);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("摄像头初始化失败 0x%x —— 检查摄像头排线\n", err);
    return;   // 摄像头失败则停在此，WiFi 不启动
  }
  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);   // 默认流畅优先
  Serial.println("摄像头 OK");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("热点已启动: "); Serial.println(AP_SSID);
  Serial.print("浏览器打开: http://");
  Serial.println(WiFi.softAPIP());   // 固定 192.168.4.1

  startCameraServer();
  Serial.println("===== 就绪 =====");
}

void loop() {
  if (millis() - lastCmdTime > 800) {   // 断线自动停车
    motorLeft(0);
    motorRight(0);
  }
  delay(50);
}
