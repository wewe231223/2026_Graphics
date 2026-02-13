// 2026_Graphics.cpp : 애플리케이션에 대한 진입점을 정의합니다.
extern "C" { __declspec(dllexport) extern const unsigned int D3D12SDKVersion = 618; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".//D3D/"; }

#include "framework.h"
#include "2026_Graphics.h"

#define MAX_LOADSTRING 100


#include "Core/Config.h"
#include "DirectXTK12/Keyboard.h"
#include "DirectXTK12/Mouse.h"
#include "Core/DX/DirectQueue.h"
#include "Core/DX/CopyQueue.h"

// 전역 변수:
HINSTANCE hInst;                                // 현재 인스턴스입니다.
WCHAR szTitle[MAX_LOADSTRING];                  // 제목 표시줄 텍스트입니다.
WCHAR szWindowClass[MAX_LOADSTRING];            // 기본 창 클래스 이름입니다.
HWND hWnd; 

// 01-15 TODO
// RenderQueue, ComputeQueue 따로 분리할 생각 해보기 -> Renderer 이런거 만들지 않기. 
// ComputeTask 같은건 좀 복잡할 거 같은데? Compute Shader 를 위한 파이프라인 생각해보기. 

// 이 코드 모듈에 포함된 함수의 선언을 전달합니다:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: 여기에 코드를 입력합니다.

	FileConfig config("Core/config.prop");
	Config::Init(&config);

    // 전역 문자열을 초기화합니다.
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_MY2026GRAPHICS, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // 애플리케이션 초기화를 수행합니다:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MY2026GRAPHICS));

    MSG msg;

	Core::DX::DirectQueue directQueue(hWnd);
	Core::DX::CopyQueue copyQueue(directQueue.GetDevice());

    // 기본 메시지 루프입니다:
    while (true) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                break;
            }
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }


        directQueue.Update(); 

    }

    
    return (int) msg.wParam;
}



//
//  함수: MyRegisterClass()
//
//  용도: 창 클래스를 등록합니다.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{ 0 };

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MY2026GRAPHICS));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   함수: InitInstance(HINSTANCE, int)
//
//   용도: 인스턴스 핸들을 저장하고 주 창을 만듭니다.
//
//   주석:
//
//        이 함수를 통해 인스턴스 핸들을 전역 변수에 저장하고
//        주 프로그램 창을 만든 다음 표시합니다.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // 인스턴스 핸들을 전역 변수에 저장합니다.


    if (Config::Query().Get<bool>("Windowed")) {
        DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME;
        DWORD exStyle = WS_EX_OVERLAPPEDWINDOW;

        int posX = (GetSystemMetrics(SM_CXSCREEN) / 2) - (Config::Query().Get<int>("Window_Width") / 2);
        int posY = (GetSystemMetrics(SM_CYSCREEN) / 2) - (Config::Query().Get<int>("Window_Height") / 2);

        RECT adjustedRect{ 0, 0, Config::Query().Get<long>("Window_Width"), Config::Query().Get<long>("Window_Height")};
        ::AdjustWindowRectEx(std::addressof(adjustedRect), style, FALSE, exStyle);

        hWnd = CreateWindowEx(
            exStyle,                                // 확장 스타일
            szWindowClass,                          // 윈도우 클래스 이름
            szTitle,                                // 윈도우 타이틀 
            style,                                  // 윈도우 스타일
            posX, posY,                             // 위치
            adjustedRect.right - adjustedRect.left,
            adjustedRect.bottom - adjustedRect.top, // 크기
            nullptr,                                // 부모 윈도우
            nullptr,                                // 메뉴
            hInstance,                              // 인스턴스 핸들
            nullptr                                 // 추가 매개변수
        );
    }
    else {
        DWORD style = WS_POPUP;
        DWORD exStyle = NULL;

        int posX = (GetSystemMetrics(SM_CXSCREEN) / 2) - (Config::Query().Get<int>("Window_Width") / 2);
        int posY = (GetSystemMetrics(SM_CYSCREEN) / 2) - (Config::Query().Get<int>("Window_Height") / 2);

        hWnd = CreateWindowEx(
            exStyle,                                // 확장 스타일
            szWindowClass,                          // 윈도우 클래스 이름
            szTitle,                                // 윈도우 타이틀 
            style,                                  // 윈도우 스타일
            posX, posY,                             // 위치 
            Config::Query().Get<long>("Window_Width"), Config::Query().Get<long>("Window_Height"), // 크기
            nullptr,                                // 부모 윈도우
            nullptr,                                // 메뉴
            hInstance,                              // 인스턴스 핸들
            nullptr                                 // 추가 매개변수
        );
    }

    if (!hWnd) {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

//
//  함수: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  용도: 주 창의 메시지를 처리합니다.
//
//  WM_COMMAND  - 애플리케이션 메뉴를 처리합니다.
//  WM_PAINT    - 주 창을 그립니다.
//  WM_DESTROY  - 종료 메시지를 게시하고 반환합니다.
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    constexpr UINT keyPressedCheckBitMask = 0x60000000;
    constexpr UINT keyPressedAtTime = 0x20000000;

    switch (message) {
    /**
    *	**아래의 WM_SYSKEYDOWN 메세지에 등장하는 KeyPressedBitMask, KeyPressedAtTime 값에 대한 설명입니다**
    *
    *	(lParam & 0x60000000) == 0x20000000 부분은 키보드 입력 메시지에서 lParam 값을 검사하여
    *	키가 눌린 상태와 반복된 횟수를 확인하는 것입니다.
    *	KeyPressedCheckBitMask 가 0x60000000, KeyPressedAtTime 이 0x20000000으로 정의되어 있습니다.
    *
    *	lParam 값은 키보드 메시지에서 다양한 정보를 담고 있는 32비트 값으로 구성되어 있습니다.
    *	lParam의 각 비트의 의미를 설명하면 다음과 같습니다:
    *
    *	비트 0-15: 키가 눌려진 반복 횟수
    *	비트 16-23: 키의 스캔 코드
    *	비트 24: 확장 키 여부 (0: 일반 키, 1: 확장 키)
    *	비트 25-28: 예약된 비트
    *	비트 29: 이전 키 상태 (0: 이전에 눌리지 않았음, 1: 이전에 눌림)
    *	비트 30: 키 상태 (0: 키가 눌렸음, 1: 키가 놓였음)
    *	비트 31: 전송된 메시지의 종류 (0: WM_KEYDOWN, 1: WM_KEYUP)
    *	이제 (lParam & 0x60000000) == 0x20000000의 의미를 해석해 보겠습니다:
    *
    *	0x60000000는 이진수로 01100000 00000000 00000000 00000000입니다.
    *	0x20000000는 이진수로 00100000 00000000 00000000 00000000입니다.
    *	이 조건문은 lParam 값의 비트 29와 비트 30을 확인하고 있습니다. 이를 비트별로 분석해보면:
    *
    *	(lParam & 0x60000000)은 lParam 값에서 비트 29와 비트 30을 추출합니다.
    *	비트 29는 키가 이전에 눌렸는지 여부를 나타냅니다.
    *	비트 30은 키가 현재 눌렸는지 여부를 나타냅니다.
    *	== 0x20000000은 비트 29가 0이고 비트 30이 1인지를 확인합니다.
    *
    *	즉, (lParam & 0x60000000) == 0x20000000 조건은 키가 처음 눌렸을 때만 참이 됩니다.
    *	키가 반복되거나 키가 놓였을 때는 이 조건이 거짓이 됩니다.
    *	이 조건을 통해 키가 처음 눌렸을 때 ALT+ENTER 조합을 처리하는 것입니다.
    *
    **/
    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN && (lParam & keyPressedCheckBitMask) == keyPressedAtTime) {
            // 여기에는 Alt + Enter 과 같은 경우에 대한 처리를 추가하면 됩니다. 

        }
        break;
    case WM_KILLFOCUS:
    case WM_SETFOCUS:
        // Input.UpdateFocus(message);
        break;

        // 아래는 모든 입력을 Keyboard 에게 넘기는 부분입니다. 
    case WM_ACTIVATEAPP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
        break;
    case WM_MENUCHAR:
        return MAKELRESULT(0, MNC_CLOSE);
        // 아래는 모든 입력을 Mouse 에게 넘기는 부분입니다. 
    case WM_INPUT:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_MOUSEHOVER:
        DirectX::Mouse::ProcessMessage(message, wParam, lParam);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
