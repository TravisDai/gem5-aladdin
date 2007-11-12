/*
 * Copyright (c) 2007 The Hewlett-Packard Development Company
 * All rights reserved.
 *
 * Redistribution and use of this software in source and binary forms,
 * with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * The software must be used only for Non-Commercial Use which means any
 * use which is NOT directed to receiving any direct monetary
 * compensation for, or commercial advantage from such use.  Illustrative
 * examples of non-commercial use are academic research, personal study,
 * teaching, education and corporate research & development.
 * Illustrative examples of commercial use are distributing products for
 * commercial advantage and providing services using the software for
 * commercial advantage.
 *
 * If you wish to use this software or functionality therein that may be
 * covered by patents for commercial use, please contact:
 *     Director of Intellectual Property Licensing
 *     Office of Strategy and Technology
 *     Hewlett-Packard Company
 *     1501 Page Mill Road
 *     Palo Alto, California  94304
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.  Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.  Neither the name of
 * the COPYRIGHT HOLDER(s), HEWLETT-PACKARD COMPANY, nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.  No right of
 * sublicense is granted herewith.  Derivatives of the software and
 * output created using the software may be prepared, but only for
 * Non-Commercial Uses.  Derivatives of the software may be shared with
 * others provided: (i) the others agree to abide by the list of
 * conditions herein which includes the Non-Commercial Use restrictions;
 * and (ii) such Derivatives of the software include the above copyright
 * notice to acknowledge the contribution from this software where
 * applicable, this list of conditions and the disclaimer below.
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
 *
 * Authors: Gabe Black
 */

#ifndef __ARCH_X86_TLB_HH__
#define __ARCH_X86_TLB_HH__

#include <list>
#include <vector>
#include <string>

#include "arch/x86/pagetable.hh"
#include "arch/x86/segmentregs.hh"
#include "config/full_system.hh"
#include "mem/mem_object.hh"
#include "mem/request.hh"
#include "params/X86DTB.hh"
#include "params/X86ITB.hh"
#include "sim/faults.hh"
#include "sim/sim_object.hh"

class ThreadContext;
class Packet;

namespace X86ISA
{
    static const unsigned StoreCheck = 1 << NUM_SEGMENTREGS;

    class TLB;

    class TLB : public MemObject
    {
      protected:
        friend class FakeITLBFault;
        friend class FakeDTLBFault;

        System * sys;

        bool allowNX;

      public:
        typedef X86TLBParams Params;
        TLB(const Params *p);

        void dumpAll();

        TlbEntry *lookup(Addr va, bool update_lru = true);

#if FULL_SYSTEM
      protected:
        class Walker
        {
          public:
            enum State {
                Ready,
                Waiting,
                LongPML4,
                LongPDP,
                LongPD,
                LongPTE,
                PAEPDP,
                PAEPD,
                PAEPTE,
                PSEPD,
                PD,
                PTE
            };

            // Act on the current state and determine what to do next. read
            // should be the packet that just came back from a read and write
            // should be NULL. When the function returns, read is either NULL
            // if the machine is finished, or points to a packet to initiate
            // the next read. If any write is required to update an "accessed"
            // bit, write will point to a packet to do the write. Otherwise it
            // will be NULL.
            void doNext(PacketPtr &read, PacketPtr &write);

            // Kick off the state machine.
            void start(ThreadContext * _tc, Addr vaddr);

          protected:
            friend class TLB;

            /*
             * State having to do with sending packets.
             */
            PacketPtr read;
            std::vector<PacketPtr> writes;

            // How many memory operations are in flight.
            unsigned inflight;

            bool retrying;

            /*
             * Functions for dealing with packets.
             */
            bool recvTiming(PacketPtr pkt);
            void recvRetry();

            void sendPackets();

            /*
             * Port for accessing memory
             */
            class WalkerPort : public Port
            {
              public:
                WalkerPort(const std::string &_name, Walker * _walker) :
                      Port(_name, _walker->tlb), walker(_walker),
                      snoopRangeSent(false)
                {}

              protected:
                Walker * walker;

                bool snoopRangeSent;

                bool recvTiming(PacketPtr pkt);
                Tick recvAtomic(PacketPtr pkt);
                void recvFunctional(PacketPtr pkt);
                void recvStatusChange(Status status);
                void recvRetry();
                void getDeviceAddressRanges(AddrRangeList &resp,
                        bool &snoop)
                {
                    resp.clear();
                    snoop = true;
                }
            };

            friend class WalkerPort;

            WalkerPort port;

            // The TLB we're supposed to load.
            TLB * tlb;

            /*
             * State machine state.
             */
            ThreadContext * tc;
            State state;
            State nextState;
            int size;
            bool enableNX;
            TlbEntry entry;

          public:
            Walker(const std::string &_name, TLB * _tlb) :
                read(NULL), inflight(0), retrying(false),
                port(_name + "-walker_port", this),
                tlb(_tlb),
                tc(NULL), state(Ready), nextState(Ready)
            {
            }
        };

        Walker walker;

#endif

        Port *getPort(const std::string &if_name, int idx = -1);

      protected:
        int size;

        TlbEntry * tlb;

        typedef std::list<TlbEntry *> EntryList;
        EntryList freeList;
        EntryList entryList;

        void insert(Addr vpn, TlbEntry &entry);

        void invalidateAll();

        void invalidateNonGlobal();

        void demapPage(Addr va);

        template<class TlbFault>
        Fault translate(RequestPtr &req, ThreadContext *tc,
                bool write, bool execute);

      public:
        // Checkpointing
        virtual void serialize(std::ostream &os);
        virtual void unserialize(Checkpoint *cp, const std::string &section);
    };

    class ITB : public TLB
    {
      public:
        typedef X86ITBParams Params;
        ITB(const Params *p) : TLB(p)
        {
            sys = p->system;
            allowNX = false;
        }

        Fault translate(RequestPtr &req, ThreadContext *tc);

        friend class DTB;
    };

    class DTB : public TLB
    {
      public:
        typedef X86DTBParams Params;
        DTB(const Params *p) : TLB(p)
        {
            sys = p->system;
            allowNX = true;
        }
        Fault translate(RequestPtr &req, ThreadContext *tc, bool write);
#if FULL_SYSTEM
        Tick doMmuRegRead(ThreadContext *tc, Packet *pkt);
        Tick doMmuRegWrite(ThreadContext *tc, Packet *pkt);
#endif

        // Checkpointing
        virtual void serialize(std::ostream &os);
        virtual void unserialize(Checkpoint *cp, const std::string &section);
    };
}

#endif // __ARCH_X86_TLB_HH__
