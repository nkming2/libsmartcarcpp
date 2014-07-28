/*
 * gpio.h
 *
 * Author: Ming Tsang
 * Copyright (c) 2014 HKUST SmartCar Team
 */

#ifndef LIBBASE_K60_GPIO_H_
#define LIBBASE_K60_GPIO_H_

#include <cstddef>
#include <cstdint>

#include <bitset>

#include "libbase/k60/pin.h"

namespace libbase
{
namespace k60
{

class Gpo;

class Gpi
{
public:
	struct Config
	{
		PinConfig::Name pin;
		std::bitset<PinConfig::ConfigBit::kSize> config;
	};

	explicit Gpi(const Config &config);
	Gpi(Gpi &&rhs);
	explicit Gpi(Pin &&rhs);
	explicit Gpi(nullptr_t);

	Gpi& operator=(Gpi &&rhs);

	bool Get() const;
	Gpo ToGpo();

	Pin* GetPin()
	{
		return &m_pin;
	}

private:
	Pin m_pin;
};

class Gpo
{
public:
	struct Config
	{
		PinConfig::Name pin;
		std::bitset<PinConfig::ConfigBit::kSize> config;
		bool is_high = false;
	};

	explicit Gpo(const Config &config);
	Gpo(Gpo &&rhs);
	explicit Gpo(Pin &&rhs);
	explicit Gpo(nullptr_t);

	Gpo& operator=(Gpo &&rhs);

	void Set(bool is_high);
	void Turn();
	Gpi ToGpi();

	Pin* GetPin()
	{
		return &m_pin;
	}

private:
	Pin m_pin;
};

class Gpio
{
public:
	explicit Gpio(const Gpi::Config &config);
	explicit Gpio(const Gpo::Config &config);
	explicit Gpio(nullptr_t);

	void Set(bool is_high)
	{
		EnsureGpo();
		m_gpo.Set(is_high);
	}

	void Turn()
	{
		EnsureGpo();
		m_gpo.Turn();
	}

	bool Get()
	{
		EnsureGpi();
		return m_gpi.Get();
	}

	void EnsureGpi();
	void EnsureGpo();

private:
	Gpi m_gpi;
	Gpo m_gpo;
	bool m_is_gpo;
};

}
}

#endif /* LIBBASE_K60_GPIO_H_ */