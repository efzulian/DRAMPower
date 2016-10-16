/*
 * Copyright (c) 2012-2014, TU Delft
 * Copyright (c) 2012-2014, TU Eindhoven
 * Copyright (c) 2012-2014, TU Kaiserslautern
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "CommandAnalysis.h"

using std::cerr;
using std::endl;
using std::max;

using namespace Data;

int64_t zero_guard(int64_t cycles_in)
{
  // Calculate max(0, cycles_in)
  int64_t zero = 0;
  return max(zero, cycles_in);
}


void CommandAnalysis::handleAct(unsigned bank, int64_t timestamp)
{
  printWarningIfPoweredDown("Command issued while in power-down mode.", MemCommand::ACT, timestamp, bank);
  // If command is ACT - update number of acts, bank state of the
  // target bank, first and latest activation cycle and the memory
  // state. Update the number of precharged/idle-precharged cycles.
  // If the bank is already active ignore the command and generate a
  // warning.
  if (isPrecharged(bank)) {
    numberofacts++;

    if (nActiveBanks() == 0) {
      // Here a memory state transition to ACT is happening. Save the
      // number of cycles in precharge state (increment the counter).
      first_act_cycle = timestamp;
      precycles += zero_guard(timestamp - last_pre_cycle);
      idle_pre_update(timestamp, latest_pre_cycle);
    }

    bank_state[bank] = BANK_ACTIVE;
    latest_act_cycle = timestamp;
  } else {
    printWarning("Bank is already active!", MemCommand::ACT, timestamp, bank);
  }
}

void CommandAnalysis::handleRd(unsigned bank, int64_t timestamp)
{
  printWarningIfPoweredDown("Command issued while in power-down mode.", MemCommand::RD, timestamp, bank);
  // If command is RD - update number of reads and read cycle. Check
  // for active idle cycles (if any).
  if (isPrecharged(bank)) {
    printWarning("Bank is not active!", MemCommand::RD, timestamp, bank);
  }
  numberofreads++;
  idle_act_update(latest_read_cycle, latest_write_cycle, latest_act_cycle, timestamp);
  latest_read_cycle = timestamp;
}

void CommandAnalysis::handleWr(unsigned bank, int64_t timestamp)
{
  printWarningIfPoweredDown("Command issued while in power-down mode.", MemCommand::WR, timestamp, bank);
  // If command is WR - update number of writes and write cycle. Check
  // for active idle cycles (if any).
  if (isPrecharged(bank)) {
    printWarning("Bank is not active!", MemCommand::WR, timestamp, bank);
  }
  numberofwrites++;
  idle_act_update(latest_read_cycle, latest_write_cycle, latest_act_cycle, timestamp);
  latest_write_cycle = timestamp;
}

void CommandAnalysis::handleRef(unsigned bank, int64_t timestamp)
{
  printWarningIfPoweredDown("Command issued while in power-down mode.", MemCommand::REF, timestamp, bank);
  // If command is REF - update number of refreshes, set bank state of
  // all banks to ACT, set the last PRE cycles at RFC-RP cycles from
  // timestamp, set the number of active cycles to RFC-RP and check
  // for active and precharged cycles and idle active and idle
  // precharged cycles before refresh. Change memory state to 0.
  printWarningIfActive("One or more banks are active! REF requires all banks to be precharged.", MemCommand::REF, timestamp, bank);
  numberofrefs++;
  idle_pre_update(timestamp, latest_pre_cycle);
  first_act_cycle  = timestamp;
  precycles       += zero_guard(timestamp - last_pre_cycle);
  last_pre_cycle   = timestamp + memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP;
  latest_pre_cycle = last_pre_cycle;
  actcycles       += memSpec.memTimingSpec.RFC - memSpec.memTimingSpec.RP;
  for (auto& bs : bank_state) {
    bs = BANK_PRECHARGED;
  }
}

void CommandAnalysis::handlePre(unsigned bank, int64_t timestamp)
{
  printWarningIfPoweredDown("Command issued while in power-down mode.", MemCommand::PRE, timestamp, bank);
  // If command is explicit PRE - update number of precharges, bank
  // state of the target bank and last and latest precharge cycle.
  // Calculate the number of active cycles if the memory was in the
  // active state before, but there is a state transition to PRE now
  // (i.e., this is the last active bank).
  // If the bank is already precharged ignore the command and generate a
  // warning.

  // Precharge only if the target bank is active
  if (bank_state[bank] == BANK_ACTIVE) {
    numberofpres++;
    // Since we got here, at least one bank is active
    assert(nActiveBanks() != 0);

    if (nActiveBanks() == 1) {
      // This is the last active bank. Therefore, here a memory state
      // transition to PRE is happening. Let's increment the active cycle
      // counter.
      actcycles += zero_guard(timestamp - first_act_cycle);
      last_pre_cycle = timestamp;
      idle_act_update(latest_read_cycle, latest_write_cycle, latest_act_cycle, timestamp);
    }

    bank_state[bank] = BANK_PRECHARGED;
    latest_pre_cycle = timestamp;
  } else {
    printWarning("Bank is already precharged!", MemCommand::PRE, timestamp, bank);
  }
}

void CommandAnalysis::handlePreA(unsigned bank, int64_t timestamp)
{
  printWarningIfPoweredDown("Command issued while in power-down mode.", MemCommand::PREA, timestamp, bank);
  // If command is explicit PREA (precharge all banks) - update
  // number of precharges by the number of active banks, update the bank
  // state of all banks to PRE and set the precharge cycle (the cycle in
  // which the memory state changes from ACT to PRE, aka last_pre_cycle).
  // Calculate the number of active cycles if the memory was in the
  // active state before, but there is a state transition to PRE now.
  
  if (nActiveBanks() > 0) {
    // Active banks are being precharged
    numberofpres += nActiveBanks();
    // At least one bank was active, therefore the current memory state is
    // ACT. Since all banks are being precharged a memory state transition
    // to PRE is happening. Add to the counter the amount of cycles the
    // memory remained in the ACT state.

    actcycles += zero_guard(timestamp - first_act_cycle);
    last_pre_cycle = timestamp;
    idle_act_update(latest_read_cycle, latest_write_cycle, latest_act_cycle, timestamp);

    latest_pre_cycle = timestamp;
    // Reset the state for all banks to precharged.
    for (auto& bs : bank_state) {
      bs = BANK_PRECHARGED;
    }
  } else {
    printWarning("All banks are already precharged!", MemCommand::PREA, timestamp, bank);
  }
}

void CommandAnalysis::handlePdnFAct(unsigned bank, int64_t timestamp)
{
  // If command is fast-exit active power-down - update number of
  // power-downs, set the power-down cycle and the memory mode to
  // fast-exit active power-down. Save states of all the banks from
  // the cycle before entering active power-down, to be returned to
  // after powering-up. Update active and active idle cycles.
  printWarningIfNotActive("All banks are precharged! Incorrect use of Active Power-Down.", MemCommand::PDN_F_ACT, timestamp, bank);
  f_act_pdns++;
  last_bank_state = bank_state;
  pdn_cycle  = timestamp;
  actcycles += zero_guard(timestamp - first_act_cycle);
  idle_act_update(latest_read_cycle, latest_write_cycle, latest_act_cycle, timestamp);
  mem_state  = CommandAnalysis::MS_PDN_F_ACT;
}

void CommandAnalysis::handlePdnSAct(unsigned bank, int64_t timestamp)
{
  // If command is slow-exit active power-down - update number of
  // power-downs, set the power-down cycle and the memory mode to
  // slow-exit active power-down. Save states of all the banks from
  // the cycle before entering active power-down, to be returned to
  // after powering-up. Update active and active idle cycles.
  printWarningIfNotActive("All banks are precharged! Incorrect use of Active Power-Down.", MemCommand::PDN_S_ACT, timestamp, bank);
  s_act_pdns++;
  last_bank_state = bank_state;
  pdn_cycle  = timestamp;
  actcycles += zero_guard(timestamp - first_act_cycle);
  idle_act_update(latest_read_cycle, latest_write_cycle, latest_act_cycle, timestamp);
  mem_state  = CommandAnalysis::MS_PDN_S_ACT;
}

void CommandAnalysis::handlePdnFPre(unsigned bank, int64_t timestamp)
{
  // If command is fast-exit precharged power-down - update number of
  // power-downs, set the power-down cycle and the memory mode to
  // fast-exit precahrged power-down. Update precharged and precharged
  // idle cycles.
  printWarningIfActive("One or more banks are active! Incorrect use of Precharged Power-Down.", MemCommand::PDN_F_PRE, timestamp, bank);
  f_pre_pdns++;
  pdn_cycle  = timestamp;
  precycles += zero_guard(timestamp - last_pre_cycle);
  idle_pre_update(timestamp, latest_pre_cycle);
  mem_state  = CommandAnalysis::MS_PDN_F_PRE;
}

void CommandAnalysis::handlePdnSPre(unsigned bank, int64_t timestamp)
{
  // If command is slow-exit precharged power-down - update number of
  // power-downs, set the power-down cycle and the memory mode to
  // slow-exit precahrged power-down. Update precharged and precharged
  // idle cycles.
  printWarningIfActive("One or more banks are active! Incorrect use of Precharged Power-Down.",  MemCommand::PDN_S_PRE, timestamp, bank);
  s_pre_pdns++;
  pdn_cycle  = timestamp;
  precycles += zero_guard(timestamp - last_pre_cycle);
  idle_pre_update(timestamp, latest_pre_cycle);
  mem_state  = CommandAnalysis::MS_PDN_S_PRE;
}

void CommandAnalysis::handlePupAct(int64_t timestamp)
{
  // If command is power-up in the active mode - check the power-down
  // exit-mode employed (fast or slow), update the number of power-down
  // and power-up cycles and the latest and first act cycle. Also, reset
  // all the individual bank states to the respective saved states
  // before entering power-down.
  const MemTimingSpec& t = memSpec.memTimingSpec;

  if (mem_state == CommandAnalysis::MS_PDN_F_ACT) {
    f_act_pdcycles  += zero_guard(timestamp - pdn_cycle);
    pup_act_cycles  += t.XP;
    latest_act_cycle = timestamp + zero_guard(t.XP - t.RCD);
  } else if (mem_state == CommandAnalysis::MS_PDN_S_ACT) {
    s_act_pdcycles += zero_guard(timestamp - pdn_cycle);
    if (memSpec.memArchSpec.dll == false) {
      pup_act_cycles  += t.XP;
      latest_act_cycle = timestamp + zero_guard(t.XP - t.RCD);
    } else {
      pup_act_cycles  += t.XPDLL - t.RCD;
      latest_act_cycle = timestamp + zero_guard(t.XPDLL - (2 * t.RCD));
    }
  } else {
    cerr << "Incorrect use of Active Power-Up!" << endl;
  }
  mem_state = MS_NOT_IN_PD;
  bank_state = last_bank_state;
  first_act_cycle = timestamp;
}

void CommandAnalysis::handlePupPre(int64_t timestamp)
{
  // If command is power-up in the precharged mode - check the power-down
  // exit-mode employed (fast or slow), update the number of power-down
  // and power-up cycles and the latest and last pre cycle.
  const MemTimingSpec& t = memSpec.memTimingSpec;
  if (mem_state == CommandAnalysis::MS_PDN_F_PRE) {
    f_pre_pdcycles  += zero_guard(timestamp - pdn_cycle);
    pup_pre_cycles  += t.XP;
    latest_pre_cycle = timestamp + zero_guard(t.XP - t.RP);
  } else if (mem_state == CommandAnalysis::MS_PDN_S_PRE) {
    s_pre_pdcycles += zero_guard(timestamp - pdn_cycle);
    if (memSpec.memArchSpec.dll == false) {
      pup_pre_cycles  += t.XP;
      latest_pre_cycle = timestamp + zero_guard(t.XP - t.RP);
    } else {
      pup_pre_cycles  += t.XPDLL - t.RCD;
      latest_pre_cycle = timestamp + zero_guard(t.XPDLL - t.RCD - t.RP);
    }
  } else {
    cerr << "Incorrect use of Precharged Power-Up!" << endl;
  }
  mem_state      = MS_NOT_IN_PD;
  last_pre_cycle = timestamp;
}

void CommandAnalysis::handleSREn(unsigned bank, int64_t timestamp)
{
  // If command is self-refresh - update number of self-refreshes,
  // set memory state to SREF, update precharge and idle precharge
  // cycles and set the self-refresh cycle.
  printWarningIfActive("One or more banks are active! SREF requires all banks to be precharged.", MemCommand::SREN, timestamp, bank);
  numberofsrefs++;
  sref_cycle = timestamp;
  sref_cycle_window = timestamp;
  sref_ref_pre_cycles_window = 0;
  sref_ref_act_cycles_window = 0;      
  precycles += zero_guard(timestamp - last_pre_cycle);
  idle_pre_update(timestamp, latest_pre_cycle);
  mem_state  = CommandAnalysis::MS_SREF;
}

void CommandAnalysis::handleSREx(unsigned bank, int64_t timestamp)
{
  // If command is self-refresh exit - update the number of self-refresh
  // clock cycles, number of active and precharged auto-refresh clock
  // cycles during self-refresh and self-refresh exit based on the number
  // of cycles in the self-refresh mode and auto-refresh duration (RFC).
  // Set the last and latest precharge cycle accordingly and set the
  // memory state to 0.
  const MemTimingSpec& t = memSpec.memTimingSpec;
  if (mem_state != CommandAnalysis::MS_SREF) {
    cerr << "Incorrect use of Self-Refresh Power-Up!" << endl;
  }
  // The total duration of self-refresh is given by the difference between
  // the current clock cycle and the clock cycle of entering self-refresh.
  int64_t sref_duration = timestamp - sref_cycle;

  // Negative or zero duration should never happen.
  if (sref_duration <= 0) {
    printWarning("Invalid Self-Refresh duration!", MemCommand::SREX, timestamp, bank);
    sref_duration = 0;
  }

  // The minimum time that the DRAM must remain in Self-Refresh is CKESR.
  if (sref_duration < t.CKESR) {
    printWarning("Self-Refresh duration < CKESR!", MemCommand::SREX, timestamp, bank);
  }

  if (sref_duration >= t.RFC) {
    /*
     * Self-refresh Exit Context 1 (tSREF >= tRFC):
     * The memory remained in self-refresh for a certain number of clock
     * cycles greater than a refresh cycle time (RFC). Consequently, the
     * initial auto-refresh accomplished.
     *
     *
     *  SREN                                #              SREX
     *  |                                   #                ^
     *  |                                   #                |
     *  |<------------------------- tSREF ----------...----->|
     *  |                                   #                |
     *  |      Initial Auto-Refresh         #                |
     *  v                                   #                |
     *  ------------------------------------#-------...-----------------> t
     *                                      #
     *   <------------- tRFC -------------->#
     *   <---- (tRFC - tRP) ----><-- tRP -->#
     *               |                |
     *               v                v
     *     sref_ref_act_cycles     sref_ref_pre_cycles
     *
     *
     * Summary:
     * sref_cycles_idd6 += tSREF – tRFC
     * sref_ref_act_cycles += tRFC - tRP
     * sref_ref_pre_cycles += tRP
     * spup_ref_act_cycles += 0
     * spup_ref_pre_cycles += 0
     *
     */

    // The initial auto-refresh consumes (IDD5 − IDD3N) over one refresh
    // period (RFC) from the start of the self-refresh.
    sref_ref_act_cycles += t.RFC -
                           t.RP - sref_ref_act_cycles_window;
    sref_ref_pre_cycles += t.RP - sref_ref_pre_cycles_window;
    last_pre_cycle       = timestamp;

    // The IDD6 current is consumed for the time period spent in the
    // self-refresh mode, which excludes the time spent in finishing the
    // initial auto-refresh.
    if (sref_cycle_window > sref_cycle + t.RFC) {
        sref_cycles_idd6 += zero_guard(timestamp - sref_cycle_window);
    } else {
        sref_cycles_idd6 += zero_guard(timestamp - sref_cycle - t.RFC);
    }

    // IDD2N current is consumed when exiting the self-refresh state.
    if (memSpec.memArchSpec.dll == false) {
      spup_cycles     += t.XS;
      latest_pre_cycle = timestamp + zero_guard(t.XS - t.RP);
    } else {
      spup_cycles     += t.XSDLL - t.RCD;
      latest_pre_cycle = timestamp + zero_guard(t.XSDLL - t.RCD  - t.RP);
    }

  } else {
    // Self-refresh Exit Context 2 (tSREF < tRFC):
    // Exit self-refresh before the completion of the initial
    // auto-refresh.

    // Number of active cycles needed by an auto-refresh.
    int64_t ref_act_cycles = t.RFC - t.RP;

    if (sref_duration >= ref_act_cycles) {
      /*
       * Self-refresh Exit Context 2A (tSREF < tRFC && tSREF >= tRFC - tRP):
       * The duration of self-refresh is equal or greater than the number
       * of active cycles needed by the initial auto-refresh.
       *
       *
       *  SREN                                           SREX
       *  |                                                ^         #
       *  |                                                |         #
       *  |<------------------ tSREF --------------------->|         #
       *  |                                                |         #
       *  |                                  Initial Auto-Refresh    #
       *  v                                                |         #
       *  -----------------------------------------------------------#--> t
       *                                                             #
       *   <------------------------ tRFC -------------------------->#
       *   <------------- (tRFC - tRP)--------------><----- tRP ---->#
       *           |                                 <-----><------->
       *           v                                  |         |
       *     sref_ref_act_cycles                      v         v
       *                             sref_ref_pre_cycles spup_ref_pre_cycles
       *
       *
       * Summary:
       * sref_cycles_idd6 += 0
       * sref_ref_act_cycles += tRFC - tRP
       * sref_ref_pre_cycles += tSREF – (tRFC – tRP)
       * spup_ref_act_cycles += 0
       * spup_ref_pre_cycles += tRP – sref_ref_pre_cycles
       *
       */

      // Number of precharged cycles (zero <= pre_cycles < RP)
      int64_t pre_cycles = sref_duration - ref_act_cycles - sref_ref_pre_cycles_window;

      sref_ref_act_cycles += ref_act_cycles - sref_ref_act_cycles_window;
      sref_ref_pre_cycles += pre_cycles;

      // Number of precharged cycles during the self-refresh power-up. It
      // is at maximum tRP (if pre_cycles is zero).
      int64_t spup_pre = t.RP - pre_cycles;

      spup_ref_pre_cycles += spup_pre;

      last_pre_cycle       = timestamp + spup_pre;

      if (memSpec.memArchSpec.dll == false) {
        spup_cycles     += t.XS - spup_pre;
        latest_pre_cycle = timestamp + zero_guard(t.XS - spup_pre - t.RP);
      } else {
        spup_cycles     += t.XSDLL - t.RCD - spup_pre;
        latest_pre_cycle = timestamp + zero_guard(t.XSDLL - t.RCD - spup_pre - t.RP);
      }
    } else {
      /*
       * Self-refresh Exit Context 2B (tSREF < tRFC - tRP):
       * self-refresh duration is shorter than the number of active cycles
       * needed by the initial auto-refresh.
       *
       *
       *  SREN                             SREX
       *  |                                  ^                        #
       *  |                                  |                        #
       *  |<-------------- tSREF ----------->|                        #
       *  |                                  |                        #
       *  |                       Initial Auto-Refresh                #
       *  v                                  |                        #
       *  ------------------------------------------------------------#--> t
       *                                                              #
       *   <------------------------ tRFC --------------------------->#
       *   <-------------- (tRFC - tRP)-------------><------ tRP ---->#
       *   <--------------------------------><------><--------------->
       *               |                        |             |
       *               v                        v             v
       *     sref_ref_act_cycles    spup_ref_act_cycles spup_ref_pre_cycles
       *
       *
       * Summary:
       * sref_cycles_idd6 += 0
       * sref_ref_act_cycles += tSREF
       * sref_ref_pre_cycles += 0
       * spup_ref_act_cycles += (tRFC – tRP) - tSREF
       * spup_ref_pre_cycles += tRP
       *
       */

      sref_ref_act_cycles += sref_duration - sref_ref_act_cycles_window;

      int64_t spup_act = (t.RFC - t.RP) - sref_duration;

      spup_ref_act_cycles += spup_act;
      spup_ref_pre_cycles += t.RP;

      last_pre_cycle       = timestamp + spup_act + t.RP;
      if (memSpec.memArchSpec.dll == false) {
        spup_cycles     += t.XS - spup_act - t.RP;
        latest_pre_cycle = timestamp + zero_guard(t.XS - spup_act - (2 * t.RP));
      } else {
        spup_cycles     += t.XSDLL - t.RCD - spup_act - t.RP;
        latest_pre_cycle = timestamp + zero_guard(t.XSDLL - t.RCD - spup_act - (2 * t.RP));
      }
    }
  }
  mem_state = MS_NOT_IN_PD;
}


void CommandAnalysis::handleNopEnd(int64_t timestamp)
{
  // May be optionally used at the end of memory trace for better accuracy
  // Update all counters based on completion of operations.
  const MemTimingSpec& t = memSpec.memTimingSpec;

  if (nActiveBanks() > 0 && mem_state == MS_NOT_IN_PD) {
    actcycles += zero_guard(timestamp - first_act_cycle);
    idle_act_update(latest_read_cycle, latest_write_cycle,
                    latest_act_cycle, timestamp);
  } else if (nActiveBanks() == 0 && mem_state == MS_NOT_IN_PD) {
    precycles += zero_guard(timestamp - last_pre_cycle);
    idle_pre_update(timestamp, latest_pre_cycle);
  } else if (mem_state == CommandAnalysis::MS_PDN_F_ACT) {
    f_act_pdcycles += zero_guard(timestamp - pdn_cycle);
  } else if (mem_state == CommandAnalysis::MS_PDN_S_ACT) {
    s_act_pdcycles += zero_guard(timestamp - pdn_cycle);
  } else if (mem_state == CommandAnalysis::MS_PDN_F_PRE) {
    f_pre_pdcycles += zero_guard(timestamp - pdn_cycle);
  } else if (mem_state == CommandAnalysis::MS_PDN_S_PRE) {
    s_pre_pdcycles += zero_guard(timestamp - pdn_cycle);
  } else if (mem_state == CommandAnalysis::MS_SREF) {
    auto rfc_minus_rp = (t.RFC - t.RP);

    if (timestamp > sref_cycle + t.RFC) {
      if (sref_cycle_window <= sref_cycle + rfc_minus_rp) {
        sref_ref_act_cycles += rfc_minus_rp - sref_ref_act_cycles_window;
        sref_ref_act_cycles_window = rfc_minus_rp;
        sref_cycle_window = sref_cycle + rfc_minus_rp;
      }
      if (sref_cycle_window <= sref_cycle + t.RFC) {
        sref_ref_pre_cycles += t.RP - sref_ref_pre_cycles_window;
        sref_ref_pre_cycles_window = t.RP;
        sref_cycle_window = sref_cycle + t.RFC;
      }
      sref_cycles_idd6 += zero_guard(timestamp - sref_cycle_window);
    } else if (timestamp > sref_cycle + rfc_minus_rp) {
      
      if (sref_cycle_window <= sref_cycle + rfc_minus_rp) {
        sref_ref_act_cycles += rfc_minus_rp - sref_ref_act_cycles_window;
        sref_ref_act_cycles_window = rfc_minus_rp;
        sref_cycle_window = sref_cycle + rfc_minus_rp;
      }
      sref_ref_pre_cycles_window += timestamp - sref_cycle_window;
      sref_ref_pre_cycles += timestamp - sref_cycle_window;
    } else {
      sref_ref_act_cycles_window += timestamp - sref_cycle_window;
      sref_ref_act_cycles += timestamp - sref_cycle_window;
    }
  }
}