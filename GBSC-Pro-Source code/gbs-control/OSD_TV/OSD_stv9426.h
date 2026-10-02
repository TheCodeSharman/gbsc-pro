/*
   STV9426 library for ESP8266 for arduino ide operation

   Created in 0:50 16.02.2024
   By Karabanov Aleksandr
   https://www.youtube.com/channel/UCkNk5gQMIu8k9xW-uENsBzw

*/

#ifndef OSD_STV9426_H_
#define OSD_STV9426_H_

#include "../src/osd/OSD.h"

#define ADDR_STV 0x5D

//Линии сканирования N1
#define A1_lines 0x43
#define A2_lines 0x00
#define A3_lines 0xD0

//Линии сканирования N2
#define B1_lines 0x45
#define B2_lines 0x00
#define B3_lines 0xFE

//Линии сканирования N3
#define C1_lines 0x47
#define C2_lines 0x00
#define C3_lines 0xF3

//Линии сканирования N0
#define D1_lines 0x40
#define D2_lines 0x00
#define D3_lines 0x3A

//ДЛИТЕЛЬНОСТЬ СТРОКИ (Движение по горизонтали)
#define A1_parameters 0xF0
#define A2_parameters 0x3F
#define A3_parameters 0x3F

//ГОРИЗОНТАЛЬНАЯ ЗАДЕРЖКА (Движение по горизонтали)
#define B1_parameters 0xF1
#define B2_parameters 0x3F
#define B3_parameters 0x26

//ВЫСОТА СИМВОЛОВ
#define C1_parameters 0xF2
#define C2_parameters 0x3F
#define C3_parameters 0x15

//УМНОЖИТЕЛЬ ЧАСТОТЫ (Помогает стабилизировать картинку)
//#define Z1_parameters 0xF7
//#define Z2_parameters 0x3F
//#define Z3_parameters 0x09

//НАЧАЛЬНЫЙ ПЕРИОД ПИКСЕЛЯ
#define D1_parameters 0xF6
#define D2_parameters 0x3F
#define D3_parameters 0x02

//LOCKING CONDITION TIME CONSTANT (Убирает желе)
#define E1_parameters 0xF4
#define E2_parameters 0x3F
#define E3_parameters 0x02

//CAPTURE PROCESS TIME CONSTANT (Захват)
#define G1_parameters 0xF5
#define G2_parameters 0x3F
#define G3_parameters 0x03

//DISPLAYCONTROL (Управление дисплеем )
#define H1_parameters 0xF3
#define H2_parameters 0x3F
#define H3_parameters 0x81

//------------------------------delivery of settings for STV9426-------------------------------//
void OSD_parameters(char A_data, char B_data, char C_data ) {
  Wire.beginTransmission(ADDR_STV);
  Wire.write(A_data);//LSB
  Wire.write(B_data);//MSB
  Wire.write(C_data);//
  Wire.endTransmission();
}

//-----------------------------------------Settings-----------------------------------------//
void OSD() {
  for (byte SPACING = 0x40; SPACING < 0x48; ++SPACING) {//48
    OSD_parameters(SPACING, 0x00, 0x00);
  }
  OSD_parameters(A1_lines, A2_lines, A3_lines); // Линии сканирования N1
  OSD_parameters(B1_lines, B2_lines, B3_lines); // Линии сканирования N2
  OSD_parameters(C1_lines, C2_lines, C3_lines); // Линии сканирования N3
  OSD_parameters(D1_lines, D2_lines, D3_lines); // Линии сканирования N0
  OSD_parameters(A1_parameters, A2_parameters, A3_parameters); // 行长
  OSD_parameters(B1_parameters, B2_parameters, B3_parameters); // 水平延迟
  OSD_parameters(C1_parameters, C2_parameters, C3_parameters); // 字符高度
  OSD_parameters(D1_parameters, D2_parameters, D3_parameters); // 像素的初始周期
  OSD_parameters(E1_parameters, E2_parameters, E3_parameters); // 锁定条件时间常数
  OSD_parameters(G1_parameters, G2_parameters, G3_parameters); // 捕捉过程时间常数
  OSD_parameters(H1_parameters, H2_parameters, H3_parameters); // 显示控制
}

//*************************************FBLK clear**********************************************//
void OSD_symbols_1() {
  for (byte addressC1 = 0x00; addressC1 < 0x3F; ++addressC1) 
  { //0xFF
    OSD_parameters(addressC1, 0x00, 0x00);
  }
}
void OSD_Cut_0x01() {
  for (byte addressC2 = 0x00; addressC2 < 0xFF; ++addressC2) 
  { //0xFF
    OSD_parameters(addressC2, 0x00, 0xC0);
  }
}
void OSD_symbols_2() 
{
  for (byte addressP = 0x00; addressP < 0xFF; ++addressP) 
  { //0xFF
    OSD_parameters(addressP, 0x02, 0x00);
  }
}
void OSD_Cut_0x02() 
{
  for (byte addressP = 0x00; addressP < 0xFF; ++addressP) 
  { //0xFF
    OSD_parameters(addressP, 0x02, 0xC0);
  }
}
void OSD_symbols_3() {
  for (byte addressC = 0x00; addressC < 0xFF; ++addressC) { //0xFF
    OSD_parameters(addressC, 0x03, 0x00);
  }
}
void OSD_Cut_0x03() {
  for (byte addressC = 0x00; addressC < 0xFF; ++addressC) { //0xFF
    OSD_parameters(addressC, 0x03, 0xC0);
  }
}

//***************************************OSD clear*********************************************//
void OSD_clear() {
  OSD_symbols_1();
  OSD_symbols_2();
  OSD_symbols_3();
  OSD_Cut_0x01();
  OSD_Cut_0x02();
  OSD_Cut_0x03();

  // The part no longer holds what Osd::OSD last sent it, so the next row goes
  // on whole rather than as the cells that moved.
  Osd::OSD::forget();
}

#endif
