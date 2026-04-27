#ifndef __CAN_BSP_H__
#define __CAN_BSP_H__
/******************************** 弃用 *********************************/
#include "main.h"
#include "fdcan.h"

void can_bsp_init(void);
void std_can_filter_init(void);
uint8_t canx_send_data(FDCAN_HandleTypeDef *hfdcan, uint16_t id, uint8_t *data, uint32_t len);
uint8_t canx_receive(FDCAN_HandleTypeDef *hfdcan, uint8_t *buf);
void can1_rx_callback(void);
void can2_rx_callback(void);
void can3_rx_callback(void);
#endif /* __CAN_BSP_H_ */

