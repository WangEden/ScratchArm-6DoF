/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "adc.h"
#include "dma.h"
#include "fdcan.h"
#include "octospi.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "vofa.h"
#include "w25q64.h"
#include <stdio.h>
#include <string.h>
#include "bsp_fdcan.h"
#include "dm_motor_ctrl.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RESPONSE_OK(__MODE__) do {\
  memset(tx_temp_buffer, 0, RX_LEN);\
  memcpy(tx_temp_buffer, __MODE__, status_code_len);\
  tx_temp_buffer[status_code_len + 1] = '\r';\
  tx_temp_buffer[status_code_len + 2] = 0;\
  HAL_UART_Transmit(&huart1, tx_temp_buffer, status_code_len + 2, 100);\
} while (0);
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
#define W25Qxx_NumByteToTest   	32*1024					// 测试数据的长度，32K

int32_t OSPI_Status; 		 //检测标志位

uint8_t  W25Qxx_WriteBuffer[W25Qxx_NumByteToTest];		//	写数据数组
uint8_t  W25Qxx_ReadBuffer[W25Qxx_NumByteToTest];		//	读数据数组

// **********************************缓冲区定义**********************************
#define RX_LEN 32*8*4 // 1KB接收缓冲区大小
__attribute__((aligned(32))) uint8_t rx_buffer[RX_LEN]; // H7要求32字节对齐
uint8_t tx_temp_buffer[RX_LEN]; // 发送1次时的缓冲区
uint8_t can1_id[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}; // CAN1~6号CAN ID
uint8_t can1_tx_data[8] = {0x7F, 0xFF, 0x7F, 0xF0, 0x00, 0x00, 0x07, 0xFF}; // CAN1发送数据缓冲区
const char *cmd_ok = "CMDOK"; // 命令执行成功响应
const char *can_ok = "CANOK"; // CAN执行成功响应
const char *power_ok = "PWROK"; // 电源执行成功响应
const char *cmd_err = "ERROR"; // 命令执行失败响应
char *dm_init_status = "dmok1";
uint8_t status_code_len = 5;
const char *prefix = "Recv:"; // 接收数据前缀
uint8_t prefix_len = 0; // 前缀长度
uint8_t can_send_req_flag = 0; // 发送CAN数据请求标志位，volatile防止编译器优化
uint8_t can_data_buffer[RX_LEN]; // CAN接收数据缓冲区，存储STM32接收到的数据
extern uint8_t rx_data1[8]; // CAN1接收数据缓冲区，存储STM32接收到的数据
uint8_t last_can_rx_data1[8] = {0};
uint16_t uart1_rx_size = 0;
uint8_t pos_code = 0;
// **********************************缓冲区定义**********************************
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/***************************************************************************************************
*	函数名称: OSPI_W25Qxx_Test
*	输入参数: 无
*	返回值: OSPI_W25Qxx_OK - 操作成功通过
*	函数功能: 测试外部SPI Flash的读写速度
*	说明    : 无	
***************************************************************************************************/

int8_t OSPI_W25Qxx_Test(void)		//Flash读写测试
{
    uint32_t i = 0X8000;	// 计数变量
    uint32_t W25Qxx_TestAddr  =	0	;							// 测试地址	
    uint32_t ExecutionTime_Begin;		// 开始时间
    uint32_t ExecutionTime_End;		// 结束时间
    uint32_t ExecutionTime;				// 执行时间	
    float    ExecutionSpeed;			// 执行速度

// 擦除 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   
    
    
    ExecutionTime_Begin 	= HAL_GetTick();	// 获取 systick 当前时间，单位ms
    OSPI_Status 			= OSPI_W25Qxx_BlockErase_32K(W25Qxx_TestAddr);	// 擦除32K字节
    ExecutionTime_End		= HAL_GetTick();	// 获取 systick 当前时间，单位ms
    
    ExecutionTime = ExecutionTime_End - ExecutionTime_Begin; // 计算擦除时间，单位ms
    
    if( OSPI_Status == OSPI_W25Qxx_OK )
    {
        printf ("\r\nW25Q64 erase succeed, time: %d ms\r\n",ExecutionTime);		
    }
    else
    {
        printf ("\r\n erase error!!!!!  ERROR CODE:%d\r\n",OSPI_Status);
        while (1);
    }   
    
// 写入 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 

    for(i=0;i<W25Qxx_NumByteToTest;i++)  //先将数据写入数组
    {
        W25Qxx_WriteBuffer[i] = i;
    }
    ExecutionTime_Begin 	= HAL_GetTick();	// 获取 systick 当前时间，单位ms
    OSPI_Status				= OSPI_W25Qxx_WriteBuffer(W25Qxx_WriteBuffer,W25Qxx_TestAddr,W25Qxx_NumByteToTest); // 写数据
    ExecutionTime_End		= HAL_GetTick();	// 获取 systick 当前时间，单位ms
    
    ExecutionTime  = ExecutionTime_End - ExecutionTime_Begin; 		// 计算擦除时间，单位ms
    ExecutionSpeed = (float)W25Qxx_NumByteToTest / ExecutionTime ; // 计算写入速度，单位 KB/S
    if( OSPI_Status == OSPI_W25Qxx_OK )
    {
        printf ("\r\nwrite succeed, data size: %d KB, time: %d ms, speed: %.2f KB/s\r\n",W25Qxx_NumByteToTest/1024,ExecutionTime,ExecutionSpeed);		
    }
    else
    {
        printf ("\r\nwrite error!!!!!  error code: %d\r\n",OSPI_Status);
        while (1);
    }	
    
// 读取	>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 

    
    ExecutionTime_Begin 	= HAL_GetTick();	// 获取 systick 当前时间，单位ms	
    OSPI_Status				= OSPI_W25Qxx_ReadBuffer(W25Qxx_ReadBuffer,W25Qxx_TestAddr,W25Qxx_NumByteToTest);	// 读取数据
    ExecutionTime_End		= HAL_GetTick();	// 获取 systick 当前时间，单位ms
    
    ExecutionTime  = ExecutionTime_End - ExecutionTime_Begin; 					// 计算执行时间，单位ms
    ExecutionSpeed = (float)W25Qxx_NumByteToTest/1024/1024 / ExecutionTime*1000 ; 	// 计算读取速度，单位 MB/S 
    
    if( OSPI_Status == OSPI_W25Qxx_OK )
    {
        printf ("\r\nread succeed, size: %d KB, time: %d ms, speed: %.2f MB/s \r\n",W25Qxx_NumByteToTest/1024,ExecutionTime,ExecutionSpeed);		
    }
    else
    {
        printf ("\r\nread error!!!!!  error code:%d\r\n",OSPI_Status);
        while (1);
    }   
// 数据校验 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   
    
    for(i=0;i<W25Qxx_NumByteToTest;i++)	//验证读取数据是否与写入数据一致
    {
        if( W25Qxx_WriteBuffer[i] != W25Qxx_ReadBuffer[i] )	//数据不一致，返回0	
        {
            printf ("\r\ndata check error!!!!!pos: %d\r\n",i);	
            while(1);
        }
    }   
    printf ("\r\ncheck pass!!!!!\r\n"); 
    
// 读取整片Flash的数据，用以测试速度 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    
    printf ("\r\n*****************************************************************************************************\r\n");		
    printf ("\r\nIn the above test, the data read is relatively small and takes a short time. In addition, the minimum unit of measurement is ms, and the calculated reading speed has a large error.\r\n");		
    printf ("\r\nNext, read the entire flash data to test the speed. The speed error obtained in this way is relatively small.\r\n");		
    printf ("\r\nread start>>>>\r\n");		
    ExecutionTime_Begin 	= HAL_GetTick();	// 获取 systick 当前时间，单位ms		
    
    for(i=0;i<W25Qxx_FlashSize/(W25Qxx_NumByteToTest);i++)	// 每次读取 W25Qxx_NumByteToTest 个字节的数据
    {
        OSPI_Status     = OSPI_W25Qxx_ReadBuffer(W25Qxx_ReadBuffer,W25Qxx_TestAddr,W25Qxx_NumByteToTest);	// 读取数据
        W25Qxx_TestAddr = W25Qxx_TestAddr + W25Qxx_NumByteToTest;		
    }
    ExecutionTime_End   = HAL_GetTick();	// 获取 systick 当前时间，单位ms
    
    ExecutionTime       = ExecutionTime_End - ExecutionTime_Begin; 								// 计算执行时间，单位ms
    ExecutionSpeed      = (float)W25Qxx_FlashSize/1024/1024 / ExecutionTime*1000  ; 	// 计算读取速度，单位 MB/S 

    if( OSPI_Status == OSPI_W25Qxx_OK )
    {
        printf ("\r\nread succeed, size: %d MB, time: %d ms, speed: %.2f MB/s \r\n",W25Qxx_FlashSize/1024/1024,ExecutionTime,ExecutionSpeed);		
    }
    else
    {
        printf ("\r\nread error!!!!!  error code:%d\r\n",OSPI_Status);
        while (1);
    }	
    
    return OSPI_W25Qxx_OK ;  // 操作成功				
    
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM23 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */
	if (htim->Instance == TIM3) {
		
		read_all_motor_data(&motor[Motor1]);
		
		if(motor[Motor1].tmp.read_flag == 0)
			dm_motor_ctrl_send(&hfdcan1, &motor[Motor1]);
	}
  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM23) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_TIM12_Init();
  MX_SPI1_Init();
  MX_SPI6_Init();
  MX_SPI2_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_UART7_Init();
  MX_USART10_UART_Init();
  MX_FDCAN1_Init();
  MX_FDCAN2_Init();
  MX_FDCAN3_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_OCTOSPI2_Init();
  /* USER CODE BEGIN 2 */
//    OSPI_W25Qxx_Init();     // 初始化OSPI和W25Q64
//    OSPI_W25Qxx_Test();     // Flash读写测试
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buffer, RX_LEN); //启动DMA接收串口信息
  
  HAL_GPIO_WritePin(GPIOC, POWER_24V_1_Pin, GPIO_PIN_SET); // CAN模块24V电源使能
	HAL_Delay(1000); // 延时1秒，等待电源稳定
  RESPONSE_OK(power_ok);

	bsp_fdcan_set_baud(&hfdcan1, CAN_CLASS, CAN_BR_1M);

  bsp_can_init();
	dm_motor_init();
	motor[Motor1].ctrl.mode 	= mit_mode;

	write_motor_data(motor[Motor1].id, 10, mit_mode, 0, 0, 0);
	HAL_Delay(100);

	read_motor_data(motor[Motor1].id, RID_CAN_BR); 

	dm_motor_disable(&hfdcan1, &motor[Motor1]);
	HAL_Delay(100);

	save_motor_data(motor[Motor1].id, 10);
	HAL_Delay(100);

	dm_motor_enable(&hfdcan1, &motor[Motor1]);
	HAL_Delay(1000);

  motor[Motor1].ctrl.kp_set   = 1.0f;  // 位置比例系数
  motor[Motor1].ctrl.kd_set   = 0.5f;  // 速度比例系数
  motor[Motor1].ctrl.pos_set  = 0.0f;
	HAL_Delay(100);

	HAL_TIM_Base_Start_IT(&htim3);

  // printf("NewProj HSE_VALUE Macro: %d\r\n", HSE_VALUE);

  /* USER CODE END 2 */

  /* Call init function for freertos objects (in freertos.c) */
  // MX_FREERTOS_Init(); // RTOS初始化

  /* Start scheduler */
  // osKernelStart(); // RTOS内核启动函数

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
//	  vofa_start();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (can_send_req_flag == 0)
    {
      // printf("Live...\r\n");
      if (pos_code == 0) motor[Motor1].ctrl.pos_set += 1.5708f;
      else if (pos_code == 1) motor[Motor1].ctrl.pos_set -= 1.5708f;

      if (motor[Motor1].ctrl.pos_set >= 6.2832f) {
          pos_code = 1;
      } else if (motor[Motor1].ctrl.pos_set <= 0.0f) {
          pos_code = 0;
      }
      HAL_Delay(2000);
    }
    else if (can_send_req_flag == 1) // 从串口接收到数据，通过CAN转发出去（弃用）
    {
      /*
        can_data_buffer  数据格式为：
        前 3 个字节："CAN"标识，0x43 0x41 0x4E 
        第 4 个字节：电机ID，0x01~0x06
        后 8 个字节：MIT模式CAN数据，0x7F 0xFF 0x7F 0xF0 0x00 0x00 0x07 0xFF
      */
      uint16_t motor_id = (uint16_t)can_data_buffer[3]; // 电机ID
      if (motor_id < 1 || motor_id > 6) // ID是否合法
      {
        can_send_req_flag = 0; // 清除CAN数据标志位
        memset(can_data_buffer, 0, RX_LEN); // 清空数据缓冲区，准备下一次接收
        HAL_Delay(10); // 延时等待
      }
      else 
      {
        for (uint8_t i = 0; i < 8; i++)
          can1_tx_data[i] = can_data_buffer[i + 4];
        // canx_send_data(&hfdcan1, motor_id, can1_tx_data, 8); // 通过CAN转发出去
        can_send_req_flag = 0; // 清除CAN数据标志位
        memset(can_data_buffer, 0, RX_LEN); // 清空数据缓冲区，准备下一次接收
			  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buffer, RX_LEN); //启动DMA接收串口信息
        HAL_Delay(10); // 延时等待
      }
    }
    else if (can_send_req_flag == 2)
    {
      // 从CAN1接收到数据，通过串口转发出去
      if (memcmp(rx_data1, last_can_rx_data1, 8) != 0) // 数据不重复
      {
        memcpy(last_can_rx_data1, rx_data1, 8); // 保存最新接收到的数据
        memset(tx_temp_buffer, 0, RX_LEN); // 清空缓冲区
        memcpy(tx_temp_buffer, rx_data1, 8);
        tx_temp_buffer[8] = '\r';
        tx_temp_buffer[9] = '\r';
        HAL_UART_Transmit(&huart1, tx_temp_buffer, 10, 100); 
      }
      else 
      {
        can_send_req_flag = 0; // 清除CAN数据标志位
        memset(can_data_buffer, 0, RX_LEN); // 清空数据缓冲区，准备下一次接收
      }
			HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buffer, RX_LEN); //启动DMA接收串口信息
      HAL_Delay(10); // 延时等待
      
    }
    else if (can_send_req_flag == 3)
    {
      prefix_len = strlen(prefix);
			if (uart1_rx_size > (RX_LEN - prefix_len - 2)) // 防止溢出，保证最多接收96字节数据
      {
        uart1_rx_size = RX_LEN - prefix_len - 2; 
      }
			memset(tx_temp_buffer, 0, RX_LEN); // 清空缓冲区
			memcpy(tx_temp_buffer, prefix, prefix_len); // 复制前缀
      memcpy(tx_temp_buffer + prefix_len, rx_buffer, uart1_rx_size); // 复制接收到的数据到缓冲区
      tx_temp_buffer[prefix_len + uart1_rx_size] = '\r'; // 添加回车符
			tx_temp_buffer[uart1_rx_size + prefix_len + 1] = 0; // 确保字符串以0结尾
			HAL_UART_Transmit(&huart1, tx_temp_buffer, uart1_rx_size + prefix_len + 2, 100); // 原样发送数据
      can_send_req_flag = 0; // 清除CAN数据标志位
      uart1_rx_size = 0;
			HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buffer, RX_LEN); //启动DMA接收串口信息
      HAL_Delay(10); // 延时等待
			// printf("Recv: %s\r", tx_temp_buffer); // 原样输出接收到的数据，printf可能会影响实时性
    }

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI
                              |RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 6;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_1);
}

/* USER CODE BEGIN 4 */
// 串口接收完成回调
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if (huart->Instance == USART1)
	{
		SCB_InvalidateDCache_by_Addr((uint32_t *)rx_buffer, RX_LEN); // 使能从RAM读取数据刷新Cache
		if (strncmp((char *)rx_buffer, "CMD", 3) == 0) // 接收到的数据为 CMD 开头则不转发
		{
      // RESPONSE_OK(cmd_ok);

			// HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buffer, RX_LEN); //启动DMA接收串口信息
      can_send_req_flag = 2;
		}
    else if (strncmp((char *)rx_buffer, "CAN", 3) == 0) // 接收到的数据为 CAN 开头则将信息转发CAN1
    {
      // RESPONSE_OK(can_ok);

      if (Size > RX_LEN) Size = RX_LEN;
      memcpy(can_data_buffer, rx_buffer, RX_LEN);
			// HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buffer, Size); //启动DMA接收串口信息
      can_send_req_flag = 1; // 设置CAN数据标志位
    }
		else // 其他情况为串口回显
		{
			// HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buffer, RX_LEN); //启动DMA接收串口信息
      uart1_rx_size = Size;
      can_send_req_flag = 3;
		}
	}
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
	}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
