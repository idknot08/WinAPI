// Test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <WinSock2.h> // cái này khai báo trước để không bị kéo winsock.h vào từ windows.h
#include <Windows.h> 
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib") //requitment
using namespace std;

BOOL dangKyWinSock() {
    WSADATA wsaData; // khởi tạo winsock (instance cua 1 struct )

// Kiem tra loi
    int iResult;
    int iResult = WSAStartup(
        MAKEWORD(2, 2),
        &wsaData
    );
    if (iResult != 0) {
        wcout << "Đăng ký dùng winsock2 thất bại\n";
        wcout << "WSAStartup failed: "<< iResult;
        wcout << "Mã lỗi : " << GetLastError() << "\n";
        return 1;
    }
}


int main()
{
    std::cout << "Hello World!\n";
}

