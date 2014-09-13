/*
 * vectors.h
 *
 * Author: Ming Tsang
 * Copyright (c) 2014 HKUST SmartCar Team
 */
/* ###################################################################
**     THIS COMPONENT MODULE IS GENERATED BY THE TOOL. DO NOT MODIFY IT.
**     Filename    : Cpu.h
**     Project     : ProcessorExpert
**     Processor   : MK60DN512ZVLQ10
**     Component   : MK60N512LQ100
**     Version     : Component 01.000, Driver 01.04, CPU db: 3.00.001
**     Datasheet   : K60P144M100SF2RM, Rev. 5, 8 May 2011
**     Compiler    : GNU C Compiler
**     Date/Time   : 2014-01-15, 23:29, # CodeGen: 0
**     Abstract    :
**
**     Settings    :
**
**     Contents    :
**         No public methods
**
**     (c) Freescale Semiconductor, Inc.
**     2004 All Rights Reserved
**
**     Copyright : 1997 - 2013 Freescale Semiconductor, Inc. All Rights Reserved.
**     SOURCE DISTRIBUTION PERMISSIBLE as directed in End User License Agreement.
**
**     http      : www.freescale.com
**     mail      : support@freescale.com
** ###################################################################*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "libbase/k60/hardware.h"

#include "libbase/k60/misc_utils_c.h"

/* Interrupt vector table type definition */
typedef void (*tIsrFunc)(void);
typedef struct {
	void * __ptr;
	tIsrFunc __fun[0x77];
} tVectorTable;

typedef void (*HardFaultHandler)(void);
extern HardFaultHandler g_hard_fault_handler;

#define GetActiveVector() ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) >> SCB_ICSR_VECTACTIVE_Pos)
#define VectorToIrq(x) (x - 16)
#define GetActiveIrq() VectorToIrq(GetActiveVector())

void InitVectorTable(void);
void SetIsr(IRQn_Type irq, tIsrFunc handler);
void EnableIrq(IRQn_Type irq);
void DisableIrq(IRQn_Type irq);
__ISR void DefaultIsr(void);

#ifdef __cplusplus
}
#endif