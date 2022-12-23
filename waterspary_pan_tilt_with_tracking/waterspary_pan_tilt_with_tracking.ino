/*********
  Rui Santos
  Complete instructions at https://RandomNerdTutorials.com/esp32-cam-projects-ebook/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/

#include "esp_camera.h"

#include "camera_pins.h"        // Must include this after setting
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"             // disable brownout problems
#include "soc/rtc_cntl_reg.h"    // disable brownout problems
#include "esp_http_server.h"
#include <ESP32Servo.h>

#include "helpers.h"
unsigned long last_still=0;

bool last_region_g4 = true;
bool toggle_track = true;

unsigned long squirt_t;
bool tracking_on = false;

// Replace with your network credentials
const char* ssid = "REPLACE_WITH_YOUR_SSID";
const char* password = "REPLACE_WITH_YOUR_PASSWORD";

#define PART_BOUNDARY "123456789000000000000987654321"


//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WITHOUT_PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM_B
//#define CAMERA_MODEL_WROVER_KIT

#define SERVO_1      14  //top
#define SERVO_2      15  //bottom

////here
#define DRIVER_PIN   12 //

#define SERVO_STEP   5

Servo servoN1;
Servo servoN2;
Servo servo1;
Servo servo2;

int servo1Pos = 90;
int servo2Pos = 90;

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<html>
  <head>
    <title>ESP32-CAM Robot</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}
      table { margin-left: auto; margin-right: auto; }
      td { padding: 8 px; }
      .button {
        background-color: #2f4468;
        border: none;
        color: white;
        padding: 10px 20px;
        text-align: center;
        text-decoration: none;
        display: inline-block;
        font-size: 18px;
        margin: 6px 3px;
        cursor: pointer;
        -webkit-touch-callout: none;
        -webkit-user-select: none;
        -khtml-user-select: none;
        -moz-user-select: none;
        -ms-user-select: none;
        user-select: none;
        -webkit-tap-highlight-color: rgba(0,0,0,0);
      }
      img {  width: auto ;
        max-width: 100% ;
        height: auto ; 
      }
    </style>
  </head>
  <body>
    <h1>ESP32-CAM Pan and Tilt</h1>
    <img src="" id="photo" >
    <table>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('up');" ontouchstart="toggleCheckbox('up');">Up</button></td></tr>
      <tr><td align="center"><button class="button" onmousedown="toggleCheckbox('left');" ontouchstart="toggleCheckbox('left');">Left</button></td><td align="center"></td><td align="center"><button class="button" onmousedown="toggleCheckbox('right');" ontouchstart="toggleCheckbox('right');">Right</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('down');" ontouchstart="toggleCheckbox('down');">Down</button></td></tr>                   
    </table>
     <table>
      <tr><td align="center"><button class="button" onmousedown="toggleCheckbox('squirt');" ontouchstart="toggleCheckbox('squirt');">Squirt</button></td><td align="center"></td>
      <td align="center"><button class="button" onmousedown="toggleCheckbox('track');" ontouchstart="toggleCheckbox('track');">Track</button></td>
      <td align="center"><button class="button" onmousedown="toggleCheckbox('track_off');" ontouchstart="toggleCheckbox('track_off');">Track off</button></td>
      </tr>
     </table>
       
   <script>
   function toggleCheckbox(x) {
     var xhr = new XMLHttpRequest();
     xhr.open("GET", "/action?go=" + x, true);
     xhr.send();
   }
   window.onload = document.getElementById("photo").src = window.location.href.slice(0, -1) + ":81/stream";
  </script>
  </body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req){
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

void move_left(int degs){
//  degs = 1;
  Serial.print("in move left ");
  Serial.println(servo2Pos);
  Serial.println(degs);
  for(int i = 0; i<degs; i++){
    if(servo2Pos <= 180-SERVO_STEP) {
      servo2Pos += SERVO_STEP;
      Serial.print("in move left ");
      Serial.println(servo2Pos);
      servo2.write(servo2Pos);
      Serial.println(degs);
      delay(30);
    }
  }
}

void move_right(int degs){
//  degs = 1;
  Serial.print("in move right ");
  Serial.println(degs);
  for(int i = 0; i<degs; i++){
    if(servo2Pos >= SERVO_STEP) {
      servo2Pos -= SERVO_STEP;
      Serial.print("in move right ");
      Serial.println(servo2Pos);
      servo2.write(servo2Pos);
      Serial.println(degs);
      delay(30);
    }
  }
}


static esp_err_t stream_handler(httpd_req_t *req){
  fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      //This is the motion tracking stuff here
      if(tracking_on && finished_tracking)
      {
//        toggle_track = false;
        last_still = millis();
      if (!capture_still())
          {
              Serial.println("Failed capture");
          } else{
            cnt++;
            if(first_capture) {
              //for first capture we copy the current to the previous frame
              //so that we don't have stale data from before we moved
              //the servo in the previous frame
              cnt = 0;
              update_frame();
              first_capture = false;
             }
            }
        
        if (motion_detect())
              {
                cnt = 0;
                do_tracking = true; //loop will activate the servo tracking if do_tracking is true
                finished_tracking = false; //don't do any more image processing until serov
                                           //is finished because we don't want to mistake the motion of the servo
                                           //for something moving
                Serial.println("motion detected");
              } else if(cnt>1000){
                //motion hasn't been detected for 1000 frames
                servo2.write(90);
                delay(100);
                capture_still();
                update_frame();
              }
        update_frame();
      
      } else {
        toggle_track = true; //this is not used
      }
      //end motion tracking and convert to jpeg for transportation
      if(fb->width > 100){
        if(fb->format != PIXFORMAT_JPEG){
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if(!jpeg_converted){
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    
    if(res != ESP_OK){
      break;
    }
    //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
  }
  return res;
}

static esp_err_t cmd_handler(httpd_req_t *req){
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};
  
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if(!buf){
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  sensor_t * s = esp_camera_sensor_get();
  //flip the camera vertically
  //s->set_vflip(s, 1);          // 0 = disable , 1 = enable
  // mirror effect
  //s->set_hmirror(s, 1);          // 0 = disable , 1 = enable

  int res = 0;
  
  if(!strcmp(variable, "up")) {
    if(servo1Pos <= 150) {
      servo1Pos += SERVO_STEP;
      servo1.write(servo1Pos);
    }
    Serial.println(servo1Pos);
    Serial.println("Up");
  }
  else if(!strcmp(variable, "left")) {
    if(servo2Pos <= 170) {
      servo2Pos += SERVO_STEP;
      servo2.write(servo2Pos);
    }
    Serial.println(servo2Pos);
    Serial.println("Left");
  }
  else if(!strcmp(variable, "right")) {
    if(servo2Pos >= 10) {
      servo2Pos -= SERVO_STEP;
      servo2.write(servo2Pos);
    }
    Serial.println(servo2Pos);
    Serial.println("Right");
  }
  else if(!strcmp(variable, "down")) {
    if(servo1Pos >= 30) {
      servo1Pos -= SERVO_STEP;
      servo1.write(servo1Pos);
    }
    Serial.println(servo1Pos);
    Serial.println("Down");
  }
  ////////changed here
  else if(!strcmp(variable, "squirt")) {
    digitalWrite(DRIVER_PIN,HIGH);
    squirt_t = millis();
    
//    Serial.println(servo1Pos);
    Serial.println("squirt");
  }
  else if(!strcmp(variable, "track")) {
    tracking_on = true;
    
//    Serial.println(servo1Pos);
    Serial.println("track");
  }
  else if(!strcmp(variable, "track_off")) {
    tracking_on = false;
    
//    Serial.println(servo1Pos);
    Serial.println("track off");
  }
  else {
    res = -1;
  }

  if(res){
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t cmd_uri = {
    .uri       = "/action",
    .method    = HTTP_GET,
    .handler   = cmd_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
  }
  config.server_port += 1;
  config.ctrl_port += 1;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  ////////////////////////////////////////////////////////////
  //changed here
  squirt_t = millis();
  pinMode(DRIVER_PIN, OUTPUT);
  digitalWrite(DRIVER_PIN, LOW); //make sure motor is off
  ///////////////////////////////////////////////////////
  
  servo1.setPeriodHertz(50);    // standard 50 hz servo
  servo2.setPeriodHertz(50);    // standard 50 hz servo
  servoN1.attach(2, 1000, 2000);
  servoN2.attach(13, 1000, 2000);
  
  servo1.attach(SERVO_1, 1000, 2000);
  servo2.attach(SERVO_2, 1000, 2000);
  
  servo1.write(servo1Pos);
  servo2.write(servo2Pos);
  
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE; //PIXFORMAT_JPEG; 
  
  if(psramFound()){
    config.frame_size = MY_FRAMESIZE; //FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = MY_FRAMESIZE; //FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  // Wi-Fi connection
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.println(WiFi.localIP());
  
  // Start streaming web server
  startCameraServer();
}

void loop() {
  //changed this
  if((millis()-squirt_t)>400){
    digitalWrite(DRIVER_PIN, LOW);
    watergun_off_time = millis();
  }
  if(tracking_on && do_tracking){
    do_tracking = false;
    finished_tracking = false;
     Serial.println("Motion detected");
     
    //do stuff here
    if(region_of_interest<4){
      
      //to the left
//      if(!last_region_g4)
      move_left(3*(4-region_of_interest));
      last_region_g4 = false;
    } else if(region_of_interest>4){
//      if(last_region_g4)
      move_right(3*(region_of_interest-4));
      last_region_g4 = true;
    }
    if(fire_waterpistol){ //only fire water pistol if enough motion is detected
      if(millis() - watergun_off_time > 100){
        //don't want to turn the water gun on again unless it's been off for at least 300ms
        digitalWrite(DRIVER_PIN,HIGH);
        squirt_t = millis(); }
    }
    
    delay(100);
    first_capture = true;
    finished_tracking = true;
    Serial.print("regin_of_interest = ");
    Serial.println(region_of_interest);
    
  }
//  delay(10);
}
