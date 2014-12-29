/*
 * light_sensor.h
 *
 * Author: Ming Tsang
 * Copyright (c) 2014 HKUST SmartCar Team
 * Refer to LICENSE for details
 */

#pragma once

#include <cstdint>

#include <functional>

#include "libbase/k60/gpio.h"

namespace libsc
{
namespace k60
{

class LightSensor
{
public:
	typedef std::function<void(const uint8_t id)> OnDetectListener;

	struct Config
	{
		uint8_t id;
		bool is_active_low;
		OnDetectListener listener;
	};

	explicit LightSensor(const Config &config);

	bool IsDetected() const;

private:
	OnDetectListener m_isr;
	libbase::k60::Gpi m_pin;
	bool m_is_active_low;
};

}
}
