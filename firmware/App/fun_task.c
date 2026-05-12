#include "fun_task.h"
#include "usart.h"
#include "main.h"
#include "fdcan.h"
#include "bsp_fdcan.h"
#include "dm_motor_ctrl.h"
#include "dm_motor_drv.h"
#include <string.h>

/* ======================== UART Buffers ======================== */

#define UART_RX_BUF_SIZE  256
#define UART_TX_BUF_SIZE  256

__attribute__((aligned(32))) uint8_t uart1_rx_buf[UART_RX_BUF_SIZE];
volatile uint8_t  uart1_rx_flag = 0;
volatile uint16_t uart1_rx_size = 0;

static uint8_t uart1_tx_buf[UART_TX_BUF_SIZE];

/* ======================== Helpers ======================== */

static void float_to_bytes(float val, uint8_t *buf)
{
    memcpy(buf, &val, 4);
}

static float bytes_to_float(const uint8_t *buf)
{
    float val;
    memcpy(&val, buf, 4);
    return val;
}

/* ======================== Protocol Response ======================== */

static void proto_send(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    uint16_t idx = 0;
    uart1_tx_buf[idx++] = PROTO_HEADER_0;
    uart1_tx_buf[idx++] = PROTO_HEADER_1;
    uart1_tx_buf[idx++] = cmd;
    uart1_tx_buf[idx++] = data_len;
    if (data_len > 0 && data)
    {
        memcpy(&uart1_tx_buf[idx], data, data_len);
        idx += data_len;
    }
    /* checksum: XOR of cmd + len + all data */
    uint8_t chk = cmd ^ data_len;
    for (uint8_t i = 0; i < data_len; i++)
        chk ^= data[i];
    uart1_tx_buf[idx++] = chk;
    uart1_tx_buf[idx++] = PROTO_TAIL_0;
    uart1_tx_buf[idx++] = PROTO_TAIL_1;
    HAL_UART_Transmit(&huart1, uart1_tx_buf, idx, 100);
}

static void send_ack(uint8_t cmd)
{
    uint8_t d = cmd;
    proto_send(RSP_ACK, &d, 1);
}

static void send_nack(uint8_t cmd, uint8_t err)
{
    uint8_t d[2] = {cmd, err};
    proto_send(RSP_NACK, d, 2);
}

static void send_motor_status(uint8_t id)
{
    if (id < 1 || id > MOTOR_COUNT)
    {
        send_nack(CMD_READ_STATUS, ERR_INVALID_ID);
        return;
    }
    motor_t *m = &motor[id - 1];
    uint8_t d[15];
    uint16_t idx = 0;
    d[idx++] = id;
    d[idx++] = (uint8_t)m->para.state;
    float_to_bytes(m->para.pos, &d[idx]); idx += 4;
    float_to_bytes(m->para.vel, &d[idx]); idx += 4;
    float_to_bytes(m->para.tor, &d[idx]); idx += 4;
    d[idx++] = (uint8_t)m->para.Tmos;
    d[idx++] = (uint8_t)m->para.Tcoil;
    proto_send(RSP_STATUS, d, (uint8_t)idx);
}

/* ======================== Motor API ======================== */

static int8_t motor_enable(uint8_t id)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    dm_motor_enable(&hfdcan1, &motor[id - 1]);
    return 0;
}

static int8_t motor_disable(uint8_t id)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    dm_motor_disable(&hfdcan1, &motor[id - 1]);
    return 0;
}

static int8_t motor_set_mit(uint8_t id, float pos, float vel, float kp, float kd, float tor)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    motor_t *m = &motor[id - 1];
    m->ctrl.mode   = mit_mode;
    m->ctrl.pos_set = pos;
    m->ctrl.vel_set = vel;
    m->ctrl.kp_set  = kp;
    m->ctrl.kd_set  = kd;
    m->ctrl.tor_set = tor;
    return 0;
}

static int8_t motor_set_pos(uint8_t id, float pos, float vel)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    motor_t *m = &motor[id - 1];
    m->ctrl.mode   = pos_mode;
    m->ctrl.pos_set = pos;
    m->ctrl.vel_set = vel;
    return 0;
}

static int8_t motor_set_spd(uint8_t id, float vel)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    motor_t *m = &motor[id - 1];
    m->ctrl.mode   = spd_mode;
    m->ctrl.vel_set = vel;
    return 0;
}

static int8_t motor_set_psi(uint8_t id, float pos, float vel, float cur)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    motor_t *m = &motor[id - 1];
    m->ctrl.mode   = psi_mode;
    m->ctrl.pos_set = pos;
    m->ctrl.vel_set = vel;
    m->ctrl.cur_set = cur;
    return 0;
}

static int8_t motor_save_zero(uint8_t id)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    motor_t *m = &motor[id - 1];
    dm_motor_disable(&hfdcan1, m);
    osDelay(100);
    save_pos_zero(&hfdcan1, m->id, MIT_MODE);
    osDelay(100);
    dm_motor_enable(&hfdcan1, m);
    return 0;
}

static int8_t motor_clear_error(uint8_t id)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    dm_motor_clear_err(&hfdcan1, &motor[id - 1]);
    return 0;
}

/* Lock: high-Kp MIT hold at current position */
static int8_t motor_lock(uint8_t id)
{
    if (id < 1 || id > MOTOR_COUNT) return -1;
    motor_t *m = &motor[id - 1];
    m->ctrl.mode   = mit_mode;
    m->ctrl.pos_set = m->para.pos; /* hold current position */
    m->ctrl.vel_set = 0.0f;
    m->ctrl.kp_set  = 100.0f;
    m->ctrl.kd_set  = 5.0f;
    m->ctrl.tor_set = 0.0f;
    return 0;
}

/* ======================== Command Dispatch ======================== */

static void dispatch_for_all(uint8_t cmd, int8_t (*fn)(uint8_t))
{
    for (uint8_t i = 1; i <= MOTOR_COUNT; i++)
        fn(i);
    send_ack(cmd);
}

static void proto_dispatch(uint8_t cmd, const uint8_t *data, uint8_t dlen)
{
    switch (cmd)
    {
    /* ---- Enable / Disable ---- */
    case CMD_ENABLE:
        if (dlen < 1) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (data[0] == 0) { dispatch_for_all(cmd, motor_enable); return; }
        if (motor_enable(data[0]) != 0) { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    case CMD_DISABLE:
        if (dlen < 1) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (data[0] == 0) { dispatch_for_all(cmd, motor_disable); return; }
        if (motor_disable(data[0]) != 0) { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    /* ---- MIT mode: id + 5 floats = 21 bytes ---- */
    case CMD_MIT_CTRL:
        if (dlen < 21) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (motor_set_mit(data[0],
                          bytes_to_float(&data[1]),  bytes_to_float(&data[5]),
                          bytes_to_float(&data[9]),  bytes_to_float(&data[13]),
                          bytes_to_float(&data[17])) != 0)
        { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    /* ---- Position mode: id + 2 floats = 9 bytes ---- */
    case CMD_POS_CTRL:
        if (dlen < 9) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (motor_set_pos(data[0],
                          bytes_to_float(&data[1]),
                          bytes_to_float(&data[5])) != 0)
        { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    /* ---- Speed mode: id + 1 float = 5 bytes ---- */
    case CMD_SPD_CTRL:
        if (dlen < 5) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (motor_set_spd(data[0], bytes_to_float(&data[1])) != 0)
        { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    /* ---- PSI mode: id + 3 floats = 13 bytes ---- */
    case CMD_PSI_CTRL:
        if (dlen < 13) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (motor_set_psi(data[0],
                          bytes_to_float(&data[1]),
                          bytes_to_float(&data[5]),
                          bytes_to_float(&data[9])) != 0)
        { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    /* ---- Read status ---- */
    case CMD_READ_STATUS:
        if (dlen < 1) { send_nack(cmd, ERR_INVALID_LEN); return; }
        send_motor_status(data[0]);
        break;

    /* ---- Save zero position ---- */
    case CMD_SAVE_ZERO:
        if (dlen < 1) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (data[0] == 0) { dispatch_for_all(cmd, motor_save_zero); return; }
        if (motor_save_zero(data[0]) != 0) { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    /* ---- Clear error ---- */
    case CMD_CLEAR_ERR:
        if (dlen < 1) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (data[0] == 0) { dispatch_for_all(cmd, motor_clear_error); return; }
        if (motor_clear_error(data[0]) != 0) { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    /* ---- Lock motor ---- */
    case CMD_LOCK_MOTOR:
        if (dlen < 1) { send_nack(cmd, ERR_INVALID_LEN); return; }
        if (data[0] == 0) { dispatch_for_all(cmd, motor_lock); return; }
        if (motor_lock(data[0]) != 0) { send_nack(cmd, ERR_INVALID_ID); return; }
        send_ack(cmd);
        break;

    /* ---- All 6 motors: position control (MIT mode, 24 bytes) ---- */
    case CMD_ALL_POS:
        if (dlen < 24) { send_nack(cmd, ERR_INVALID_LEN); return; }
        for (uint8_t i = 0; i < MOTOR_COUNT; i++)
        {
            motor[i].ctrl.mode   = mit_mode;
            motor[i].ctrl.pos_set = bytes_to_float(&data[i * 4]);
            motor[i].ctrl.vel_set = 0.0f;
            motor[i].ctrl.kp_set  = 1.0f;
            motor[i].ctrl.kd_set  = 0.5f;
            motor[i].ctrl.tor_set = 0.0f;
        }
        send_ack(cmd);
        break;

    /* ---- All 6 motors: torque control (MIT mode, 24 bytes) ---- */
    case CMD_ALL_TORQUE:
        if (dlen < 24) { send_nack(cmd, ERR_INVALID_LEN); return; }
        for (uint8_t i = 0; i < MOTOR_COUNT; i++)
        {
            motor[i].ctrl.mode   = mit_mode;
            motor[i].ctrl.pos_set = 0.0f;
            motor[i].ctrl.vel_set = 0.0f;
            motor[i].ctrl.kp_set  = 0.0f;
            motor[i].ctrl.kd_set  = 0.0f;
            motor[i].ctrl.tor_set = bytes_to_float(&data[i * 4]);
        }
        send_ack(cmd);
        break;

    /* ---- All 6 motors: full MIT (120 bytes) ---- */
    case CMD_ALL_MIT:
        if (dlen < 120) { send_nack(cmd, ERR_INVALID_LEN); return; }
        for (uint8_t i = 0; i < MOTOR_COUNT; i++)
        {
            uint16_t off = i * 20;
            motor[i].ctrl.mode   = mit_mode;
            motor[i].ctrl.pos_set = bytes_to_float(&data[off]);
            motor[i].ctrl.vel_set = bytes_to_float(&data[off + 4]);
            motor[i].ctrl.kp_set  = bytes_to_float(&data[off + 8]);
            motor[i].ctrl.kd_set  = bytes_to_float(&data[off + 12]);
            motor[i].ctrl.tor_set = bytes_to_float(&data[off + 16]);
        }
        send_ack(cmd);
        break;

    default:
        send_nack(cmd, ERR_UNKNOWN_CMD);
        break;
    }
}

/* ======================== Frame Parser ======================== */

static void proto_parse(const uint8_t *buf, uint16_t len)
{
    /* minimum frame = header(2)+cmd(1)+len(1)+chk(1)+tail(2) = 7 */
    if (len < 7) return;

    /* find header */
    uint16_t i = 0;
    while (i <= len - 7)
    {
        if (buf[i] == PROTO_HEADER_0 && buf[i + 1] == PROTO_HEADER_1)
            break;
        i++;
    }
    if (i > len - 7) return;

    uint8_t cmd     = buf[i + 2];
    uint8_t data_len = buf[i + 3];
    uint16_t frame_len = 2 + 1 + 1 + data_len + 1 + 2; /* hdr+cmd+len+data+chk+tail */

    if (i + frame_len > len) return; /* incomplete */

    const uint8_t *data = &buf[i + 4];
    uint8_t chk    = buf[i + 4 + data_len];
    uint8_t tail0  = buf[i + 4 + data_len + 1];
    uint8_t tail1  = buf[i + 4 + data_len + 2];

    if (tail0 != PROTO_TAIL_0 || tail1 != PROTO_TAIL_1) return;

    uint8_t calc = cmd ^ data_len;
    for (uint8_t j = 0; j < data_len; j++)
        calc ^= data[j];
    if (calc != chk)
    {
        send_nack(cmd, ERR_CHECKSUM);
        return;
    }

    proto_dispatch(cmd, data, data_len);
}

/* ======================== Motor Init ======================== */

static void motor_init_all(void)
{
    dm_motor_init();
    for (uint8_t i = 0; i < MOTOR_COUNT; i++)
    {
        motor[i].ctrl.mode   = mit_mode;
        motor[i].ctrl.kp_set  = 1.0f;
        motor[i].ctrl.kd_set  = 0.5f;
        motor[i].ctrl.pos_set = 0.0f;
        motor[i].ctrl.vel_set = 0.0f;
        motor[i].ctrl.tor_set = 0.0f;
    }
}

/* ======================== Test ======================== */

static void smooth_zero_test(void)
{
    const float kp = 1.0f;
    const float kd = 0.5f;
    const float vel_limit = 2.0f;
    const float threshold = 0.05f;
    const uint32_t settle_ms = 3000;

    // 使能
    for (uint8_t id = 1; id <= 4; id++)
    {
        dm_motor_enable(&hfdcan1, &motor[id - 1]);
        osDelay(100);
    }
    osDelay(1000);

    /* set target=0, let motor PD handle the motion */
    for (uint8_t id = 1; id <= 4; id++)
        motor_set_mit(id, 0.0f, vel_limit, kp, kd, 0.0f);

    uint32_t elapsed = 0;
    while (elapsed < settle_ms)
    {
        for (uint8_t id = 1; id <= 4; id++)
        {
            if (motor[id - 1].ctrl.mode != 0)
                dm_motor_ctrl_send(&hfdcan1, &motor[id - 1]);
        }
        osDelay(MOTOR_CTRL_PERIOD);
        elapsed += MOTOR_CTRL_PERIOD;
    }
}

/* mode 0: set-target-once, motor PD handles smooth tracking */
static void control_test_direct(void)
{
    const float kp = 1.0f;
    const float kd = 0.5f;
    const float vel_limit = 2.0f;
    const float waypoints[] = {
        -3.1415f, 0.0f, 3.1415f, 0.0f
    };
    const uint32_t move_ms  = 4000;
    const uint32_t pause_ms = 1000;

    // for (uint8_t id = 1; id <= 4; id++)
    // {
    //     dm_motor_enable(&hfdcan1, &motor[id - 1]);
    //     osDelay(100);
    // }
    // osDelay(2000);

    for (;;)
    {
        for (int p = 0; p < 4; p++)
        {
            for (uint8_t id = 1; id <= 4; id++)
                motor_set_mit(id, waypoints[p], vel_limit, kp, kd, 0.0f);

            uint32_t elapsed = 0;
            while (elapsed < move_ms)
            {
                for (uint8_t id = 1; id <= 4; id++)
                {
                    if (motor[id - 1].ctrl.mode != 0)
                        dm_motor_ctrl_send(&hfdcan1, &motor[id - 1]);
                }
                osDelay(MOTOR_CTRL_PERIOD);
                elapsed += MOTOR_CTRL_PERIOD;
            }
        }
        osDelay(pause_ms);
    }
}

/* mode 1: linear position interpolation */
static void control_test_interp(void)
{
    const float kp = 5.0f;
    const float kd = 0.5f;
    const float waypoints[] = {-3.1415f, 3.1415f, 0.0f};
    const uint32_t ramp_ms  = 3000;
    const uint32_t pause_ms = 1000;

    // for (uint8_t id = 1; id <= 4; id++)
    // {
    //     dm_motor_enable(&hfdcan1, &motor[id - 1]);
    //     osDelay(100);
    // }
    // osDelay(2000);

    for (;;)
    {
        for (int p = 0; p < 3; p++)
        {
            float start_pos = (p == 0) ? 0.0f : waypoints[p - 1];
            float end_pos   = waypoints[p];
            uint32_t elapsed = 0;

            while (elapsed < ramp_ms)
            {
                float t = (float)elapsed / (float)ramp_ms;
                float pos = start_pos + (end_pos - start_pos) * t;

                for (uint8_t id = 1; id <= 4; id++)
                {
                    motor_set_mit(id, pos, 0.0f, kp, kd, 0.0f);
                    dm_motor_ctrl_send(&hfdcan1, &motor[id - 1]);
                }
                osDelay(MOTOR_CTRL_PERIOD);
                elapsed += MOTOR_CTRL_PERIOD;
            }
        }
        osDelay(pause_ms);
    }
}

static void control_test(uint8_t mode)
{
    if (mode == 0)
        control_test_direct();
    else
        control_test_interp();
}

/* ======================== Task Entry ======================== */

void FunTask_Entry(void const *argument)
{
    /* CAN: configure FDCAN1 for classic CAN 1Mbps */
    bsp_fdcan_set_baud(&hfdcan1, CAN_CLASS, CAN_BR_1M);
    bsp_can_init();

    /* Init motor structs for 6 motors */
    motor_init_all();

    /* Start UART1 DMA idle-line receive */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart1_rx_buf, UART_RX_BUF_SIZE);

    smooth_zero_test();
    osDelay(500);
    control_test(0);
#if 0
    for (;;)
    {
        /* ---- Process UART commands ---- */
        if (uart1_rx_flag)
        {
            uart1_rx_flag = 0;
            SCB_InvalidateDCache_by_Addr((uint32_t *)uart1_rx_buf, UART_RX_BUF_SIZE);
            proto_parse(uart1_rx_buf, uart1_rx_size);
            HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart1_rx_buf, UART_RX_BUF_SIZE);
        }

        /* ---- Send motor control commands ---- */
        for (uint8_t i = 0; i < MOTOR_COUNT; i++)
        {
            if (motor[i].ctrl.mode != 0)
                dm_motor_ctrl_send(&hfdcan1, &motor[i]);
        }

        osDelay(MOTOR_CTRL_PERIOD);
    }
#endif
}

/* ======================== UART DMA Callback ======================== */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        uart1_rx_size = Size;
        uart1_rx_flag = 1;
    }
}
