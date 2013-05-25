/*
 * Multiplatform Async Network Library
 * Copyright (c) 2007 Burlex
 *
 * ListenSocket<T>: Creates a socket listener on specified address and port,
 *				  requires Update() to be called every loop.
 *
 */

#pragma once

#ifdef CONFIG_USE_IOCP

template <class T>
uint32 THREADCALL ListenSocketThread(void * lpParam)
{
	ListenSocket<T> * ls = (ListenSocket<T> *)lpParam;
	return ls->runnable() ? 0 : 1;
}

template<class T>
class ListenSocket
{
public:
	ListenSocket(SocketMgr *socketMgr, const char * ListenAddress, uint32 Port) : m_threadRunning(false)
	{
#ifndef USE_STD_THREAD
		m_hThread = nullptr;
#endif

		m_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);

		// Enable blocking on the socket
		SocketOps::Blocking(m_socket);

		m_address.sin_family = AF_INET;
		m_address.sin_port = ntohs((u_short)Port);
		m_address.sin_addr.s_addr = htonl(INADDR_ANY);
		m_opened = false;

		if (strcmp(ListenAddress, "0.0.0.0"))
		{
			struct hostent * hostname = gethostbyname(ListenAddress);
			if (hostname != nullptr)
				memcpy(&m_address.sin_addr.s_addr, hostname->h_addr_list[0], hostname->h_length);
		}

		// bind. well, attempt to...
		int ret = ::bind(m_socket, (const sockaddr*)&m_address, sizeof(m_address));
		if (ret != 0)
		{
			TRACE("Bind unsuccessful on port %u.\n", Port);
			return;
		}

		ret = listen(m_socket, 5);
		if (ret != 0) 
		{
			TRACE("Unable to listen on port %u.\n", Port);
			return;
		}

		m_opened = true;
		m_cp = socketMgr->GetCompletionPort();
		m_socketMgr = socketMgr;
	}

	~ListenSocket() { Close(); }

	bool run()
	{
#ifdef USE_STD_THREAD
		if (m_hThread.joinable())
			return false;

		m_hThread = std::thread(ListenSocketThread<T>, this);
		m_hThread.detach();
		return true;
#else
		if (m_hThread != nullptr)
			return false;

		DWORD id;
		m_hThread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)&ListenSocketThread<T>, this, 0, &id);
		return m_hThread != nullptr;
#endif
	}

	void suspend()
	{
#ifndef USE_STD_THREAD
		if (m_hThread != nullptr)
			SuspendThread(m_hThread);
#else
		SuspendThread(m_hThread.native_handle());
#endif
	}

	void resume()
	{
#ifndef USE_STD_THREAD
		if (m_hThread != nullptr)
			ResumeThread(m_hThread);
#else
		ResumeThread(m_hThread.native_handle());
#endif
	}

	bool runnable()
	{
		struct sockaddr_in m_tempAddress;
		uint32 len = sizeof(sockaddr_in);
		m_threadRunning = true;

		// Remove blocking on the socket
		// SocketOps::Nonblocking(m_socket);
		while (m_opened && m_threadRunning)
		{
			//SOCKET aSocket = accept(m_socket, (sockaddr*)&m_tempAddress, (socklen_t*)&len);
			SOCKET aSocket = WSAAccept(m_socket, (sockaddr*)&m_tempAddress, (socklen_t*)&len, 0, 0);
			if (aSocket == INVALID_SOCKET)
			{
				//sleep(10); // Don't kill the CPU!
				continue;
			}

			// Attempt to assign the socket to an available session
			Socket *socket = m_socketMgr->AssignSocket(aSocket);

			// No available sessions... unfortunately, we're going to have to let you go.
			if (socket == nullptr)
			{
				shutdown(aSocket, SD_BOTH);
				closesocket(aSocket);
				continue;
			}
			socket->SetCompletionPort(m_cp);
			socket->Accept(&m_tempAddress);
		}
		return true;
	}

	void Close()
	{
		// prevent a race condition here.
		bool mo = m_opened;
		m_opened = false;

		if (mo)
			SocketOps::CloseSocket(m_socket);

		m_threadRunning = false;

#ifdef USE_STD_THREAD
		if (m_hThread.joinable())
			m_hThread.join(); 
#else
		WaitForSingleObject(m_hThread, INFINITE);
#endif
	}

	INLINE bool IsOpen() { return m_opened; }
	INLINE HANDLE GetCompletionPort() { return m_cp; }

private:
	bool m_threadRunning;

#ifdef USE_STD_THREAD
	std::thread m_hThread;
#else
	HANDLE m_hThread;
#endif

	HANDLE m_cp;
	SocketMgr *m_socketMgr;
	SOCKET m_socket;
	struct sockaddr_in m_address;
	bool m_opened;
};

#endif