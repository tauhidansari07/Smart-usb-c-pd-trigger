#include "debug.h"
#include "ch32v00x.h"

/*
 * PIN CONFIGURATION REFERENCE (TSSOP-20):
 * VDD (Pin 6)   -> 3.3V LDO Output
 * VSS (Pin 5)   -> GND Plane
 * PD1 (Pin 3)   -> SWDIO (Programming)
 * PD4 (Pin 17)  -> ADC Input (Center of 100k / 15k Resistor Divider)
 * PC1 (Pin 8)   -> CH224K CFG1
 * PC2 (Pin 9)   -> CH224K CFG2
 * PC3 (Pin 10)  -> CH224K CFG3
 * PD2 (Pin 15)  -> 5V LED (via 1k resistor)
 * PD3 (Pin 16)  -> 9V LED (via 1k resistor)
 * PC4 (Pin 11)  -> 12V LED (via 1k resistor)
 * PD5 (Pin 18)  -> 15V LED (via 1k resistor)
 * PD6 (Pin 19)  -> 20V LED (via 1k resistor)
 * PD7 (Pin 20)  -> Push Button (Active Low, internal Pull-Up)
 */

// Global State tracking
volatile uint8_t requested_state = 0; // 0=5V, 1=9V, 2=12V, 3=15V, 4=20V

// ADC Thresholds for a 100k/15k divider with a 3.3V reference (10-bit ADC: 0-1023)
// 5V input  ≈ 0.65V at pin ≈ ADC value 202 (Threshold: > 280 for 9V)
// 9V input  ≈ 1.17V at pin ≈ ADC value 364 (Threshold: > 420 for 12V)
// 12V input ≈ 1.56V at pin ≈ ADC value 485 (Threshold: > 550 for 15V)
// 15V input ≈ 1.95V at pin ≈ ADC value 606 (Threshold: > 700 for 20V)
// 20V input ≈ 2.60V at pin ≈ ADC value 809

void System_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    ADC_InitTypeDef ADC_InitStructure = {0};

    // 1. Enable Clocks for Port C, Port D, and ADC1
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD | RCC_APB2Periph_ADC1, ENABLE);
    
    // Set ADC prescaler (ADCCLK = SYSCLK / 8)
    RCC_ADCCLKConfig(RCC_ADCCLK_Div8);

    // 2. Configure LEDs (PD2, PD3, PD5, PD6) as Push-Pull Outputs
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_5 | GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // Configure 12V LED (PC4) as Push-Pull Output
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // 3. Configure Button (PD7) as Input with Pull-Up
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // 4. Configure CFG pins (PC1, PC2, PC3) as Push-Pull Outputs initially
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // 5. Configure Voltage Sensing Pin (PD4 / AIN7) as Analog Input
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // 6. Initialize ADC1
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    // Enable ADC1
    ADC_Cmd(ADC1, ENABLE);

    // Calibrate ADC
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
}

uint16_t Read_Voltage_ADC(void) {
    // Set up Channel 7 (PD4), Sample time 241 cycles
    ADC_RegularChannelConfig(ADC1, ADC_Channel_7, 1, ADC_SampleTime_241Cycles);
    
    // Start conversion
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    
    // Wait until conversion is complete
    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
    
    // Return conversion raw 10-bit value
    return ADC_GetConversionValue(ADC1);
}

void Set_PD_Negotiation_Target(uint8_t state) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // Ensure PC1 (CFG1) is returned to Output Mode if coming out of the 20V High-Z state
    if(state != 4) {
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
        GPIO_Init(GPIOC, &GPIO_InitStructure);
    }

    // Apply the exact logic table for the WCH CH224K Trigger IC
    switch(state) {
        case 0: // 5V Request (CFG1=0, CFG2=0, CFG3=0)
            GPIO_WriteBit(GPIOC, GPIO_Pin_1, Bit_RESET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_RESET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_3, Bit_RESET);
            break;

        case 1: // 9V Request (CFG1=0, CFG2=0, CFG3=1)
            GPIO_WriteBit(GPIOC, GPIO_Pin_1, Bit_RESET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_RESET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_3, Bit_SET);
            break;

        case 2: // 12V Request (CFG1=0, CFG2=1, CFG3=1)
            GPIO_WriteBit(GPIOC, GPIO_Pin_1, Bit_RESET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_SET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_3, Bit_SET);
            break;

        case 3: // 15V Request (CFG1=1, CFG2=1, CFG3=1)
            GPIO_WriteBit(GPIOC, GPIO_Pin_1, Bit_SET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_SET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_3, Bit_SET);
            break;

        case 4: // 20V Request (CFG1=High-Z, CFG2=0, CFG3=1)
            // Convert PC1 dynamically to Floating Input to create a High-Z state
            GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
            GPIO_Init(GPIOC, &GPIO_InitStructure);
            
            GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_RESET);
            GPIO_WriteBit(GPIOC, GPIO_Pin_3, Bit_SET);
            break;
    }
}

void Update_LED_Indicators(void) {
    uint16_t adc_val = Read_Voltage_ADC();
    
    // Clear all status indicators
    GPIO_WriteBit(GPIOD, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_5 | GPIO_Pin_6, Bit_RESET);
    GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_RESET);

    // Map measured ADC thresholds to active status indicator
    if (adc_val > 700) {
        GPIO_WriteBit(GPIOD, GPIO_Pin_6, Bit_SET); // 20V Solid ON
    } 
    else if (adc_val > 550) {
        GPIO_WriteBit(GPIOD, GPIO_Pin_5, Bit_SET); // 15V Solid ON
    } 
    else if (adc_val > 420) {
        GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_SET); // 12V Solid ON
    } 
    else if (adc_val > 280) {
        GPIO_WriteBit(GPIOD, GPIO_Pin_3, Bit_SET); // 9V Solid ON
    } 
    else {
        GPIO_WriteBit(GPIOD, GPIO_Pin_2, Bit_SET); // Default 5V Solid ON
    }
}

int main(void) {
    // Core Clock and Driver Init Routines
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    System_Init();

    // Default to safest base configuration state (5V) on bootup
    Set_PD_Negotiation_Target(requested_state);

    while(1) {
        // Read Momentary Button (Active Low)
        if(GPIO_ReadInputDataBit(GPIOD, GPIO_Pin_7) == Bit_RESET) {
            
            Delay_Ms(40); // Standard Debounce Window
            
            if(GPIO_ReadInputDataBit(GPIOD, GPIO_Pin_7) == Bit_RESET) {
                
                // Advance targets and cycle back if over bounds
                requested_state++;
                if(requested_state > 4) {
                    requested_state = 0;
                }
                
                // Commit states to CFG pins to trigger the CH224K handshake
                Set_PD_Negotiation_Target(requested_state);
                
                // Lock execution here until user completely releases the physical button
                while(GPIO_ReadInputDataBit(GPIOD, GPIO_Pin_7) == Bit_RESET) {
                    Delay_Ms(10);
                }
            }
        }
        
        // Execute closed-loop system checks and update outputs dynamically
        Update_LED_Indicators();
        Delay_Ms(100); 
    }
}