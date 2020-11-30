#pragma once

#include "Precompiled.h"
#include "CacheSim/CacheSimData.h"

#include <functional>

namespace CacheSim
{
  struct UnresolvedAddressData
  {
    const SerializedModuleEntry*  m_Modules = nullptr;
    const QString*                m_ModuleNames = nullptr;
    uint32_t                      m_ModuleCount = 0;
    const uintptr_t*              m_Stacks = nullptr;
    uint32_t                      m_StackCount = 0;
    const SerializedNode*         m_Nodes = nullptr;
    uint32_t                      m_NodeCount = 0;
  };

  struct ResolvedSymbol
  {
    uintptr_t   m_Rip;
    uint32_t    m_ModuleIndex;

	struct SymbolInfo {
		QString     m_Name;
		QString     m_FileName;
		uint32_t    m_LineNumber;
		uint32_t    m_Displacement;
	};

	SymbolInfo m_Symbol;
	SymbolInfo m_InlinedSymbol;
  };

  using SymbolResolveProgressCallbackType = std::function<void(int, int)>;

  bool ResolveSymbols(const UnresolvedAddressData& input, QVector<ResolvedSymbol>* resolvedSymbolsOut, SymbolResolveProgressCallbackType reportProgress);
}