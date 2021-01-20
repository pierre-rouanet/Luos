/******************************************************************************
 * @file reception
 * @brief reception state machine
 * @author Luos
 * @version 0.0.0
 ******************************************************************************/
#include "reception.h"

#include <string.h>
#include <stdbool.h>
#include "luos_hal.h"
#include "target.h"
#include "transmission.h"
#include "msg_alloc.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#ifdef DEBUG
#include <stdio.h>
#endif

#define COLLISION_DETECTION_NUMBER 4
/*******************************************************************************
 * Variables
 ******************************************************************************/
uint16_t data_count = 0;
uint16_t data_size = 0;
uint16_t crc_val = 0;
msg_t *current_msg;

/*******************************************************************************
 * Function
 ******************************************************************************/

/******************************************************************************
 * @brief Reception init.
 * @param None
 * @return None
 ******************************************************************************/
void Recep_Init(void)
{
    // Initialize the reception state machine
    ctx.rx.status.unmap = 0;
    ctx.rx.callback = Recep_GetHeader;
    // Get allocation values
    current_msg = MsgAlloc_GetCurrentMsg();
}
/******************************************************************************
 * @brief Callback to get a complete header
 * @param data come from RX
 * @return None
 ******************************************************************************/
void Recep_GetHeader(volatile uint8_t *data)
{
    // Catch a byte.
    MsgAlloc_SetData(*data);
    data_count++;

    // Check if we have all we need.
    switch (data_count)
    {
    case 1: //reset CRC computation
        ctx.tx.lock = true;
        crc_val = 0xFFFF;
        break;

    case 3: //check if message is for the node
        if(Recep_NodeConcerned((header_t *)&current_msg->header) == false)
        {
            MsgAlloc_ValidHeader(false, data_size);
            ctx.rx.callback = Recep_Drop;
            return;
        }
        break;

    case (sizeof(header_t)): //Process at the header
#ifdef DEBUG
        printf("*******header data*******\n");
        printf("protocol : 0x%04x\n", current_msg->header.protocol);       /*!< Protocol version. */
        printf("target : 0x%04x\n", current_msg->header.target);           /*!< Target address, it can be (ID, Multicast/Broadcast, Type). */
        printf("target_mode : 0x%04x\n", current_msg->header.target_mode); /*!< Select targeting mode (ID, ID+ACK, Multicast/Broadcast, Type). */
        printf("source : 0x%04x\n", current_msg->header.source);           /*!< Source address, it can be (ID, Multicast/Broadcast, Type). */
        printf("cmd : 0x%04x\n", current_msg->header.cmd);                 /*!< msg definition. */
        printf("size : 0x%04x\n", current_msg->header.size);               /*!< Size of the data field. */
#endif
        // Reset the catcher.
        data_count = 0;
        // Switch state machiine to data reception
        ctx.rx.callback = Recep_GetData;
        // Cap size for big messages
        if (current_msg->header.size > MAX_DATA_MSG_SIZE)
        {
            data_size = MAX_DATA_MSG_SIZE;
        }
        else
        {
            data_size = current_msg->header.size;
        }

        if ((ctx.rx.status.rx_framing_error == false))
        {
            if (data_size)
            {
                MsgAlloc_ValidHeader(true, data_size);
            }
        }
        else
        {
            MsgAlloc_ValidHeader(false, data_size);
            ctx.rx.callback = Recep_Drop;
            return;
        }
        break;

    default:
        break;
    }
    LuosHAL_ComputeCRC((uint8_t *)data, (uint8_t *)&crc_val);
}
/******************************************************************************
 * @brief Callback to get a complete data
 * @param data come from RX
 * @return None
 ******************************************************************************/
void Recep_GetData(volatile uint8_t *data)
{
    MsgAlloc_SetData(*data);
    if (data_count < data_size)
    {
        // Continue CRC computation until the end of data
        LuosHAL_ComputeCRC((uint8_t *)data, (uint8_t *)&crc_val);
    }
    else if(data_count > data_size)
    {
        uint16_t crc = ((uint16_t)current_msg->data[data_size]) |
                       ((uint16_t)current_msg->data[data_size + 1] << 8);
        if (crc == crc_val)
        {
            if (((current_msg->header.target_mode == IDACK) || (current_msg->header.target_mode == NODEIDACK)))
            {
                Transmit_SendAck();
            }

            // Make an exception for reset detection command
            if (current_msg->header.cmd == RESET_DETECTION)
            {
                ctx.node.node_id = 0;
                PortMng_Init();
                MsgAlloc_Init(NULL);
            }
            else
            {
                MsgAlloc_EndMsg();
            }
        }
        else
        {
            ctx.rx.status.rx_error = TRUE;
            if ((current_msg->header.target_mode == IDACK) || (current_msg->header.target_mode == NODEIDACK))
            {
                Transmit_SendAck();
            }
            MsgAlloc_InvalidMsg();
        }
    }
    data_count++;
}
/******************************************************************************
 * @brief Callback to get a collision beetween RX and Tx
 * @param data come from RX
 * @return None
 ******************************************************************************/
void Recep_GetCollision(volatile uint8_t *data)
{
    // send all received datas
    if ((ctx.tx.data[data_count++] != *data) || (!ctx.tx.lock) || (ctx.rx.status.rx_framing_error == true))
    {
        //data dont match, or we don't start to send, there is a collision
        ctx.tx.collision = TRUE;
        //Stop TX trying to save input datas
        LuosHAL_SetTxState(false);
        // switch to get header.
        for(uint8_t i = 0; i < data_count-1; i++)
        {
            MsgAlloc_SetData(*ctx.tx.data + i);
        }
        MsgAlloc_SetData(*data);
        ctx.rx.callback = Recep_GetHeader;
        if(data_count >= 3)
        {
            if(Recep_NodeConcerned((header_t *)&current_msg->header) == false)
            {
                MsgAlloc_ValidHeader(false, data_size);
                ctx.rx.callback = Recep_Drop;
                return;
            }
        }
    }
    else
    {
        if(data_count == COLLISION_DETECTION_NUMBER)
        {
            LuosHAL_SetRxState(false);
            // switch to get header.
            ctx.rx.callback = Recep_GetHeader;
            return;
        }
    }
    LuosHAL_ComputeCRC((uint8_t *)data, (uint8_t *)&crc_val);
}
/******************************************************************************
 * @brief Callback to get a complete header
 * @param data come from RX
 * @return None
 ******************************************************************************/
void Recep_Drop(volatile uint8_t *data)
{
    return;
}
/******************************************************************************
 * @brief end of a reception
 * @param None
 * @return None
 ******************************************************************************/
void Recep_Timeout(void)
{
    if (ctx.rx.callback != Recep_GetHeader)
    {
        ctx.rx.status.rx_timeout = TRUE;
    }
    MsgAlloc_InvalidMsg();
    ctx.tx.lock = false;
    Recep_Reset();
}
/******************************************************************************
 * @brief reset the reception state machine
 * @param None
 * @return None
 ******************************************************************************/
void Recep_Reset(void)
{
    data_count = 0;
    crc_val = 0xFFFF;
    ctx.rx.status.rx_framing_error = false;
    LuosHAL_SetIrqState(false);
    ctx.rx.callback = Recep_GetHeader;
    LuosHAL_SetIrqState(true);
    LuosHAL_SetTxLockDetecState(true);
}
/******************************************************************************
 * @brief Catch ack when needed for the sended msg
 * @param data come from RX
 * @return None
 ******************************************************************************/
void Recep_CatchAck(volatile uint8_t *data)
{
    ctx.ack = *data;
    ctx.rx.callback = Recep_GetHeader;
}
/******************************************************************************
 * @brief Parse msg to find a module concerned
 * @param header of message
 * @return ll_container pointer
 ******************************************************************************/
ll_container_t *Recep_GetConcernedLLContainer(header_t *header)
{
    uint16_t i = 0;
    // Find if we are concerned by this message.
    switch (header->target_mode)
    {
    case IDACK:
    case ID:
        // Check all ll_container id
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            if ((header->target == ctx.ll_container_table[i].id))
            {
                return (ll_container_t *)&ctx.ll_container_table[i];
            }
        }
        break;
    case TYPE:
        // Check all ll_container type
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            if (header->target == ctx.ll_container_table[i].type)
            {
                return (ll_container_t *)&ctx.ll_container_table[i];
            }
        }
        break;
    case BROADCAST:
    case NODEIDACK:
    case NODEID:
        return (ll_container_t *)&ctx.ll_container_table[0];
        break;
    case MULTICAST: // For now Multicast is disabled
    default:
        return NULL;
        break;
    }
    return NULL;
}
/******************************************************************************
 * @brief Parse msg to find a module concerne
 * @param header of message
 * @return None
 ******************************************************************************/
uint8_t Recep_NodeConcerned(header_t *header)
{
    uint16_t i = 0;
    // Find if we are concerned by this message.
    switch (header->target_mode)
    {
    case IDACK:
        ctx.rx.status.rx_error = FALSE;
    case ID:
        // Check all ll_container id
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            if ((header->target == ctx.ll_container_table[i].id))
            {
                return true;
            }
        }
        break;
    case TYPE:
        // Check all ll_container type
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            if (header->target == ctx.ll_container_table[i].type)
            {
                return true;
            }
        }
        break;
    case BROADCAST:
        if (header->target == BROADCAST_VAL)
        {
            return true;
        }
        break;
    case NODEIDACK:
        ctx.rx.status.rx_error = FALSE;
    case NODEID:
        if ((header->target == 0) && (ctx.port.activ != NBR_PORT) && (ctx.port.keepLine == false))
        {
            return true; // discard message if ID = 0 and no Port activ
        }
        else
        {
            if ((header->target == ctx.node.node_id) && (header->target != 0))
            {
                return true;
            }
        }
        break;
    case MULTICAST: // For now Multicast is disabled
    default:
        return false;
        break;
    }
    return false;
}
/******************************************************************************
 * @brief Parse msg to find all modules concerned and create
 * @param msg pointer
 * @return None
 ******************************************************************************/
void Recep_InterpretMsgProtocol(msg_t *msg)
{
    uint16_t i = 0;
    // Find if we are concerned by this message.
    switch (msg->header.target_mode)
    {
    case IDACK:
    case ID:
        // Check all ll_container id
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            if (msg->header.target == ctx.ll_container_table[i].id)
            {
                MsgAlloc_LuosTaskAlloc((ll_container_t *)&ctx.ll_container_table[i], msg);
                return;
            }
        }
        break;
    case TYPE:
        // Check all ll_container type
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            if (msg->header.target == ctx.ll_container_table[i].type)
            {
                MsgAlloc_LuosTaskAlloc((ll_container_t *)&ctx.ll_container_table[i], msg);
                return;
            }
        }
        break;
    case BROADCAST:
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            MsgAlloc_LuosTaskAlloc((ll_container_t *)&ctx.ll_container_table[i], msg);
        }
        return;
        break;
    case MULTICAST:
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            if (Trgt_MulticastTargetBank((ll_container_t *)&ctx.ll_container_table[i], msg->header.target))
            {
                //TODO manage multiple slave concerned
                MsgAlloc_LuosTaskAlloc((ll_container_t *)&ctx.ll_container_table[i], msg);
                return;
            }
        }
        break;
    case NODEIDACK:
    case NODEID:
        if (msg->header.target == DEFAULTID) //on default ID it's always a luos command create only one task
        {
            MsgAlloc_LuosTaskAlloc((ll_container_t *)&ctx.ll_container_table[0], msg);
            return;
        }
        for (i = 0; i < ctx.ll_container_number; i++)
        {
            MsgAlloc_LuosTaskAlloc((ll_container_t *)&ctx.ll_container_table[i], msg);
        }
        return;
        break;
    default:
        break;
    }
}
