#include "virtualcam2-filter.hpp"
#include "virtualcam2-guid.h"

/* ========================================================================= */

static const REGPINTYPES AMSMediaTypesV2 = {&MEDIATYPE_Video,
					    &MEDIASUBTYPE_NV12};

static const REGFILTERPINS AMSPinVideo2 = {nullptr,    false, true,
					   false,       false, &CLSID_NULL,
					   nullptr,     1,     &AMSMediaTypesV2};

HINSTANCE dll_inst = nullptr;
volatile long locks = 0;

/* ========================================================================= */

class VCamFactory2 : public IClassFactory {
	volatile long refs = 1;
	CLSID cls;

public:
	inline VCamFactory2(CLSID cls_) : cls(cls_) {}

	STDMETHODIMP QueryInterface(REFIID riid, void **p_ptr);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	STDMETHODIMP CreateInstance(LPUNKNOWN parent, REFIID riid,
				    void **p_ptr);
	STDMETHODIMP LockServer(BOOL lock);
};

STDMETHODIMP VCamFactory2::QueryInterface(REFIID riid, void **p_ptr)
{
	if (!p_ptr) {
		return E_POINTER;
	}

	if ((riid == IID_IUnknown) || (riid == IID_IClassFactory)) {
		AddRef();
		*p_ptr = (void *)this;
		return S_OK;
	} else {
		*p_ptr = nullptr;
		return E_NOINTERFACE;
	}
}

STDMETHODIMP_(ULONG) VCamFactory2::AddRef()
{
	return os_atomic_inc_long(&refs);
}

STDMETHODIMP_(ULONG) VCamFactory2::Release()
{
	long new_refs = os_atomic_dec_long(&refs);
	if (new_refs == 0) {
		delete this;
		return 0;
	}

	return (ULONG)new_refs;
}

STDMETHODIMP VCamFactory2::CreateInstance(LPUNKNOWN parent, REFIID,
					  void **p_ptr)
{
	if (!p_ptr) {
		return E_POINTER;
	}

	*p_ptr = nullptr;

	if (parent) {
		return E_NOINTERFACE;
	}

	if (IsEqualCLSID(cls, CLSID_OBS_VirtualVideo2)) {
		*p_ptr = (void *)new VCamFilter2();
		return S_OK;
	}

	return E_NOINTERFACE;
}

STDMETHODIMP VCamFactory2::LockServer(BOOL lock)
{
	if (lock) {
		os_atomic_inc_long(&locks);
	} else {
		os_atomic_dec_long(&locks);
	}

	return S_OK;
}

/* ========================================================================= */

static inline DWORD string_size2(const wchar_t *str)
{
	return (DWORD)(wcslen(str) + 1) * sizeof(wchar_t);
}

static bool RegServer2(const CLSID &cls, const wchar_t *desc,
		       const wchar_t *file, const wchar_t *model = L"Both",
		       const wchar_t *type = L"InprocServer32")
{
	wchar_t cls_str[CHARS_IN_GUID];
	wchar_t temp[MAX_PATH];
	HKEY key = nullptr;
	HKEY subkey = nullptr;
	bool success = false;

	StringFromGUID2(cls, cls_str, CHARS_IN_GUID);
	StringCbPrintf(temp, sizeof(temp), L"CLSID\\%s", cls_str);

	if (RegCreateKey(HKEY_CLASSES_ROOT, temp, &key) != ERROR_SUCCESS) {
		goto fail;
	}

	RegSetValueW(key, nullptr, REG_SZ, desc, string_size2(desc));

	if (RegCreateKey(key, type, &subkey) != ERROR_SUCCESS) {
		goto fail;
	}

	RegSetValueW(subkey, nullptr, REG_SZ, file, string_size2(file));
	RegSetValueExW(subkey, L"ThreadingModel", 0, REG_SZ,
		       (const BYTE *)model, string_size2(model));

	success = true;

fail:
	if (key) {
		RegCloseKey(key);
	}
	if (subkey) {
		RegCloseKey(subkey);
	}

	return success;
}

static bool UnregServer2(const CLSID &cls)
{
	wchar_t cls_str[CHARS_IN_GUID];
	wchar_t temp[MAX_PATH];

	StringFromGUID2(cls, cls_str, CHARS_IN_GUID);
	StringCbPrintf(temp, sizeof(temp), L"CLSID\\%s", cls_str);

	return RegDeleteTreeW(HKEY_CLASSES_ROOT, temp) == ERROR_SUCCESS;
}

static bool RegServers2(bool reg)
{
	wchar_t file[MAX_PATH];

	if (!GetModuleFileNameW(dll_inst, file, MAX_PATH)) {
		return false;
	}

	if (reg) {
		return RegServer2(CLSID_OBS_VirtualVideo2,
				  L"OBS Virtual Camera 2", file);
	} else {
		return UnregServer2(CLSID_OBS_VirtualVideo2);
	}
}

static bool RegFilters2(bool reg)
{
	ComPtr<IFilterMapper2> fm;
	HRESULT hr;

	hr = CoCreateInstance(CLSID_FilterMapper2, nullptr,
			      CLSCTX_INPROC_SERVER, IID_IFilterMapper2,
			      (void **)&fm);
	if (FAILED(hr)) {
		return false;
	}

	if (reg) {
		ComPtr<IMoniker> moniker;
		REGFILTER2 rf2;
		rf2.dwVersion = 1;
		rf2.dwMerit = MERIT_DO_NOT_USE;
		rf2.cPins = 1;
		rf2.rgPins = &AMSPinVideo2;

		hr = fm->RegisterFilter(CLSID_OBS_VirtualVideo2,
					L"OBS Virtual Camera 2", &moniker,
					&CLSID_VideoInputDeviceCategory, nullptr,
					&rf2);
		if (FAILED(hr)) {
			return false;
		}
	} else {
		hr = fm->UnregisterFilter(&CLSID_VideoInputDeviceCategory, 0,
					  CLSID_OBS_VirtualVideo2);
		if (FAILED(hr)) {
			return false;
		}
	}

	return true;
}

/* ========================================================================= */

STDAPI DllRegisterServer()
{
	if (!RegServers2(true)) {
		RegServers2(false);
		return E_FAIL;
	}

	CoInitialize(0);

	if (!RegFilters2(true)) {
		RegFilters2(false);
		RegServers2(false);
		CoUninitialize();
		return E_FAIL;
	}

	CoUninitialize();
	return S_OK;
}

STDAPI DllUnregisterServer()
{
	CoInitialize(0);
	RegFilters2(false);
	RegServers2(false);
	CoUninitialize();
	return S_OK;
}

STDAPI DllInstall(BOOL install, LPCWSTR)
{
	if (!install) {
		return DllUnregisterServer();
	} else {
		return DllRegisterServer();
	}
}

STDAPI DllCanUnloadNow()
{
	return os_atomic_load_long(&locks) ? S_FALSE : S_OK;
}

STDAPI DllGetClassObject(REFCLSID cls, REFIID riid, void **p_ptr)
{
	if (!p_ptr) {
		return E_POINTER;
	}

	*p_ptr = nullptr;

	if (riid != IID_IClassFactory && riid != IID_IUnknown) {
		return E_NOINTERFACE;
	}
	if (!IsEqualCLSID(cls, CLSID_OBS_VirtualVideo2)) {
		return E_INVALIDARG;
	}

	*p_ptr = (void *)new VCamFactory2(cls);
	return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		dll_inst = inst;
	}

	return true;
}
