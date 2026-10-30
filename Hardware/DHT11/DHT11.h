#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f10x.h"                  // Device header

void DHT11_GPIO_OUT(void);
void DHT11_GPIO_IN(void);
void DHT11_ReadData(uint8_t *temp, uint8_t *humi);
uint8_t DHT11_ReadByte(void);

#endif
