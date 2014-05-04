/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2011 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "agesawrapper.h"
#include "amdlib.h"
#include "BiosCallOuts.h"
#include "Ids.h"
#include "OptionsIds.h"
#include "heapManager.h"
#include "Hudson-2.h"

#ifndef SB_GPIO_REG01
#define SB_GPIO_REG01   1
#endif

#ifndef SB_GPIO_REG24
#define SB_GPIO_REG24   24
#endif

#ifndef SB_GPIO_REG27
#define SB_GPIO_REG27   27
#endif

STATIC BIOS_CALLOUT_STRUCT BiosCallouts[] =
{
	{AGESA_ALLOCATE_BUFFER,			BiosAllocateBuffer },
	{AGESA_DEALLOCATE_BUFFER,		BiosDeallocateBuffer },
	{AGESA_DO_RESET,			BiosReset },
	{AGESA_LOCATE_BUFFER,			BiosLocateBuffer },
	{AGESA_READ_SPD,			BiosReadSpd },
	{AGESA_READ_SPD_RECOVERY,		agesa_NoopUnsupported },
	{AGESA_RUNFUNC_ONAP,			BiosRunFuncOnAp },
	{AGESA_GNB_PCIE_SLOT_RESET,		BiosGnbPcieSlotReset },
	{AGESA_GET_IDS_INIT_DATA,		agesa_EmptyIdsInitData },
	{AGESA_HOOKBEFORE_DRAM_INIT,		BiosHookBeforeDramInit },
	{AGESA_HOOKBEFORE_DRAM_INIT_RECOVERY,	agesa_NoopSuccess },
	{AGESA_HOOKBEFORE_DQS_TRAINING,		agesa_NoopSuccess },
	{AGESA_HOOKBEFORE_EXIT_SELF_REF,	agesa_NoopSuccess },
};

AGESA_STATUS GetBiosCallout (UINT32 Func, UINT32 Data, VOID *ConfigPtr)
{
  UINTN i;
  AGESA_STATUS CalloutStatus;
  UINTN CallOutCount = sizeof (BiosCallouts) / sizeof (BiosCallouts [0]);

  for (i = 0; i < CallOutCount; i++)
  {
    if (BiosCallouts[i].CalloutName == Func)
    {
      break;
    }
  }

  if(i >= CallOutCount)
  {
    return AGESA_UNSUPPORTED;
  }

  CalloutStatus = BiosCallouts[i].CalloutPtr (Func, Data, ConfigPtr);

  return CalloutStatus;
}


/*  Call the host environment interface to provide a user hook opportunity. */
AGESA_STATUS BiosHookBeforeDramInit (UINT32 Func, UINT32 Data, VOID *ConfigPtr)
{
  AGESA_STATUS      Status;
  UINTN             FcnData;
  MEM_DATA_STRUCT   *MemData;
  UINT32            AcpiMmioAddr;
  UINT32            GpioMmioAddr;
  UINT8             Data8;
  UINT16            Data16;

  FcnData = Data;
  MemData = ConfigPtr;

  Status  = AGESA_SUCCESS;
  /* Get SB MMIO Base (AcpiMmioAddr) */
  WriteIo8 (0xCD6, 0x27);
  Data8   = ReadIo8(0xCD7);
  Data16  = Data8<<8;
  WriteIo8 (0xCD6, 0x26);
  Data8   = ReadIo8(0xCD7);
  Data16  |= Data8;
  AcpiMmioAddr = (UINT32)Data16 << 16;
  GpioMmioAddr = AcpiMmioAddr + GPIO_BASE;

  switch(MemData->ParameterListPtr->DDR3Voltage){
    case VOLT1_35:
      Data8 =  Read64Mem8 (GpioMmioAddr+SB_GPIO_REG178);
      Data8 &= ~(UINT8)BIT6;
      Write64Mem8(GpioMmioAddr+SB_GPIO_REG178, Data8);
      Data8 =  Read64Mem8 (GpioMmioAddr+SB_GPIO_REG179);
      Data8 |= (UINT8)BIT6;
      Write64Mem8(GpioMmioAddr+SB_GPIO_REG179, Data8);
      break;
    case VOLT1_25:
      Data8 =  Read64Mem8 (GpioMmioAddr+SB_GPIO_REG178);
      Data8 &= ~(UINT8)BIT6;
      Write64Mem8(GpioMmioAddr+SB_GPIO_REG178, Data8);
      Data8 =  Read64Mem8 (GpioMmioAddr+SB_GPIO_REG179);
      Data8 &= ~(UINT8)BIT6;
      Write64Mem8(GpioMmioAddr+SB_GPIO_REG179, Data8);
      break;
    case VOLT1_5:
    default:
      Data8 =  Read64Mem8 (GpioMmioAddr+SB_GPIO_REG178);
      Data8 |= (UINT8)BIT6;
      Write64Mem8(GpioMmioAddr+SB_GPIO_REG178, Data8);
  }
  return Status;
}

/* PCIE slot reset control */
AGESA_STATUS BiosGnbPcieSlotReset (UINT32 Func, UINT32 Data, VOID *ConfigPtr)
{
  AGESA_STATUS Status;
  UINTN                 FcnData;
  PCIe_SLOT_RESET_INFO  *ResetInfo;

  UINT32  GpioMmioAddr;
  UINT32  AcpiMmioAddr;
  UINT8   Data8;
  UINT16  Data16;

  FcnData   = Data;
  ResetInfo = ConfigPtr;
  // Get SB MMIO Base (AcpiMmioAddr)
  WriteIo8(0xCD6, 0x27);
  Data8 = ReadIo8(0xCD7);
  Data16=Data8<<8;
  WriteIo8(0xCD6, 0x26);
  Data8 = ReadIo8(0xCD7);
  Data16|=Data8;
  AcpiMmioAddr = (UINT32)Data16 << 16;
  Status = AGESA_UNSUPPORTED;
  GpioMmioAddr = AcpiMmioAddr + GPIO_BASE;

  if (ResetInfo->ResetControl == DeassertSlotReset) {
    if (ResetInfo->ResetId & (BIT2+BIT3)) {    //de-assert
      // [GPIO] GPIO45: PE_GPIO1 MXM_POWER_ENABLE, SET HIGH
      Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG45);
      if (Data8 & BIT7) {
        Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG28);
        while (!(Data8 & BIT7)) {
          Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG28);
        }
        // GPIO44: PE_GPIO0 MXM Reset
        Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG44);
        Data8 |= BIT6 ;
        Write64Mem8 (GpioMmioAddr+SB_GPIO_REG44, Data8);
        Status = AGESA_SUCCESS;
      }
    } else {
      Status = AGESA_UNSUPPORTED;
    }
    // Travis
    Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG24);
    Data8 |= BIT6;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG24, Data8);
    //DE-Assert ALL PCIE RESET
    // APU GPP0 (Dev 4)
        Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG25);
        Data8 |= BIT6 ;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG25, Data8);
    // APU GPP1 (Dev 5)
    Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG01);
    Data8 |= BIT6;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG01, Data8);
    // APU GPP2 (Dev 6)
    Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG00);
    Data8 |= BIT6;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG00, Data8);
    // APU GPP3 (Dev 7)
    Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG27);
    Data8 |= BIT6;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG27, Data8);
  } else {
    if (ResetInfo->ResetId & (BIT2+BIT3)) {  //Pcie Slot Reset is supported
      // GPIO44: PE_GPIO0 MXM Reset
      Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG44);
      Data8 &= ~(UINT8)BIT6;
      Write64Mem8 (GpioMmioAddr+SB_GPIO_REG44, Data8);
        Status = AGESA_SUCCESS;
      }
    // Travis
    Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG24);
        Data8 &= ~(UINT8)BIT6 ;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG24, Data8);
    //Assert ALL PCIE RESET
    // APU GPP0 (Dev 4)
        Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG25);
    Data8 &= ~(UINT8)BIT6;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG25, Data8);
    // APU GPP1 (Dev 5)
    Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG01);
    Data8 &= ~(UINT8)BIT6;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG01, Data8);
    // APU GPP2 (Dev 6)
    Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG00);
    Data8 &= ~(UINT8)BIT6;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG00, Data8);
    // APU GPP3 (Dev 7)
    Data8 = Read64Mem8(GpioMmioAddr+SB_GPIO_REG27);
    Data8 &= ~(UINT8)BIT6;
    Write64Mem8 (GpioMmioAddr+SB_GPIO_REG27, Data8);
  }
  return  Status;
}
