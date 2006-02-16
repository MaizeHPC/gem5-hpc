/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @file
 * Base class for UART
 */

#ifndef __UART_HH__
#define __UART_HH__

#include "base/range.hh"
#include "dev/io_device.hh"

class SimConsole;
class Platform;

const int RX_INT = 0x1;
const int TX_INT = 0x2;


class Uart : public PioDevice
{

  protected:
    int status;
    Addr addr;
    Addr size;
    SimConsole *cons;

  public:
    Uart(const std::string &name, SimConsole *c, MemoryController *mmu,
         Addr a, Addr s, HierParams *hier, Bus *bus, Tick pio_latency,
         Platform *p);

    virtual Fault * read(MemReqPtr &req, uint8_t *data) = 0;
    virtual Fault * write(MemReqPtr &req, const uint8_t *data) = 0;


    /**
     * Inform the uart that there is data available.
     */
    virtual void dataAvailable() = 0;


    /**
     * Return if we have an interrupt pending
     * @return interrupt status
     */
    bool intStatus() { return status ? true : false; }

    /**
     * Return how long this access will take.
     * @param req the memory request to calcuate
     * @return Tick when the request is done
     */
    Tick cacheAccess(MemReqPtr &req);
};

#endif // __UART_HH__
