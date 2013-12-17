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
#include "demux/receivebuffer/SmartReceiveBufferPtr.h"
#include "ReceiveCallback.h"

namespace ipstack {
	/**
	 * Inherit from this class to get an abstract method receiveCallback that is
	 * called after demux processing if your socket object got new data via addToReceiveQueue.
	 * 
	 * If managed sockets live in a seperate task receiveCallback is not called after
	 * demux processing but from the Management_Task class.
	 */
	class ReceiveDemuxCallback :public ReceiveCallback, public LinkedList<ReceiveDemuxCallback, true> {};

} // namespace ipstack
