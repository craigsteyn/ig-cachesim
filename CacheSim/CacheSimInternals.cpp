/*
Copyright (c) 2017, Insomniac Games
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "Precompiled.h"
#include "CacheSim/CacheSimInternals.h"

CacheSim::AccessResult CacheSim::JaguarModule::Access(int core_index, uintptr_t addr, AccessMode mode)
{
  if (kWrite == mode)
  {
    // Kick the line out of every other L1 and the other L2 package.
    for (int i = 0; i < 4; ++i)
    {
      if (i == core_index)
        continue;

      m_CoreD1[i].Invalidate(addr);
      m_CoreI1[i].Invalidate(addr);
    }

    m_OtherModule->m_Level2.Invalidate(addr);
  }

  // Start at the L2, because the cache hierarchy is inclusive.
  bool l2_hit = m_Level2.Access(addr);
  bool l1_hit = false;

  if (kCodeRead == mode)
  {
    l1_hit = m_CoreI1[core_index].Access(addr);
  }
  else
  {
    l1_hit = m_CoreD1[core_index].Access(addr);
  }

  if (l2_hit && l1_hit)
  {
    if (kCodeRead == mode)
      return kI1Hit;
    else
      return kD1Hit;
  }
  else if (l2_hit)
  {
    return kL2Hit;
  }
  else
  {
    if (kCodeRead == mode)
      return kL2IMiss;
    else
      return kL2DMiss;
  }
}

CacheSim::AccessResult CacheSim::JaguarCacheSim::Access(int core_index, uintptr_t addr, size_t size, AccessMode mode)
{
  AccessResult r = AccessResult::kD1Hit;

  // Handle straddling cache lines by looping.
  uint64_t line_base = addr & ~63ull;
  uint64_t line_end = (addr + size) & ~63ull;

  int module_index = (core_index / 4) & 1;
  while (line_base <= line_end)
  {
    AccessResult r2 = m_Modules[module_index].Access(core_index & 3, line_base, mode);
    if (r2 > r)
      r = r2;
    line_base += 64;
  }

  return r;
}


/// Apple A9 Access

CacheSim::AccessResult CacheSim::AppleA9Module::Access(int core_index, uintptr_t addr, AccessMode mode) {
	if (kWrite == mode) {
		// Kick the line out of every other L1 and the other L2 package.
		for (int i = 0; i < AppleA9Module::kCoreCount; ++i) {
			if (i == core_index)
				continue;

			m_CoreD1[i].Invalidate(addr);
			m_CoreI1[i].Invalidate(addr);
		}
	}

	//Attempt to read from l1
	if (kCodeRead == mode) {
		if(m_CoreI1[core_index].Access(addr)) {
			return kI1Hit;
		}
	}
	else {
		if(m_CoreD1[core_index].Access(addr)) {
			return kD1Hit;
		}
	}

	//We have missed on l1, try to fetch from l2
	if(m_Level2.Access(addr)) {
		return kL2Hit;
	}

	//We missed on l2, but we have a victim cache that might still have what we need
	//@TODO
	

	if (kCodeRead == mode)
		return kL2IMiss;
	else
		return kL2DMiss;
}

/// Apple A11 Access
CacheSim::AccessResult CacheSim::AppleA11Module::Access(int core_index, uintptr_t addr, AccessMode mode) {
	if (kWrite == mode) {
		// Kick the line out of every other L1
		for (int i = 0; i < AppleA11Module::kCoreCount; ++i) {
			if (i == core_index)
				continue;

			m_CoreD1[i].Invalidate(addr);
			m_CoreI1[i].Invalidate(addr);
		}
	}

	//Attempt to read from l1
	if (kCodeRead == mode) {
		if (m_CoreI1[core_index].Access(addr)) {
			return kI1Hit;
		}
	}
	else {
		if (m_CoreD1[core_index].Access(addr)) {
			return kD1Hit;
		}
	}

	//We have missed on l1, try to fetch from l2
	if (m_Level2[core_index].Access(addr)) {
		return kL2Hit;
	}

	if (kCodeRead == mode)
		return kL2IMiss;
	else
		return kL2DMiss;
}

CacheSim::AccessResult CacheSim::Snapdragon845Module::Access(int core_index, uintptr_t addr, AccessMode mode) {
	bool goodChip = core_index < 4;
	int chip_core_index = goodChip ? core_index : (core_index - 4);

	if (kWrite == mode) {
		// Kick the line out of every other L1
		for (int i = 0; i < Snapdragon845Module::kCoreCount / 2; ++i) {
			if (i == core_index)
				continue;

			if(goodChip) {
				m_A75_CoreD1[i].Invalidate(addr);
				m_A75_CoreI1[i].Invalidate(addr);
			}				   
			else {
				m_A55_CoreD1[i].Invalidate(addr);
				m_A55_CoreI1[i].Invalidate(addr);
			}
		}
	}

	//Attempt to read from l1
	if (kCodeRead == mode) {
		if ((goodChip ? m_A75_CoreI1[chip_core_index].Access(addr) : m_A55_CoreI1[chip_core_index].Access(addr))) {
			return kI1Hit;
		}
	}
	else {
		if ((goodChip ? m_A75_CoreD1[chip_core_index].Access(addr) : m_A55_CoreD1[chip_core_index].Access(addr))) {
			return kD1Hit;
		}
	}

	//We have missed on l1, try to fetch from l2
	if ((goodChip ? m_A75_Level2[chip_core_index].Access(addr) : m_A55_Level2[chip_core_index].Access(addr))) {
		return kL2Hit;
	}

	if (kCodeRead == mode)
		return kL2IMiss;
	else
		return kL2DMiss;
}
