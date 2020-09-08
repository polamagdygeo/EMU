/**
 ******************************************************************************
 * @file           : Emulated EEPROM
 * @brief          :
 ******************************************************************************
 */

#ifndef EMULATED_EEPROM_H_
#define EMULATED_EEPROM_H_

/** @defgroup Emulated_EEPROM_Module
  * @brief
  * @{
  */

#include <stdint.h>

/* Exported functions -------------------------------------------------------*/
/** @defgroup Emulated_EEPROM_Module_Exported_FunctionsPrototype Emulated_EEPROM_Module_Exported_FunctionsPrototype
  * @brief Public functions declaration.
  * @{
  */
void Emulated_EEPROM_init(void);
uint8_t Emulated_EEPROM_ReadHalfWord(const uint32_t logical_addr,uint16_t *const p_half_word);
uint8_t Emulated_EEPROM_WriteHalfWord(const uint32_t logical_addr,const uint16_t half_word);

/**
  * @}
  */

/**
  * @}
  */
#endif /* EMULATED_EEPROM_H_ */
