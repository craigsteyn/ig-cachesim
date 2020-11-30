#pragma once

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

#include "CacheSim.h"

#include <atomic>
#include <utility> // for std::swap

namespace CacheSim
{
  enum AccessResult
  {
    kD1Hit    = 0,              ///< Access hit the D1
    kI1Hit,                     ///< Access hit the I1
    kL2Hit,                     ///< Access hit the L2
    kL2IMiss,                   ///< Access missed the L2 due to an instruction fetch
    kL2DMiss,                   ///< Access missed the L2 due to a data access
    kPrefetchHitD1,
    kPrefetchHitL2,
    kInstructionsExecuted,
    kAccessResultCount
  };

  enum AccessMode
  {
    kRead,
    kCodeRead,
    kWrite
  };

  template <size_t kWays>
  struct SetData
  {
    uint64_t  m_Addr[kWays];    ///< Virtual address cached, or zero (invalid).
  };

  template <size_t kCacheSizeBytes, size_t kWays>
  class Cache
  {
  public:

    static constexpr size_t  kLineSize     = 64;   ///< We're on x86.
    static constexpr size_t  kSetSizeShift = 6;    ///< Log2(kLineSize)
    static constexpr size_t  kSetCount     = kCacheSizeBytes / kLineSize / kWays;
    //static constexpr size_t  kSetMask      = kSetCount - 1;

    static_assert((kWays & (kWays - 1)) == 0,                         "Way count must be power of 2");
   // static_assert((kSetCount & (~size_t(kSetMask))) == kSetCount,     "Set count must be power of 2");
    static_assert(kSetCount * kLineSize * kWays == kCacheSizeBytes,   "Size must divide perfectly");

    void Init()
    {
      memset(m_Sets, 0, sizeof m_Sets);
    }

    SetData<kWays> m_Sets[kSetCount];

    bool Access(uint64_t addr)
    {
      uint64_t base = addr >> kSetSizeShift;

      const uint32_t line_index = base % kSetCount;

      SetData<kWays>* set = &m_Sets[line_index];

      for (size_t way = 0; way < kWays; ++way)
      {
        const uint64_t stored_addr = set->m_Addr[way];

        if (stored_addr == base)
        {
          // Shift the hit way to the front of the set to reflect MRU status.
          // This isn't optimal, but it doesn't really matter much in the grand scheme of things.
          while (way > 0)
          {
            std::swap(set->m_Addr[way], set->m_Addr[way-1]);
            --way;
          }
          return true;
        }
      }

      // Miss: Move everything in the way to the right and insert this thing as the MRU.
      for (size_t i = kWays - 1; i > 0; --i)
      {
        set->m_Addr[i] = set->m_Addr[i - 1];

      }
      set->m_Addr[0] = base;
      return false;
    }

    void Invalidate(uint64_t addr)
    {
      uint64_t base = addr >> kSetSizeShift;

      const uint32_t line_index = base % kSetCount;

      SetData<kWays>* set = &m_Sets[line_index];

      for (size_t way = 0; way < kWays; ++way)
      {
        if (set->m_Addr[way] == base)
        {
          // Take the invalidated way out of the array by moving in elements from the right (that then survive longer)
          for (size_t rw = way; rw < kWays - 1; ++rw)
          {
            set->m_Addr[rw] = set->m_Addr[rw + 1];
          }

          // Mark the last way as 0 so we don't hit it later.
          set->m_Addr[kWays - 1] = 0;
          break;
        }
      }
    }
  };

  /// Simulate the Jaguar 32 KB L1 cache
  /// 512 lines or 64 bytes each, 8 ways per line
  using JaguarD1 = Cache<32 * 1024, 8>;
  /// I1 is 2-way set assoc, 32 KB
  using JaguarI1 = Cache<32 * 1024, 2>;
  /// Jaguar L2 is 2 MB, 16 way set assoc.
  using JaguarL2 = Cache<2 * 1024 * 1024, 16>;

  class JaguarModule
  {
  public:
    enum { kCoreCount = 4 };

  private:
    JaguarD1      m_CoreD1[kCoreCount];
    JaguarI1      m_CoreI1[kCoreCount];
    JaguarL2      m_Level2;
    JaguarModule* m_OtherModule;

  public:
    void Init(JaguarModule* other_module)
    {
      for (int i = 0; i < kCoreCount; ++i)
      {
        m_CoreD1[i].Init();
        m_CoreI1[i].Init();
      } 
      m_Level2.Init();
      m_OtherModule = other_module;
    }

    IG_CACHESIM_API AccessResult Access(int core_index, uintptr_t addr, AccessMode mode);
  };

  class JaguarCacheSim
  {
  private:
    JaguarModule m_Modules[2];

  public:
    void Init()
    {
      m_Modules[0].Init(&m_Modules[1]);
      m_Modules[1].Init(&m_Modules[0]);
    }

    IG_CACHESIM_API AccessResult Access(int core_index, uintptr_t addr, size_t size, AccessMode mode);

    std::atomic<int> core = { 0 };
	  int GetNextCore() {
		  int nextCore = core % (JaguarModule::kCoreCount);
		  ++core;
		  return nextCore;
	  }
  };



  ///Apple A9 Chip
  /// L1 Data - 64kb, 64b / line, 4-way
  /// L1 Instruction - 64k, 64b / line, 2-way
  /// L2 - 3 MB, shared between all cores
  /// L3 - 4 MB, victim cache shared between cpu + gpu
  using AppleA9D1 = Cache<64 * 1024, 4>;
  using AppleA9I1 = Cache<64 * 1024, 2>;
  using AppleA9L2 = Cache<3 * 1024 * 1024, 16>;
  using AppleA9L3 = Cache<4 * 1024 * 1024, 1>;

  class AppleA9Module {
  public:
	  enum { kCoreCount = 2 };

  private:
	  AppleA9D1      m_CoreD1[kCoreCount];
	  AppleA9I1      m_CoreI1[kCoreCount];
	  AppleA9L2      m_Level2;
	  AppleA9L3      m_Level3Victim;

  public:
	  void Init() {
		  for (int i = 0; i < kCoreCount; ++i) {
			  m_CoreD1[i].Init();
			  m_CoreI1[i].Init();
		  }
		  m_Level2.Init();
		  m_Level3Victim.Init();
	  }

	  IG_CACHESIM_API AccessResult Access(int core_index, uintptr_t addr, AccessMode mode);
  };


  ///Apple A11 Chip
  /// L1 Data - 64kb, 64b / line, 4-way
  /// L1 Instruction - 64k, 64b / line, 2-way
  /// L2 - 8 MB per core
  using AppleA11D1 = Cache<64 * 1024, 4>;
  using AppleA11I1 = Cache<64 * 1024, 2>;
  using AppleA11L2 = Cache<8 * 1024 * 1024, 16>;

  class AppleA11Module {
  public:
	  enum { kCoreCount = 6 };

  private:
	  AppleA11D1      m_CoreD1[kCoreCount];
	  AppleA11I1      m_CoreI1[kCoreCount];
	  AppleA11L2      m_Level2[kCoreCount];

  public:
	  void Init() {
		  for (int i = 0; i < kCoreCount; ++i) {
			  m_CoreD1[i].Init();
			  m_CoreI1[i].Init();
			  m_Level2[i].Init();
		  }
	  }

	  IG_CACHESIM_API AccessResult Access(int core_index, uintptr_t addr, AccessMode mode);
  };


  template<typename Module>
  class AppleChipSim {
  private:
	  Module m_Module;

  public:
	  void Init() {
		  m_Module.Init();
	  }

	  std::atomic<int> core = { 0 };
	  int GetNextCore() {
		  int nextCore = core % (Module::kCoreCount);
		  ++core;
		  return nextCore;
	  }

	  IG_CACHESIM_API AccessResult Access(int core_index, uintptr_t addr, size_t size, AccessMode mode) {
		  AccessResult r = AccessResult::kD1Hit;

		  // Handle straddling cache lines by looping.
		  uint64_t line_base = addr & ~63ull;
		  uint64_t line_end = (addr + size) & ~63ull;

		  while (line_base <= line_end) {
			  AccessResult r2 = m_Module.Access(core_index % (Module::kCoreCount), line_base, mode);
			  if (r2 > r)
				  r = r2;
			  line_base += 64;
		  }

		  return r;
	  }
  };


  ///Snapdragon 845 Chip
  using Snapdragon845_A75_D1 = Cache<64 * 1024, 4>;
  using Snapdragon845_A75_I1 = Cache<64 * 1024, 4>;
  using Snapdragon845_A75_L2 = Cache<256 * 1024, 8>;

  using Snapdragon845_A55_D1 = Cache<64 * 1024, 2>;
  using Snapdragon845_A55_I1 = Cache<64 * 1024, 4>;
  using Snapdragon845_A55_L2 = Cache<128 * 1024, 8>;

  class Snapdragon845Module {
  public:
	  enum { kCoreCount = 8 };

  private:
	  Snapdragon845_A75_D1      m_A75_CoreD1[kCoreCount / 2];
	  Snapdragon845_A75_I1      m_A75_CoreI1[kCoreCount / 2];
	  Snapdragon845_A75_L2      m_A75_Level2[kCoreCount / 2];

	  Snapdragon845_A55_D1      m_A55_CoreD1[kCoreCount / 2];
	  Snapdragon845_A55_I1      m_A55_CoreI1[kCoreCount / 2];
	  Snapdragon845_A55_L2      m_A55_Level2[kCoreCount / 2];

  public:
	  void Init() {
		  for (int i = 0; i < kCoreCount / 2; ++i) {
			  m_A75_CoreD1[i].Init();
			  m_A75_CoreI1[i].Init();
			  m_A75_Level2[i].Init();

			  m_A55_CoreD1[i].Init();
			  m_A55_CoreI1[i].Init();
			  m_A55_Level2[i].Init();
		  }
	  }

	  IG_CACHESIM_API AccessResult Access(int core_index, uintptr_t addr, AccessMode mode);
  };


  template<typename Module>
  class Snapdragon845ChipSim {
  private:
	  Module m_Module;

  public:
	  void Init() {
		  m_Module.Init();
	  }

	  std::atomic<int> core = { 1 };
	  int GetNextCore() {
		  int nextCore = core % (Module::kCoreCount);
		  if (nextCore == 0) {
			  nextCore = 1;
			  ++core;
		  }
		  ++core;

		  return nextCore;
	  }

	  IG_CACHESIM_API AccessResult Access(int core_index, uintptr_t addr, size_t size, AccessMode mode) {
		  AccessResult r = AccessResult::kD1Hit;

		  // Handle straddling cache lines by looping.
		  uint64_t line_base = addr & ~63ull;
		  uint64_t line_end = (addr + size) & ~63ull;

		  while (line_base <= line_end) {
			  AccessResult r2 = m_Module.Access(core_index % (Module::kCoreCount), line_base, mode);
			  if (r2 > r)
				  r = r2;
			  line_base += 64;
		  }

		  return r;
	  }
  };
}
