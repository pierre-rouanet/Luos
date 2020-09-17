#include <luos_board.h>
#include <string.h>
#include "node_config.h"
#include <Arduino.h>



int node_msg_handler(module_t *module, msg_t *input, msg_t *output)
{
    if (input->header.cmd == NODE_LED)
    {
        if (input->data[0] < 2)
        {
            status_led(input->data[0]);
            return LUOS_PROTOCOL_NB;
        }
    }
    
}

void status_led(char state)
{
    digitalWrite(LED_BUILTIN,(state!=0)); //we use the Arduino builtin led
}

void node_init(void)
{
    // ********************* led Gpio ****************************
    pinMode(LED_BUILTIN,OUTPUT);
}

void node_loop(void) {}

// ******** Alias management ****************
void write_alias(unsigned short local_id, char *alias)
{
    const uint16_t addr = local_id * (MAX_ALIAS_SIZE + 1);
    for (uint8_t i = 0; i < MAX_ALIAS_SIZE; i++)
    {
        eeprom_write_word(addr + i,(uint16_t)alias[i]);
    }
}

char read_alias(unsigned short local_id, char *alias)
{
    const uint16_t addr = local_id * (MAX_ALIAS_SIZE + 1);
    uint16_t data;
    data = eeprom_read_word(addr);
    // Check name integrity
    if (((((char)data < 'A') | ((char)data > 'Z')) & (((char)data < 'a') | ((char)data > 'z'))) | ((char)data == '\0'))
    {
        return 0;
    }
    else
    {
        alias[0] = (char)data;
    }
    for (uint8_t i = 1; i < MAX_ALIAS_SIZE; i++)
    {
        data = eeprom_read_word(addr+i);
        alias[i] = (char)data;
    }
    return 1;
}

/**
 * \fn void board_disable_irq(void)
 * \brief disable IRQ
 *
 * \return error
 */
void node_disable_irq(void)
{
    noInterrupts();
}

/**
 * \fn void board_enable_irq(void)
 * \brief enable IRQ
 *
 * \return error
 */
void node_enable_irq(void)
{
    interrupts();
}
