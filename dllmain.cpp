#include "pch.h"

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class DX12_Base
{
private:
	typedef HRESULT(APIENTRY* Present12) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
	Present12 oPresent = NULL;

	typedef void(APIENTRY* ExecuteCommandLists)(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
	ExecuteCommandLists oExecuteCommandLists = NULL;

	typedef HRESULT(APIENTRY* ResizeTarget)(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters);
	ResizeTarget oResizeTarget = NULL;

	struct _FrameContext 
	{
		ID3D12CommandAllocator*					CommandAllocator;
		ID3D12Resource*							Resource;
		D3D12_CPU_DESCRIPTOR_HANDLE				DescriptorHandle;
	};
	WNDCLASSEXA									WindowClass;
	HWND										WindowHwnd;
	HWND										m_GameWindow{};
	IDXGISwapChain*								m_Swapchain{};
	IDXGISwapChain3*							m_Swapchain3{};
	DXGI_SWAP_CHAIN_DESC						m_SwapDesc{};
	ID3D12Device*								m_Device{};
	ID3D12DescriptorHeap*						m_DescriptorHeap{};
	ID3D12DescriptorHeap*						m_rtvDescriptorHeap{};
	ID3D12CommandAllocator**					m_CommandAllocator;
	ID3D12GraphicsCommandList*					m_CommandList{};
	ID3D12CommandQueue*							m_CommandQueue{};
	ID3D12Resource**							m_BackBuffer;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>	m_RenderTargets;
	uint64_t									m_BuffersCounts = -1;
	_FrameContext*								m_FrameContext{}; 
	uint64_t*									MethodsTable = NULL;

public:
	bool										m_Init{};
	uint64_t									m_OldWndProc{};
	bool										m_ShowMenu	= TRUE;
	bool										bRunning	= FALSE;
	bool										bKillswitch = FALSE;

public:
	bool										InitializeWindow(std::string windowName);
	bool										FindGameWindow(std::string title, HWND& out);
	bool										ObtainDevice(IDXGISwapChain* pSwapChain);
	void										InitializeImGui(ID3D12Device* pDevice);
	void										Overlay();
	void										ResetRenderState();
	void										DestroyWindowContext();
	void										DestroyImGuiContext();

public:	//	Hook Helpers [Uses Minhook]
	bool										CreateHook(uint16_t Index, void** Original, void* Function);
	bool										DisableHook(uint16_t Index);
	void										EnableAllHooks();
	void										DisableAllHooks();

public:	//	Hooked Functions in Target Process
	static LRESULT								WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static HRESULT APIENTRY						HookPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
	static void									HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
	static HRESULT APIENTRY						HookResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters);

public:	//	CONSTRUCTOR
	explicit DX12_Base() = default;
	~DX12_Base() noexcept;
	DX12_Base(DX12_Base const&) = delete;
	DX12_Base(DX12_Base&&) = delete;
	DX12_Base& operator=(DX12_Base const&) = delete;
	DX12_Base& operator=(DX12_Base&&) = delete;
};
inline std::unique_ptr<DX12_Base> g_DX12;

//	CONSTRUCTOR | DESTRUCTOR
DX12_Base::~DX12_Base() { DisableAllHooks(); }

//	FUNCTION HOOKS
LRESULT DX12_Base::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui::GetCurrentContext())
		ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

	return CallWindowProc((WNDPROC)g_DX12->m_OldWndProc, hWnd, msg, wParam, lParam);
}

HRESULT APIENTRY DX12_Base::HookResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
	g_DX12->ResetRenderState();
	return g_DX12->oResizeTarget(_this, pNewTargetParameters);
}

HRESULT APIENTRY DX12_Base::HookPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	if (g_DX12->bKillswitch) 
	{
		g_DX12->bKillswitch = TRUE;		// To prevent subsequent iterations , though this should be impossible
		g_DX12->bRunning	= FALSE;	//	
		SetWindowLongPtr(g_DX12->m_GameWindow, GWLP_WNDPROC, (LONG_PTR)g_DX12->m_OldWndProc);	//	
		g_DX12->DisableAllHooks();
		return g_DX12->oPresent(pSwapChain, SyncInterval, Flags);
	}

	//	GetDevice
	if (!g_DX12->m_Init)
	{
		if (!g_DX12->ObtainDevice(pSwapChain))
			return g_DX12->oPresent(pSwapChain, SyncInterval, Flags);
	}

	if (g_DX12->m_CommandQueue == nullptr)
		return g_DX12->oPresent(pSwapChain, SyncInterval, Flags);

	pSwapChain->QueryInterface(IID_PPV_ARGS(&g_DX12->m_Swapchain3));
	if (g_DX12->m_Swapchain3 == nullptr)
		return g_DX12->oPresent(pSwapChain, SyncInterval, Flags);

	g_DX12->m_Swapchain3->GetDesc(&g_DX12->m_SwapDesc);


	//	Render Overlay
	if (g_DX12->m_ShowMenu)
		g_DX12->Overlay();

	//	Flush scene
	UINT bufferIndex				= g_DX12->m_Swapchain3->GetCurrentBackBufferIndex();
	D3D12_RESOURCE_BARRIER barrier	= {};
	barrier.Type					= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags					= D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource	= g_DX12->m_BackBuffer[bufferIndex];
	barrier.Transition.Subresource	= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore	= D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter	= D3D12_RESOURCE_STATE_RENDER_TARGET;
	g_DX12->m_CommandAllocator[bufferIndex]->Reset();
	g_DX12->m_CommandList->Reset(g_DX12->m_CommandAllocator[bufferIndex], NULL);
	g_DX12->m_CommandList->ResourceBarrier(1, &barrier);
	g_DX12->m_CommandList->OMSetRenderTargets(1, &g_DX12->m_RenderTargets[bufferIndex], FALSE, NULL);
	g_DX12->m_CommandList->SetDescriptorHeaps(1, &g_DX12->m_DescriptorHeap);

	if (g_DX12->m_ShowMenu)
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_DX12->m_CommandList);

	barrier.Transition.StateBefore	= D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter	= D3D12_RESOURCE_STATE_PRESENT;
	g_DX12->m_CommandList->ResourceBarrier(1, &barrier);
	g_DX12->m_CommandList->Close();
	g_DX12->m_CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&g_DX12->m_CommandList);
	g_DX12->m_Swapchain3->Release();

	//	jmp back to original execution flow
	return g_DX12->oPresent(pSwapChain, SyncInterval, Flags);
}

void DX12_Base::HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists) 
{
	if (!g_DX12->m_CommandQueue)
		g_DX12->m_CommandQueue = queue;
	g_DX12->oExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
}

bool DX12_Base::FindGameWindow(std::string title, HWND& out)
{
	DWORD	tempWndwPID	= NULL;
	HWND	tempHWND	= NULL;
	DWORD	procID		= GetCurrentProcessId();
	HANDLE	tempHandle	= OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, procID);
	do
	{
		tempHWND = FindWindowEx(NULL, tempHWND, NULL, NULL);
		GetWindowThreadProcessId(tempHWND, &tempWndwPID);
		if (tempWndwPID == procID)
		{

			char tempSTRING[MAX_PATH];
			GetWindowTextA(tempHWND, (LPSTR)tempSTRING, MAX_PATH);

			if (tempSTRING == title)
			{
				//	@TODO: This is a perfect time to retrieve the rest of the window information
				out = tempHWND;
				return TRUE;
			}
		}
	} while (tempHWND != NULL);
}

//	CUSTOM FUNCTIONS
bool DX12_Base::InitializeWindow(std::string windowName)
{
	if (!FindGameWindow("PS2 BIOS (USA)", this->m_GameWindow))
		return FALSE;

	if (this->m_GameWindow == nullptr)
		return FALSE;

	//	Create Window
	this->WindowClass.cbSize			= sizeof(WNDCLASSEXA);
	this->WindowClass.style				= CS_HREDRAW | CS_VREDRAW;
	this->WindowClass.lpfnWndProc		= DefWindowProc;
	this->WindowClass.cbClsExtra		= 0;
	this->WindowClass.cbWndExtra		= 0;
	this->WindowClass.hInstance			= GetModuleHandle(NULL);
	this->WindowClass.hIcon				= NULL;
	this->WindowClass.hCursor			= NULL;
	this->WindowClass.hbrBackground		= NULL;
	this->WindowClass.lpszMenuName		= NULL;
	this->WindowClass.lpszClassName		= "MJ";
	this->WindowClass.hIconSm			= NULL;
	RegisterClassExA(&this->WindowClass);
	this->WindowHwnd = CreateWindowA(this->WindowClass.lpszClassName, "HookWindow", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, this->WindowClass.hInstance, NULL);
	if (this->WindowHwnd == NULL) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	// Get DirectX Modules
	HMODULE D3D12Module = GetModuleHandleA("d3d12.dll");
	HMODULE DXGIModule	= GetModuleHandleA("dxgi.dll");
	if (D3D12Module == NULL || DXGIModule == NULL) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	//	Get DXGI Factory
	void* CreateDXGIFactory = GetProcAddress(DXGIModule, "CreateDXGIFactory");
	if (CreateDXGIFactory == NULL) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	//	Call something
	IDXGIFactory* Factory;
	if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(__uuidof(IDXGIFactory), (void**)&Factory) < 0) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	//	Get Adapter
	IDXGIAdapter* Adapter;
	if (Factory->EnumAdapters(0, &Adapter) == DXGI_ERROR_NOT_FOUND) 
	{
		DestroyWindowContext();
		return FALSE;
	}
	
	//	
	void* D3D12CreateDevice = GetProcAddress(D3D12Module, "D3D12CreateDevice");
	if (D3D12CreateDevice == NULL) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	//	Create Device
	ID3D12Device* Device;
	if (((long(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**))(D3D12CreateDevice))(Adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&Device) < 0) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	//	
	D3D12_COMMAND_QUEUE_DESC QueueDesc;
	QueueDesc.Type		= D3D12_COMMAND_LIST_TYPE_DIRECT;
	QueueDesc.Priority	= 0;
	QueueDesc.Flags		= D3D12_COMMAND_QUEUE_FLAG_NONE;
	QueueDesc.NodeMask	= 0;

	//	
	ID3D12CommandQueue* CommandQueue;
	if (Device->CreateCommandQueue(&QueueDesc, __uuidof(ID3D12CommandQueue), (void**)&CommandQueue) < 0) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	//	
	ID3D12CommandAllocator* CommandAllocator;
	if (Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&CommandAllocator) < 0) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	//	
	ID3D12GraphicsCommandList* CommandList;
	if (Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, NULL, __uuidof(ID3D12GraphicsCommandList), (void**)&CommandList) < 0) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	DXGI_RATIONAL RefreshRate;
	RefreshRate.Numerator		= 60;
	RefreshRate.Denominator		= 1;

	DXGI_MODE_DESC BufferDesc;
	BufferDesc.Width			= 0;
	BufferDesc.Height			= 0;
	BufferDesc.RefreshRate		= RefreshRate;
	BufferDesc.Format			= DXGI_FORMAT_R8G8B8A8_UNORM;
	BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	BufferDesc.Scaling			= DXGI_MODE_SCALING_UNSPECIFIED;

	DXGI_SAMPLE_DESC SampleDesc;
	SampleDesc.Count			= 1;
	SampleDesc.Quality			= 0;

	DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
	SwapChainDesc.BufferDesc	= BufferDesc;
	SwapChainDesc.SampleDesc	= SampleDesc;
	SwapChainDesc.BufferUsage	= DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.BufferCount	= 2;
	SwapChainDesc.OutputWindow	= WindowHwnd;
	SwapChainDesc.Windowed		= 1;
	SwapChainDesc.SwapEffect	= DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapChainDesc.Flags			= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	
	//	
	IDXGISwapChain* SwapChain;
	if (Factory->CreateSwapChain(CommandQueue, &SwapChainDesc, &SwapChain) < 0) 
	{
		DestroyWindowContext();
		return FALSE;
	}

	//	
	this->MethodsTable = (uint64_t*)::calloc(150, sizeof(uint64_t));
	memcpy(this->MethodsTable, *(uint64_t**)Device, 44 * sizeof(uint64_t));
	memcpy(this->MethodsTable + 44, *(uint64_t**)CommandQueue, 19 * sizeof(uint64_t));
	memcpy(this->MethodsTable + 44 + 19, *(uint64_t**)CommandAllocator, 9 * sizeof(uint64_t));
	memcpy(this->MethodsTable + 44 + 19 + 9, *(uint64_t**)CommandList, 60 * sizeof(uint64_t));
	memcpy(this->MethodsTable + 44 + 19 + 9 + 60, *(uint64_t**)SwapChain, 18 * sizeof(uint64_t));
	Sleep(100);

	//	Minhook
	MH_Initialize();
	Device->Release();
	Device = NULL;
	CommandQueue->Release();
	CommandQueue = NULL;
	CommandAllocator->Release();
	CommandAllocator = NULL;
	CommandList->Release();
	CommandList = NULL;
	SwapChain->Release();
	SwapChain = NULL;

	//	Destroy window 
	DestroyWindow(this->WindowHwnd);

	//	Unregister the class
	UnregisterClassA(this->WindowClass.lpszClassName, GetModuleHandle(NULL));

	//	Clear the class instance
	this->WindowClass = WNDCLASSEXA();

	// Hooks
	EnableAllHooks();

	//	success
	return TRUE;
}

bool DX12_Base::ObtainDevice(IDXGISwapChain* pSwapChain)
{
	pSwapChain->QueryInterface(IID_PPV_ARGS(&this->m_Swapchain3));
	if (this->m_Swapchain3 == nullptr)
		return FALSE;

	this->m_Swapchain3->GetDesc(&this->m_SwapDesc);

	UINT			bufferIndex = this->m_Swapchain3->GetCurrentBackBufferIndex();
	if (this->m_Swapchain3->GetDevice(IID_PPV_ARGS(&this->m_Device)) != S_OK)
		return FALSE;

	this->m_BuffersCounts = this->m_SwapDesc.BufferCount;
	this->m_RenderTargets.clear();

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type			= D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 1;
	desc.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (this->m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&this->m_DescriptorHeap)) != S_OK)
	{
		this->m_Device->Release();
		this->m_Swapchain3->Release();
		return FALSE;
	}
	desc = D3D12_DESCRIPTOR_HEAP_DESC();
	desc.Type			= D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	desc.NumDescriptors = this->m_BuffersCounts;
	desc.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NodeMask		= 1;
	if (this->m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&this->m_rtvDescriptorHeap)) != S_OK)
	{
		this->m_Device->Release();
		this->m_Swapchain3->Release();
		this->m_DescriptorHeap->Release();
		return FALSE;
	}

	SIZE_T						rtvDescriptorSize	= this->m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle			= this->m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	this->m_CommandAllocator						= new ID3D12CommandAllocator * [this->m_BuffersCounts];
	for (int i = 0; i < this->m_BuffersCounts; ++i)
	{
		this->m_RenderTargets.push_back(rtvHandle);
		rtvHandle.ptr += rtvDescriptorSize;
	}

	for (UINT i = 0; i < this->m_SwapDesc.BufferCount; ++i)
	{
		if (this->m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&this->m_CommandAllocator[i])) != S_OK)
		{
			this->m_Device->Release();
			this->m_Swapchain3->Release();
			this->m_DescriptorHeap->Release();
			for (UINT j = 0; j < i; ++j)
			{
				this->m_CommandAllocator[j]->Release();
			}
			this->m_rtvDescriptorHeap->Release();
			delete[]this->m_CommandAllocator;
			return FALSE;
		}
	}

	if (this->m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, this->m_CommandAllocator[0], NULL, IID_PPV_ARGS(&this->m_CommandList)) != S_OK || this->m_CommandList->Close() != S_OK)
	{
		this->m_Device->Release();
		this->m_Swapchain3->Release();
		this->m_DescriptorHeap->Release();
		for (UINT i = 0; i < this->m_BuffersCounts; ++i)
			this->m_CommandAllocator[i]->Release();
		this->m_rtvDescriptorHeap->Release();
		delete[]this->m_CommandAllocator;
		return FALSE;
	}

	this->m_BackBuffer = new ID3D12Resource * [this->m_BuffersCounts];
	for (UINT i = 0; i < this->m_BuffersCounts; i++)
	{
		this->m_Swapchain3->GetBuffer(i, IID_PPV_ARGS(&this->m_BackBuffer[i]));
		this->m_Device->CreateRenderTargetView(this->m_BackBuffer[i], NULL, this->m_RenderTargets[i]);
	}
	
	//	Finished
	this->m_OldWndProc				= SetWindowLongPtr(this->m_GameWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);
	this->m_Init					= TRUE;
	this->InitializeImGui(this->m_Device);
}

void DX12_Base::InitializeImGui(ID3D12Device* pDevice)
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.WantCaptureMouse || io.WantTextInput || io.WantCaptureKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ImeWindowHandle = this->m_GameWindow;
	io.IniFilename = NULL;
	ImGui_ImplWin32_Init(this->m_GameWindow);
	ImGui_ImplDX12_Init(pDevice, this->m_BuffersCounts, DXGI_FORMAT_R8G8B8A8_UNORM, NULL, this->m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart(), this->m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	ImGui_ImplDX12_CreateDeviceObjects();
	ImGui::StyleColorsDark();
	pDevice->Release();
}

static float f = 0.0f;
static int counter = 0;
static bool bDemoWindow;
static bool bStyleEditor;
ImVec4 clear_color = ImVec4(0.06f, 0.06f, 0.06f, .94f);
void DX12_Base::Overlay()
{

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	if (bDemoWindow)
		ImGui::ShowDemoWindow();

	if (bStyleEditor)
		ImGui::ShowStyleEditor();

	IM_ASSERT(ImGui::GetCurrentContext() != NULL && "Missing dear imgui context. Refer to examples app!");
	ImGui::PushStyleColor(ImGuiCol_WindowBg, clear_color);
	if (!ImGui::Begin("(DX12) ImGui Internal Base", NULL))
	{
		ImGui::PopStyleColor();
		ImGui::End();
		return;
	}
	ImGui::PopStyleColor();

	ImGui::Text("Base Created by: NightFyre");
	ImGui::Checkbox("ImGui Demo", &bDemoWindow);      
	ImGui::Checkbox("Style Editor", &bStyleEditor);    

	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	ImGui::SliderFloat("##float", &f, 0.0f, 1.0f);				
	ImGui::ColorEdit4("clear color", (float*)&clear_color);		
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

	ImGui::Separator();
	if (ImGui::Button("EXIT", { ImGui::GetContentRegionAvail().x, 0.0f }))
		this->bKillswitch = TRUE;
	
	ImGui::End();
	ImGui::EndFrame();
	ImGui::Render();
}

bool DX12_Base::CreateHook(uint16_t Index, void** Original, void* Function)
{
	void* target = reinterpret_cast<void*>(this->MethodsTable[Index]);
	if (MH_CreateHook(target, Function, Original) != MH_OK || MH_EnableHook(target) != MH_OK)
		return false;
	return true;
}    

void DX12_Base::EnableAllHooks()
{
	CreateHook(54, (void**)&oExecuteCommandLists, HookExecuteCommandLists);
	CreateHook(140, (void**)&oPresent, HookPresent);
	CreateHook(146, (void**)&oResizeTarget, HookResizeTarget);
	MH_EnableHook(MH_ALL_HOOKS);
}

void DX12_Base::ResetRenderState()
{
	if (!this->m_Init)
		return;

	this->m_DescriptorHeap->Release();
	for (UINT i = 0; i < m_BuffersCounts; ++i)
	{
		this->m_CommandAllocator[i]->Release();
		this->m_BackBuffer[i]->Release();
	}
	this->m_rtvDescriptorHeap->Release();
	delete[]this->m_CommandAllocator;
	delete[]this->m_BackBuffer;
	this->DestroyImGuiContext();
	SetWindowLongPtr(this->m_GameWindow, GWLP_WNDPROC, (LONG_PTR)this->m_OldWndProc);
	this->m_Init = false;
}

void DX12_Base::DestroyWindowContext()
{
	//	A window should only be destroyed if it there
	if (this->WindowHwnd > NULL)
		DestroyWindow(this->WindowHwnd);	//	Destroy window 

	//	Unregister the class
	//	@TODO: While it's generally safe to call UnregisterClass on a class that was never registered after windows XP ... Its still good practice to only call the function on a registered class.
	//	there will be a future boolean in this class to signify the registration of the window class.
	UnregisterClassA(this->WindowClass.lpszClassName, GetModuleHandle(NULL));

	//	Clear the class instance
	WindowClass = WNDCLASSEXA();
}

void DX12_Base::DestroyImGuiContext()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

bool DX12_Base::DisableHook(uint16_t Index)
{
	void* target = reinterpret_cast<void*>(this->MethodsTable[Index]);
	if (MH_DisableHook(target) != MH_OK || MH_RemoveHook(target) != MH_OK)
		return FALSE;
	return TRUE;
}

void DX12_Base::DisableAllHooks()
{
	DisableHook(MethodsTable[54]);
	DisableHook(MethodsTable[140]);
	DisableHook(MethodsTable[146]);
	MH_RemoveHook(MH_ALL_HOOKS);
}

void APIENTRY MainThread(HMODULE hInst)
{
	g_DX12				= std::make_unique<DX12_Base>();
	g_DX12->bRunning	= g_DX12->InitializeWindow("PS2 BIOS (USA)");
	while (g_DX12->bRunning)
	{
		if (GetAsyncKeyState(VK_INSERT) & 1)	g_DX12->m_ShowMenu	^= 1;
		if (GetAsyncKeyState(VK_END) & 1)		g_DX12->bKillswitch	= TRUE;
		std::this_thread::sleep_for(1ms);
		std::this_thread::yield();
	}
	FreeLibraryAndExitThread(hInst, 0);
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved )
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
		DisableThreadLibraryCalls(hModule);
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)MainThread, hModule, NULL, NULL);
    }
    return TRUE;
}