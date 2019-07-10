/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "light.h"
#include <cmath>
#include "util/numeric.h"
#include "settings.h"

#ifndef SERVER

static u8 light_LUT[LIGHT_SUN + 1];

// The const ref to light_LUT is what is actually used in the code
const u8 *light_decode_table = light_LUT;

struct LightingParams {
	float a, b, c; // polynomial coefficients
	float boost, center, sigma; // normal boost parameters
	float gamma;
};

static LightingParams params;

float decode_light_f(float x)
{
	if (x >= 1.0f) // x is equal to 1.0f half the time
		return 1.0f;
	x = std::fmax(x, 0.0f);
	float brightness = ((params.a * x + params.b) * x + params.c) * x;
	brightness += params.boost * std::exp(-0.5f * sqr((x - params.center) / params.sigma));
	if (brightness <= 0.0f) // may happen if parameters are insane
		return 0.0f;
	if (brightness >= 1.0f)
		return 1.0f;
	return powf(brightness, 1.0f / params.gamma);
}

// Initialize or update the light value tables using the specified gamma
void set_light_table(float gamma)
{
// Lighting curve derivatives
	const float alpha = g_settings->getFloat("lighting_alpha");
	const float beta  = g_settings->getFloat("lighting_beta");
// Lighting curve coefficients
	params.a = alpha + beta - 2.0f;
	params.b = 3.0f - 2.0f * alpha - beta;
	params.c = alpha;
// Mid boost
	params.boost = g_settings->getFloat("lighting_boost");
	params.center = g_settings->getFloat("lighting_boost_center");
	params.sigma = g_settings->getFloat("lighting_boost_spread");
// Gamma correction
	params.gamma = rangelim(gamma, 0.5f, 10.0f);

// Boundary values should be fixed
	light_LUT[0] = 0;
	light_LUT[LIGHT_SUN] = 255;

	for (size_t i = 1; i < LIGHT_SUN; i++) {
		float brightness = decode_light_f((float)i / LIGHT_SUN);
		// Strictly speaking, rangelim is not necessary here—if the implementation
		// is conforming. But we don’t want problems in any case.
		light_LUT[i] = rangelim((s32)(255.0f * brightness), 0, 255);
		// Ensure light brightens with each level
		if (i > 1 && light_LUT[i] <= light_LUT[i - 1])
			light_LUT[i] = light_LUT[i - 1] + 1;
	}
}

#endif
