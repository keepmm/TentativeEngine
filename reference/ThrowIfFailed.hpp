/*****************************************************************//**
 * \file   ThrowIfFailed.hpp
 * \brief  
 * 
 * 作成者 
 * 作成日 2026/9/25
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <Windows.h>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>

/*****************************************************************//**
 * \brief  HRESULT をチェックし、失敗していたら例外を投げる
 * \param  hr HRESULT
 * \throw  std::runtime_error
 * *********************************************************************/
inline void ThrowIfFailedHelper(HRESULT hr,const char* file,int line)
{
	if (FAILED(hr))
	{
		std::stringstream ss;
		ss << "DirectX Error : HRESULT 0x" << std::hex << std::uppercase << std::setw(8) <<
			std::setfill('0') << static_cast<unsigned long>(hr);
		if (file)
		{
			ss << " at " << file << ":" << std::dec << line;
		}

		std::string msg = ss.str();
		OutputDebugStringA(msg.c_str());
		OutputDebugStringA("\n");
		throw std::runtime_error(msg);
	}
}

#define ThrowIfFailed(hr) ThrowIfFailedHelper(hr, __FILE__, __LINE__)

