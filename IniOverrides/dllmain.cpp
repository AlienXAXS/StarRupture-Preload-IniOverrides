// WIN32_LEAN_AND_MEAN comes from the project, alongside NOMINMAX.
#include <Windows.h>

// DllMain for a preload plugin runs during Stage 1, before the game's entry
// point -- so it runs under the loader lock with the main thread parked. Keep
// it empty. Anything that allocates, loads another module, or waits on
// anything belongs in PreloadInit, which the loader calls from a normal thread
// with no lock held.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(module);
		break;
	case DLL_PROCESS_DETACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
