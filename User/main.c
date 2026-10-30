#include "delay.h"
#include "key.h"
#include "sys.h"
#include "oled.h"
#include "DHT11.h"
#include "W25Q64.h"
u8 App_DHT11_Init(u8 *temp,u8 *humi);
void App_DHT11_GPIO_OUT(void);
void App_DHT11_GPIO_IN(void);


 
void App_DZQJ_Init(void);



int main(void){
	u8 temp,humi;
	u8 ret;
	u16 jdr1, light_pct;
	u32 ts = 0;
  LogRec rec;
	float ccr1;
	
	delay_init();
	OLED_Init();
	App_DZQJ_Init();
	Key_Init();
	OLED_Clear();
	W25Q64_Init();
	Key_BootCheckClear();       /* 上电双键长按 3 秒清空；必须在 Log_Init 之前 */
	Log_Init();
	
	
	OLED_ShowString(1,1,"LIGHT:");
	Key_LogViewUpdate();
	while(1){
		ret = App_DHT11_Init(&temp,&humi);
		if(1 == ret){
			OLED_ShowString(2,1,"temp:");
			OLED_ShowNum(2,6,temp,2);
			OLED_ShowString(2,8," humi:");
			OLED_ShowNum(2,12,humi,2);
		}
		else{
			OLED_ShowString(2,1,"Read Failure");
		}
		Key_WaitMs(1200); 
		
		ADC_ClearFlag(ADC1,ADC_FLAG_JEOC);
	TIM_ClearFlag(TIM1,TIM_FLAG_CC1);
	while(RESET == ADC_GetFlagStatus(ADC1,ADC_FLAG_JEOC));
		jdr1 = ADC_GetInjectedConversionValue(ADC1,ADC_InjectedChannel_1);
		ccr1 = (jdr1-2200)*1000.0f/(4095-2200);
	light_pct = (4095 - jdr1) * 100 / 4095;
	if (light_pct > 100) light_pct = 100;	
		
	if(jdr1>2200){
		TIM_SetCompare1(TIM1,ccr1);
	
	}
	else{
	TIM_SetCompare1(TIM1,0);
	}
		OLED_ShowNum(1,8,(4095-jdr1)*100/4095,2);
		OLED_ShowChar(1,10,'%');
	if (ret == 1) {
            rec.ts    = ts;
            rec.light = light_pct;
            rec.temp  = temp;
            rec.humi  = humi;
            rec.flags = 0;
            rec.rsv   = 0;
            rec.rsv2  = 0;
            Log_Append(&rec);          /* 失败可以先不处理 */
        }
				Key_LogViewUpdate();
				
        ts += 2;
        Key_WaitMs(2000);                /* DHT11 至少 1 s，2 s 更稳 */
			
	
	
	
	}



}
void App_DHT11_GPIO_IN(void){
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;	
	GPIO_Init(GPIOB,&GPIO_InitStruct);



}
void App_DHT11_GPIO_OUT(void){
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_InitStruct);

}

u8 App_DHT11_Init(u8 *temp,u8 *humi){
	
	u8 i,j;
	u16 buf[5] = {0,0,0,0,0};
	App_DHT11_GPIO_OUT();
	GPIO_WriteBit(GPIOB,GPIO_Pin_8,Bit_RESET);
	delay_ms(18);
	GPIO_WriteBit(GPIOB,GPIO_Pin_8,Bit_SET);
	delay_us(40);
	App_DHT11_GPIO_IN();
	if(RESET == GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_8)){
			while(RESET == GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_8));
			while(SET == GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_8));
			for(i = 0;i<5;i++){
				for(j = 0;j<8;j++){
					while(RESET == GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_8));
						delay_us(30);
					if(SET == GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_8)){
							buf[i] |= 1<<(7-j);
					while(SET == GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_8));
					}
				}
				
			}
			if(buf[0]+buf[1]+buf[2]+buf[3] == buf[4]){
				*humi = buf[0];
				*temp = buf[2];
				return 1;
			
			}
	}
	
		return 0;
	
}
void App_DZQJ_Init(void){
	//LED
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStruct);
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1,ENABLE);
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
	TIM_TimeBaseInitStruct.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStruct.TIM_Period = 999;
	TIM_TimeBaseInitStruct.TIM_Prescaler = 71;
	TIM_TimeBaseInitStruct.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM1,&TIM_TimeBaseInitStruct);
	
	TIM_OCInitTypeDef TIM_OCInitStruct;
	TIM_OCInitStruct.TIM_OCIdleState = TIM_OCIdleState_Reset;
	TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStruct.TIM_Pulse = 0;
	TIM_ARRPreloadConfig(TIM1,ENABLE);
	TIM_OC1PreloadConfig(TIM1,TIM_OCPreload_Enable);
	TIM_CtrlPWMOutputs(TIM1,ENABLE);
	TIM_OC1Init(TIM1,&TIM_OCInitStruct);
	TIM_SelectOutputTrigger(TIM1,TIM_TRGOSource_Update);
	TIM_Cmd(TIM1,ENABLE);
	
	
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_Init(GPIOA,&GPIO_InitStruct);
	
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1,ENABLE);
	ADC_InitTypeDef ADC_InitStruct;
	ADC_InitStruct.ADC_ContinuousConvMode = DISABLE;
	ADC_InitStruct.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStruct.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
	ADC_InitStruct.ADC_Mode = ADC_Mode_Independent;
	ADC_InitStruct.ADC_NbrOfChannel = 1;
	ADC_InitStruct.ADC_ScanConvMode =DISABLE;
	ADC_Init(ADC1,&ADC_InitStruct);
	
	ADC_InjectedChannelConfig(ADC1,ADC_Channel_0,1,ADC_SampleTime_13Cycles5);
	ADC_InjectedSequencerLengthConfig(ADC1,1);
	ADC_ExternalTrigInjectedConvConfig(ADC1,ADC_ExternalTrigInjecConv_T1_TRGO);
	ADC_ExternalTrigInjectedConvCmd(ADC1,ENABLE);
	ADC_Cmd(ADC1,ENABLE);


}









