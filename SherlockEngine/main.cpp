#include "pch.h"
#include "TestApp.h"

int main() 
{
    TestApp testApp;

    if (!testApp.Initialize())
    {
        std::cout << "[실패] App 초기화 실패!" << std::endl;
        return -1;
    }

    return testApp.Run();
}