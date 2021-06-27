#include "stdafx.h"
#include "SpehsEngine/Audio/AudioLib.h"
#include "SpehsEngine/Core/CoreLib.h"
#include "SpehsEngine/Core/Inifile.h"
#include "SpehsEngine/Core/StringOperations.h"
#include "SpehsEngine/Core/StringUtilityFunctions.h"
#include "SpehsEngine/Core/DeltaTimeSystem.h"
#include "SpehsEngine/Core/RNG.h"
#include "SpehsEngine/Core/Thread.h"
#include "SpehsEngine/GUI/GUIRectangle.h"
#include "SpehsEngine/Math/MathLib.h"
#include "SpehsEngine/Net/NetLib.h"
#include "SpehsEngine/Net/ConnectionManager2.h"
#include "SpehsEngine/Net/IOService.h"
#include "SpehsEngine/Net/AddressUtilityFunctions.h"
#include "SpehsEngine/Rendering/RenderingLib.h"
#include "SpehsEngine/Rendering/Camera2D.h"
#include "SpehsEngine/Rendering/ConsoleVisualizer.h"
#include "SpehsEngine/Rendering/BatchManager.h"
#include "SpehsEngine/Rendering/ShaderManager.h"
#include "SpehsEngine/Rendering/TextureManager.h"
#include "SpehsEngine/Rendering/Text.h"
#include "SpehsEngine/Rendering/Window.h"
#include "SpehsEngine/Input/InputLib.h"
#include "SpehsEngine/Input/InputManager.h"
#include "SpehsEngine/Input/EventCatcher.h"
#include "SpehsEngine/Input/EventSignaler.h"
#include "SpehsEngine/ImGui/Utility/BackendWrapper.h"
#include "SpehsEngine/Physics/PhysicsLib.h"
#include "SpehsEngine/GUI/GUILib.h"
#include "SpehsEngine/Debug/DebugLib.h"
#include "SpehsEngine/Debug/ScopeProfilerVisualizer.h"
#include "SpehsEngine/Debug/ConnectionManagerVisualizer.h"
#include <set>


//int main()
//{
//	se::CoreLib core;
//	se::NetLib net(core);
//	se::MathLib math(core);
//	se::PhysicsLib physics(math);
//	se::AudioLib audio(core);
//	se::rendering::RenderingLib rendering(math);
//	se::InputLib input(rendering);
//	se::GUILib gui(input, audio);
//	se::debug::DebugLib debug(gui);
//}

#pragma optimize("", off)


#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <map>
#include <cctype>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#ifndef STEAMNETWORKINGSOCKETS_OPENSOURCE
#include <steam/steam_api.h>
#endif

#define WIN32
#ifdef WIN32
#include <windows.h> // Ug, for NukeProcess -- see below
#else
#include <unistd.h>
#include <signal.h>
#endif


#pragma region // trivial stuff
/////////////////////////////////////////////////////////////////////////////
//
// Common stuff
//
/////////////////////////////////////////////////////////////////////////////

bool g_bQuit = false;

SteamNetworkingMicroseconds g_logTimeZero;

// We do this because I won't want to figure out how to cleanly shut
// down the thread that is reading from stdin.
static void NukeProcess(int rc)
{
#ifdef WIN32
	ExitProcess(rc);
#else
	(void)rc; // Unused formal parameter
	kill(getpid(), SIGKILL);
#endif
}

static void DebugOutput(ESteamNetworkingSocketsDebugOutputType eType, const char* pszMsg)
{
	SteamNetworkingMicroseconds time = SteamNetworkingUtils()->GetLocalTimestamp() - g_logTimeZero;
	printf("%10.6f %s\n", time * 1e-6, pszMsg);
	fflush(stdout);
	if (eType == k_ESteamNetworkingSocketsDebugOutputType_Bug)
	{
		fflush(stdout);
		fflush(stderr);
		NukeProcess(1);
	}
}

static void FatalError(const char* fmt, ...)
{
	char text[2048];
	va_list ap;
	va_start(ap, fmt);
	vsprintf_s(text, fmt, ap);
	va_end(ap);
	char* nl = strchr(text, '\0') - 1;
	if (nl >= text && *nl == '\n')
		*nl = '\0';
	DebugOutput(k_ESteamNetworkingSocketsDebugOutputType_Bug, text);
}

static void Printf(const char* fmt, ...)
{
	char text[2048];
	va_list ap;
	va_start(ap, fmt);
	vsprintf_s(text, fmt, ap);
	va_end(ap);
	char* nl = strchr(text, '\0') - 1;
	if (nl >= text && *nl == '\n')
		*nl = '\0';
	DebugOutput(k_ESteamNetworkingSocketsDebugOutputType_Msg, text);
}

static void InitSteamDatagramConnectionSockets()
{
	SteamDatagramErrMsg errMsg;
	if (!GameNetworkingSockets_Init(nullptr, errMsg))
		FatalError("GameNetworkingSockets_Init failed.  %s", errMsg);

	g_logTimeZero = SteamNetworkingUtils()->GetLocalTimestamp();

	SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg, DebugOutput);
}

static void ShutdownSteamDatagramConnectionSockets()
{
	// Give connections time to finish up.  This is an application layer protocol
	// here, it's not TCP.  Note that if you have an application and you need to be
	// more sure about cleanup, you won't be able to do this.  You will need to send
	// a message and then either wait for the peer to close the connection, or
	// you can pool the connection to see if any reliable data is pending.
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

#ifdef STEAMNETWORKINGSOCKETS_OPENSOURCE
	GameNetworkingSockets_Kill();
#else
	SteamDatagramClient_Kill();
#endif
}

/////////////////////////////////////////////////////////////////////////////
//
// Non-blocking console user input.  Sort of.
// Why is this so hard?
//
/////////////////////////////////////////////////////////////////////////////

std::mutex mutexUserInputQueue;
std::queue< std::string > queueUserInput;

std::thread* s_pThreadUserInput = nullptr;

void LocalUserInput_Init()
{
	s_pThreadUserInput = new std::thread([]()
		{
			while (!g_bQuit)
			{
				char szLine[4000];
				if (!fgets(szLine, sizeof(szLine), stdin))
				{
					// Well, you would hope that you could close the handle
					// from the other thread to trigger this.  Nope.
					if (g_bQuit)
						return;
					g_bQuit = true;
					Printf("Failed to read on stdin, quitting\n");
					break;
				}

				mutexUserInputQueue.lock();
				queueUserInput.push(std::string(szLine));
				mutexUserInputQueue.unlock();
			}
		});
}

void LocalUserInput_Kill()
{
	// Does not work.  We won't clean up, we'll just nuke the process.
	//	g_bQuit = true;
	//	_close( fileno( stdin ) );
	//
	//	if ( s_pThreadUserInput )
	//	{
	//		s_pThreadUserInput->join();
	//		delete s_pThreadUserInput;
	//		s_pThreadUserInput = nullptr;
	//	}
}

// You really gotta wonder what kind of pedantic garbage was
// going through the minds of people who designed std::string
// that they decided not to include trim.
// https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring

// trim from start (in place)
static inline void ltrim(std::string& s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !std::isspace(ch);
		}));
}

// trim from end (in place)
static inline void rtrim(std::string& s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !std::isspace(ch);
		}).base(), s.end());
}


// Read the next line of input from stdin, if anything is available.
bool LocalUserInput_GetNext(std::string& result)
{
	bool got_input = false;
	mutexUserInputQueue.lock();
	while (!queueUserInput.empty() && !got_input)
	{
		result = queueUserInput.front();
		queueUserInput.pop();
		ltrim(result);
		rtrim(result);
		got_input = !result.empty(); // ignore blank lines
	}
	mutexUserInputQueue.unlock();
	return got_input;
}

#pragma endregion

/////////////////////////////////////////////////////////////////////////////
//
// ChatServer
//
/////////////////////////////////////////////////////////////////////////////

class ChatServer
{
public:
	void Run(uint16 nPort)
	{
		// Select instance to use.  For now we'll always use the default.
		// But we could use SteamGameServerNetworkingSockets() on Steam.
		m_pInterface = SteamNetworkingSockets();

		// Start listening
		SteamNetworkingIPAddr serverLocalAddr;
		serverLocalAddr.Clear();
		serverLocalAddr.m_port = nPort;
		SteamNetworkingConfigValue_t opt;
		opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback);
		m_hListenSock = m_pInterface->CreateListenSocketIP(serverLocalAddr, 1, &opt);
		if (m_hListenSock == k_HSteamListenSocket_Invalid)
			FatalError("Failed to listen on port %d", nPort);
		m_hPollGroup = m_pInterface->CreatePollGroup();
		if (m_hPollGroup == k_HSteamNetPollGroup_Invalid)
			FatalError("Failed to listen on port %d", nPort);
		Printf("Server listening on port %d\n", nPort);

		while (!g_bQuit)
		{
			PollIncomingMessages();
			PollConnectionStateChanges();
			PollLocalUserInput();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		// Close all the connections
		Printf("Closing connections...\n");
		for (auto it : m_mapClients)
		{
			// Send them one more goodbye message.  Note that we also have the
			// connection close reason as a place to send final data.  However,
			// that's usually best left for more diagnostic/debug text not actual
			// protocol strings.
			SendStringToClient(it.first, "Server is shutting down.  Goodbye.");

			// Close the connection.  We use "linger mode" to ask SteamNetworkingSockets
			// to flush this out and close gracefully.
			m_pInterface->CloseConnection(it.first, 0, "Server Shutdown", true);
		}
		m_mapClients.clear();

		m_pInterface->CloseListenSocket(m_hListenSock);
		m_hListenSock = k_HSteamListenSocket_Invalid;

		m_pInterface->DestroyPollGroup(m_hPollGroup);
		m_hPollGroup = k_HSteamNetPollGroup_Invalid;
	}
private:

	HSteamListenSocket m_hListenSock;
	HSteamNetPollGroup m_hPollGroup;
	ISteamNetworkingSockets* m_pInterface;

	struct Client_t
	{
		std::string m_sNick;
	};

	std::map< HSteamNetConnection, Client_t > m_mapClients;

	void SendStringToClient(HSteamNetConnection conn, const char* str)
	{
		m_pInterface->SendMessageToConnection(conn, str, (uint32)strlen(str), k_nSteamNetworkingSend_Reliable, nullptr);
	}

	void SendStringToAllClients(const char* str, HSteamNetConnection except = k_HSteamNetConnection_Invalid)
	{
		for (auto& c : m_mapClients)
		{
			if (c.first != except)
				SendStringToClient(c.first, str);
		}
	}

	void PollIncomingMessages()
	{
		char temp[1024];

		while (!g_bQuit)
		{
			ISteamNetworkingMessage* pIncomingMsg = nullptr;
			int numMsgs = m_pInterface->ReceiveMessagesOnPollGroup(m_hPollGroup, &pIncomingMsg, 1);
			if (numMsgs == 0)
				break;
			if (numMsgs < 0)
				FatalError("Error checking for messages");
			assert(numMsgs == 1 && pIncomingMsg);
			auto itClient = m_mapClients.find(pIncomingMsg->m_conn);
			assert(itClient != m_mapClients.end());

			// '\0'-terminate it to make it easier to parse
			std::string sCmd;
			sCmd.assign((const char*)pIncomingMsg->m_pData, pIncomingMsg->m_cbSize);
			const char* cmd = sCmd.c_str();

			// We don't need this anymore.
			pIncomingMsg->Release();

			// Check for known commands.  None of this example code is secure or robust.
			// Don't write a real server like this, please.

			if (strncmp(cmd, "/nick", 5) == 0)
			{
				const char* nick = cmd + 5;
				while (isspace(*nick))
					++nick;

				// Let everybody else know they changed their name
				sprintf_s(temp, "%s shall henceforth be known as %s", itClient->second.m_sNick.c_str(), nick);
				SendStringToAllClients(temp, itClient->first);

				// Respond to client
				sprintf_s(temp, "Ye shall henceforth be known as %s", nick);
				SendStringToClient(itClient->first, temp);

				// Actually change their name
				SetClientNick(itClient->first, nick);
				continue;
			}

			// Assume it's just a ordinary chat message, dispatch to everybody else
			sprintf_s(temp, "%s: %s", itClient->second.m_sNick.c_str(), cmd);
			SendStringToAllClients(temp, itClient->first);
		}
	}

	void PollLocalUserInput()
	{
		std::string cmd;
		while (!g_bQuit && LocalUserInput_GetNext(cmd))
		{
			if (strcmp(cmd.c_str(), "/quit") == 0)
			{
				g_bQuit = true;
				Printf("Shutting down server");
				break;
			}

			// That's the only command we support
			Printf("The server only knows one command: '/quit'");
		}
	}

	void SetClientNick(HSteamNetConnection hConn, const char* nick)
	{

		// Remember their nick
		m_mapClients[hConn].m_sNick = nick;

		// Set the connection name, too, which is useful for debugging
		m_pInterface->SetConnectionName(hConn, nick);
	}

	void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
	{
		char temp[1024];

		// What's the state of the connection?
		switch (pInfo->m_info.m_eState)
		{
		case k_ESteamNetworkingConnectionState_None:
			// NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
			break;

		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		{
			// Ignore if they were not previously connected.  (If they disconnected
			// before we accepted the connection.)
			if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connected)
			{

				// Locate the client.  Note that it should have been found, because this
				// is the only codepath where we remove clients (except on shutdown),
				// and connection change callbacks are dispatched in queue order.
				auto itClient = m_mapClients.find(pInfo->m_hConn);
				assert(itClient != m_mapClients.end());

				// Select appropriate log messages
				const char* pszDebugLogAction;
				if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
				{
					pszDebugLogAction = "problem detected locally";
					sprintf_s(temp, "Alas, %s hath fallen into shadow.  (%s)", itClient->second.m_sNick.c_str(), pInfo->m_info.m_szEndDebug);
				}
				else
				{
					// Note that here we could check the reason code to see if
					// it was a "usual" connection or an "unusual" one.
					pszDebugLogAction = "closed by peer";
					sprintf_s(temp, "%s hath departed", itClient->second.m_sNick.c_str());
				}

				// Spew something to our own log.  Note that because we put their nick
				// as the connection description, it will show up, along with their
				// transport-specific data (e.g. their IP address)
				Printf("Connection %s %s, reason %d: %s\n",
					pInfo->m_info.m_szConnectionDescription,
					pszDebugLogAction,
					pInfo->m_info.m_eEndReason,
					pInfo->m_info.m_szEndDebug
				);

				m_mapClients.erase(itClient);

				// Send a message so everybody else knows what happened
				SendStringToAllClients(temp);
			}
			else
			{
				assert(pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting);
			}

			// Clean up the connection.  This is important!
			// The connection is "closed" in the network sense, but
			// it has not been destroyed.  We must close it on our end, too
			// to finish up.  The reason information do not matter in this case,
			// and we cannot linger because it's already closed on the other end,
			// so we just pass 0's.
			m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
			break;
		}

		case k_ESteamNetworkingConnectionState_Connecting:
		{
			// This must be a new connection
			assert(m_mapClients.find(pInfo->m_hConn) == m_mapClients.end());

			Printf("Connection request from %s", pInfo->m_info.m_szConnectionDescription);

			// A client is attempting to connect
			// Try to accept the connection.
			if (m_pInterface->AcceptConnection(pInfo->m_hConn) != k_EResultOK)
			{
				// This could fail.  If the remote host tried to connect, but then
				// disconnected, the connection may already be half closed.  Just
				// destroy whatever we have on our side.
				m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
				Printf("Can't accept connection.  (It was already closed?)");
				break;
			}

			// Assign the poll group
			if (!m_pInterface->SetConnectionPollGroup(pInfo->m_hConn, m_hPollGroup))
			{
				m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
				Printf("Failed to set poll group?");
				break;
			}

			// Generate a random nick.  A random temporary nick
			// is really dumb and not how you would write a real chat server.
			// You would want them to have some sort of signon message,
			// and you would keep their client in a state of limbo (connected,
			// but not logged on) until them.  I'm trying to keep this example
			// code really simple.
			char nick[64];
			sprintf_s(nick, "BraveWarrior%d", 10000 + (rand() % 100000));

			// Send them a welcome message
			sprintf_s(temp, "Welcome, stranger.  Thou art known to us for now as '%s'; upon thine command '/nick' we shall know thee otherwise.", nick);
			SendStringToClient(pInfo->m_hConn, temp);

			// Also send them a list of everybody who is already connected
			if (m_mapClients.empty())
			{
				SendStringToClient(pInfo->m_hConn, "Thou art utterly alone.");
			}
			else
			{
				sprintf_s(temp, "%d companions greet you:", (int)m_mapClients.size());
				for (auto& c : m_mapClients)
					SendStringToClient(pInfo->m_hConn, c.second.m_sNick.c_str());
			}

			// Let everybody else know who they are for now
			sprintf_s(temp, "Hark!  A stranger hath joined this merry host.  For now we shall call them '%s'", nick);
			SendStringToAllClients(temp, pInfo->m_hConn);

			// Add them to the client list, using std::map wacky syntax
			m_mapClients[pInfo->m_hConn];
			SetClientNick(pInfo->m_hConn, nick);
			break;
		}

		case k_ESteamNetworkingConnectionState_Connected:
			// We will get a callback immediately after accepting the connection.
			// Since we are the server, we can ignore this, it's not news to us.
			break;

		default:
			// Silences -Wswitch
			break;
		}
	}

	static ChatServer* s_pCallbackInstance;
	static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo)
	{
		s_pCallbackInstance->OnSteamNetConnectionStatusChanged(pInfo);
	}

	void PollConnectionStateChanges()
	{
		s_pCallbackInstance = this;
		m_pInterface->RunCallbacks();
	}
};

ChatServer* ChatServer::s_pCallbackInstance = nullptr;

/////////////////////////////////////////////////////////////////////////////
//
// ChatClient
//
/////////////////////////////////////////////////////////////////////////////

class ChatClient
{
public:
	void Run(const SteamNetworkingIPAddr& serverAddr)
	{
		// Select instance to use.  For now we'll always use the default.
		m_pInterface = SteamNetworkingSockets();

		// Start connecting
		char szAddr[SteamNetworkingIPAddr::k_cchMaxString];
		serverAddr.ToString(szAddr, sizeof(szAddr), true);
		Printf("Connecting to chat server at %s", szAddr);
		SteamNetworkingConfigValue_t opt;
		opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback);
		m_hConnection = m_pInterface->ConnectByIPAddress(serverAddr, 1, &opt);
		if (m_hConnection == k_HSteamNetConnection_Invalid)
			FatalError("Failed to create connection");

		while (!g_bQuit)
		{
			PollIncomingMessages();
			PollConnectionStateChanges();
			PollLocalUserInput();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
private:

	HSteamNetConnection m_hConnection;
	ISteamNetworkingSockets* m_pInterface;

	void PollIncomingMessages()
	{
		while (!g_bQuit)
		{
			ISteamNetworkingMessage* pIncomingMsg = nullptr;
			int numMsgs = m_pInterface->ReceiveMessagesOnConnection(m_hConnection, &pIncomingMsg, 1);
			if (numMsgs == 0)
				break;
			if (numMsgs < 0)
				FatalError("Error checking for messages");

			// Just echo anything we get from the server
			fwrite(pIncomingMsg->m_pData, 1, pIncomingMsg->m_cbSize, stdout);
			fputc('\n', stdout);

			// We don't need this anymore.
			pIncomingMsg->Release();
		}
	}

	void PollLocalUserInput()
	{
		std::string cmd;
		while (!g_bQuit && LocalUserInput_GetNext(cmd))
		{

			// Check for known commands
			if (strcmp(cmd.c_str(), "/quit") == 0)
			{
				g_bQuit = true;
				Printf("Disconnecting from chat server");

				// Close the connection gracefully.
				// We use linger mode to ask for any remaining reliable data
				// to be flushed out.  But remember this is an application
				// protocol on UDP.  See ShutdownSteamDatagramConnectionSockets
				m_pInterface->CloseConnection(m_hConnection, 0, "Goodbye", true);
				break;
			}

			// Anything else, just send it to the server and let them parse it
			m_pInterface->SendMessageToConnection(m_hConnection, cmd.c_str(), (uint32)cmd.length(), k_nSteamNetworkingSend_Reliable, nullptr);
		}
	}

	void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
	{
		assert(pInfo->m_hConn == m_hConnection || m_hConnection == k_HSteamNetConnection_Invalid);

		// What's the state of the connection?
		switch (pInfo->m_info.m_eState)
		{
		case k_ESteamNetworkingConnectionState_None:
			// NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
			break;

		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		{
			g_bQuit = true;

			// Print an appropriate message
			if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting)
			{
				// Note: we could distinguish between a timeout, a rejected connection,
				// or some other transport problem.
				Printf("We sought the remote host, yet our efforts were met with defeat.  (%s)", pInfo->m_info.m_szEndDebug);
			}
			else if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
			{
				Printf("Alas, troubles beset us; we have lost contact with the host.  (%s)", pInfo->m_info.m_szEndDebug);
			}
			else
			{
				// NOTE: We could check the reason code for a normal disconnection
				Printf("The host hath bidden us farewell.  (%s)", pInfo->m_info.m_szEndDebug);
			}

			// Clean up the connection.  This is important!
			// The connection is "closed" in the network sense, but
			// it has not been destroyed.  We must close it on our end, too
			// to finish up.  The reason information do not matter in this case,
			// and we cannot linger because it's already closed on the other end,
			// so we just pass 0's.
			m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
			m_hConnection = k_HSteamNetConnection_Invalid;
			break;
		}

		case k_ESteamNetworkingConnectionState_Connecting:
			// We will get this callback when we start connecting.
			// We can ignore this.
			break;

		case k_ESteamNetworkingConnectionState_Connected:
			Printf("Connected to server OK");
			break;

		default:
			// Silences -Wswitch
			break;
		}
	}

	static ChatClient* s_pCallbackInstance;
	static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo)
	{
		s_pCallbackInstance->OnSteamNetConnectionStatusChanged(pInfo);
	}

	void PollConnectionStateChanges()
	{
		s_pCallbackInstance = this;
		m_pInterface->RunCallbacks();
	}
};

ChatClient* ChatClient::s_pCallbackInstance = nullptr;

const uint16 DEFAULT_SERVER_PORT = 27020;

void PrintUsageAndExit(int rc = 1)
{
	fflush(stderr);
	printf(
		R"usage(Usage:
    example_chat client SERVER_ADDR
    example_chat server [--port PORT]
)usage"
);
	fflush(stdout);
	exit(rc);
}

void runClient()
{
	se::setThreadName("RUN CLIENT");
	//SteamNetworkingIPAddr serverAddress;
	//serverAddress.Clear();
	//serverAddress.ParseString("192.168.100.41");
	//serverAddress.SetIPv4(serverAddress.GetIPv4(), DEFAULT_SERVER_PORT);
	//ChatClient client;
	//client.Run(serverAddress);

	se::net::ConnectionManager2 connectionManager("client");
	std::shared_ptr<se::net::Connection2> connection = connectionManager.connect(se::net::Endpoint(se::net::Address("localhost"), se::net::Port(41623)));
	//std::shared_ptr<se::net::Connection2> connection = connectionManager.connect(se::net::Endpoint(se::net::Address("192.168.100.41"), se::net::Port(41623)));
	if (connection)
	{
		connection->setReceiveHandler([](se::ReadBuffer& readBuffer, const bool reliable)
			{
				std::string message;
				if (readBuffer.read(message))
				{
					se::log::info(message, se::log::TextColor::CYAN);
					//se::time::sleep(se::time::fromSeconds(0.2f));
				}
			});
		while (true)
		{
			connectionManager.update();
		}
	}
	else
	{
		se::log::error("Failed to connect");
	}
}

void runServer()
{
	se::setThreadName("RUN SERVER");
	//int nPort = DEFAULT_SERVER_PORT;
	//ChatServer server;
	//server.Run((uint16)nPort);

	se::net::ConnectionManager2 connectionManager("server");
	connectionManager.startListening(41623);
	boost::signals2::scoped_connection incomingConnectionScopedConnection;
	std::vector<std::shared_ptr<se::net::Connection2>> connections;
	std::vector<std::shared_ptr<se::net::Connection2>> connectingConnections;
	connectionManager.connectToIncomingConnectionSignal(incomingConnectionScopedConnection, [&connections, &connectingConnections](std::shared_ptr<se::net::Connection2>& connection)
		{
			connections.push_back(connection);
			connectingConnections.push_back(connection);
		});
	while (true)
	{
		connectionManager.update();
		for (size_t i = 0; i < connectingConnections.size(); i++)
		{
			if (connectingConnections[i]->isConnected())
			{
				std::string message = "welcome";
				se::WriteBuffer writeBuffer;
				writeBuffer.write(message);
				connectingConnections[i]->sendPacket(writeBuffer, true);

				connectingConnections[i] = connectingConnections.back();
				connectingConnections.pop_back();
				i--;
			}
		}
	}
}

int main(int argc, const char* argv[])
{
	se::CoreLib core;
	se::NetLib net(core);

	LocalUserInput_Init();

	std::thread clientThread(&runClient);
	std::thread serverThread(&runServer);
	clientThread.join();
	serverThread.join();
}
