#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "tubiao.h"
#include <FS.h>
#include "LittleFS.h"

#define FS_NO_GLOBALS
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// 选择正确的墨水屏型号
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(GxEPD2_290(/*CS=D8*/ 15, /*DC=D3*/ 27, /*RST=D4*/ 26, /*BUSY=D2*/ 25));  // 2.9黑白   GDEH029A1   128x296, SSD1608 (IL3820)
//GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(GxEPD2_290(/*CS*/ 15, /*DC*/ 4, /*RST*/ 2, /*BUSY*/ 5));  // GDEM029A01 屏幕尺寸和8266引脚设置


const uint8_t snakeSize = 4;  // 蛇身单位尺寸
const uint8_t screenWidth = 288 / snakeSize;
const uint8_t screenHeight = 103 / snakeSize;

const int buttonUpDown = 0;      // 上/下按钮连接的数字引脚
const int buttonLeftRight = 16;  // 左/右按钮连接的数字引脚

const uint8_t wallOffsetX = 4;   // 墙距离屏幕左右边缘的距离
const uint8_t wallOffsetY = 21;  // 墙距离屏幕上边缘的距离

struct Point {
  int8_t x;
  int8_t y;
};

Point snake[screenWidth * screenHeight];  // 蛇身数组
uint8_t snakeLength;
Point food;         // 食物坐标
int score = 0;      // 分数
int highscore = 0;  // 最高分数
enum Direction { UP,
                 DOWN,
                 LEFT,
                 RIGHT };  // 方向枚举
Direction snakeDirection = RIGHT;
unsigned long moveInterval = 100;
unsigned long moveTimer = 0;

void longpressbtn() {
  LittleFS.format();
  Serial.println("LittleFS格式化成功,最高分清零,系统马上重启");
  ESP.restart();
}

void formatffsshow() {
  display.setFullWindow();
  display.firstPage();
  display.fillScreen(GxEPD_WHITE);
  u8g2Fonts.setCursor(35, 75);
  u8g2Fonts.print("LittleFS格式化中,最高分清零,系统马上重启");
  display.nextPage();
}

void IRAM_ATTR changeDirectionUpDown() {
  static unsigned long lastChangeTime = 0;
  // 检查距离上次切换的时间是否足够长，如果是则切换方向
  if (millis() - lastChangeTime > 200) {
    Serial.println("按键1按下");
    lastChangeTime = millis();
    if (snakeDirection == UP) {
      snakeDirection = RIGHT;
    } else if (snakeDirection == RIGHT) {
      snakeDirection = DOWN;
    } else if (snakeDirection == DOWN) {
      snakeDirection = LEFT;
    } else if (snakeDirection == LEFT) {
      snakeDirection = UP;
    }
  }
}

void IRAM_ATTR changeDirectionLeftRight() {
  static unsigned long lastChangeTime = 0;
  // 检查距离上次切换的时间是否足够长，如果是则切换方向
  if (millis() - lastChangeTime > 200) {
    Serial.println("按键2按下");
    lastChangeTime = millis();
    if (snakeDirection == UP) {
      snakeDirection = LEFT;
    } else if (snakeDirection == LEFT) {
      snakeDirection = DOWN;
    } else if (snakeDirection == DOWN) {
      snakeDirection = RIGHT;
    } else if (snakeDirection == RIGHT) {
      snakeDirection = UP;
    }
  }
}


void setup() {

  SPI.end();                  // release standard SPI pins, e.g. SCK(18), MISO(19), MOSI(23), SS(5)
  SPI.begin(13, 12, 14, 15);  // map and init SPI pins SCK(13), MISO(12), MOSI(14), SS(15)
  Serial.begin(115200);
  delay(10);
  display.init(115200, true, 2, false);
  display.setRotation(1);  // 0 是横向
  u8g2Fonts.begin(display);
  u8g2Fonts.setFontDirection(0);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);  // 设置前景色
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);  // 设置背景色

  LittleFS.begin();  // 初始化LittleFS
  File file = LittleFS.open("/highscore.txt", "r");
  if (file) {
    highscore = file.parseInt();
    Serial.println("读取最高分成功");
    file.close();
  }

  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);

  pinMode(buttonUpDown, INPUT_PULLUP);
  pinMode(buttonLeftRight, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(buttonUpDown), changeDirectionUpDown, FALLING);
  attachInterrupt(digitalPinToInterrupt(buttonLeftRight), changeDirectionLeftRight, FALLING);

  mainint();

  spawnFood();
  resetGame();
}

void loop() {
  // 蛇移动
  if (millis() - moveTimer > moveInterval) {
    moveTimer = millis();
    moveSnake();
  }

  if (digitalRead(buttonUpDown) == LOW && digitalRead(buttonLeftRight) == LOW) {
    Serial.println("按键1和按键2同时按下");
    // 执行你想要的函数
    formatffsshow();
    longpressbtn();
  }
}

void resetGame() {
  snakeLength = 5;
  for (int i = 0; i < screenWidth * screenHeight; i++) {
    snake[i].x = -1;
    snake[i].y = -1;
  }
  snake[0].x = 10;
  snake[0].y = 10;
  for (uint8_t i = 1; i < snakeLength; i++) {
    snake[i].x = snake[i - 1].x - 1;
    snake[i].y = snake[i - 1].y;
  }
  score = 0;
  drawGame();
}


void spawnFood() {
  // 随机生成食物坐标
  do {
    food.x = random(screenWidth);
    food.y = random(screenHeight);
  } while (isSnakeAt(food.x, food.y));  // 如果和蛇身重合，则重新生成坐标
}

bool isSnakeAt(int16_t x, int16_t y) {
  // 检查蛇身是否在指定的坐标上
  for (uint8_t i = 0; i < snakeLength; i++) {
    if (snake[i].x == x && snake[i].y == y) {
      return true;
    }
  }
  return false;
}

void moveSnake() {
  // 打印当前蛇身和蛇头位置
// Serial.print("蛇身：");
// for (uint8_t i = 1; i < snakeLength; i++) {
//   Serial.print("(");
//   Serial.print(snake[i].x);
//   Serial.print(",");
//   Serial.print(snake[i].y);
//   Serial.print(")");
// }
// Serial.print(" 蛇头位置：(");
// Serial.print(snake[0].x);
// Serial.print(",");
// Serial.print(snake[0].y);
// Serial.println(")");

  // 保存蛇头位置
  Point head = snake[0];

  // 从尾部开始，移动蛇身
  for (uint8_t i = snakeLength - 1; i > 0; i--) {
    snake[i].x = snake[i - 1].x;
    snake[i].y = snake[i - 1].y;
  }

  // 将蛇头移动到新的位置
  switch (snakeDirection) {
    case UP:
      head.y--;
      break;
    case DOWN:
      head.y++;
      break;
    case LEFT:
      head.x--;
      break;
    case RIGHT:
      head.x++;
      break;
  }

  // 碰撞检测
  if (head.x < 0 || head.x >= screenWidth || head.y < 0 || head.y >= screenHeight) {
    // 蛇头撞墙
    zhuangqiang();
    // 等待按键输入
    while (digitalRead(buttonUpDown) == HIGH && digitalRead(buttonLeftRight) == HIGH) {
      delay(50);
    }
    resetGame();
    return;
  }

  for (int i = 1; i < snakeLength; i++) {
    if (head.x == snake[i].x && head.y == snake[i].y) {
      // 蛇头撞到自己身体
      Serial.println("吃自己了！！！！！！！！");
      chiziij();
      // 等待按键输入
      while (digitalRead(buttonUpDown) == HIGH && digitalRead(buttonLeftRight) == HIGH) {
        delay(50);
      }

      resetGame();
      return;
    }
  }

  // 将蛇头放回数组的第一个位置
  snake[0] = head;

  if (snake[0].x == food.x && snake[0].y == food.y) {
    // 吃到食物
    eatFood();
  }





  drawGame();
}

void eatFood() {
  // 增加蛇的长度
  snakeLength++;
  score += 10;  // 将分数增加10
  // 将新的蛇头位置添加到数组的第一个元素
  for (uint8_t i = snakeLength - 1; i > 0; i--) {
    snake[i].x = snake[i - 1].x;
    snake[i].y = snake[i - 1].y;
  }
  snake[0].x = food.x;
  snake[0].y = food.y;
  // 生成新的食物坐标
  spawnFood();
}

void drawGame() {
  display.setPartialWindow(0, 0, 296, 128);
  display.firstPage();

  display.fillScreen(GxEPD_WHITE);

  // 画蛇
  for (uint8_t i = 0; i < snakeLength; i++) {
    display.fillRect((snake[i].x * snakeSize) + wallOffsetX, (snake[i].y * snakeSize) + wallOffsetY, snakeSize, snakeSize, GxEPD_BLACK);
  }

  // 画食物
  display.fillRect((food.x * snakeSize) + wallOffsetX, (food.y * snakeSize) + wallOffsetY, snakeSize, snakeSize, GxEPD_BLACK);


  // 显示分数
  u8g2Fonts.setCursor(5, 15);
  u8g2Fonts.print("得分:");
  u8g2Fonts.setCursor(35, 15);
  u8g2Fonts.print(score);



  u8g2Fonts.setCursor(228, 15);
  u8g2Fonts.print("最高分:");
  u8g2Fonts.setCursor(270, 15);
  u8g2Fonts.print(highscore);

  u8g2Fonts.setCursor(109, 15);
  u8g2Fonts.print("贪吃蛇E-paper");

  display.drawRoundRect(4, 21, 288, 102, 2, GxEPD_BLACK);

  //Serial.println(score);

  display.nextPage();
}

void mainint() {

  display.setPartialWindow(0, 0, 296, 128);
  display.firstPage();
  display.fillScreen(GxEPD_WHITE);

  int logoX = 76;
  int logoY = 104;
  int moveStep = -8;  // 每次向上移动4个像素

  while (logoY > 24) {
    display.fillScreen(GxEPD_WHITE);
    logoY += moveStep;
    display.drawInvertedBitmap(logoX, logoY, logo, 143, 66, GxEPD_BLACK);
    u8g2Fonts.setCursor(95, logoY + 80);
    u8g2Fonts.print("按任意按键开始游戏");
    display.nextPage();
  }

  // 等待按键输入
  while (digitalRead(buttonUpDown) == HIGH && digitalRead(buttonLeftRight) == HIGH) {
    delay(50);
  }
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.firstPage();
  display.nextPage();
}

void zhuangqiang() {

  display.setPartialWindow(4, 21, 288, 103);
  display.firstPage();
  display.fillScreen(GxEPD_WHITE);
  u8g2Fonts.setCursor(80, 59);
  u8g2Fonts.print("怎么玩的，别往墙上撞啊!");
  if (score > highscore) {
    highscore = score;
    saveHighscore();
    u8g2Fonts.setCursor(61, 75);
    u8g2Fonts.print("但是恭喜获得新纪录：最高分");
    u8g2Fonts.setCursor(218, 75);
    u8g2Fonts.print(highscore);
  }


  display.nextPage();
}
void chiziij() {

  display.setPartialWindow(4, 21, 288, 103);
  display.firstPage();
  display.fillScreen(GxEPD_WHITE);
  u8g2Fonts.setCursor(80, 59);
  u8g2Fonts.print("吃食物啊，吃自己干嘛!");
  if (score > highscore) {
    highscore = score;
    saveHighscore();
    u8g2Fonts.setCursor(61, 75);
    u8g2Fonts.print("但是恭喜获得新纪录：最高分");
    u8g2Fonts.setCursor(218, 75);
    u8g2Fonts.print(highscore);
  }

  display.nextPage();
}

void saveHighscore() {
  File file = LittleFS.open("/highscore.txt", "w");
  if (file) {
    file.print(highscore);
    file.close();
  }
  //printHighscore();
}
void printHighscore() {
  File file = LittleFS.open("/highscore.txt", "r");
  if (file) {
    String content = file.readString();
    Serial.println(content);
    file.close();
  }
}