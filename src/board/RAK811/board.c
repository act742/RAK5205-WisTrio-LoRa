/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech

Description: Target board general functions implementation

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Andreas Pella (IMST GmbH), Miguel Luis and Gregory Cristian
*/
#include "board.h"
#include "stm32l1xx_hal_iwdg.h"
#include "BME680-board.h"
#include "rw_sys.h"

/*!
 * Potentiometer max and min levels definition
 */
#define POTI_MAX_LEVEL 900
#define POTI_MIN_LEVEL 10

/*!
 * Vref values definition
 */
#define PDDADC_VREF_BANDGAP                             1224 // mV
#define PDDADC_MAX_VALUE                                4096

/*!
 * Factory power supply
 */
#define FACTORY_POWER_SUPPLY                        3300 // mV

/*!
 * VREF calibration value
 */
#define VREFINT_CAL                                 ( *( uint16_t* )0x1FF80078 )

/*!
 * ADC maximum value
 */
#define ADC_MAX_VALUE                               4095


/*!
 * Battery thresholds
 */
#define BATTERY_MAX_LEVEL                           4150 // mV
#define BATTERY_MIN_LEVEL                           3200 // mV
#define BATTERY_SHUTDOWN_LEVEL                      3100 // mV

/*!
 * Battery level ratio (battery dependent)
 */
#define BATTERY_STEP_LEVEL                          0.23

/*!
 * Unique Devices IDs register set ( STM32L1xxx )
 */
#define         ID1                                 ( 0x1FF80050 )
#define         ID2                                 ( 0x1FF80054 )
#define         ID3                                 ( 0x1FF80064 )

//Gpio_t Led2;
//Gpio_t Led3;
//Gpio_t Led4;
/*!
 * LED GPIO pins objects
 */
#ifdef TRACKERBOARD
	Gpio_t Led1;
	Gpio_t Led2;
#endif
/*
 * MCU objects
 */
Adc_t Adc;
I2c_t I2c;
Uart_t Uart1;
Uart_t GpsUart;

IWDG_HandleTypeDef hiwdg;

/*!
 * Initializes the unused GPIO to a know status
 */
static void BoardUnusedIoInit( void );

/*!
 * System Clock Configuration
 */
static void SystemClockConfig( void );

/*!
 * Used to measure and calibrate the system wake-up time from STOP mode
 */
static void CalibrateSystemWakeupTime( void );

/*!
 * System Clock Re-Configuration when waking up from STOP mode
 */
void SystemClockReConfig( void );

/*!
 * Timer used at first boot to calibrate the SystemWakeupTime
 */
static TimerEvent_t CalibrateSystemWakeupTimeTimer;

/*!
 * Flag to indicate if the MCU is Initialized
 */
static bool McuInitialized = false;

/*!
 * Flag to indicate if the SystemWakeupTime is Calibrated
 */
static bool SystemWakeupTimeCalibrated = false;

/*!
 * Callback indicating the end of the system wake-up time calibration
 */
static void OnCalibrateSystemWakeupTimeTimerEvent( void )
{
    SystemWakeupTimeCalibrated = true;
}


static uint8_t IrqNestLevel = 0;

uint8_t g_power_source = BATTERY_POWER;

void BoardDisableIrq( void )
{
  __disable_irq( );
  IrqNestLevel++;
}

void BoardEnableIrq( void )
{
  IrqNestLevel--;
  if ( IrqNestLevel == 0 )
  {
    __enable_irq( );
  }
}

void BoardInitPeriph( void )
{
  GpsInit( );
	LIS3DH_Init( );
	BME680_Init( );
}

void BoardInitMcu( void )
{

    if( McuInitialized == false )
    {
#if defined( USE_BOOTLOADER )
        // Set the Vector Table base location at 0x3000
        SCB->VTOR = FLASH_BASE | 0x3000;
#endif
        HAL_Init();
		SystemClockConfig();
		RtcInit(); 
		BoardUnusedIoInit();
#ifdef TRACKERBOARD
		GpioInit( &Led1, LED_1, PIN_OUTPUT, PIN_PUSH_PULL, PIN_NO_PULL, 1 );
        GpioInit( &Led2, LED_2, PIN_OUTPUT, PIN_PUSH_PULL, PIN_NO_PULL, 1 );
#endif			
        DelayMs(1000);        
    }
    else
    {
        SystemClockReConfig( );
    }

    AdcInit( &Adc, BAT_LEVEL_PIN );

    SpiInit( &SX1276.Spi, RADIO_MOSI, RADIO_MISO, RADIO_SCLK, NC );
    
    SX1276IoInit();
    GpioInit( &SX1276.Xtal, RADIO_XTAL_EN, PIN_OUTPUT, PIN_PUSH_PULL, PIN_NO_PULL, 1 );

    I2cInit( &I2c, I2C_SCL, I2C_SDA );
		
 #ifdef TRACKERBOARD   
    UartInit(&GpsUart, GPS_UART, GPS_UART_TX, GPS_UART_RX);
    UartMcuConfig( &GpsUart, RX_ONLY, 9600, UART_8_BIT, UART_1_STOP_BIT, NO_PARITY, NO_FLOW_CTRL );
#endif
    
    if( McuInitialized == false )
    {
        McuInitialized = true;
        
        if( GetBoardPowerSource( ) == BATTERY_POWER )
        {
            CalibrateSystemWakeupTime( );
        }
    }

}

void BoardDeInitMcu( void )
{
    //Gpio_t ioPin;
    AdcDeInit( &Adc );
    SpiDeInit( &SX1276.Spi );
    I2cDeInit(&I2c);
    SX1276IoDeInit( );
    GpioWrite( &SX1276.Xtal, 0 );
  	UartDeInit(&GpsUart);
  	UartDeInit(&Uart1);
}



void BoardHiwdogInit( void ){

	hiwdg.Instance = IWDG;
	hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
	hiwdg.Init.Reload = 0xFFF;

	HAL_IWDG_Init(&hiwdg);
	
	HAL_IWDG_Start(&hiwdg);

	return;
}

void BoardHIWDGRefresh( void ){
	HAL_IWDG_Refresh(&hiwdg);
	return;
}

uint32_t BoardGetRandomSeed( void )
{
    return ( ( *( uint32_t* )ID1 ) ^ ( *( uint32_t* )ID2 ) ^ ( *( uint32_t* )ID3 ) );
}

void BoardGetUniqueId( uint8_t *id )
{
    id[7] = ( ( *( uint32_t* )ID1 )+ ( *( uint32_t* )ID3 ) ) >> 24;
    id[6] = ( ( *( uint32_t* )ID1 )+ ( *( uint32_t* )ID3 ) ) >> 16;
    id[5] = ( ( *( uint32_t* )ID1 )+ ( *( uint32_t* )ID3 ) ) >> 8;
    id[4] = ( ( *( uint32_t* )ID1 )+ ( *( uint32_t* )ID3 ) );
    id[3] = ( ( *( uint32_t* )ID2 ) ) >> 24;
    id[2] = ( ( *( uint32_t* )ID2 ) ) >> 16;
    id[1] = ( ( *( uint32_t* )ID2 ) ) >> 8;
    id[0] = ( ( *( uint32_t* )ID2 ) );
}

#if 0
uint8_t BoardMeasurePotiLevel( void )
{
    uint8_t potiLevel = 0;
    uint16_t MeasuredLevel = 0;

    // read the current potentiometer setting
    MeasuredLevel = AdcMcuRead( &Adc , ADC_CHANNEL_3 );

    // check the limits
    if( MeasuredLevel >= POTI_MAX_LEVEL )
    {
        potiLevel = 100;
    }
    else if( MeasuredLevel <= POTI_MIN_LEVEL )
    {
        potiLevel = 0;
    }
    else
    {
        // if the value is in the area, calculate the percentage value
        potiLevel = ( ( MeasuredLevel - POTI_MIN_LEVEL ) * 100 ) / POTI_MAX_LEVEL;
    }
    return potiLevel;
}

uint16_t BoardMeasureVdd( void )
{
    uint16_t MeasuredLevel = 0;
    uint32_t milliVolt = 0;

    // Read the current Voltage
    MeasuredLevel = AdcMcuRead( &Adc , ADC_CHANNEL_17 );

    // We don't use the VREF from calibValues here.
    // calculate the Voltage in miliVolt
    milliVolt = ( uint32_t )PDDADC_VREF_BANDGAP * ( uint32_t )PDDADC_MAX_VALUE;
    milliVolt = milliVolt / ( uint32_t ) MeasuredLevel;

    return ( uint16_t ) milliVolt;
}
#endif




uint16_t BoardBatteryMeasureVolage( void )
{
    uint16_t vcal = VREFINT_CAL;
    uint16_t vref = 0;
    uint16_t vdiv = 0;
    uint16_t batteryVoltage = 0;

    vdiv = AdcReadChannel( &Adc, BAT_LEVEL_CHANNEL );
    vref = AdcReadChannel( &Adc, ADC_CHANNEL_VREFINT );

    batteryVoltage = (float)3000 * vcal * vdiv / (float)( vref * 4096);
			
    //                                vDiv
    // Divider bridge  VBAT <-> 100k -<--|-->- 150k <-> GND => vBat = (5 * vDiv )/3
    batteryVoltage = (5 * batteryVoltage )/3;
    return batteryVoltage;
}


uint8_t BoardGetBatteryLevel( void )
{
    uint8_t batteryLevel = 0;

    uint16_t BatteryVoltage = BoardBatteryMeasureVolage( );

    if( GetBoardPowerSource( ) == USB_POWER )
    {
        batteryLevel = 0;
    }
    else
    {
        if( BatteryVoltage >= BATTERY_MAX_LEVEL )
        {
            batteryLevel = 254;
        }
        else if( ( BatteryVoltage > BATTERY_MIN_LEVEL ) && ( BatteryVoltage < BATTERY_MAX_LEVEL ) )
        {
            batteryLevel = ( ( 253 * ( BatteryVoltage - BATTERY_MIN_LEVEL ) ) / ( BATTERY_MAX_LEVEL - BATTERY_MIN_LEVEL ) ) + 1;
        }
        else if( ( BatteryVoltage > BATTERY_SHUTDOWN_LEVEL ) && ( BatteryVoltage <= BATTERY_MIN_LEVEL ) )
        {
            batteryLevel = 1;
        }
        else //if( BatteryVoltage <= BATTERY_SHUTDOWN_LEVEL )
        {
            batteryLevel = 255;
        }
    }
    return batteryLevel;
}

static void BoardUnusedIoInit( void )
{
    Gpio_t ioPin;

#if defined( USE_DEBUGGER )
    HAL_DBGMCU_EnableDBGStopMode( );
    HAL_DBGMCU_EnableDBGSleepMode( );
    HAL_DBGMCU_EnableDBGStandbyMode( );
#else

    for (int i = 0; i < 8 * 16; i++) {
        if(i==UART_TX || i== UART_RX) {
            continue;
        }
        GpioInit( &ioPin, i, PIN_OUTPUT, PIN_PUSH_PULL, PIN_PULL_DOWN, 0 );
    }
    
    HAL_DBGMCU_DisableDBGSleepMode( );
    HAL_DBGMCU_DisableDBGStopMode( );
    HAL_DBGMCU_DisableDBGStandbyMode( );

    GpioInit( &ioPin, JTAG_TMS, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    GpioInit( &ioPin, JTAG_TCK, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
#ifdef LORA_HF_BOARD
    GpioInit( &ioPin, JTAG_TDI, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    GpioInit( &ioPin, JTAG_TDO, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    //GpioInit( &ioPin, JTAG_NRST, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
#endif
		
#endif
}

void SystemClockConfig( void )
{
    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_PeriphCLKInitTypeDef PeriphClkInit;

    __HAL_RCC_PWR_CLK_ENABLE( );

    __HAL_PWR_VOLTAGESCALING_CONFIG( PWR_REGULATOR_VOLTAGE_SCALE2 );

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
	  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_OFF;
//    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
//    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
//    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL8;
//    RCC_OscInitStruct.PLL.PLLDIV = RCC_PLL_DIV3;
    HAL_RCC_OscConfig( &RCC_OscInitStruct );

    RCC_ClkInitStruct.ClockType = ( RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 );
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig( &RCC_ClkInitStruct, FLASH_LATENCY_1 );

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    HAL_RCCEx_PeriphCLKConfig( &PeriphClkInit );

    HAL_SYSTICK_Config( HAL_RCC_GetHCLKFreq( ) / 1000 );

    HAL_SYSTICK_CLKSourceConfig( SYSTICK_CLKSOURCE_HCLK );

    /* HAL_NVIC_GetPriorityGrouping*/
    HAL_NVIC_SetPriorityGrouping( NVIC_PRIORITYGROUP_4 );

    /* SysTick_IRQn interrupt configuration */
    HAL_NVIC_SetPriority( SysTick_IRQn, 0, 0 );
}

void CalibrateSystemWakeupTime( void )
{
    if( SystemWakeupTimeCalibrated == false )
    {
        TimerInit( &CalibrateSystemWakeupTimeTimer, OnCalibrateSystemWakeupTimeTimerEvent );
        TimerSetValue( &CalibrateSystemWakeupTimeTimer, 1000 );
        TimerStart( &CalibrateSystemWakeupTimeTimer );
        while( SystemWakeupTimeCalibrated == false )
        {
            TimerLowPowerHandler( );
        }
    }
}

void SystemClockReConfig( void )
{
    __HAL_RCC_PWR_CLK_ENABLE( );
    __HAL_PWR_VOLTAGESCALING_CONFIG( PWR_REGULATOR_VOLTAGE_SCALE2 );

    /* Enable HSI */
    __HAL_RCC_HSI_ENABLE();
    /* Wait till HSE is ready */
    while( __HAL_RCC_GET_FLAG( RCC_FLAG_HSIRDY ) == RESET )
    {
    }

    /* Select PLL as system clock source */
    __HAL_RCC_SYSCLK_CONFIG ( RCC_SYSCLKSOURCE_HSI );

    /* Wait till PLL is used as system clock source */
    while( __HAL_RCC_GET_SYSCLK_SOURCE( ) != RCC_SYSCLKSOURCE_STATUS_HSI )
    {
    }
}

void SysTick_Handler( void )
{
    HAL_IncTick( );
    HAL_SYSTICK_IRQHandler( );
}

uint8_t GetBoardPowerSource( void )
{
	if(g_power_source) return BATTERY_POWER;
	else return USB_POWER;
}

void InstallWakeUpPin(void)
{
    uint32_t temp;
    uint32_t iocurrent;
    /* Enable SYSCFG Clock */
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    
    iocurrent = (GPIO_PIN_10) & ((uint32_t)1 << 10);
    temp = SYSCFG->EXTICR[10 >> 2];
    CLEAR_BIT(temp, ((uint32_t)0x0F) << (4 * (10 & 0x03)));
    SET_BIT(temp, (GPIO_GET_INDEX(GPIOA)) << (4 * (10 & 0x03)));
    SYSCFG->EXTICR[10 >> 2] = temp;
    
    /* Clear EXTI line configuration */
    temp = EXTI->IMR;
    CLEAR_BIT(temp, (uint32_t)iocurrent);
    
    SET_BIT(temp, iocurrent); 
    
    EXTI->IMR = temp;
    
    temp = EXTI->EMR;
    CLEAR_BIT(temp, (uint32_t)iocurrent);      

    EXTI->EMR = temp;
    
    /* Clear Rising Falling edge configuration */
    temp = EXTI->RTSR;
    CLEAR_BIT(temp, (uint32_t)iocurrent); 
    
    SET_BIT(temp, iocurrent); 
    
    EXTI->RTSR = temp;
    
    temp = EXTI->FTSR;
    CLEAR_BIT(temp, (uint32_t)iocurrent); 
    
    SET_BIT(temp, iocurrent); 
    
    EXTI->FTSR = temp;
}

void UninstallWakeUpPin(void)
{
    uint32_t tmp = SYSCFG->EXTICR[10 >> 2];
    uint32_t iocurrent = (GPIO_PIN_10) & ((uint32_t)1 << 10);
    
    tmp &= (((uint32_t)0x0F) << (4 * (10 & 0x03)));
    if(tmp == (GPIO_GET_INDEX(GPIOA) << (4 * (10 & 0x03))))
    {
        tmp = ((uint32_t)0x0F) << (4 * (10 & 0x03));
        CLEAR_BIT(SYSCFG->EXTICR[10 >> 2], tmp); 
        /* Clear EXTI line configuration */
        CLEAR_BIT(EXTI->IMR, (uint32_t)iocurrent);
        CLEAR_BIT(EXTI->EMR, (uint32_t)iocurrent);
        /* Clear Rising Falling edge configuration */
        CLEAR_BIT(EXTI->RTSR, (uint32_t)iocurrent);
        CLEAR_BIT(EXTI->FTSR, (uint32_t)iocurrent);        
    } 
}

void SysEnterUltraPowerStopMode( void )
{
    InstallWakeUpPin();
    
    
    // Disable the Power Voltage Detector
    HAL_PWR_DisablePVD( );
    
    SET_BIT( PWR->CR, PWR_CR_CWUF );
    
    // Enable Ultra low power mode
    HAL_PWREx_EnableUltraLowPower( );
    
    // Enable the fast wake up from Ultra low power mode
    HAL_PWREx_EnableFastWakeUp( );
    
    // Enter Stop Mode
    HAL_PWR_EnterSTOPMode( PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI );

    __HAL_PWR_CLEAR_FLAG( PWR_FLAG_WU );
    
    SystemClockReConfig();

    UninstallWakeUpPin();
    
    GpioWrite( &SX1276.Xtal, 1 );
}


#ifdef USE_FULL_ASSERT
/*
 * Function Name  : assert_failed
 * Description    : Reports the name of the source file and the source line number
 *                  where the assert_param error has occurred.
 * Input          : - file: pointer to the source file name
 *                  - line: assert_param error line source number
 * Output         : None
 * Return         : None
 */
void assert_failed( uint8_t* file, uint32_t line )
{
    /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
		e_printf( "Wrong parameters value: file %s on line %u\r\n", ( const char* )file, line );
    /* Infinite loop */
    while( 1 )
    {
    }
}
#endif
