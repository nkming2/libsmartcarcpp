/*
 * spi_master.cpp
 *
 * Author: Harrison Ng
 * Copyright (c) 2014-2015 HKUST SmartCar Team
 * Refer to LICENSE for details
 */

#include "libbase/kl26/hardware.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <functional>

#include "libbase/log.h"
#include "libbase/kl26/clock_utils.h"
#include "libbase/kl26/misc_utils.h"
#include "libbase/kl26/pin.h"
#include "libbase/kl26/pinout.h"
#include "libbase/kl26/sim.h"
#include "libbase/kl26/spi_master.h"
#include "libbase/kl26/spi_utils.h"
#include "libbase/kl26/vectors.h"

#include "libutil/misc.h"

using namespace libutil;

#define TX_FIFO_SIZE 8

namespace libbase
{
namespace kl26
{

namespace
{

constexpr SPI_Type * MEM_MAPS[PINOUT::GetSpiCount()] = {SPI0, SPI1};

SpiMaster* g_instances[PINOUT::GetSpiCount()] = {};

}

SpiMaster::SpiMaster(const Config &config)
		: m_is_init(false)
{
	if (!InitModule(config) || g_instances[m_module])
	{
		assert(false);
		return;
	}

	m_is_init = true;
	g_instances[m_module] = this;

	Sim::SetEnableClockGate(EnumAdvance(Sim::ClockGate::kSpi0, m_module), true);
	InitPin(config);
	InitBrReg(config);
	InitC2Reg(config);
	InitC1Reg(config);
	if (m_module == 1)
	{
		InitC3Reg(config);
	}
	InitInterrupt(config);

	SetEnable(true);
}

SpiMaster::SpiMaster(nullptr_t)
		: m_module(0),
		  m_is_init(false)
{}

SpiMaster::~SpiMaster()
{
	Uninit();
}

bool SpiMaster::InitModule(const Config &config)
{
	const Spi::MisoName miso = PINOUT::GetSpiMiso(config.sin_pin);
	const int miso_module = (miso != Spi::MisoName::kDisable)
			? static_cast<int>(miso) : -1;

	const Spi::MosiName mosi = PINOUT::GetSpiMosi(config.sout_pin);
	const int mosi_module = (mosi != Spi::MosiName::kDisable)
			? static_cast<int>(mosi) : -1;

	const Spi::SckName sck = PINOUT::GetSpiSck(config.sck_pin);
	const int sck_module = static_cast<int>(sck);

	const Spi::PcsName pcs = PINOUT::GetSpiPcs(config.pcs_pin);
	const int pcs_module = SpiUtils::GetSpiModule(pcs);

	if ((miso == Spi::MisoName::kDisable && mosi == Spi::MosiName::kDisable)
			|| sck == Spi::SckName::kDisable || pcs == Spi::PcsName::kDisable)
	{
		return false;
	}
	if (miso_module != mosi_module && miso_module != -1 && mosi_module != -1)
	{
		return false;
	}
	const int module = (miso_module != -1) ? miso_module : mosi_module;
	if (module == -1)
	{
		return false;
	}
	if (module != sck_module || module != pcs_module)
	{
		return false;
	}

	m_module = module;
	return true;
}

void SpiMaster::InitPin(const Config &config)
{
	if (config.sin_pin != Pin::Name::kDisable)
	{
		Pin::Config sin_config;
		sin_config.pin = config.sin_pin;
		sin_config.mux = PINOUT::GetSpiMisoMux(config.sin_pin);
		m_sin = Pin(sin_config);
	}

	if (config.sout_pin != Pin::Name::kDisable)
	{
		Pin::Config sout_config;
		sout_config.pin = config.sout_pin;
		sout_config.mux = PINOUT::GetSpiMosiMux(config.sout_pin);
		m_sout = Pin(sout_config);
	}

	Pin::Config sck_config;
	sck_config.pin = config.sck_pin;
	sck_config.mux = PINOUT::GetSpiSckMux(config.sck_pin);
	m_sck = Pin(sck_config);

	Pin::Config cs_config;
	cs_config.pin = config.pcs_pin;
	cs_config.mux = PINOUT::GetSpiPcsMux(config.pcs_pin);
	m_cs = Pin(cs_config);
}

void SpiMaster::InitBrReg(const Config &config)
{
	// BaudRateDivisor = (SPPR + 1) * 2 ^ (SPR + 1)
	// BaudRate = SPI Module Clock / BaudRateDivisor

	uint8_t reg = 0;

	uint32_t module_clock = 0;
	if (m_module == 0)
	{
		module_clock = ClockUtils::GetBusClockKhz();
	}
	else if (m_module == 1)
	{
		module_clock = ClockUtils::GetCoreClockKhz();
	}

	Uint best_sppr = 0;
	Uint best_spr = 0;
	Uint min_diff = static_cast<Uint>(-1);
	for (Uint spr = 0; spr < 9; ++spr)
	{
		for (Uint sppr = 0; sppr < 8; ++sppr)
		{
			const Uint divisor = (sppr + 1) * ((2 << spr) + 1);
			const uint32_t this_baud_rate = module_clock / divisor;
			const Uint this_diff = abs((int32_t)(this_baud_rate
					- config.baud_rate_khz));
			if (this_diff < min_diff)
			{
				min_diff = this_diff;
				best_sppr = sppr;
				best_spr = spr;
			}

			if (min_diff == 0)
			{
				break;
			}
		}
	}
	reg |= SPI_BR_SPPR(best_sppr);
	reg |= SPI_BR_SPR(best_spr);

	MEM_MAPS[m_module]->BR = reg;
}

void SpiMaster::InitC2Reg(const Config&)
{
	uint8_t reg = 0;

	SET_BIT(reg, SPI_C2_MODFEN_SHIFT);

	MEM_MAPS[m_module]->C2 = reg;
}

void SpiMaster::InitC1Reg(const Config &config)
{
	uint8_t reg = 0;

	SET_BIT(reg, SPI_C1_MSTR_SHIFT);
	if (!config.is_sck_idle_low)
	{
		SET_BIT(reg, SPI_C1_CPOL_SHIFT);
	}
	if (!config.is_sck_capture_first)
	{
		SET_BIT(reg, SPI_C1_CPHA_SHIFT);
	}
	SET_BIT(reg, SPI_C1_SSOE_SHIFT);
	if (!config.is_msb_firt)
	{
		SET_BIT(reg, SPI_C1_LSBFE_SHIFT);
	}

	MEM_MAPS[m_module]->C1 = reg;
}

void SpiMaster::InitC3Reg(const Config&)
{
	uint8_t reg = 0;

	SET_BIT(reg, SPI_C3_FIFOMODE_SHIFT);

	MEM_MAPS[m_module]->C3 = reg;
}

void SpiMaster::InitInterrupt(const Config &config)
{
	m_rx_isr = config.rx_isr;
	m_tx_isr = config.tx_isr;

	SetInterrupt((bool)m_rx_isr, (bool)m_tx_isr);
}

void SpiMaster::SetInterrupt(const bool tx_flag, const bool rx_flag)
{
	// If we init the interrupt here, Tx isr will be called immediately which
	// may not be intended
	SetEnableRxIrq(false);
	SetEnableTxIrq(false);

	if (tx_flag || rx_flag)
	{
		SetIsr(EnumAdvance(SPI0_IRQn, m_module), IrqHandler);
		EnableIrq(EnumAdvance(SPI0_IRQn, m_module));
	}
	else
	{
		DisableIrq(EnumAdvance(SPI0_IRQn, m_module));
		SetIsr(EnumAdvance(SPI0_IRQn, m_module), nullptr);
	}
}

void SpiMaster::Uninit()
{
	if (m_is_init)
	{
		m_is_init = false;

		SetEnable(false);
		SetInterrupt(false, false);

		Sim::SetEnableClockGate(EnumAdvance(Sim::ClockGate::kSpi0, m_module),
				false);
		g_instances[m_module] = nullptr;
	}
}

void SpiMaster::SetEnable(const bool flag)
{
	STATE_GUARD(SpiMaster, VOID);

	if (flag)
	{
		SET_BIT(MEM_MAPS[m_module]->C1, SPI_C1_SPE_SHIFT);
	}
	else
	{
		CLEAR_BIT(MEM_MAPS[m_module]->C1, SPI_C1_SPE_SHIFT);
	}
}

uint16_t SpiMaster::ExchangeData(const uint8_t slave_id, const uint16_t data)
{
	STATE_GUARD(SpiMaster, 0);
	assert((slave_id == 0));

	// Wait until Tx buffer is empty
	while (!GET_BIT(MEM_MAPS[m_module]->S,SPI_S_SPTEF_SHIFT))
	{}
	MEM_MAPS[m_module]->DH = SPI_DH_Bits(data >> 8);
	MEM_MAPS[m_module]->DL = SPI_DL_Bits(data);
	// Wait until Tx buffer is empty
	while (!GET_BIT(MEM_MAPS[m_module]->S,SPI_S_SPTEF_SHIFT))
	{}

	uint16_t receive = 0;
	receive = (MEM_MAPS[m_module]->DH << 8) | (MEM_MAPS[m_module]->DL);
	return receive;
}

size_t SpiMaster::PushData(const uint8_t slave_id, const uint16_t *data,
		const size_t size)
{
	return PushData(slave_id, reinterpret_cast<const uint8_t*>(data), size * 2);
}

size_t SpiMaster::PushData(const uint8_t slave_id, const uint8_t *data,
		const size_t size)
{
	STATE_GUARD(SpiMaster, 0);
	assert((slave_id == 0));

	if (m_module == 0)
	{
		return PushDirect(slave_id, data, size);
	}
	else
	{
		return PushFifo(slave_id, data, size);
	}
}

size_t SpiMaster::PushDirect(const uint8_t, const uint8_t *data,
		const size_t size)
{
	if (GET_BIT(MEM_MAPS[m_module]->S, SPI_S_SPTEF_SHIFT) && size > 0)
	{
		MEM_MAPS[m_module]->DH = SPI_DH_Bits(data[0] >> 8);
		MEM_MAPS[m_module]->DL = SPI_DL_Bits(data[0]);
		return 1;
	}
	else
	{
		return 0;
	}
}

size_t SpiMaster::PushFifo(const uint8_t, const uint8_t *data,
		const size_t size)
{
	size_t push = 0;
	while (!GET_BIT(MEM_MAPS[m_module]->S, SPI_S_TXFULLF_SHIFT) && push < size)
	{
		MEM_MAPS[m_module]->DH = SPI_DH_Bits(data[push] >> 8);
		MEM_MAPS[m_module]->DL = SPI_DL_Bits(data[push]);
		++push;
	}
	return push;
}

void SpiMaster::SetEnableRxIrq(const bool flag)
{
	STATE_GUARD(SpiMaster, VOID);

	if (flag)
	{
		SET_BIT(MEM_MAPS[m_module]->C1, SPI_C1_SPIE_SHIFT);
	}
	else
	{
		CLEAR_BIT(MEM_MAPS[m_module]->C1, SPI_C1_SPIE_SHIFT);
	}
}

void SpiMaster::SetEnableTxIrq(const bool flag)
{
	STATE_GUARD(SpiMaster, VOID);

	if (flag)
	{
		SET_BIT(MEM_MAPS[m_module]->C1, SPI_C1_SPTIE_SHIFT);
	}
	else
	{
		CLEAR_BIT(MEM_MAPS[m_module]->C1, SPI_C1_SPTIE_SHIFT);
	}
}

__ISR void SpiMaster::IrqHandler()
{
	const int module = GetActiveIrq() - SPI0_IRQn;
	SpiMaster *const that = g_instances[module];
	if (!that || !(*that))
	{
		// Something wrong?
		assert(false);
		that->SetInterrupt(false, false);
		return;
	}

	if (GET_BIT(MEM_MAPS[module]->S, SPI_S_SPRF_SHIFT)
			&& GET_BIT(MEM_MAPS[module]->C1, SPI_C1_SPIE_SHIFT))
	{
		if (that->m_rx_isr)
		{
			that->m_rx_isr(that);
		}
		else
		{
			that->SetEnableRxIrq(false);
		}
	}

	if (GET_BIT(MEM_MAPS[module]->S, SPI_S_SPTEF_SHIFT)
			&& GET_BIT(MEM_MAPS[module]->C1, SPI_C1_SPTIE_SHIFT))
	{
		if (that->m_tx_isr)
		{
			that->m_tx_isr(that);
		}
		else
		{
			that->SetEnableTxIrq(false);
		}
	}
}

}
}
