// This file is part of Aspect-Oriented-IP.
//
// Aspect-Oriented-IP is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Aspect-Oriented-IP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Aspect-Oriented-IP.  If not, see <http://www.gnu.org/licenses/>.
// 
// Copyright (C) 2013 David Gräff

#pragma once

namespace ipstack {

class System {
public:
	/**
	  * The ipstack calls this method to halt the system on fatal errors
	  * (should not happen in normal operation mode and only on memory
	  * corruption).
	  */
	static void haltsystem();
	
	/**
	  * Call this for ipstack initalization.
	  */
	static void init();

	/**
	 * Call this periodically every minute. This is for time-related maintenance like
	 * ARP/NDP entries expire, IPv6 address expire etc. If you did not enable expire
	 * functionallity this method does nothing.
	 */
	static void periodic1min();
	
	/**
	 * Convenience method for your main loop, to call periodic1min
	 * every minute (based on your Clock implementation)
	 */
	static void periodic();
	
};

}
