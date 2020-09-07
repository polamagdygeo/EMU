/*
 * emulated_eeprom.h
 *
 *  Created on: Aug 24, 2020
 *      Author: Pola
 */

#ifndef EMULATED_EEPROM_H_
#define EMULATED_EEPROM_H_

#include <stdint.h>

void Emulated_EEPROM_init(void);
uint8_t Emulated_EEPROM_ReadHalfWord(const uint32_t logical_addr,uint16_t *const p_half_word);
uint8_t Emulated_EEPROM_WriteHalfWord(const uint32_t logical_addr,const uint16_t half_word);

#endif /* EMULATED_EEPROM_H_ */
