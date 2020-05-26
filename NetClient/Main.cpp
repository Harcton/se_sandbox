#include "stdafx.h"

#include "SpehsEngine/Audio/AudioLib.h"
#include "SpehsEngine/Core/CoreLib.h"
#include "SpehsEngine/Core/Inifile.h"
#include "SpehsEngine/Core/StringOperations.h"
#include "SpehsEngine/Core/DeltaTimeSystem.h"
#include "SpehsEngine/Core/Thread.h"
#include "SpehsEngine/GUI/GUIRectangle.h"
#include "SpehsEngine/Math/MathLib.h"
#include "SpehsEngine/Net/NetLib.h"
#include "SpehsEngine/Net/ConnectionManager.h"
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
#include "SpehsEngine/Physics/PhysicsLib.h"
#include "SpehsEngine/GUI/GUILib.h"
#include "SpehsEngine/Debug/DebugLib.h"
#include "SpehsEngine/Debug/ConnectionProfiler.h"
#include "SpehsEngine/Debug/ScopeProfilerVisualizer.h"
#include <thread>


int main()
{
	se::CoreLib core;
	se::NetLib net(core);
	se::MathLib math(core);
	se::PhysicsLib physics(math);
	se::AudioLib audio(core);
	se::rendering::RenderingLib rendering(math);
	se::InputLib input(rendering);
	se::GUILib gui(input, audio);
	se::debug::DebugLib debug(gui);

	se::Inifile inifile("netclient");
	se::Inivar<unsigned>& windowWidth = inifile.get("video", "window_width", 800u);
	se::Inivar<unsigned>& windowHeight = inifile.get("video", "window_height", 900u);
	se::Inivar<unsigned>& limitFps = inifile.get("video", "limit_fps", 60u);
	const se::net::Address serverAddress(inifile.get("network", "server_address", std::string("127.0.0.1")));
	const se::net::Port serverPort(inifile.get("network", "server_port", uint16_t(41666)));
	const se::net::Endpoint serverEndpoint(serverAddress, serverPort);

	const se::time::Time minFrameTime = se::time::fromSeconds(1.0f / float(limitFps));

	se::GUIRectangle::defaultColor = se::Color(0.2f, 0.2f, 0.2f);
	se::GUIRectangle::defaultStringColor = se::Color(0.9f, 0.9f, 0.9f);

	se::rendering::Window window(windowWidth, windowHeight);
	se::rendering::ShaderManager shaderManager;
	se::rendering::Camera2D camera2D(window);
	se::rendering::BatchManager batchManager2D(window, shaderManager, camera2D);
	window.setTitle("NetClient");
	window.setClearColor(se::Color(0.8f, 0.8f, 0.8f));

	se::input::InputManager inputManager(window);
	se::input::EventCatcher eventCatcher;
	se::input::EventSignaler eventSignaler;
	se::time::DeltaTimeSystem deltaTimeSystem;
	se::GUIContext guiContext(batchManager2D, inputManager, deltaTimeSystem);

	se::net::IOService ioService;
	se::net::ConnectionManager connectionManager(ioService, "client");
	std::shared_ptr<se::net::Connection> connection;
	boost::signals2::scoped_connection connectionStatusChangedConnection;
	connectionManager.setDebugLogLevel(1);
	connectionManager.bind();

	// Console
	se::Console console;
	se::rendering::ConsoleVisualizer consoleVisualizer(console, inputManager, batchManager2D);

	// Connection profiler
	se::debug::ConnectionProfiler connectionProfiler(guiContext);
	connectionProfiler.setTextColor(se::Color(0.0f, 0.0f, 0.0f));

	// Profiler visualizer
	se::debug::ScopeProfilerVisualizer scopeProfilerVisualizer(guiContext);
	scopeProfilerVisualizer.setTargetRootSectionWidth(&minFrameTime);

	const se::time::Time beginTime = se::time::now();

	if (true)
	{
		uint64_t dataIndex = 0u;
		uint64_t reliableBytesReceived = 0u;
		std::function<void(se::ReadBuffer&, const boost::asio::ip::udp::endpoint&, const bool)> receiveHandler = [&dataIndex, &reliableBytesReceived, beginTime](se::ReadBuffer& readBuffer, const boost::asio::ip::udp::endpoint&, const bool reliable)
		{
			SE_SCOPE_PROFILER("packetsize : " + std::to_string(readBuffer.getSize()));
			if (reliable)
			{
				reliableBytesReceived += readBuffer.getSize();
			}

			const size_t count = readBuffer.getSize() / sizeof(uint64_t);
			for (size_t i = 0; i < count; i++)
			{
				uint64_t data;
				if (readBuffer.read(data))
				{
					se_assert(data == dataIndex);
					dataIndex++;
					const se::time::Time now = se::time::now();
					const se::time::Time timeSinceBegin = now - beginTime;
					const double reliableBytesPerSecond = double(reliableBytesReceived) / double(timeSinceBegin.asSeconds());
					//se::log::info("Data index: " + std::to_string(dataIndex) +
					//	"\trMB: " + std::to_string(reliableBytesReceived / 1024.0) +
					//	"\trMBps: " + std::to_string(reliableBytesPerSecond / 1024.0));
				}
			}
			se_assert(readBuffer.getBytesRemaining() == 0);
		};

		while (true)
		{
			SE_SCOPE_PROFILER("Frame");
			const se::time::ScopedFrameLimiter frameLimiter(minFrameTime);

			if (!connection || connection->getStatus() == se::net::Connection::Status::Disconnected)
			{
				connection = connectionManager.startConnecting(serverEndpoint);
				if (connection)
				{
					connectionProfiler.addConnection(connection);
					connection->connectToStatusChangedSignal(connectionStatusChangedConnection, [&connection, receiveHandler](const se::net::Connection::Status oldStatus, const se::net::Connection::Status newStatus)
						{
							se::log::info("Client: connection status changed: " + se::toString(int(oldStatus)) + "->" + std::to_string(int(newStatus)));
							if (newStatus == se::net::Connection::Status::Connected)
							{
								se_assert(connection);
								connection->setReceiveHandler(receiveHandler);
							}
						});
				}
			}

			//Input
			input.update();
			audio.update();
			eventCatcher.pollEvents();
			eventSignaler.signalEvents(eventCatcher);
			inputManager.update(eventCatcher);

			//Update
			deltaTimeSystem.deltaTimeSystemUpdate();
			inifile.update();
			consoleVisualizer.update(deltaTimeSystem.deltaTime);
			connectionManager.update();
			connectionProfiler.update();
			scopeProfilerVisualizer.update(deltaTimeSystem.deltaTime);
			if (inputManager.isKeyPressed(unsigned(se::input::Key::BACKSPACE)))
			{
				if (connection)
				{
					connection->resetReliableFragmentSendCounters();
					connection->resetMutexTimes();
				}
			}

			//Render
			window.renderBegin();
			consoleVisualizer.render();
			batchManager2D.render();
			window.renderEnd();
		}
	}
	else
	{
		std::function<void(se::ReadBuffer&, const boost::asio::ip::udp::endpoint&, const bool)> receiveHandler = [beginTime](se::ReadBuffer& readBuffer, const boost::asio::ip::udp::endpoint&, const bool reliable)
		{
			SE_SCOPE_PROFILER("packetsize : " + std::to_string(readBuffer.getSize()));
			se::log::info("Received packet that contains " + std::to_string(readBuffer.getSize()) + " bytes.");
			uint8_t expectedDataIndex = 0;
			while (readBuffer.getBytesRemaining())
			{
				uint8_t dataIndex = 0;
				if (readBuffer.read(dataIndex))
				{
					se_assert(dataIndex == expectedDataIndex);
					expectedDataIndex++;
				}
				else
				{
					se_assert(false && "Packet data is corrupt.");
				}
			}
		};

		while (true)
		{
			SE_SCOPE_PROFILER("Frame");
			const se::time::ScopedFrameLimiter frameLimiter(minFrameTime);

			if (!connection || connection->getStatus() == se::net::Connection::Status::Disconnected)
			{
				connection = connectionManager.startConnecting(serverEndpoint);
				if (connection)
				{
					connectionProfiler.addConnection(connection);
					connection->connectToStatusChangedSignal(connectionStatusChangedConnection, [&connection, receiveHandler](const se::net::Connection::Status oldStatus, const se::net::Connection::Status newStatus)
						{
							se::log::info("Client: connection status changed: " + se::toString(int(oldStatus)) + "->" + std::to_string(int(newStatus)));
							if (newStatus == se::net::Connection::Status::Connected)
							{
								se_assert(connection);
								connection->setReceiveHandler(receiveHandler);
							}
						});
				}
			}

			//Input
			input.update();
			audio.update();
			eventCatcher.pollEvents();
			eventSignaler.signalEvents(eventCatcher);
			inputManager.update(eventCatcher);

			//Update
			deltaTimeSystem.deltaTimeSystemUpdate();
			inifile.update();
			consoleVisualizer.update(deltaTimeSystem.deltaTime);
			connectionManager.update();
			connectionProfiler.update();
			scopeProfilerVisualizer.update(deltaTimeSystem.deltaTime);
			if (inputManager.isKeyPressed(unsigned(se::input::Key::BACKSPACE)))
			{
				if (connection)
				{
					connection->resetReliableFragmentSendCounters();
					connection->resetMutexTimes();
				}
			}

			//Render
			window.renderBegin();
			consoleVisualizer.render();
			batchManager2D.render();
			window.renderEnd();
		}
	}

	return 0;
}
