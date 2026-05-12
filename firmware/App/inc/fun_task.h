#ifndef __FUN_TASK_H__
#define __FUN_TASK_H__

#include "cmsis_os.h"

/* ======================== UART Protocol Definitions ======================== */
/* Frame: [0x5A][0xA5][CMD][LEN][DATA...][CHK][0x0D][0x0A]
 * CHK = XOR of CMD, LEN, and all DATA bytes
 * LEN = number of DATA bytes (excluding header/cmd/len/chk/tail)
 */

#define PROTO_HEADER_0    0x5A
#define PROTO_HEADER_1    0xA5
#define PROTO_TAIL_0      0x0D
#define PROTO_TAIL_1      0x0A

/* Command codes */
#define CMD_ENABLE         0x01  /* DATA: [motor_id] (0=all) */
#define CMD_DISABLE        0x02  /* DATA: [motor_id] (0=all) */
#define CMD_MIT_CTRL       0x03  /* DATA: [id, pos(4f), vel(4f), kp(4f), kd(4f), tor(4f)] */
#define CMD_POS_CTRL       0x04  /* DATA: [id, pos(4f), vel(4f)] */
#define CMD_SPD_CTRL       0x05  /* DATA: [id, vel(4f)] */
#define CMD_PSI_CTRL       0x06  /* DATA: [id, pos(4f), vel(4f), cur(4f)] */
#define CMD_READ_STATUS    0x07  /* DATA: [motor_id] */
#define CMD_SAVE_ZERO      0x08  /* DATA: [motor_id] (0=all) */
#define CMD_CLEAR_ERR      0x09  /* DATA: [motor_id] (0=all) */
#define CMD_LOCK_MOTOR     0x0A  /* DATA: [motor_id] (0=all) */
#define CMD_ALL_POS        0x10  /* DATA: [6x pos(4f)] = 24B */
#define CMD_ALL_TORQUE     0x11  /* DATA: [6x tor(4f)] = 24B */
#define CMD_ALL_MIT        0x12  /* DATA: [6x(pos,vel,kp,kd,tor)(4f)] = 120B */

/* Response codes */
#define RSP_STATUS         0x80  /* DATA: [id, state, pos(4f), vel(4f), tor(4f), Tmos, Tcoil] */
#define RSP_ACK            0xFE  /* DATA: [cmd] */
#define RSP_NACK           0xFF  /* DATA: [cmd, err] */

/* NACK error codes */
#define ERR_INVALID_ID     0x01
#define ERR_INVALID_LEN    0x02
#define ERR_UNKNOWN_CMD    0x03
#define ERR_CHECKSUM       0x04

#define MOTOR_COUNT        6
#define MOTOR_CTRL_PERIOD  5     /* ms, 200Hz control loop */

void FunTask_Entry(void const *argument);

#endif /* __FUN_TASK_H__ */
