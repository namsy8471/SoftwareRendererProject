// SoftrendererProject.cpp : 애플리케이션에 대한 진입점을 정의합니다.
//

#include "Core/pch.h"
#include "SoftrendererProject.h"
#include "Core/Framework.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    Framework app;

    if (app.Initialize(hInstance, nCmdShow))
    {
        app.Run();
    }

    app.Shutdown();

    return 0;
} 
