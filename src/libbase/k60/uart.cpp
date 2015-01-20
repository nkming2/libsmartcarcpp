/*
 * uart.cpp
 *
 * Author: Ming Tsang
 * Copyright (c) 2014-2015 HKUST SmartCar Team
 * Refer to LICENSE for details
 */

#include "libbase/k60/hardware.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include <algorithm>
#include <bitset>
#include <functional>
#include <vector>

#include "libbase/log.h"
#include "libbase/k60/clock_utils.h"
#include "libbase/k60/pin.h"
#include "libbase/k60/pinout.h"
#include "libbase/k60/sim.h"
#include "libbase/k60/uart.h"
#include "libbase/k60/vectors.h"

#include "libutil/misc.h"

using namespace libutil;
using namespace std;

namespace libbase
{
namespace k60
{

namespace
{

constexpr UART_Type* MEM_MAPS[PINOUT::GetUartCount()] = {UART0, UART1, UART2,
		UART3, UART4, UART5};

Uart* g_instances[PINOUT::GetUartCount()] = {};

uint32_t GetBaudRate(Uart::Config::BaudRate br)
{
	switch (br)
	{
	case Uart::Config::BaudRate::k2400:
		return 2400;

	case Uart::Config::BaudRate::k4800:
		return 4800;

	case Uart::Config::BaudRate::k9600:
		return 9600;

	case Uart::Config::BaudRate::k14400:
		return 14400;

	case Uart::Config::BaudRate::k19200:
		return 19200;

	case Uart::Config::BaudRate::k28800:
		return 28800;

	case Uart::Config::BaudRate::k38400:
		return 38400;

	case Uart::Config::BaudRate::k57600:
		return 57600;

	case Uart::Config::BaudRate::k76800:
		return 76800;

	case Uart::Config::BaudRate::k115200:
		return 115200;

	case Uart::Config::BaudRate::k230400:
		return 230400;

	case Uart::Config::BaudRate::k460800:
		return 460800;

	default:
		return 0;
	}
}

uint8_t GetFifoSize(const uint8_t buf_depth_reg)
{
	switch (buf_depth_reg)
	{
	default:
	case 0x0:
		return 1;
	case 0x1:
		return 4;
	case 0x2:
		return 8;
	case 0x3:
		return 16;
	case 0x4:
		return 32;
	case 0x5:
		return 64;
	case 0x6:
		return 128;
	}
}

}

Uart::Uart(const Config &config)
		: m_is_fifo(false), m_tx(nullptr), m_tx_fifo_size(1), m_tx_isr(nullptr),
		  m_rx(nullptr), m_rx_fifo_size(1), m_rx_isr(nullptr),
		  m_is_init(false)
{
	if (!InitModule(config.tx_pin, config.rx_pin) || g_instances[m_module])
	{
		assert(false);
		return;
	}

	m_is_init = true;

	Sim::SetEnableClockGate(EnumAdvance(Sim::ClockGate::kUart0, m_module), true);
	// Disable UART
	MEM_MAPS[m_module]->C2 = 0;

	g_instances[m_module] = this;
	InitBaudRate(config.baud_rate);
	InitPin(config);

	InitC1Reg(config);
	InitFifo(config);
	InitInterrupt(config);
	MEM_MAPS[m_module]->C2 |= UART_C2_TE_MASK | UART_C2_RE_MASK;
}

Uart::Uart(Uart &&rhs)
		: Uart(nullptr)
{
	*this = std::move(rhs);
}

Uart::Uart(nullptr_t)
		: m_module(Module::kUart0),
		  m_is_fifo(false),

		  m_tx(nullptr),
		  m_tx_fifo_size(0),

		  m_rx(nullptr),
		  m_rx_fifo_size(0),

		  m_is_init(false)
{}

Uart::~Uart()
{
	Uninit();
}

Uart& Uart::operator=(Uart &&rhs)
{
	if (this != &rhs)
	{
		Uninit();
		if (rhs)
		{
			rhs.m_is_init = false;

			m_module = rhs.m_module;
			m_is_fifo = rhs.m_is_fifo;

			m_tx = std::move(rhs.m_tx);
			m_tx_fifo_size = rhs.m_tx_fifo_size;
			// Copy instead of move to prevent race condition
			m_tx_isr = rhs.m_tx_isr;

			m_rx = std::move(rhs.m_rx);
			m_rx_fifo_size = rhs.m_rx_fifo_size;
			// Copy instead of move to prevent race condition
			m_rx_isr = rhs.m_rx_isr;

			m_is_init = true;
			g_instances[m_module] = this;
		}
	}
	return *this;
}

bool Uart::InitModule(const Pin::Name tx_pin, const Pin::Name rx_pin)
{
	int tx_module = -1;
	switch (tx_pin)
	{
	case Pin::Name::kPta2:
	case Pin::Name::kPta14:
	case Pin::Name::kPtb17:
	case Pin::Name::kPtd7:
		tx_module = 0;
		break;

	case Pin::Name::kPtc4:
	case Pin::Name::kPte0:
		tx_module = 1;
		break;

	case Pin::Name::kPtd3:
		tx_module = 2;
		break;

	case Pin::Name::kPtb11:
	case Pin::Name::kPtc17:
	case Pin::Name::kPte4:
		tx_module = 3;
		break;

	case Pin::Name::kPtc15:
	case Pin::Name::kPte24:
		tx_module = 4;
		break;

	case Pin::Name::kPtd9:
	case Pin::Name::kPte8:
		tx_module = 5;
		break;

	default:
		break;
	}

	int rx_module = -1;
	switch (rx_pin)
	{
	case Pin::Name::kPta1:
	case Pin::Name::kPta15:
	case Pin::Name::kPtb16:
	case Pin::Name::kPtd6:
		rx_module = 0;
		break;

	case Pin::Name::kPtc3:
	case Pin::Name::kPte1:
		rx_module = 1;
		break;

	case Pin::Name::kPtd2:
		rx_module = 2;
		break;

	case Pin::Name::kPtb10:
	case Pin::Name::kPtc16:
	case Pin::Name::kPte5:
		rx_module = 3;
		break;

	case Pin::Name::kPtc14:
	case Pin::Name::kPte25:
		rx_module = 4;
		break;

	case Pin::Name::kPtd8:
	case Pin::Name::kPte9:
		rx_module = 5;
		break;

	default:
		break;
	}

	if (tx_module == rx_module && tx_module != -1)
	{
		m_module = (Module)tx_module;
		return true;
	}
	else
	{
		return false;
	}
}

void Uart::InitBaudRate(const Config::BaudRate br)
{
	const uint32_t clock = (m_module < 2) ? ClockUtils::GetCoreClock()
			: ClockUtils::GetBusClock();
	const float target = clock / (float)GetBaudRate(br) / 16.0f;
	const int sbr = static_cast<int>(target);
	assert(sbr <= 0x1FFF);

	UART_Type* uart_ptr = MEM_MAPS[m_module];
	uart_ptr->BDH = UART_BDH_SBR(sbr >> 8);
	uart_ptr->BDL = UART_BDL_SBR(sbr);

	float null;
	const int brfa = roundf(modf(target, &null) * 32);
	uart_ptr->C4 &= ~UART_C4_BRFA_MASK;
	uart_ptr->C4 |= brfa;
}

void Uart::InitPin(const Config &config)
{
	Pin::Config tx_config, rx_config;
	tx_config.pin = config.tx_pin;
	rx_config.pin = config.rx_pin;
	if (m_module == 0)
	{
		tx_config.mux = (config.tx_pin == Pin::Name::kPta2)
				? Pin::Config::MuxControl::kAlt2
						: Pin::Config::MuxControl::kAlt3;
		rx_config.mux = (config.rx_pin == Pin::Name::kPta1)
				? Pin::Config::MuxControl::kAlt2
						: Pin::Config::MuxControl::kAlt3;
	}
	else
	{
		tx_config.mux = Pin::Config::MuxControl::kAlt3;
		rx_config.mux = Pin::Config::MuxControl::kAlt3;
	}
	tx_config.config = config.tx_config;
	rx_config.config = config.rx_config;

	m_tx = Pin(tx_config);
	m_rx = Pin(rx_config);
}

void Uart::InitC1Reg(const Config &config)
{
	UART_Type* uart_ptr = MEM_MAPS[m_module];
	uart_ptr->C1 = 0;
	SetLoopMode(config.config[Config::ConfigBit::kLoopMode]);
	if (config.config[Config::ConfigBit::kEnableEvenParity])
	{
		SET_BIT(uart_ptr->C1, UART_C1_PE_MASK);
	}
}

void Uart::InitFifo(const Config &config)
{
	UART_Type* uart_ptr = MEM_MAPS[m_module];
	uart_ptr->CFIFO = 0;
	m_is_fifo = config.config[Config::ConfigBit::kFifo];
	if (m_is_fifo)
	{
		SET_BIT(uart_ptr->PFIFO, UART_PFIFO_TXFE_SHIFT);
		m_tx_fifo_size = GetFifoSize((uart_ptr->PFIFO
				& UART_PFIFO_TXFIFOSIZE_MASK) >> UART_PFIFO_TXFIFOSIZE_SHIFT);
		uint8_t twfifo = config.tx_irq_threshold;
		if (config.is_tx_irq_threshold_percentage)
		{
			twfifo = std::min<uint8_t>(twfifo, 100);
			twfifo = m_tx_fifo_size * (twfifo / 100.0f);
		}
		uart_ptr->TWFIFO = libutil::Clamp<uint8_t>(0, twfifo, m_tx_fifo_size - 1);

		SET_BIT(uart_ptr->PFIFO, UART_PFIFO_RXFE_SHIFT);
		m_rx_fifo_size = GetFifoSize((uart_ptr->PFIFO
				& UART_PFIFO_RXFIFOSIZE_MASK) >> UART_PFIFO_RXFIFOSIZE_SHIFT);
		uint8_t rwfifo = config.rx_irq_threshold;
		if (config.is_rx_irq_threshold_percentage)
		{
			rwfifo = std::min<uint8_t>(rwfifo, 100);
			rwfifo = m_tx_fifo_size * (rwfifo / 100.0f);
		}
		uart_ptr->RWFIFO = libutil::Clamp<uint8_t>(1, rwfifo, m_rx_fifo_size);
	}
	else
	{
		CLEAR_BIT(uart_ptr->PFIFO, UART_PFIFO_TXFE_SHIFT);
		CLEAR_BIT(uart_ptr->PFIFO, UART_PFIFO_RXFE_SHIFT);
	}
}

void Uart::InitInterrupt(const Config &config)
{
	m_tx_isr = config.tx_isr;
	m_rx_isr = config.rx_isr;

	SetInterrupt((bool)m_tx_isr, (bool)m_rx_isr);
}

void Uart::Uninit()
{
	if (m_is_init)
	{
		m_is_init = false;

		// Disable Tx, Rx and IRQ
		MEM_MAPS[m_module]->C2 = 0;
		SetInterrupt(false, false);

		Sim::SetEnableClockGate(EnumAdvance(Sim::ClockGate::kUart0, m_module),
				false);
		g_instances[m_module] = nullptr;
	}
}

void Uart::SetLoopMode(const bool flag)
{
	STATE_GUARD(Uart, VOID);

	if (flag)
	{
		SET_BIT(MEM_MAPS[m_module]->C1, UART_C1_LOOPS_SHIFT);
	}
	else
	{
		CLEAR_BIT(MEM_MAPS[m_module]->C1, UART_C1_LOOPS_SHIFT);
	}
}

void Uart::SetInterrupt(const bool tx_flag, const bool rx_flag)
{
	// If we init the interrupt here, Tx isr will be called immediately which
	// may not be intended
	SetEnableTxIrq(false);
	SetEnableRxIrq(false);

	if (tx_flag || rx_flag)
	{
		SetIsr(EnumAdvance(UART0_RX_TX_IRQn, m_module << 1), IrqHandler);
		EnableIrq(EnumAdvance(UART0_RX_TX_IRQn, m_module << 1));
	}
	else
	{
		DisableIrq(EnumAdvance(UART0_RX_TX_IRQn, m_module << 1));
		SetIsr(EnumAdvance(UART0_RX_TX_IRQn, m_module << 1), nullptr);
	}
}

void Uart::SetEnableTxIrq(const bool flag)
{
	STATE_GUARD(Uart, VOID);

	if (flag)
	{
		SET_BIT(MEM_MAPS[m_module]->C2, UART_C2_TIE_SHIFT);
	}
	else
	{
		CLEAR_BIT(MEM_MAPS[m_module]->C2, UART_C2_TIE_SHIFT);
	}
}

void Uart::SetEnableRxIrq(const bool flag)
{
	STATE_GUARD(Uart, VOID);

	if (flag)
	{
		SET_BIT(MEM_MAPS[m_module]->C2, UART_C2_RIE_SHIFT);
	}
	else
	{
		CLEAR_BIT(MEM_MAPS[m_module]->C2, UART_C2_RIE_SHIFT);
	}
}

uint8_t Uart::GetAvailableBytes() const
{
	STATE_GUARD(Uart, 0);

	return MEM_MAPS[m_module]->RCFIFO;
}

Byte Uart::GetByte() const
{
	STATE_GUARD(Uart, 0);

	// Read S1 to clear RDRF
	while (!GET_BIT(MEM_MAPS[m_module]->S1, UART_S1_RDRF_SHIFT)
			&& !GetAvailableBytes())
	{}
	return MEM_MAPS[m_module]->D;
}

bool Uart::PeekByte(Byte *out_byte) const
{
	STATE_GUARD(Uart, false);

	// Read S1 to clear RDRF
	if (GET_BIT(MEM_MAPS[m_module]->S1, UART_S1_RDRF_SHIFT) || GetAvailableBytes())
	{
		*out_byte = MEM_MAPS[m_module]->D;
		return true;
	}
	else
	{
		return false;
	}
}

vector<Byte> Uart::GetBytes() const
{
	STATE_GUARD(Uart, {});

	vector<Byte> data;
	while (!PeekBytes(&data))
	{}
	return data;
}

bool Uart::PeekBytes(vector<Byte> *out_bytes) const
{
	STATE_GUARD(Uart, false);

	const int size = MEM_MAPS[m_module]->RCFIFO;
	if (size == 0)
	{
		return false;
	}

	out_bytes->resize(size);
	for (int i = 0; i < size - 1; ++i)
	{
		(*out_bytes)[i] = MEM_MAPS[m_module]->D;
	}
	// Read S1 to clear RDRF
	if (GET_BIT(MEM_MAPS[m_module]->S1, UART_S1_RDRF_SHIFT)
			|| MEM_MAPS[m_module]->RCFIFO > 0)
	{
		(*out_bytes)[size - 1] = MEM_MAPS[m_module]->D;
	}
	return true;
}

void Uart::SendByte(const Byte byte)
{
	STATE_GUARD(Uart, VOID);

	// Read S1 to clear TDRE
	while (!GET_BIT(MEM_MAPS[m_module]->S1, UART_S1_TDRE_SHIFT)
			&& MEM_MAPS[m_module]->TCFIFO >= m_tx_fifo_size)
	{}
	MEM_MAPS[m_module]->D = byte;
}

bool Uart::PutByte(const Byte byte)
{
	STATE_GUARD(Uart, false);

	if (GET_BIT(MEM_MAPS[m_module]->S1, UART_S1_TDRE_SHIFT))
	{
		MEM_MAPS[m_module]->D = byte;
		return true;
	}
	else
	{
		return false;
	}
}

size_t Uart::PutBytes(const Byte *bytes, const size_t size)
{
	STATE_GUARD(Uart, 0);

	const size_t send = std::min<Uint>(
			m_tx_fifo_size - MEM_MAPS[m_module]->TCFIFO, size);
	if (send == 0)
	{
		return 0;
	}

	for (size_t i = 0; i < send - 1; ++i)
	{
		MEM_MAPS[m_module]->D = bytes[i];
	}
	// Read S1 to clear TDRE
	if (GET_BIT(MEM_MAPS[m_module]->S1, UART_S1_TDRE_SHIFT)
			|| MEM_MAPS[m_module]->TCFIFO < m_tx_fifo_size)
	{
		MEM_MAPS[m_module]->D = bytes[send - 1];
	}
	return send;
}

__ISR void Uart::IrqHandler()
{
	const int module = (GetActiveIrq() - UART0_RX_TX_IRQn) >> 1;
	Uart *const that = g_instances[module];
	if (!that || !(*that))
	{
		// Something wrong?
		assert(false);
		CLEAR_BIT(MEM_MAPS[module]->C2, UART_C2_TIE_SHIFT);
		CLEAR_BIT(MEM_MAPS[module]->C2, UART_C2_RIE_SHIFT);
		return;
	}

	if (GET_BIT(MEM_MAPS[module]->C2, UART_C2_TIE_SHIFT)
			&& GET_BIT(MEM_MAPS[module]->S1, UART_S1_TDRE_SHIFT))
	{
		if (that->m_tx_isr)
		{
			that->m_tx_isr(that);
		}
		else
		{
			CLEAR_BIT(MEM_MAPS[module]->C2, UART_C2_TIE_SHIFT);
		}
	}

	if (GET_BIT(MEM_MAPS[module]->C2, UART_C2_RIE_SHIFT)
			&& GET_BIT(MEM_MAPS[module]->S1, UART_S1_RDRF_SHIFT))
	{
		if (that->m_rx_isr)
		{
			that->m_rx_isr(that);
		}
		else
		{
			CLEAR_BIT(MEM_MAPS[module]->C2, UART_C2_RIE_SHIFT);
		}
	}
}

}
}
