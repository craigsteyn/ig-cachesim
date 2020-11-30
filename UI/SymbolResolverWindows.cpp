#define _CRT_SECURE_NO_WARNINGS

#include "Precompiled.h"
#include "SymbolResolver.h"

#include <imagehlp.h>
#include <comutil.h>


bool CacheSim::ResolveSymbols(const UnresolvedAddressData& input, QVector<ResolvedSymbol>* resolvedSymbolsOut, SymbolResolveProgressCallbackType reportProgress)
{
  const HANDLE hproc = (HANDLE)0x1;    // Really doesn't matter. But can't be null.

  {
    DWORD sym_options = SymGetOptions();
    //sym_options |= SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS;
    sym_options |= SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_DEBUG | SYMOPT_DISABLE_SYMSRV_AUTODETECT | SYMOPT_DEFERRED_LOADS;
    sym_options &= ~(SYMOPT_UNDNAME);

    SymSetOptions(sym_options);
  }

  if (!SymInitialize(hproc, nullptr, FALSE))
  {
    qDebug() << "Failed to initialize DbgHelp library; Last Error:" << GetLastError();
    return false;
  }

  {
    // Ugh.
    QString symDirPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("\\CacheSimSymbols");
    QDir symDir(symDirPath);
    if (!symDir.exists())
    {
      symDir.mkpath(QStringLiteral("."));
    }

    QFileInfo firstModule(QString::fromUtf8(input.m_ModuleNames[0].toUtf8()));

    QString symbolPath = QStringLiteral("%1;srv*%2*https://msdl.microsoft.com/download/symbols")
      .arg(firstModule.absolutePath().replace(QLatin1Char('/'), QLatin1Char('\\')))
      .arg(symDir.absolutePath().replace(QLatin1Char('/'), QLatin1Char('\\')));
    if (!SymSetSearchPath(hproc, symbolPath.toUtf8().constData()))
    {
      qDebug() << "Failed to set symbol path; err:" << GetLastError();
      return false;
    }
  }

  //SymRegisterCallback(hproc, DbgHelpCallback, 0);

  for (uint32_t modIndex = 0; modIndex < input.m_ModuleCount; ++modIndex)
  {
    const SerializedModuleEntry& module = input.m_Modules[modIndex];

    if (0 == SymLoadModule64(hproc, nullptr, input.m_ModuleNames[modIndex].toUtf8(), nullptr, module.m_ImageBase, module.m_SizeBytes))
    {
      //Warn("Failed to load module \"%s\" (base: %016llx, size: %lld); error=%u\n", mod.m_ModuleName, mod.m_ImageBase, mod.m_SizeBytes, GetLastError());
    }
  }

  SYMBOL_INFO* sym = static_cast<SYMBOL_INFO*>(malloc(sizeof(SYMBOL_INFO) + 1024 * sizeof sym->Name[0]));

  QSet<uintptr_t> ripLookup;
  int resolve_count = 0;
  int fail_count = 0;

  auto resolve_symbol = [&](uintptr_t rip) -> void
  {
    if (ripLookup.contains(rip))
      return;

    ++resolve_count;

    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 1024;

    DWORD64 disp64 = 0;
    DWORD disp32 = 0;
    IMAGEHLP_LINE64 line_info = { sizeof line_info, 0 };

    ResolvedSymbol out_sym;
    out_sym.m_Rip = rip;

    if (SymFromAddr(hproc, rip, &disp64, sym))
    {
      out_sym.m_Symbol.m_Name = sym->Name;

      if (SymGetLineFromAddr64(hproc, rip, &disp32, &line_info))
      {
		  out_sym.m_Symbol.m_FileName = line_info.FileName;
		  out_sym.m_Symbol.m_LineNumber = line_info.LineNumber;
		  out_sym.m_Symbol.m_Displacement = disp32;
      }
    }

    if (out_sym.m_Symbol.m_Name.isEmpty())
    {
		out_sym.m_Symbol.m_Name = QStringLiteral("[%1]").arg(rip, 16, 16, QLatin1Char('0'));
      ++fail_count;
    }

    // Try to find the module..
    out_sym.m_ModuleIndex = ~0u;
    for (uint32_t i = 0; i < input.m_ModuleCount; ++i)
    {
      const SerializedModuleEntry& mod = input.m_Modules[i];
      if (rip >= mod.m_ImageBase && rip <= mod.m_ImageBase + mod.m_SizeBytes)
      {
        out_sym.m_ModuleIndex = i;
        break;
      }
    }

	if (out_sym.m_InlinedSymbol.m_Name.isEmpty()) {
		out_sym.m_InlinedSymbol = out_sym.m_Symbol;
	}

    ripLookup.insert(rip);
    resolvedSymbolsOut->push_back(out_sym);
  };

  int total = input.m_StackCount + input.m_NodeCount;
  int completed = 0;

  reportProgress(completed, total);

  for (uint32_t i = 0; i < input.m_StackCount; ++i, ++completed)
  {
    if (uintptr_t rip = input.m_Stacks[i])
    {
      resolve_symbol(rip);
    }

    if (0 == (completed & 0x400))
    {
      reportProgress(completed, total);
    }
  }

  // Resolve any instructions used in leaf functions.
  for (uint32_t i = 0; i < input.m_NodeCount; ++i, ++completed)
  {
    resolve_symbol(input.m_Nodes[i].m_Rip);

    if (0 == (completed & 0x400))
    {
      reportProgress(completed, total);
    }
  }

  reportProgress(completed, total);

  if (fail_count)
  {
    //Warn("%d out of %d symbols failed to resolve\n", fail_count, resolve_count);
  }


  free(sym);
  sym = nullptr;

  return true;
}

/*

#include <atlbase.h>
#include <assert.h>
#include <dia2.h>
class CDiaBSTR {
	BSTR m_bstr;
public:
	CDiaBSTR() { m_bstr = NULL; }
	~CDiaBSTR() { if (m_bstr != NULL) SysFreeString(m_bstr); }
	BSTR *operator &() { assert(m_bstr == NULL); return &m_bstr; }
	operator BSTR() { assert(m_bstr != NULL); return m_bstr; }
	QString toQString() {  return m_bstr == nullptr ? QString() : QString::fromUtf16((ushort*)m_bstr);
	}
};
	
void Fatal(const char *str) {
	qDebug() << str;
	__debugbreak();
}

bool CacheSim::ResolveSymbols(const UnresolvedAddressData& input, QVector<ResolvedSymbol>* resolvedSymbolsOut, SymbolResolveProgressCallbackType reportProgress) {
	struct ModuleSession {
		CComPtr<IDiaDataSource> source;
		CComPtr<IDiaSession> session;
	};

	std::vector<ModuleSession> moduleSessions;

	for (uint32_t modIndex = 0; modIndex < input.m_ModuleCount; ++modIndex) {
		ModuleSession moduleData;

		auto hr = CoCreateInstance(CLSID_DiaSource,
			NULL,
			CLSCTX_INPROC_SERVER,
			__uuidof(IDiaDataSource),
			(void **)&moduleData.source);

		if (FAILED(hr)) {
			Fatal("Could not CoCreate CLSID_DiaSource. Register msdia80.dll.");
			return false;
		}

		const SerializedModuleEntry& module = input.m_Modules[modIndex];

		bool failed = false;
		wchar_t wszFilename[_MAX_PATH];
		mbstowcs(wszFilename, input.m_ModuleNames[modIndex].toUtf8(), sizeof(wszFilename) / sizeof(wszFilename[0]));
		if (FAILED(moduleData.source->loadDataFromPdb(wszFilename))) {
			if (FAILED(moduleData.source->loadDataForExe(wszFilename, NULL, NULL))) {
				//Fatal(input.m_ModuleNames[modIndex].toUtf8());
				failed = true;
			}
		}
		
		if(!failed) {
			if (FAILED(moduleData.source->openSession(&moduleData.session))) {
				Fatal("openSession");
			}

			reportProgress(modIndex, input.m_ModuleCount);
			moduleData.session->put_loadAddress(module.m_ImageBase);
		}
		
		moduleSessions.emplace_back(std::move(moduleData));
	}

	QSet<uintptr_t> ripLookup;
	int resolve_count = 0;
	int fail_count = 0;

	auto get_symbol_data = [&](CComPtr<IDiaSession> &session, CComPtr<IDiaSymbol> pSym) ->ResolvedSymbol::SymbolInfo {
		ResolvedSymbol::SymbolInfo info;

		if (pSym != nullptr) {
			CDiaBSTR name;
			pSym->get_name(&name);
			info.m_Name = name.toQString();

			uintptr_t va;
			pSym->get_virtualAddress(&va);

			CComPtr<IDiaEnumLineNumbers> lineNumbers;
			if (!FAILED(session->findLinesByVA(va, 1, &lineNumbers))) {
				CComPtr<IDiaLineNumber> lineNumber;
				if (!FAILED(lineNumbers->Item(0, &lineNumber))) {
					DWORD line;
					CDiaBSTR fileName;
					lineNumber->get_lineNumber(&line);

					CComPtr<IDiaSourceFile> file;
					if (!FAILED(lineNumber->get_sourceFile(&file))) {
						file->get_fileName(&fileName);
					}

					info.m_FileName = fileName.toQString();
					info.m_LineNumber = line;
				}
			}
		}

		return info;
	};

	auto resolve_symbol = [&](uintptr_t rip) -> void {
		if (ripLookup.contains(rip))
			return;

		++resolve_count;

		ResolvedSymbol out_sym;
		out_sym.m_Rip = rip;
		
		// Try to find the module..
		out_sym.m_ModuleIndex = ~0u;
		for (uint32_t i = 0; i < input.m_ModuleCount; ++i) {
			const SerializedModuleEntry& mod = input.m_Modules[i];
			if (rip >= mod.m_ImageBase && rip <= mod.m_ImageBase + mod.m_SizeBytes) {
				out_sym.m_ModuleIndex = i;
				break;
			}
		}

		if (out_sym.m_ModuleIndex != ~0 && moduleSessions[out_sym.m_ModuleIndex].session != nullptr) {
			auto &session = moduleSessions[out_sym.m_ModuleIndex].session;

			CComPtr<IDiaSymbol> pSym;
			session->findSymbolByVA(rip, SymTagFunction, &pSym);
			out_sym.m_Symbol = get_symbol_data(session, pSym);

			//See if this symbol was actually inlined
			CComPtr<IDiaEnumSymbols> inlineFrames;
			CComPtr<IDiaSymbol> currentSymbol = pSym;
			CComPtr<IDiaSymbol> inlineSymbol;
			for (;;) {
				if (FAILED(session->findInlineFramesByVA(currentSymbol, rip, &inlineFrames))) {
					break;
				}

				if (FAILED(inlineFrames->Item(0, &currentSymbol))) {
					break;
				}

				inlineSymbol = currentSymbol;
			}

			if (inlineSymbol != nullptr) {
				out_sym.m_InlinedSymbol = get_symbol_data(session, inlineSymbol);
			}
		}

		if (out_sym.m_Symbol.m_Name.isEmpty()) {
			out_sym.m_Symbol.m_Name = QStringLiteral("[%1]").arg(rip, 16, 16, QLatin1Char('0'));
			++fail_count;
		}

		//If we didn't find an inline, just use the base symbol
		if (out_sym.m_InlinedSymbol.m_Name.isEmpty()) {
			out_sym.m_InlinedSymbol = out_sym.m_Symbol;
		}

		ripLookup.insert(rip);
		resolvedSymbolsOut->push_back(out_sym);
	};

	int total = input.m_StackCount + input.m_NodeCount;
	int completed = 0;

	reportProgress(completed, total);

	for (uint32_t i = 0; i < input.m_StackCount; ++i, ++completed) {
		if (uintptr_t rip = input.m_Stacks[i]) {
			resolve_symbol(rip);
		}

		if (0 == (completed & 0x400)) {
			reportProgress(completed, total);
		}
	}

	// Resolve any instructions used in leaf functions.
	for (uint32_t i = 0; i < input.m_NodeCount; ++i, ++completed) {
		resolve_symbol(input.m_Nodes[i].m_Rip);

		if (0 == (completed & 0x400)) {
			reportProgress(completed, total);
		}
	}

	reportProgress(completed, total);

	if (fail_count) {
		//Warn("%d out of %d symbols failed to resolve\n", fail_count, resolve_count);
	}

	return true;
}
*/

#if 0
static BOOL CALLBACK DbgHelpCallback(
  _In_     HANDLE  hProcess,
  _In_     ULONG   ActionCode,
  _In_opt_ ULONG64 CallbackData,
  _In_opt_ ULONG64 UserContext)
{
  UNREFERENCED_VARIABLE((hProcess, UserContext));

  if (CBA_DEBUG_INFO == ActionCode)
  {
    printf("dbghelp: %s", (const char*)CallbackData);
  }

  return FALSE;
}
#endif
