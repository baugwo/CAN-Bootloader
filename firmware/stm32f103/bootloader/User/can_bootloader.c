/**
  ******************************************************************************
  * @file    can_bootloader.c
  * $Author: wdluo $
  * $Revision: 17 $
  * $Date:: 2012-07-06 11:16:48 +0800 #$
  * @brief   ����CAN���ߵ�Bootloader����.
  ******************************************************************************
  * @attention
  *
  *<h3><center>&copy; Copyright 2009-2012, ViewTool</center>
  *<center><a href="http:\\www.viewtool.com">http://www.viewtool.com</a></center>
  *<center>All Rights Reserved</center></h3>
  * 
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can_bootloader.h"
#include "crc16.h"
/* Private typedef -----------------------------------------------------------*/
typedef  void (*pFunction)(void);

/* Private define ------------------------------------------------------------*/

/* Base address of the Flash sectors */
#if defined (STM32F10X_MD) || defined (STM32F10X_MD_VL)
 #define PAGE_SIZE                         (0x400)    /* 1 Kbyte */
 #define FLASH_SIZE                        (0x20000)  /* 128 KBytes */
#elif defined STM32F10X_CL
 #define PAGE_SIZE                         (0x800)    /* 2 Kbytes */
 #define FLASH_SIZE                        (0x40000)  /* 256 KBytes */
#elif defined STM32F10X_HD
 #define PAGE_SIZE                         (0x800)    /* 2 Kbytes */
 #define FLASH_SIZE                        (0x80000)  /* 512 KBytes */
#elif defined STM32F10X_XL
 #define PAGE_SIZE                         (0x800)    /* 2 Kbytes */
 #define FLASH_SIZE                        (0x100000) /* 1 MByte */
#else 
 #error "Please select first the STM32 device to be used (in stm32f10x.h)"    
#endif  
/* Private macro -------------------------------------------------------------*/
extern CBL_CMD_LIST CMD_List;
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  ��������д��ָ����ַ��Flash�� ��
  * @param  Address Flash��ʼ��ַ��
  * @param  Data ���ݴ洢����ʼ��ַ��
  * @param  DataNum �����ֽ�����
  * @retval ������д״̬��
  */
FLASH_Status CAN_BOOT_ProgramDatatoFlash(uint32_t StartAddress,uint8_t *pData,uint32_t DataNum) 
{
  FLASH_Status FLASHStatus = FLASH_COMPLETE;

  uint32_t i;

  if(StartAddress<APP_EXE_FLAG_START_ADDR){
    return FLASH_ERROR_PG;
  }
   /* Clear All pending flags */
  FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);	

  for(i=0;i<(DataNum>>2);i++)
  {
    FLASHStatus = FLASH_ProgramWord(StartAddress, *((uint32_t*)pData));
    if (FLASHStatus == FLASH_COMPLETE){
      StartAddress += 4;
      pData += 4;
    }else{ 
      return FLASHStatus;
    }
  }
  return	FLASHStatus;
}
/**
  * @brief  ����ָ�����������Flash���� ��
  * @param  StartPage ��ʼ������ַ
  * @param  EndPage ����������ַ
  * @retval ��������״̬  
  */
FLASH_Status CAN_BOOT_ErasePage(uint32_t StartPageAddr,uint32_t EndPageAddr)
{
  uint32_t i;
  FLASH_Status FLASHStatus=FLASH_COMPLETE;

  FLASH_Unlock();
  FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);	

  for(i=StartPageAddr;i<=EndPageAddr;i+=PAGE_SIZE){
    FLASHStatus = FLASH_ErasePage(i);
    if(FLASHStatus!=FLASH_COMPLETE){
      FLASH_Lock();
      return	FLASHStatus;	
    }
  }
  FLASH_Lock();
  return FLASHStatus;
}

/**
  * @brief  ��ȡ�ڵ��ַ��Ϣ
  * @param  None
  * @retval �ڵ��ַ��
  */
uint16_t CAN_BOOT_GetAddrData(void)
{
  return Read_CAN_Address();
}
/**
  * @brief  ���Ƴ�����ת��ָ��λ�ÿ�ʼִ�� ��
  * @param  Addr ����ִ�е�ַ��
  * @retval ������ת״̬��
  */
void CAN_BOOT_JumpToApplication(uint32_t Addr)
{
  static pFunction Jump_To_Application;
  __IO uint32_t JumpAddress; 
  /* Test if user code is programmed starting from address "ApplicationAddress" */
  if (((*(__IO uint32_t*)Addr) & 0x2FFE0000 ) == 0x20000000)
  { 
    /* Jump to user application */
    JumpAddress = *(__IO uint32_t*) (Addr + 4);
    Jump_To_Application = (pFunction) JumpAddress;
    __set_PRIMASK(1);//�ر������ж�
    /* Initialize user application's Stack Pointer */
    __set_MSP(*(__IO uint32_t*)Addr);
    Jump_To_Application();
  }
}


/**
  * @brief  ִ�������·�������
  * @param  pRxMessage CAN������Ϣ
  * @retval ��
  */
void CAN_BOOT_ExecutiveCommand(CanRxMsg *pRxMessage)
{
  CanTxMsg TxMessage;
  uint8_t ret,i;
  uint8_t can_cmd = (pRxMessage->ExtId)&CMD_MASK;//ID��bit0~bit3λΪ������
  uint16_t can_addr = (pRxMessage->ExtId >> CMD_WIDTH);//ID��bit4~bit15λΪ�ڵ��ַ
  uint32_t BaudRate;
  uint16_t crc_data;
  uint32_t addr_offset;
  uint32_t exe_type;
  uint32_t FlashSize;
  static uint32_t start_addr = APP_START_ADDR;
  static uint32_t data_size=0;
  static uint32_t data_index=0;
  __align(4) static uint8_t	data_temp[1028];
  //�жϽ��յ����ݵ�ַ�Ƿ�ͱ��ڵ��ַƥ�䣬����ƥ����ֱ�ӷ��أ������κ�����
  if((can_addr!=CAN_BOOT_GetAddrData())&&(can_addr!=0)){
    return;
  }
  TxMessage.DLC = 0;
  TxMessage.ExtId = 0;
  TxMessage.IDE = CAN_Id_Extended;
  TxMessage.RTR = CAN_RTR_Data;
  //CMD_List.Erase������Flash�е����ݣ���Ҫ������Flash��С�洢��Data[0]��Data[3]��
  //�����������Bootloader������ʵ�֣���APP�����п��Բ���ʵ��
  if(can_cmd == CMD_List.Erase){
    __set_PRIMASK(1);
    FLASH_Unlock();
    FlashSize = (pRxMessage->Data[0]<<24)|(pRxMessage->Data[1]<<16)|(pRxMessage->Data[2]<<8)|(pRxMessage->Data[3]<<0);
    ret = CAN_BOOT_ErasePage(APP_EXE_FLAG_START_ADDR,APP_START_ADDR+FlashSize);
    FLASH_Lock();	
    __set_PRIMASK(0);
    if(can_addr != 0x00){
      if(ret==FLASH_COMPLETE){
        TxMessage.ExtId = (CAN_BOOT_GetAddrData()<<CMD_WIDTH)|CMD_List.CmdSuccess;
      }else{
        TxMessage.ExtId = (CAN_BOOT_GetAddrData()<<CMD_WIDTH)|CMD_List.CmdFaild;
      }
      TxMessage.DLC = 0;
      CAN_WriteData(&TxMessage);
    }
    start_addr = APP_START_ADDR;
    return;
  }
  //CMD_List.SetBaudRate�����ýڵ㲨���ʣ����岨������Ϣ�洢��Data[0]��Data[3]��
  //���Ĳ����ʺ�������Ҳ��Ҫ����Ϊ��ͬ�Ĳ����ʣ�����������ͨ��
  if(can_cmd == CMD_List.SetBaudRate){
    __set_PRIMASK(1);
    BaudRate = (pRxMessage->Data[0]<<24)|(pRxMessage->Data[1]<<16)|(pRxMessage->Data[2]<<8)|(pRxMessage->Data[3]<<0);
    __set_PRIMASK(0);
    CAN_Configuration(BaudRate);
    if(can_addr != 0x00){
      TxMessage.ExtId = (CAN_BOOT_GetAddrData()<<CMD_WIDTH)|CMD_List.CmdSuccess;
      TxMessage.DLC = 0;
      BOOT_DelayMs(20);
      CAN_WriteData(&TxMessage);
    }
    return;
  }
  //CMD_List.WriteInfo������дFlash���ݵ������Ϣ������������ʼ��ַ�����ݴ�С
  //����ƫ�Ƶ�ַ�洢��Data[0]��Data[3]�У����ݴ�С�洢��Data[4]��Data[7]�У��ú���������Bootloader������ʵ�֣�APP������Բ���ʵ��
  if(can_cmd == CMD_List.WriteInfo){
    __set_PRIMASK(1);
    addr_offset = (pRxMessage->Data[0]<<24)|(pRxMessage->Data[1]<<16)|(pRxMessage->Data[2]<<8)|(pRxMessage->Data[3]<<0);
    start_addr = APP_START_ADDR+addr_offset;
    data_size = (pRxMessage->Data[4]<<24)|(pRxMessage->Data[5]<<16)|(pRxMessage->Data[6]<<8)|(pRxMessage->Data[7]<<0);
    data_index = 0;
    __set_PRIMASK(0);
    if(can_addr != 0x00){
      TxMessage.ExtId = (CAN_BOOT_GetAddrData()<<CMD_WIDTH)|CMD_List.CmdSuccess;
      TxMessage.DLC = 0;
      CAN_WriteData(&TxMessage);
    }
  }
  //CMD_List.Write���Ƚ����ݴ洢�ڱ��ػ������У�Ȼ��������ݵ�CRC����У����ȷ��д���ݵ�Flash��
  //ÿ��ִ�и����ݣ����ݻ������������ֽ���������pRxMessage->DLC�ֽڣ����������ﵽdata_size������2�ֽ�CRCУ���룩�ֽں�
  //�����ݽ���CRCУ�飬������У������������д��Flash��
  //�ú�����Bootloader�����б���ʵ�֣�APP������Բ���ʵ��
  if(can_cmd == CMD_List.Write){
    if((data_index<data_size)&&(data_index<1026)){
      __set_PRIMASK(1);
      for(i=0;i<pRxMessage->DLC;i++){
        data_temp[data_index++] = pRxMessage->Data[i];
      }
      __set_PRIMASK(0);
    }
    if((data_index>=data_size)||(data_index>=1026)){
      crc_data = crc16_ccitt(data_temp,data_size-2);
      if(crc_data==((data_temp[data_size-2]<<8)|(data_temp[data_size-1]))){
        __set_PRIMASK(1);
        FLASH_Unlock();
        ret = CAN_BOOT_ProgramDatatoFlash(start_addr,data_temp,data_size-2);
        FLASH_Lock();	
        __set_PRIMASK(0);
        if(can_addr != 0x00){
          if(ret==FLASH_COMPLETE){
            TxMessage.ExtId = (CAN_BOOT_GetAddrData()<<CMD_WIDTH)|CMD_List.CmdSuccess;
          }else{
            TxMessage.ExtId = (CAN_BOOT_GetAddrData()<<CMD_WIDTH)|CMD_List.CmdFaild;
          }
          TxMessage.DLC = 0;
          CAN_WriteData(&TxMessage);
        }
      }else{
        TxMessage.ExtId = (CAN_BOOT_GetAddrData()<<CMD_WIDTH)|CMD_List.CmdFaild;
        TxMessage.DLC = 0;
        CAN_WriteData(&TxMessage);
      }
    }
    return;
  }
  //CMD_List.Check���ڵ����߼��
  //�ڵ��յ�������󷵻ع̼��汾��Ϣ�͹̼����ͣ���������Bootloader�����APP���򶼱���ʵ��
  if(can_cmd == CMD_List.Check){
    if(can_addr != 0x00){
      TxMessage.ExtId = (CAN_BOOT_GetAddrData()<<CMD_WIDTH)|CMD_List.CmdSuccess;
      TxMessage.Data[0] = 0;//���汾�ţ����ֽ�
      TxMessage.Data[1] = 1;
      TxMessage.Data[2] = 0;//�ΰ汾�ţ����ֽ�
      TxMessage.Data[3] = 0;
      TxMessage.Data[4] = (uint8_t)(FW_TYPE>>24);
      TxMessage.Data[5] = (uint8_t)(FW_TYPE>>16);
      TxMessage.Data[6] = (uint8_t)(FW_TYPE>>8);
      TxMessage.Data[7] = (uint8_t)(FW_TYPE>>0);
      TxMessage.DLC = 8;
      CAN_WriteData(&TxMessage);
    }
    return;
  }
  //CMD_List.Excute�����Ƴ�����ת��ָ����ִַ��
  //��������Bootloader��APP�����ж�����ʵ��
  if(can_cmd == CMD_List.Excute){
    exe_type = (pRxMessage->Data[0]<<24)|(pRxMessage->Data[1]<<16)|(pRxMessage->Data[2]<<8)|(pRxMessage->Data[3]<<0);
    if(exe_type == CAN_BL_APP){
      if((*((uint32_t *)APP_START_ADDR)!=0xFFFFFFFF)){
        CAN_BOOT_JumpToApplication(APP_START_ADDR);
      }
    }
    return;
  }
}
/*********************************END OF FILE**********************************/
