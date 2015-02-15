/*
 * pin_isr_manager.h
 * Manage ISR for pins
 *
 * Author: Ming Tsang
 * Copyright (c) 2014-2015 HKUST SmartCar Team
 * Refer to LICENSE for details
 */
/*

#pragma once

#include <bitset>
#include <functional>

#include "libbase/kl26/gpio.h"
#include "libbase/kl26/misc_utils.h"
#include "libbase/kl26/pin.h"
#include "libbase/kl26/pinout.h"

namespace libbase
{
namespace kl26
{

class PinIsrManager
{
public:
	typedef std::function<void(const Pin::Name pin)> OnPinIrqListener;

	static void SetPinIsr(libbase::kl26::Pin *pin, const OnPinIrqListener &isr)
	{
		GetInstance()->SetPinIsr_(pin, isr);
	}

private:
	struct PinData
	{
		Pin *pin = nullptr;
		OnPinIrqListener isr = nullptr;
	};

	PinIsrManager();
	~PinIsrManager();

	void InitPort(const Uint port);

	static PinIsrManager* GetInstance();

	void SetPinIsr_(libbase::kl26::Pin *pin, const OnPinIrqListener &isr);

	template<Uint port>
	static __ISR void PortIrqHandler();

	PinData* m_pin_data[PINOUT::GetPortCount()];
	bool m_is_enable[PINOUT::GetPortCount()];

	static PinIsrManager *m_instance;
};

}
}
*/
