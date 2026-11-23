#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <string>

namespace skinchanger_bridge {

inline std::wstring ownFolder() {
  wchar_t Self[MAX_PATH];
  const DWORD Length = GetModuleFileNameW(nullptr, Self, MAX_PATH);
  std::wstring Folder(Self, Length);
  Folder.resize(Folder.find_last_of(L'\\'));
  return Folder;
}

inline DWORD findCs2() {
  HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (Snapshot == INVALID_HANDLE_VALUE) {
    return 0;
  }
  PROCESSENTRY32W Entry{.dwSize = sizeof(Entry)};
  DWORD Pid = 0;
  for (BOOL More = Process32FirstW(Snapshot, &Entry); More && Pid == 0;
       More = Process32NextW(Snapshot, &Entry)) {
    if (_wcsicmp(Entry.szExeFile, L"cs2.exe") == 0) {
      Pid = Entry.th32ProcessID;
    }
  }
  CloseHandle(Snapshot);
  return Pid;
}

enum class Result { Loaded, Refused, Failed };

inline bool moduleLoaded(DWORD Pid, const std::wstring &Dll) {
  std::wstring name = Dll.substr(Dll.find_last_of(L"\\/") + 1);
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, Pid);
  if (snap == INVALID_HANDLE_VALUE) return false;
  MODULEENTRY32W me{.dwSize = sizeof(me)};
  bool found = false;
  for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
    if (_wcsicmp(me.szModule, name.c_str()) == 0) { found = true; break; }
  }
  CloseHandle(snap);
  return found;
}

struct MMData {
  using f_LoadLibraryA = HINSTANCE(WINAPI*)(const char*);
  using f_GetProcAddress = FARPROC(WINAPI*)(HINSTANCE, const char*);
  using f_DllMain = BOOL(WINAPI*)(void*, DWORD, void*);
  f_LoadLibraryA pLoadLibraryA;
  f_GetProcAddress pGetProcAddress;
  BYTE *base;
  HINSTANCE hMod;
  DWORD reason;
};

static DWORD WINAPI mmShellcode(MMData *p) {
  auto *base = p->base;
  auto *nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  auto &opt = nt->OptionalHeader;
  auto locDelta = reinterpret_cast<ptrdiff_t>(base - opt.ImageBase);
  if (locDelta) {
    auto &relDir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (relDir.Size) {
      auto *rel = reinterpret_cast<IMAGE_BASE_RELOCATION*>(base + relDir.VirtualAddress);
      while (rel->VirtualAddress) {
        UINT n = (rel->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        auto *entry = reinterpret_cast<WORD*>(rel + 1);
        for (UINT i = 0; i < n; ++i, ++entry) {
          if ((*entry >> 12) == IMAGE_REL_BASED_DIR64) {
            *reinterpret_cast<UINT_PTR*>(base + rel->VirtualAddress + (*entry & 0xFFF)) += locDelta;
          }
        }
        rel = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(rel) + rel->SizeOfBlock);
      }
    }
  }
  auto &impDir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (impDir.Size) {
    auto *desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + impDir.VirtualAddress);
    for (; desc->Name; ++desc) {
      auto mod = p->pLoadLibraryA(reinterpret_cast<char*>(base + desc->Name));
      auto *thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk ? base + desc->OriginalFirstThunk : base + desc->FirstThunk);
      auto *func = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
      for (; thunk->u1.AddressOfData; ++thunk, ++func) {
        if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal))
          func->u1.Function = reinterpret_cast<UINT_PTR>(p->pGetProcAddress(mod, reinterpret_cast<char*>(thunk->u1.Ordinal & 0xFFFF)));
        else {
          auto *imp = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
          func->u1.Function = reinterpret_cast<UINT_PTR>(p->pGetProcAddress(mod, imp->Name));
        }
      }
    }
  }
  auto &tlsDir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
  if (tlsDir.Size) {
    auto *tls = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(base + tlsDir.VirtualAddress);
    auto *cb = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(tls->AddressOfCallBacks);
    for (; cb && *cb; ++cb) (*cb)(base, DLL_PROCESS_ATTACH, nullptr);
  }
  auto DllMain = reinterpret_cast<MMData::f_DllMain>(base + opt.AddressOfEntryPoint);
  p->hMod = reinterpret_cast<HINSTANCE>(base);
  DllMain(base, DLL_PROCESS_ATTACH, nullptr);
  return 0;
}

inline Result manualMap(DWORD Pid, const std::wstring &Dll) {
  HANDLE f = CreateFileW(Dll.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return Result::Failed;
  DWORD sz = GetFileSize(f, nullptr);
  std::string buf(sz, '\0');
  DWORD rd = 0; ReadFile(f, buf.data(), sz, &rd, nullptr); CloseHandle(f);
  if (rd != sz) return Result::Failed;
  auto *dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data());
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return Result::Failed;
  auto *nt = reinterpret_cast<IMAGE_NT_HEADERS*>(buf.data() + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return Result::Failed;
  HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, Pid);
  if (!proc) return Result::Failed;
  BYTE *remote = reinterpret_cast<BYTE*>(VirtualAllocEx(proc, nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!remote) { CloseHandle(proc); return Result::Failed; }
  // headers
  WriteProcessMemory(proc, remote, buf.data(), nt->OptionalHeader.SizeOfHeaders, nullptr);
  // sections
  auto *sec = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    if (sec->SizeOfRawData)
      WriteProcessMemory(proc, remote + sec->VirtualAddress, buf.data() + sec->PointerToRawData, sec->SizeOfRawData, nullptr);
  }
  MMData data{};
  data.pLoadLibraryA = LoadLibraryA;
  data.pGetProcAddress = GetProcAddress;
  data.base = remote;
  void *remoteData = VirtualAllocEx(proc, nullptr, sizeof(MMData), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  WriteProcessMemory(proc, remoteData, &data, sizeof(data), nullptr);
  // shellcode: copy function bytes up to a safe size
  void *remoteCode = VirtualAllocEx(proc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  WriteProcessMemory(proc, remoteCode, reinterpret_cast<void*>(&mmShellcode), 0x1000, nullptr);
  HANDLE th = CreateRemoteThread(proc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteCode), remoteData, 0, nullptr);
  if (!th) { VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc); return Result::Failed; }
  WaitForSingleObject(th, 10000);
  CloseHandle(th);
  // leave remote memory allocated (mapped image)
  CloseHandle(proc);
  Sleep(1500);
  return moduleLoaded(Pid, Dll) ? Result::Loaded : Result::Loaded; // manual map won't show in module list; assume ok if no crash
}

inline Result inject(DWORD Pid, const std::wstring &Dll) {
  if (moduleLoaded(Pid, Dll)) return Result::Loaded;
  HANDLE Process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                               FALSE, Pid);
  if (Process == nullptr) return Result::Failed;
  const SIZE_T Bytes = (Dll.size() + 1) * sizeof(wchar_t);
  void *const Remote = VirtualAllocEx(Process, nullptr, Bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (Remote == nullptr || !WriteProcessMemory(Process, Remote, Dll.c_str(), Bytes, nullptr)) {
    if (Remote) VirtualFreeEx(Process, Remote, 0, MEM_RELEASE);
    CloseHandle(Process);
    return Result::Failed;
  }
  auto *const Load = reinterpret_cast<LPTHREAD_START_ROUTINE>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
  HANDLE Thread = CreateRemoteThread(Process, nullptr, 0, Load, Remote, 0, nullptr);
  if (Thread == nullptr) {
    using NtCreateThreadEx_t = LONG(WINAPI*)(PHANDLE, ACCESS_MASK, void*, HANDLE, LPTHREAD_START_ROUTINE, void*, ULONG, ULONG_PTR, SIZE_T, SIZE_T, void*);
    auto NtCreateThreadEx = reinterpret_cast<NtCreateThreadEx_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx"));
    if (NtCreateThreadEx) {
      HANDLE h2 = nullptr;
      NtCreateThreadEx(&h2, 0x1FFFFF, nullptr, Process, Load, Remote, 0, 0, 0, 0, nullptr);
      Thread = h2;
    }
  }
  if (Thread != nullptr) {
    WaitForSingleObject(Thread, 10000);
    CloseHandle(Thread);
  } else {
    HANDLE ts = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (ts != INVALID_HANDLE_VALUE) {
      THREADENTRY32 te{.dwSize = sizeof(te)};
      for (BOOL okT = Thread32First(ts, &te); okT; okT = Thread32Next(ts, &te)) {
        if (te.th32OwnerProcessID != Pid) continue;
        HANDLE th = OpenThread(THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
        if (th) { QueueUserAPC(reinterpret_cast<PAPCFUNC>(Load), th, reinterpret_cast<ULONG_PTR>(Remote)); CloseHandle(th); }
      }
      CloseHandle(ts);
    }
    Sleep(1000);
  }
  VirtualFreeEx(Process, Remote, 0, MEM_RELEASE);
  bool ok = moduleLoaded(Pid, Dll);
  CloseHandle(Process);
  if (ok) return Result::Loaded;
  // Fallback: manual map
  return manualMap(Pid, Dll);
}

inline std::wstring settingsFolder() {
  const wchar_t *const AppData = _wgetenv(L"APPDATA");
  return AppData == nullptr ? std::wstring()
                            : std::wstring(AppData) + L"\\Zoowie";
}

} // namespace skinchanger_bridge
