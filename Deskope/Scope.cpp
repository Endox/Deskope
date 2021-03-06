#include "stdafx.h"
#include "Scope.h"
#include "glm/glm.hpp"

Scope::Scope() : m_fltYaw(0),
				 m_fltPitch(0),
				 m_fltRoll(0),
				 m_boolTracking(false),
				 m_intDrawRate(60),
				 m_intCaptureRate(60),
				 m_fltCursorBorder(float(0.3))
{
	m_intCursorWidth = GetSystemMetrics(SM_CXCURSOR);
	m_intCursorHeight = GetSystemMetrics(SM_CYCURSOR);
	m_intMainDisplayWidth = GetSystemMetrics(SM_CXSCREEN);
	m_intMainDisplayHeight = GetSystemMetrics(SM_CYSCREEN);
}

Scope::~Scope()
{
	ReleaseRiftResources();
	ReleaseWindowsResources();
	KillTimers();
}

int Scope::Initialize()
{
	if (SetUpRift() < 0) {
		MessageBoxW(m_hwnd, L"Error setting up the Rift.", L"Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		return -1;
	}
	if (GetRiftDisplayInfo() < 0) {
		MessageBoxW(m_hwnd, L"Error getting Rift display info.", L"Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		return -1;
	}
	ResizeSource();
	if (SetUpWindow() < 0) {
		MessageBoxW(m_hwnd, L"Windows error.", L"Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		return -1;
	}
	AllocateWindowsResources();
	if (SetUpTimers() < 0) {
		MessageBoxW(m_hwnd, L"Windows timer error.", L"Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		return -1;
	}

	return 0;
}

int Scope::SetUpRift()
{
#ifdef NO_RIFT
		return 0;
#endif
	System::Init(Log::ConfigureDefaultLog(LogMask_All));
	m_pManager = *DeviceManager::Create();
	m_pHMD = *m_pManager->EnumerateDevices<HMDDevice>().CreateDevice();
	
	if (!m_pHMD)
		return -1;

	m_pHMD->GetDeviceInfo(&m_hmdInfo);
	return 0;
}

int Scope::SetUpWindow()
{	
	WNDCLASSW wc = {0};
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpfnWndProc = Scope::WndProc;
	wc.lpszClassName = L"Deskope Window Class";
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	
	RegisterClassW(&wc);
	m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
							 L"Deskope Window Class",
							 L"Deskope Window",
							 WS_POPUP,
							 m_RiftDisplayInfo.dmPosition.x, m_RiftDisplayInfo.dmPosition.y,
							 m_RiftDisplayInfo.dmPelsWidth, m_RiftDisplayInfo.dmPelsHeight,
							 NULL,
							 NULL,
							 GetModuleHandle(NULL),
							 this);	//pass the window a pointer to this Scope object
	if (!m_hwnd)
		return -1;

	ShowWindow(m_hwnd, SW_SHOWNORMAL);
	return 0;
}

int Scope::SetUpTimers()
{ 
	m_uipDrawTimer = SetTimer(m_hwnd, 1, 1000 / m_intDrawRate, Scope::TimerProc);
	if (!m_uipDrawTimer)
		return -1;
	m_uipSendRSDTimer = SetTimer(m_hwnd, 2, 100, Scope::TimerProc);
	if (!m_uipSendRSDTimer)
		return -1;
	m_uipCaptureTimer = SetTimer(m_hwnd, 3, 1000 / m_intCaptureRate, Scope::TimerProc);
	if (!m_uipCaptureTimer)
		return -1;
	return 0;
}

void Scope::RunMessageLoop()
{
    MSG msg;

	BOOL fDone = FALSE;
	while (!fDone)
	{
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
			SleepEx(0, true);
		}
		if (msg.message == WM_QUIT)
			fDone = TRUE;
	}
}

void Scope::ReleaseRiftResources()
{
	m_pManager.Clear();
	m_pHMD.Clear();
	m_pSensor.Clear();
	System::Destroy();
}

void Scope::AllocateWindowsResources()
{
	m_hdc = GetDC(m_hwnd);
	m_winDC = GetDC(NULL);
	m_winCopyDC = CreateCompatibleDC(m_winDC);
	ResizeSource();

	m_BackDC = CreateCompatibleDC(m_hdc);
	m_BackBM = CreateCompatibleBitmap(m_hdc, m_intMainDisplayWidth, m_intMainDisplayHeight);
	SelectObject(m_BackDC, m_BackBM);

	SetStretchBltMode(m_winCopyDC, HALFTONE);
	SetBrushOrgEx(m_winCopyDC, 0, 0, NULL);
}

void Scope::ReleaseWindowsResources()
{
	ReleaseDC(m_hwnd, m_hdc);
	DeleteDC(m_hdc);
	ReleaseDC(m_hwnd, m_winDC);
	DeleteDC(m_winDC);
	ReleaseDC(m_hwnd, m_winCopyDC);
	DeleteDC(m_winCopyDC);
	ReleaseDC(m_hwnd, m_BackDC);
	DeleteDC(m_BackDC);
	DeleteObject(m_winCopyBM);
	DeleteObject(m_BackBM);
}

void Scope::ResizeSource()
{
	m_intSrcWidth = int(m_RiftDisplayInfo.dmPelsWidth / 2.0 / m_fltZoom);
	m_intSrcHeight = int(m_RiftDisplayInfo.dmPelsHeight / m_fltZoom);

	DeleteObject(m_winCopyBM);
	m_winCopyBM = CreateCompatibleBitmap(m_winDC, m_intMainDisplayWidth, m_intMainDisplayHeight);
	SelectObject(m_winCopyDC, m_winCopyBM);

	// After deleting the old bitmap, call CaptureScreen explicitly.
	// Otherwise, DrawScope() may be called while the copy of the 
	// screen is all black which causes flicker.
	CaptureScreen();
}

void Scope::KillTimers()
{	
	KillTimer(m_hwnd, m_uipDrawTimer);
	KillTimer(m_hwnd, m_uipSendRSDTimer);
	KillTimer(m_hwnd, m_uipCaptureTimer);
}

//Delegate to member function message handler (because WndProc can't be a member function, must be static)
LRESULT CALLBACK Scope::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Scope * pThis = NULL;
    if (message == WM_CREATE)
    {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (Scope*)pCreate->lpCreateParams;
        SetWindowLongW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = (Scope*)GetWindowLongW(hwnd, GWLP_USERDATA);
    }
    if (pThis)
    {
        return pThis->HandleMessage(message, wParam, lParam);
    }
    else
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

//Delegate to timer methods
VOID CALLBACK Scope::TimerProc(HWND hwnd, UINT, UINT_PTR timer_id, DWORD)
{
	Scope * pThis = (Scope*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (pThis)
	{
		if (timer_id == pThis->m_uipDrawTimer)
		{
			pThis->RestrictCursor();
			pThis->DrawScope();
			return;
		}
		if (timer_id == pThis->m_uipCaptureTimer)
		{
			pThis->CaptureScreen();
			return;
		}
		if (timer_id == pThis->m_uipSendRSDTimer)
		{
			pThis->SendRiftSensorData();
			return;
		}
	}
}

LRESULT Scope::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{	
	switch (message)
	{	
	//Change settings through Windows messages
	case WM_DESKOPE_VALUE_CHANGE:
		switch(wParam)
		{
		case IMAGE_SEPARATION:
			m_intImageSeparation = lParam;
			ResizeSource();
			break;
		case PIXELS_PER_DEGREE:
			m_intPixelsPerDegree = lParam;
			break;
		case CLIP_CURSOR:
			m_boolClipCursor = (lParam != 0);
			break;
		case RSD_RESET:
			m_sFusion.Reset();
			break;
		case SBS_OFFSET:
			m_intSBSOffset = lParam;
			ResizeSource();
			break;
		case ZOOM:
			m_fltZoom = reinterpret_cast<float&>(lParam);
			ResizeSource();
			break;
		case TRACKING:
			EnableTracking(lParam != 0);
			break;
		case SCREENCAPTURERATE:
			m_intCaptureRate = lParam;
			SetTimer(m_hwnd, m_uipCaptureTimer, 1000 / m_intCaptureRate, Scope::TimerProc);
			break;
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(m_hwnd, message, wParam, lParam);
}

void Scope::GetSourceCoordinates(int *x, int *y)
{
	if (m_boolTracking) {
		m_sFusion.GetOrientation().GetEulerAngles<Axis_Y, Axis_X, Axis_Z>(&m_fltYaw, &m_fltPitch, &m_fltRoll);
		*x = (m_fltYaw * -180.0 / PI) * m_intPixelsPerDegree;
		*y = (m_fltPitch *  -180.0 / PI) * m_intPixelsPerDegree; 
	} 
	else {
		POINT p;
		GetCursorPos(&p);
		*x = p.x - int((m_intSrcWidth + m_intSBSOffset + m_intImageSeparation / m_fltZoom) / 2);
		*y = p.y - m_intSrcHeight / 2; 
	}
}

void Scope::CaptureScreen()
{	
	GetSourceCoordinates(&m_intScreenCapX, &m_intScreenCapY);
	// calculate coordinates of source rectangle
	RECT SourceRect;
	SourceRect.left = m_intScreenCapX;
	SourceRect.top = m_intScreenCapY;
	SourceRect.right = SourceRect.left + m_intSrcWidth * 2 + long((m_intSBSOffset + m_intImageSeparation) / m_fltZoom);
	SourceRect.bottom = SourceRect.top + m_intSrcHeight;

	// blit a copy of the screen so you don't have to draw on the screen DC
	BITMAP WinCopyBMInfo;
	GetObjectW(m_winCopyBM, sizeof(BITMAP), &WinCopyBMInfo);
	BitBlt(m_winCopyDC, 0, 0, WinCopyBMInfo.bmWidth, WinCopyBMInfo.bmHeight, m_winDC, 0, 0, SRCCOPY | CAPTUREBLT);
	
	//Draw the cursor
	m_GlobalCursor.cbSize = sizeof(CURSORINFO);
	GetCursorInfo(&m_GlobalCursor);
	if (m_GlobalCursor.flags == CURSOR_SHOWING)	{
		ICONINFO CursorInfo;
		GetIconInfo((HICON)m_GlobalCursor.hCursor, &CursorInfo);
		DrawIconEx(m_winCopyDC,
			       m_GlobalCursor.ptScreenPos.x, m_GlobalCursor.ptScreenPos.y,
				   m_GlobalCursor.hCursor,
				   int(m_intCursorWidth * m_fltZoom), int(m_intCursorHeight * m_fltZoom),
				   0, NULL, DI_COMPAT | DI_NORMAL);		
		DeleteObject(CursorInfo.hbmColor);
		DeleteObject(CursorInfo.hbmMask);
	}

	// Rotate and offset the desktop

	// Build the destination parallelogram using glm
	glm::vec2 pts_vec2[3];
	pts_vec2[0] = glm::vec2(0, 0);
	pts_vec2[1] = glm::vec2(m_intMainDisplayWidth, 0);
	pts_vec2[2] = glm::vec2(0, m_intMainDisplayHeight);
	
	glm::vec2 center(m_intMainDisplayWidth / 2, m_intMainDisplayHeight / 2);

	const float roll = -m_fltRoll;
	glm::mat2 R = glm::mat2(cos(roll), -sin(roll), sin(roll), cos(roll));
	glm::mat2 S = glm::mat2(m_fltZoom, 0, 0, m_fltZoom);

	// Transform the points and convert to GDI format
	POINT pts[3];
	for (int i = 0; i < 3; ++i) {
		// Move to origin [0, 0]
		pts_vec2[i] -= center;

		// Scale (zoom)
		pts_vec2[i] = S * pts_vec2[i];

		// Offset according to HMD orientation
		pts_vec2[i].x -= m_intScreenCapX;
		pts_vec2[i].y -= m_intScreenCapY;

		// Rotate
		pts_vec2[i] = R * pts_vec2[i];

		// Move back
		pts_vec2[i] += center;

		// Convert to GDI format
		pts[i].x = pts_vec2[i].x;
		pts[i].y = pts_vec2[i].y;
	}
	
	// Clear the back buffer 
	RECT ClearRect = {0, 0, m_intMainDisplayWidth, m_intMainDisplayHeight};
	FillRect(m_BackDC, &ClearRect, (HBRUSH)COLOR_WINDOWTEXT);

	// Blit the rotated desktop to back buffer
	PlgBlt(m_BackDC, pts, m_winCopyDC, 0, 0, m_intMainDisplayWidth, m_intMainDisplayHeight, NULL, 0, 0);
}

void Scope::DrawScope()
{
	int NewX, NewY;
	GetSourceCoordinates(&NewX, &NewY);

	BitBlt( m_hdc, 
			0, 0, 
			m_RiftDisplayInfo.dmPelsWidth / 2, m_RiftDisplayInfo.dmPelsHeight, 
			m_BackDC, 
			(m_intMainDisplayWidth - m_RiftDisplayInfo.dmPelsWidth / 2 - m_intImageSeparation) / 2 + (NewX - m_intScreenCapX), (m_intMainDisplayHeight - m_RiftDisplayInfo.dmPelsHeight) / 2 + (NewY - m_intScreenCapY), 
			SRCCOPY);
	BitBlt( m_hdc,
			m_RiftDisplayInfo.dmPelsWidth / 2, 0, 
			m_RiftDisplayInfo.dmPelsWidth / 2, m_RiftDisplayInfo.dmPelsHeight,
			m_BackDC, 
			(m_intMainDisplayWidth - m_RiftDisplayInfo.dmPelsWidth / 2 + m_intImageSeparation) / 2 + (NewX - m_intScreenCapX), (m_intMainDisplayHeight - m_RiftDisplayInfo.dmPelsHeight) / 2 + (NewY - m_intScreenCapY), 
			SRCCOPY);
	
	UpdateWindow(m_hwnd);
}

void Scope::SendRiftSensorData()
{
	PostMessageW(m_hwndCaller, WM_RIFT_SENSOR_DATA, RSD_YAW, reinterpret_cast<LPARAM&>(m_fltYaw));
	PostMessageW(m_hwndCaller, WM_RIFT_SENSOR_DATA, RSD_PITCH, reinterpret_cast<LPARAM&>(m_fltPitch));
	PostMessageW(m_hwndCaller, WM_RIFT_SENSOR_DATA, RSD_ROLL, reinterpret_cast<LPARAM&>(m_fltRoll));
}

UINT Scope::RunScopeWindow(LPVOID pParam)
{
	//pParam is the handle to the calling window
	Scope scope;
	if (SUCCEEDED(scope.Initialize()))
	{
		scope.m_hwndCaller = (HWND)pParam;
		//Send the calling window a handle to the scope window
		PostMessageW((HWND)pParam, WM_DESKOPE_SCOPE_HANDLE, (WPARAM)scope.m_hwnd, NULL);
		scope.RunMessageLoop();
		ClipCursor(NULL);
		return 0;
	}
	else
		return -1;
}

DWORD Scope::CloseScopeWindow(HWND hwndScope, PHANDLE hScopeThread)
{
	//Send close message to Scope window then wait for thread to end
	::SendMessageW(hwndScope, WM_SYSCOMMAND, SC_CLOSE, 0);
	DWORD result = MsgWaitForMultipleObjects(1, hScopeThread, TRUE, INFINITE, QS_ALLEVENTS);
	*hScopeThread = NULL;
	return result;
}

int Scope::GetRiftDisplayInfo()
{	
#ifndef NO_RIFT	
	//remove last '\\Monitor#' part from hmdInfo.DisplayDeviceName
	CStringW DisplayDeviceName(m_hmdInfo.DisplayDeviceName);
	DisplayDeviceName = DisplayDeviceName.Left(DisplayDeviceName.ReverseFind('\\'));

	m_RiftDisplayInfo.dmSize = sizeof(DEVMODE);
	int result = EnumDisplaySettingsW(DisplayDeviceName, ENUM_CURRENT_SETTINGS, &m_RiftDisplayInfo);

	if (!result)
		return -1;
	else
		return result;
#else
	m_RiftDisplayInfo.dmPelsWidth = DEFAULT_SCREEN_WIDTH;
	m_RiftDisplayInfo.dmPelsHeight = DEFAULT_SCREEN_HEIGHT;
	m_RiftDisplayInfo.dmPosition.x = DEFAULT_DESKTOPX;
	m_RiftDisplayInfo.dmPosition.y = DEFAULT_DESKTOPY;
	return 0;
#endif

}

void Scope::EnableTracking(bool boolEnableTracking)
{
#ifndef NO_RIFT
	if (boolEnableTracking) {
		if (m_pHMD) {
			m_pSensor = *m_pHMD->GetSensor();
			m_sFusion.AttachToSensor(m_pSensor);
			m_sFusion.SetYawCorrectionEnabled(true); // Enable yaw correction
			m_boolTracking = true;
		}
		else {
			MessageBoxW(m_hwnd, L"Rift not found.", L"Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
			m_boolTracking = false;
		}
		
	}
	else {
		m_pSensor.Clear();
		m_boolTracking = false;
	}
#endif
}

void Scope::RestrictCursor()
{
	int SourceX, SourceY;
	GetSourceCoordinates(&SourceX, &SourceY);
	if (m_boolClipCursor) {
		RECT clip;
		clip.left = (m_intMainDisplayWidth - m_intSrcWidth * m_fltCursorBorder) / 2 + SourceX / m_fltZoom;
		clip.right = (m_intMainDisplayWidth + m_intSrcWidth * m_fltCursorBorder) / 2 + SourceX / m_fltZoom;
		clip.top = (m_intMainDisplayHeight - m_intSrcHeight * m_fltCursorBorder) / 2 + SourceY / m_fltZoom;
		clip.bottom = (m_intMainDisplayHeight + m_intSrcHeight * m_fltCursorBorder) / 2 + SourceY / m_fltZoom;
		ClipCursor(&clip);
	}
	else {
		ClipCursor(NULL); 
	}
}